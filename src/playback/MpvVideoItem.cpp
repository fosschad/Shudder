#include "playback/MpvVideoItem.h"

#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QQuickWindow>
#include <QVariant>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <utility>

struct MpvSharedState {
  QMutex mutex;
  mpv_handle *mpv = nullptr;

  ~MpvSharedState()
  {
    QMutexLocker locker(&mutex);
    if (!mpv) return;
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
    if (m_renderContext) {
      if (m_state) {
        QMutexLocker locker(&m_state->mutex);
        mpv_render_context_free(m_renderContext);
      } else {
        mpv_render_context_free(m_renderContext);
      }
      m_renderContext = nullptr;
    }
  }

  QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
  {
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    format.setSamples(0);
    return new QOpenGLFramebufferObject(size, format);
  }

  void synchronize(QQuickFramebufferObject *item) override
  {
    auto *videoItem = static_cast<MpvVideoItem *>(item);
    m_state = videoItem ? videoItem->sharedState() : nullptr;
  }

  void render() override
  {
    if (!m_state) return;
    QMutexLocker locker(&m_state->mutex);
    mpv_handle *mpv = m_state->mpv;
    if (!mpv) return;
    if (!m_renderContext) {
      mpv_opengl_init_params glInit{getProcAddress, nullptr};
      const char *api = MPV_RENDER_API_TYPE_OPENGL;
      mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(api)},
                                   {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
                                   {MPV_RENDER_PARAM_INVALID, nullptr}};
      if (mpv_render_context_create(&m_renderContext, mpv, params) < 0) return;
    }
    QOpenGLFramebufferObject *fbo = framebufferObject();
    if (!fbo) return;
    mpv_opengl_fbo mpvFbo{static_cast<int>(fbo->handle()), fbo->width(), fbo->height(), 0};
    int flipY = 0;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo},
                                  {MPV_RENDER_PARAM_FLIP_Y, &flipY},
                                  {MPV_RENDER_PARAM_INVALID, nullptr}};
    mpv_render_context_render(m_renderContext, params);
  }

private:
  std::shared_ptr<MpvSharedState> m_state;
  mpv_render_context *m_renderContext = nullptr;
};
}

MpvVideoItem::MpvVideoItem(QQuickItem *parent) : QQuickFramebufferObject(parent)
{
  setMirrorVertically(false);
  setTextureFollowsItemSize(true);
  m_state = std::make_shared<MpvSharedState>();
  m_state->mpv = mpv_create();
  if (!m_state->mpv) {
    setStatus(tr("libmpv could not be initialized."));
    return;
  }
  mpv_set_option_string(m_state->mpv, "terminal", "no");
  mpv_set_option_string(m_state->mpv, "msg-level", "all=warn");
  mpv_set_option_string(m_state->mpv, "vo", "libmpv");
  mpv_set_option_string(m_state->mpv, "hwdec", "auto-safe");
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
  setStatus(tr("Native player ready."));
  m_updateTimer.setInterval(16);
  connect(&m_updateTimer, &QTimer::timeout, this, &QQuickFramebufferObject::update);
  m_updateTimer.start();
  m_statsTimer.setInterval(1000);
  connect(&m_statsTimer, &QTimer::timeout, this, &MpvVideoItem::updateStats);
  m_statsTimer.start();
}

MpvVideoItem::~MpvVideoItem()
{
  m_updateTimer.stop();
  m_statsTimer.stop();
  if (m_state) {
    QMutexLocker locker(&m_state->mutex);
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
  if (m_source.isEmpty()) {
    command({QStringLiteral("stop")});
    resetStats();
  } else {
    command({QStringLiteral("loadfile"), m_source, QStringLiteral("replace")});
    setStatus(tr("Playing Native stream."));
  }
}

bool MpvVideoItem::paused() const { return m_paused; }

void MpvVideoItem::setPaused(bool paused)
{
  if (m_paused == paused) return;
  m_paused = paused;
  int value = paused ? 1 : 0;
  if (m_state) {
    QMutexLocker locker(&m_state->mutex);
    if (m_state->mpv) mpv_set_property(m_state->mpv, "pause", MPV_FORMAT_FLAG, &value);
  }
  emit pausedChanged();
}

bool MpvVideoItem::muted() const { return m_muted; }

void MpvVideoItem::setMuted(bool muted)
{
  if (m_muted == muted) return;
  m_muted = muted;
  int value = muted ? 1 : 0;
  if (m_state) {
    QMutexLocker locker(&m_state->mutex);
    if (m_state->mpv) mpv_set_property(m_state->mpv, "mute", MPV_FORMAT_FLAG, &value);
  }
  emit mutedChanged();
}

int MpvVideoItem::volume() const { return m_volume; }

void MpvVideoItem::setVolume(int volume)
{
  const int bounded = qBound(0, volume, 200);
  if (m_volume == bounded) return;
  m_volume = bounded;
  double value = bounded;
  if (m_state) {
    QMutexLocker locker(&m_state->mutex);
    if (m_state->mpv) mpv_set_property(m_state->mpv, "volume", MPV_FORMAT_DOUBLE, &value);
  }
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

std::shared_ptr<MpvSharedState> MpvVideoItem::sharedState() const { return m_state; }

void MpvVideoItem::command(const QVariantList &arguments)
{
  if (!m_state || arguments.isEmpty()) return;
  QVector<QByteArray> encoded;
  QVector<const char *> argv;
  encoded.reserve(arguments.size());
  argv.reserve(arguments.size() + 1);
  for (const QVariant &argument : arguments) {
    encoded.push_back(argument.toString().toUtf8());
    argv.push_back(encoded.last().constData());
  }
  argv.push_back(nullptr);
  QMutexLocker locker(&m_state->mutex);
  if (m_state->mpv) mpv_command_async(m_state->mpv, 0, argv.data());
}

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
  {
    QMutexLocker locker(&m_state->mutex);
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
  }

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
