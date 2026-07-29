#include "playback/MpvVideoItem.h"

#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>
#include <QScreen>
#include <QVariant>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <utility>

Q_LOGGING_CATEGORY(lcMpvRender, "shudder.playback.mpv", QtWarningMsg)

struct MpvSharedState {
  mpv_handle *mpv = nullptr;
  std::atomic_bool renderReady = false;
  std::atomic_int renderContextGeneration = 0;
  std::atomic<quint64> renderedFrameCount = 0;
  std::atomic<quint64> renderTargetSize = 0;
  QMutex sourceMutex;
  QString desiredSource;
  quint64 desiredSourceGeneration = 0;
  quint64 appliedSourceGeneration = 0;
  QMutex sourceCommandMutex;
  bool sourceCommandInFlight = false;
  bool inFlightSourceForced = false;
  bool forceSourceReplay = false;
  quint64 inFlightSourceGeneration = 0;
  QMutex renderLifecycleMutex;
  QMutex updateTargetMutex;
  MpvVideoItem *updateTarget = nullptr;
  std::atomic_bool updateQueued = false;
  std::atomic_bool eventQueued = false;
  std::atomic<quint64> sourceCommandCount = 0;

  void submitDesiredSource(bool force)
  {
    QMutexLocker commandLocker(&sourceCommandMutex);
    if (sourceCommandInFlight) {
      forceSourceReplay = forceSourceReplay || force;
      return;
    }
    const bool requestedForce = force || forceSourceReplay;
    QString source;
    quint64 generation = 0;
    {
      QMutexLocker locker(&sourceMutex);
      if (!requestedForce && appliedSourceGeneration == desiredSourceGeneration) return;
      source = desiredSource;
      generation = desiredSourceGeneration;
    }
    const quint64 replyToken = 1;
    int result = 0;
    if (source.isEmpty()) {
      const char *arguments[] = {"stop", nullptr};
      result = mpv_command_async(mpv, replyToken, arguments);
    } else {
      const QByteArray encodedSource = source.toUtf8();
      const char *arguments[] = {"loadfile", encodedSource.constData(), "replace", nullptr};
      result = mpv_command_async(mpv, replyToken, arguments);
    }
    if (result < 0) {
      forceSourceReplay = requestedForce;
      qCWarning(lcMpvRender) << "source command failed:" << mpv_error_string(result);
      return;
    }
    sourceCommandInFlight = true;
    inFlightSourceForced = requestedForce;
    inFlightSourceGeneration = generation;
    forceSourceReplay = false;
  }

  bool finishSourceCommand(int error, bool *forceNext)
  {
    QMutexLocker commandLocker(&sourceCommandMutex);
    if (!sourceCommandInFlight) return false;
    sourceCommandInFlight = false;
    if (error >= 0) {
      QMutexLocker sourceLocker(&sourceMutex);
      if (desiredSourceGeneration == inFlightSourceGeneration) appliedSourceGeneration = inFlightSourceGeneration;
      sourceCommandCount.fetch_add(1, std::memory_order_relaxed);
    } else {
      forceSourceReplay = forceSourceReplay || inFlightSourceForced;
      qCWarning(lcMpvRender) << "source command reply failed:" << mpv_error_string(error);
    }
    inFlightSourceForced = false;
    const bool force = forceSourceReplay;
    bool needsSubmission = force;
    {
      QMutexLocker sourceLocker(&sourceMutex);
      needsSubmission = needsSubmission || appliedSourceGeneration != desiredSourceGeneration;
    }
    forceSourceReplay = false;
    if (forceNext) *forceNext = force;
    return needsSubmission;
  }

  static void wakeup(void *context)
  {
    auto *state = static_cast<MpvSharedState *>(context);
    if (state->eventQueued.exchange(true, std::memory_order_acq_rel)) return;
    QMutexLocker locker(&state->updateTargetMutex);
    MpvVideoItem *item = state->updateTarget;
    if (!item) {
      state->eventQueued.store(false, std::memory_order_release);
      return;
    }
    QMetaObject::invokeMethod(item, [item]() { item->processMpvEvents(); }, Qt::QueuedConnection);
  }

  ~MpvSharedState()
  {
    if (!mpv) return;
    mpv_set_wakeup_callback(mpv, nullptr, nullptr);
    mpv_command_string(mpv, "stop");
    mpv_terminate_destroy(mpv);
    mpv = nullptr;
  }
};

namespace {
int mpvInt(mpv_handle *mpv, const char *name)
{
  int64_t value = 0;
  return mpv_get_property(mpv, name, MPV_FORMAT_INT64, &value) >= 0 ? int(std::clamp<int64_t>(value, int64_t(0), int64_t(INT_MAX))) : 0;
}

double mpvDouble(mpv_handle *mpv, const char *name)
{
  double value = 0.0;
  return mpv_get_property(mpv, name, MPV_FORMAT_DOUBLE, &value) >= 0 && std::isfinite(value) ? value : 0.0;
}

bool mpvBool(mpv_handle *mpv, const char *name)
{
  int value = 0;
  return mpv_get_property(mpv, name, MPV_FORMAT_FLAG, &value) >= 0 && value != 0;
}

QString mpvString(mpv_handle *mpv, const char *name)
{
  char *value = mpv_get_property_string(mpv, name);
  if (!value) return {};
  const QString result = QString::fromUtf8(value).trimmed();
  mpv_free(value);
  return result;
}

void *getProcAddress(void *, const char *name)
{
  QOpenGLContext *context = QOpenGLContext::currentContext();
  return context ? reinterpret_cast<void *>(context->getProcAddress(QByteArray(name))) : nullptr;
}

class MpvRenderer final : public QQuickFramebufferObject::Renderer {
public:
  explicit MpvRenderer(std::shared_ptr<MpvSharedState> state) : m_state(std::move(state)) {}
  ~MpvRenderer() override
  {
    QMutexLocker lifecycleLocker(&m_state->renderLifecycleMutex);
    if (m_renderContext) {
      qCDebug(lcMpvRender) << "freeing render context";
      if (m_state) m_state->renderReady.store(false, std::memory_order_release);
      mpv_render_context_set_update_callback(m_renderContext, nullptr, nullptr);
      mpv_render_context_free(m_renderContext);
      QQuickOpenGLUtils::resetOpenGLState();
      m_renderContext = nullptr;
    }
    if (m_state) {
      m_state->renderReady.store(false, std::memory_order_release);
      m_state->renderTargetSize.store(0, std::memory_order_relaxed);
    }
  }

  QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
  {
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    format.setSamples(0);
    return new QOpenGLFramebufferObject(size, format);
  }

  void render() override
  {
    if (!m_state) return;
    mpv_handle *mpv = m_state->mpv;
    if (!mpv) return;
    QQuickOpenGLUtils::resetOpenGLState();
    if (!m_renderContext) {
      QMutexLocker lifecycleLocker(&m_state->renderLifecycleMutex);
      mpv_opengl_init_params glInit{getProcAddress, nullptr};
      const char *api = MPV_RENDER_API_TYPE_OPENGL;
      mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(api)},
                                   {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
                                   {MPV_RENDER_PARAM_INVALID, nullptr}};
      const int result = mpv_render_context_create(&m_renderContext, mpv, params);
      if (result < 0) {
        if (!m_loggedContextFailure) {
          qCWarning(lcMpvRender) << "render context initialization failed:" << mpv_error_string(result);
          m_loggedContextFailure = true;
        }
        QQuickOpenGLUtils::resetOpenGLState();
        return;
      }
      mpv_render_context_set_update_callback(m_renderContext, &MpvRenderer::requestUpdate, this);
      m_state->submitDesiredSource(true);
      m_state->renderReady.store(true, std::memory_order_release);
      m_state->renderContextGeneration.fetch_add(1, std::memory_order_relaxed);
      qCDebug(lcMpvRender) << "render context initialized";
    }
    m_state->submitDesiredSource(false);
    QOpenGLFramebufferObject *fbo = framebufferObject();
    if (!fbo || !fbo->isValid()) {
      if (!m_loggedFramebufferFailure) {
        qCWarning(lcMpvRender) << "invalid framebuffer object";
        m_loggedFramebufferFailure = true;
      }
      QQuickOpenGLUtils::resetOpenGLState();
      return;
    }
    const QSize targetSize(fbo->width(), fbo->height());
    if (m_targetSize != targetSize) {
      m_targetSize = targetSize;
      const quint64 packedSize = (quint64(quint32(targetSize.width())) << 32) | quint32(targetSize.height());
      m_state->renderTargetSize.store(packedSize, std::memory_order_relaxed);
      qCDebug(lcMpvRender) << "render target changed to" << targetSize;
    }
    mpv_opengl_fbo mpvFbo{static_cast<int>(fbo->handle()), fbo->width(), fbo->height(), 0};
    int flipY = 0;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo},
                                  {MPV_RENDER_PARAM_FLIP_Y, &flipY},
                                  {MPV_RENDER_PARAM_INVALID, nullptr}};
    const int result = mpv_render_context_render(m_renderContext, params);
    if (result < 0) {
      if (!m_loggedRenderFailure) {
        qCWarning(lcMpvRender) << "frame render failed:" << mpv_error_string(result);
        m_loggedRenderFailure = true;
      }
    } else {
      m_loggedRenderFailure = false;
      m_state->renderedFrameCount.fetch_add(1, std::memory_order_relaxed);
    }
    QQuickOpenGLUtils::resetOpenGLState();
  }

private:
  static void requestUpdate(void *context)
  {
    auto *renderer = static_cast<MpvRenderer *>(context);
    if (!renderer->m_state || renderer->m_state->updateQueued.exchange(true, std::memory_order_acq_rel)) return;
    QMutexLocker locker(&renderer->m_state->updateTargetMutex);
    MpvVideoItem *item = renderer->m_state->updateTarget;
    if (!item) {
      renderer->m_state->updateQueued.store(false, std::memory_order_release);
      return;
    }
    const std::shared_ptr<MpvSharedState> state = renderer->m_state;
    QMetaObject::invokeMethod(item, [item, state]() {
      state->updateQueued.store(false, std::memory_order_release);
      item->update();
    }, Qt::QueuedConnection);
  }

  std::shared_ptr<MpvSharedState> m_state;
  mpv_render_context *m_renderContext = nullptr;
  QSize m_targetSize;
  bool m_loggedContextFailure = false;
  bool m_loggedFramebufferFailure = false;
  bool m_loggedRenderFailure = false;
};
}

MpvVideoItem::MpvVideoItem(QQuickItem *parent) : QQuickFramebufferObject(parent)
{
  qCDebug(lcMpvRender) << "creating native player item";
  setMirrorVertically(false);
  setTextureFollowsItemSize(true);
  m_state = std::make_shared<MpvSharedState>();
  m_state->updateTarget = this;
  m_state->mpv = mpv_create();
  if (!m_state->mpv) {
    setStatus(tr("libmpv could not be initialized."));
    return;
  }
  mpv_set_option_string(m_state->mpv, "terminal", "no");
  mpv_set_option_string(m_state->mpv, "msg-level", "all=warn");
  mpv_set_option_string(m_state->mpv, "vo", "libmpv");
  const QByteArray hardwareDecoder = qEnvironmentVariable("SHUDDER_MPV_HWDEC", QStringLiteral("auto-safe")).toUtf8();
  mpv_set_option_string(m_state->mpv, "hwdec", hardwareDecoder.constData());
  if (hardwareDecoder == QByteArrayLiteral("no")) mpv_set_option_string(m_state->mpv, "gpu-hwdec-interop", "no");
  mpv_set_option_string(m_state->mpv, "profile", "low-latency");
  mpv_set_option_string(m_state->mpv, "cache", "yes");
  mpv_set_option_string(m_state->mpv, "demuxer-cache-duration", "12");
  mpv_set_option_string(m_state->mpv, "audio-client-name", "Shudder");
  if (mpv_initialize(m_state->mpv) < 0) {
    setStatus(tr("libmpv initialization failed."));
    mpv_destroy(m_state->mpv);
    m_state->mpv = nullptr;
    return;
  }
  mpv_set_wakeup_callback(m_state->mpv, &MpvSharedState::wakeup, m_state.get());
  setStatus(tr("Native player ready."));
  m_statsTimer.setInterval(1000);
  connect(&m_statsTimer, &QTimer::timeout, this, &MpvVideoItem::updateStats);
  m_statsTimer.start();
  m_windowChangedConnection = connect(this, &QQuickItem::windowChanged, this, &MpvVideoItem::connectWindowDiagnostics);
  connectWindowDiagnostics(window());
}

MpvVideoItem::~MpvVideoItem()
{
  qCDebug(lcMpvRender) << "destroying native player item";
  QObject::disconnect(m_windowChangedConnection);
  QObject::disconnect(m_windowVisibilityConnection);
  QObject::disconnect(m_windowWidthConnection);
  QObject::disconnect(m_windowHeightConnection);
  QObject::disconnect(m_windowScreenConnection);
  if (m_state) {
    QMutexLocker locker(&m_state->updateTargetMutex);
    m_state->updateTarget = nullptr;
  }
  if (m_state && m_state->mpv) mpv_set_wakeup_callback(m_state->mpv, nullptr, nullptr);
  m_statsTimer.stop();
  if (m_state) {
    if (m_state->mpv) mpv_command_string(m_state->mpv, "stop");
  }
  m_state.reset();
}

QQuickFramebufferObject::Renderer *MpvVideoItem::createRenderer() const
{
  return new MpvRenderer(m_state);
}

QString MpvVideoItem::source() const { return m_source; }

void MpvVideoItem::setSource(const QString &source)
{
  if (m_source == source) return;
  m_source = source;
  emit sourceChanged();
  if (m_state) {
    QMutexLocker sourceLocker(&m_state->sourceMutex);
    m_state->desiredSource = m_source;
    ++m_state->desiredSourceGeneration;
  }
  if (m_source.isEmpty()) {
    resetStats();
  } else {
    setStatus(tr("Playing Native stream."));
  }
  if (!m_state) return;
  {
    QMutexLocker lifecycleLocker(&m_state->renderLifecycleMutex);
    if (m_state->renderReady.load(std::memory_order_acquire)) {
      m_state->submitDesiredSource(false);
      return;
    }
  }
  update();
}

bool MpvVideoItem::paused() const { return m_paused; }

void MpvVideoItem::setPaused(bool paused)
{
  if (m_paused == paused) return;
  m_paused = paused;
  int value = paused ? 1 : 0;
  if (m_state && m_state->mpv) mpv_set_property(m_state->mpv, "pause", MPV_FORMAT_FLAG, &value);
  emit pausedChanged();
}

bool MpvVideoItem::muted() const { return m_muted; }

void MpvVideoItem::setMuted(bool muted)
{
  if (m_muted == muted) return;
  m_muted = muted;
  int value = muted ? 1 : 0;
  if (m_state && m_state->mpv) mpv_set_property(m_state->mpv, "mute", MPV_FORMAT_FLAG, &value);
  emit mutedChanged();
}

int MpvVideoItem::volume() const { return m_volume; }

void MpvVideoItem::setVolume(int volume)
{
  const int bounded = qBound(0, volume, 200);
  if (m_volume == bounded) return;
  m_volume = bounded;
  double value = bounded;
  if (m_state && m_state->mpv) mpv_set_property(m_state->mpv, "volume", MPV_FORMAT_DOUBLE, &value);
  emit volumeChanged();
}

QString MpvVideoItem::status() const { return m_status; }
int MpvVideoItem::videoWidth() const { return m_videoWidth; }
int MpvVideoItem::videoHeight() const { return m_videoHeight; }
double MpvVideoItem::videoFps() const { return m_videoFps; }
double MpvVideoItem::displayFps() const { return m_displayFps; }
int MpvVideoItem::droppedFrames() const { return m_droppedFrames; }
int MpvVideoItem::decoderDroppedFrames() const { return m_decoderDroppedFrames; }
int MpvVideoItem::outputDroppedFrames() const { return m_outputDroppedFrames; }
int MpvVideoItem::mistimedFrames() const { return m_mistimedFrames; }
int MpvVideoItem::delayedFrames() const { return m_delayedFrames; }
double MpvVideoItem::avSync() const { return m_avSync; }
double MpvVideoItem::videoBitrate() const { return m_videoBitrate; }
double MpvVideoItem::audioBitrate() const { return m_audioBitrate; }
double MpvVideoItem::cacheSeconds() const { return m_cacheSeconds; }
double MpvVideoItem::cacheEndSeconds() const { return m_cacheEndSeconds; }
bool MpvVideoItem::cacheIdle() const { return m_cacheIdle; }
QString MpvVideoItem::videoCodec() const { return m_videoCodec; }
QString MpvVideoItem::audioCodec() const { return m_audioCodec; }
QString MpvVideoItem::pixelFormat() const { return m_pixelFormat; }
QString MpvVideoItem::hardwareDecoder() const { return m_hardwareDecoder; }
int MpvVideoItem::estimatedFrameNumber() const { return m_estimatedFrameNumber; }
int MpvVideoItem::estimatedFrameCount() const { return m_estimatedFrameCount; }
bool MpvVideoItem::renderReady() const { return m_state && m_state->renderReady.load(std::memory_order_acquire); }
int MpvVideoItem::renderContextGeneration() const { return m_state ? m_state->renderContextGeneration.load(std::memory_order_relaxed) : 0; }
quint64 MpvVideoItem::sourceCommandCount() const { return m_state ? m_state->sourceCommandCount.load(std::memory_order_relaxed) : 0; }
quint64 MpvVideoItem::renderedFrameCount() const { return m_state ? m_state->renderedFrameCount.load(std::memory_order_relaxed) : 0; }
QSize MpvVideoItem::renderTargetSize() const
{
  if (!m_state) return {};
  const quint64 packedSize = m_state->renderTargetSize.load(std::memory_order_relaxed);
  return QSize(int(packedSize >> 32), int(packedSize & 0xffffffffU));
}

std::shared_ptr<MpvSharedState> MpvVideoItem::sharedState() const { return m_state; }

void MpvVideoItem::updateStats()
{
  if (!m_state) return;

  int width = 0;
  int height = 0;
  double fps = 0.0;
  double displayFps = 0.0;
  int decoderDropped = 0;
  int outputDropped = 0;
  int mistimed = 0;
  int delayed = 0;
  double avSync = 0.0;
  double videoBitrate = 0.0;
  double audioBitrate = 0.0;
  double cache = 0.0;
  double cacheEnd = 0.0;
  bool cacheIdle = false;
  QString videoCodec;
  QString audioCodec;
  QString pixelFormat;
  QString hardwareDecoder;
  int estimatedFrameNumber = 0;
  int estimatedFrameCount = 0;
  if (!m_state->mpv) return;
  width = mpvInt(m_state->mpv, "width");
  height = mpvInt(m_state->mpv, "height");
  fps = mpvDouble(m_state->mpv, "container-fps");
  if (fps <= 0.0) fps = mpvDouble(m_state->mpv, "estimated-vf-fps");
  if (fps <= 0.0) fps = mpvDouble(m_state->mpv, "video-params/fps");
  if (fps <= 0.0) fps = mpvDouble(m_state->mpv, "display-fps");
  displayFps = mpvDouble(m_state->mpv, "display-fps");
  decoderDropped = mpvInt(m_state->mpv, "decoder-frame-drop-count");
  outputDropped = mpvInt(m_state->mpv, "frame-drop-count");
  mistimed = mpvInt(m_state->mpv, "mistimed-frame-count");
  delayed = mpvInt(m_state->mpv, "vo-delayed-frame-count");
  avSync = mpvDouble(m_state->mpv, "avsync");
  videoBitrate = mpvDouble(m_state->mpv, "video-bitrate");
  audioBitrate = mpvDouble(m_state->mpv, "audio-bitrate");
  cache = mpvDouble(m_state->mpv, "demuxer-cache-duration");
  cacheEnd = mpvDouble(m_state->mpv, "demuxer-cache-time");
  cacheIdle = mpvBool(m_state->mpv, "demuxer-cache-idle");
  videoCodec = mpvString(m_state->mpv, "video-codec");
  if (videoCodec.isEmpty()) videoCodec = mpvString(m_state->mpv, "video-codec-name");
  audioCodec = mpvString(m_state->mpv, "audio-codec-name");
  if (audioCodec.isEmpty()) audioCodec = mpvString(m_state->mpv, "audio-codec");
  pixelFormat = mpvString(m_state->mpv, "video-params/pixelformat");
  hardwareDecoder = mpvString(m_state->mpv, "hwdec-current");
  estimatedFrameNumber = mpvInt(m_state->mpv, "estimated-frame-number");
  estimatedFrameCount = mpvInt(m_state->mpv, "estimated-frame-count");

  const int dropped = decoderDropped + outputDropped;
  const bool changed = m_videoWidth != width || m_videoHeight != height || std::abs(m_videoFps - fps) > 0.05 ||
                       std::abs(m_displayFps - displayFps) > 0.05 || m_droppedFrames != dropped ||
                       m_decoderDroppedFrames != decoderDropped || m_outputDroppedFrames != outputDropped ||
                       m_mistimedFrames != mistimed || m_delayedFrames != delayed || std::abs(m_avSync - avSync) > 0.005 ||
                       std::abs(m_videoBitrate - videoBitrate) > 0.5 || std::abs(m_audioBitrate - audioBitrate) > 0.5 ||
                       std::abs(m_cacheSeconds - cache) > 0.05 || std::abs(m_cacheEndSeconds - cacheEnd) > 0.05 ||
                       m_cacheIdle != cacheIdle || m_videoCodec != videoCodec || m_audioCodec != audioCodec ||
                       m_pixelFormat != pixelFormat || m_hardwareDecoder != hardwareDecoder ||
                       m_estimatedFrameNumber != estimatedFrameNumber || m_estimatedFrameCount != estimatedFrameCount;
  if (!changed) return;
  m_videoWidth = width;
  m_videoHeight = height;
  m_videoFps = fps;
  m_displayFps = displayFps;
  m_droppedFrames = dropped;
  m_decoderDroppedFrames = decoderDropped;
  m_outputDroppedFrames = outputDropped;
  m_mistimedFrames = mistimed;
  m_delayedFrames = delayed;
  m_avSync = avSync;
  m_videoBitrate = videoBitrate;
  m_audioBitrate = audioBitrate;
  m_cacheSeconds = cache;
  m_cacheEndSeconds = cacheEnd;
  m_cacheIdle = cacheIdle;
  m_videoCodec = std::move(videoCodec);
  m_audioCodec = std::move(audioCodec);
  m_pixelFormat = std::move(pixelFormat);
  m_hardwareDecoder = std::move(hardwareDecoder);
  m_estimatedFrameNumber = estimatedFrameNumber;
  m_estimatedFrameCount = estimatedFrameCount;
  emit statsChanged();
}

void MpvVideoItem::processMpvEvents()
{
  if (!m_state || !m_state->mpv) return;
  m_state->eventQueued.store(false, std::memory_order_release);
  bool submitNextSource = false;
  bool forceNextSource = false;
  while (true) {
    const mpv_event *event = mpv_wait_event(m_state->mpv, 0.0);
    if (!event || event->event_id == MPV_EVENT_NONE) break;
    if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->reply_userdata == 1) {
      bool force = false;
      submitNextSource = m_state->finishSourceCommand(event->error, &force) || submitNextSource;
      forceNextSource = forceNextSource || force;
    }
  }
  if (submitNextSource) m_state->submitDesiredSource(forceNextSource);
}

void MpvVideoItem::resetStats()
{
  if (m_videoWidth == 0 && m_videoHeight == 0 && m_videoFps == 0.0 && m_displayFps == 0.0 && m_droppedFrames == 0 &&
      m_decoderDroppedFrames == 0 && m_outputDroppedFrames == 0 && m_mistimedFrames == 0 && m_delayedFrames == 0 &&
      m_avSync == 0.0 && m_videoBitrate == 0.0 && m_audioBitrate == 0.0 && m_cacheSeconds == 0.0 &&
      m_cacheEndSeconds == 0.0 && !m_cacheIdle && m_videoCodec.isEmpty() && m_audioCodec.isEmpty() &&
      m_pixelFormat.isEmpty() && m_hardwareDecoder.isEmpty() && m_estimatedFrameNumber == 0 && m_estimatedFrameCount == 0) return;
  m_videoWidth = 0;
  m_videoHeight = 0;
  m_videoFps = 0.0;
  m_displayFps = 0.0;
  m_droppedFrames = 0;
  m_decoderDroppedFrames = 0;
  m_outputDroppedFrames = 0;
  m_mistimedFrames = 0;
  m_delayedFrames = 0;
  m_avSync = 0.0;
  m_videoBitrate = 0.0;
  m_audioBitrate = 0.0;
  m_cacheSeconds = 0.0;
  m_cacheEndSeconds = 0.0;
  m_cacheIdle = false;
  m_videoCodec.clear();
  m_audioCodec.clear();
  m_pixelFormat.clear();
  m_hardwareDecoder.clear();
  m_estimatedFrameNumber = 0;
  m_estimatedFrameCount = 0;
  emit statsChanged();
}

void MpvVideoItem::setStatus(QString status)
{
  if (m_status == status) return;
  m_status = std::move(status);
  emit statusChanged();
}

void MpvVideoItem::connectWindowDiagnostics(QQuickWindow *quickWindow)
{
  QObject::disconnect(m_windowVisibilityConnection);
  QObject::disconnect(m_windowWidthConnection);
  QObject::disconnect(m_windowHeightConnection);
  QObject::disconnect(m_windowScreenConnection);
  if (!quickWindow) return;

  const auto logWindow = [this, quickWindow]() {
    const qreal dpr = quickWindow->effectiveDevicePixelRatio();
    qCDebug(lcMpvRender) << "window state" << quickWindow->visibility() << "logical" << quickWindow->size()
                         << "physical" << QSize(qRound(quickWindow->width() * dpr), qRound(quickWindow->height() * dpr))
                         << "dpr" << dpr << "video item" << size();
  };
  m_windowVisibilityConnection = connect(quickWindow, &QWindow::visibilityChanged, this, [logWindow](QWindow::Visibility) { logWindow(); });
  m_windowWidthConnection = connect(quickWindow, &QWindow::widthChanged, this, logWindow);
  m_windowHeightConnection = connect(quickWindow, &QWindow::heightChanged, this, logWindow);
  m_windowScreenConnection = connect(quickWindow, &QWindow::screenChanged, this, [logWindow](QScreen *) { logWindow(); });
  logWindow();
}
