#pragma once

#ifdef DASH_CARPLAY
#include <QImage>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <QWebSocket>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <atomic>

// Native Dash client for the bundled receiver. No embedded browser is required.
class CarPlayClient : public QObject {
    Q_OBJECT
 public:
    explicit CarPlayClient(QObject *parent = nullptr);
    ~CarPlayClient() override;
    bool connectFile(const QString &path);
    void disconnectReceiver();
    void sendCommand(const QJsonObject &message);
    void setNightMode(bool night);
    void setVolume(int percent);
    QImage frame() const;

 signals:
    void statusChanged(const QString &message);
    void connectedChanged(bool connected);
    void frameChanged();

 private:
    struct AudioPipeline { GstElement *pipeline = nullptr; GstElement *source = nullptr; };
    QWebSocket socket_;
    QTimer timer_;
    GstElement *video_ = nullptr;
    GstElement *videoSource_ = nullptr;
    int videoType_ = -1;
    QByteArray videoConfig_;
    bool waitingVideoKeyframe_ = false;
    quint64 videoReceived_ = 0;
    std::atomic<quint64> videoDecoded_{0};
    QElapsedTimer videoStats_;
    QMap<int, AudioPipeline> audio_;
    mutable QMutex frameMutex_;
    QImage frame_;
    bool night_ = false;
    double volume_ = 1.0;
    bool enabled_ = false;

    void binaryMessage(const QByteArray &data);
    void textMessage(const QString &data);
    void configureVideo(const QByteArray &data);
    void configureAudio(const QByteArray &data);
    void stopMedia();
    void stopAudio(int type);
    void checkPipelines();
    void push(GstElement *source, const QByteArray &data, bool video = false);
    static GstFlowReturn newSample(GstAppSink *sink, gpointer context);
};
#endif
