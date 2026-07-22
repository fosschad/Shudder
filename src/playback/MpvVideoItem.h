#pragma once

#include <QQuickFramebufferObject>
#include <QTimer>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <memory>

struct MpvSharedState;

class MpvVideoItem : public QQuickFramebufferObject {
  Q_OBJECT
  Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
  Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
  Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
  Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(int videoWidth READ videoWidth NOTIFY statsChanged)
  Q_PROPERTY(int videoHeight READ videoHeight NOTIFY statsChanged)
  Q_PROPERTY(double videoFps READ videoFps NOTIFY statsChanged)
  Q_PROPERTY(double displayFps READ displayFps NOTIFY statsChanged)
  Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY statsChanged)
  Q_PROPERTY(int decoderDroppedFrames READ decoderDroppedFrames NOTIFY statsChanged)
  Q_PROPERTY(int outputDroppedFrames READ outputDroppedFrames NOTIFY statsChanged)
  Q_PROPERTY(int mistimedFrames READ mistimedFrames NOTIFY statsChanged)
  Q_PROPERTY(int delayedFrames READ delayedFrames NOTIFY statsChanged)
  Q_PROPERTY(double avSync READ avSync NOTIFY statsChanged)
  Q_PROPERTY(double videoBitrate READ videoBitrate NOTIFY statsChanged)
  Q_PROPERTY(double audioBitrate READ audioBitrate NOTIFY statsChanged)
  Q_PROPERTY(double cacheSeconds READ cacheSeconds NOTIFY statsChanged)
  Q_PROPERTY(double cacheEndSeconds READ cacheEndSeconds NOTIFY statsChanged)
  Q_PROPERTY(bool cacheIdle READ cacheIdle NOTIFY statsChanged)
  Q_PROPERTY(QString videoCodec READ videoCodec NOTIFY statsChanged)
  Q_PROPERTY(QString audioCodec READ audioCodec NOTIFY statsChanged)
  Q_PROPERTY(QString pixelFormat READ pixelFormat NOTIFY statsChanged)
  Q_PROPERTY(QString hardwareDecoder READ hardwareDecoder NOTIFY statsChanged)
  Q_PROPERTY(int estimatedFrameNumber READ estimatedFrameNumber NOTIFY statsChanged)
  Q_PROPERTY(int estimatedFrameCount READ estimatedFrameCount NOTIFY statsChanged)

public:
  explicit MpvVideoItem(QQuickItem *parent = nullptr);
  ~MpvVideoItem() override;

  Renderer *createRenderer() const override;

  [[nodiscard]] QString source() const;
  void setSource(const QString &source);
  [[nodiscard]] bool paused() const;
  void setPaused(bool paused);
  [[nodiscard]] bool muted() const;
  void setMuted(bool muted);
  [[nodiscard]] int volume() const;
  void setVolume(int volume);
  [[nodiscard]] QString status() const;
  [[nodiscard]] int videoWidth() const;
  [[nodiscard]] int videoHeight() const;
  [[nodiscard]] double videoFps() const;
  [[nodiscard]] double displayFps() const;
  [[nodiscard]] int droppedFrames() const;
  [[nodiscard]] int decoderDroppedFrames() const;
  [[nodiscard]] int outputDroppedFrames() const;
  [[nodiscard]] int mistimedFrames() const;
  [[nodiscard]] int delayedFrames() const;
  [[nodiscard]] double avSync() const;
  [[nodiscard]] double videoBitrate() const;
  [[nodiscard]] double audioBitrate() const;
  [[nodiscard]] double cacheSeconds() const;
  [[nodiscard]] double cacheEndSeconds() const;
  [[nodiscard]] bool cacheIdle() const;
  [[nodiscard]] QString videoCodec() const;
  [[nodiscard]] QString audioCodec() const;
  [[nodiscard]] QString pixelFormat() const;
  [[nodiscard]] QString hardwareDecoder() const;
  [[nodiscard]] int estimatedFrameNumber() const;
  [[nodiscard]] int estimatedFrameCount() const;

  [[nodiscard]] std::shared_ptr<MpvSharedState> sharedState() const;

signals:
  void sourceChanged();
  void pausedChanged();
  void mutedChanged();
  void volumeChanged();
  void statusChanged();
  void statsChanged();

private:
  std::shared_ptr<MpvSharedState> m_state;
  QString m_source;
  QString m_status;
  bool m_paused = false;
  bool m_muted = false;
  int m_volume = 80;
  int m_videoWidth = 0;
  int m_videoHeight = 0;
  double m_videoFps = 0.0;
  double m_displayFps = 0.0;
  int m_droppedFrames = 0;
  int m_decoderDroppedFrames = 0;
  int m_outputDroppedFrames = 0;
  int m_mistimedFrames = 0;
  int m_delayedFrames = 0;
  double m_avSync = 0.0;
  double m_videoBitrate = 0.0;
  double m_audioBitrate = 0.0;
  double m_cacheSeconds = 0.0;
  double m_cacheEndSeconds = 0.0;
  bool m_cacheIdle = false;
  QString m_videoCodec;
  QString m_audioCodec;
  QString m_pixelFormat;
  QString m_hardwareDecoder;
  int m_estimatedFrameNumber = 0;
  int m_estimatedFrameCount = 0;
  QTimer m_updateTimer;
  QTimer m_statsTimer;

  void command(const QVariantList &arguments);
  void updateStats();
  void resetStats();
  void setStatus(QString status);
};
