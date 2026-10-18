#pragma once

#ifdef DASH_CARPLAY
#include "app/phone_controls.hpp"
#include "carplay/video_frame.hpp"
#include "carplay/audio_output.hpp"
#include "carplay/microphone.hpp"
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
#include <set>

// Native Dash client for the bundled receiver. No embedded browser is required.
class CarPlayClient : public QObject {
    Q_OBJECT
 public:
    explicit CarPlayClient(QObject *parent = nullptr);
    ~CarPlayClient() override;
    bool connectFile(const QString &path);
    void disconnectReceiver();
    void sendCommand(const QJsonObject &message);
    void sendControl(PhoneControls::Command command, PhoneControls::State state);
    bool sessionActive() const { return sessionActive_; }
    void setNightMode(bool night);
    void setVolume(int percent);
    void setAudioSettings(const CarPlayAudio::Settings &settings);
    CarPlayAudio::Settings audioSettings() const { return audioSettings_; }
    void testSpeakers();
    QImage frame() const;
    std::shared_ptr<const CarPlayVideoFrame> videoFrame() const;

 signals:
    void statusChanged(const QString &message);
    void connectedChanged(bool connected);
    void frameChanged();
    void hostUiRequested();
    void audioStatusChanged(const QString &message);
    void speakerTestChanged(bool playing);

 private:
    struct AudioPipeline {
        GstElement *pipeline = nullptr;
        GstElement *source = nullptr;
        QByteArray config;
        bool music = true;
    };
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
    CarPlayAudio::Settings audioSettings_;
    bool audioFallback_ = false;
    GstElement *speakerTest_ = nullptr;
    CarPlayMicrophone *microphone_ = nullptr;
    QJsonObject microphoneRequest_;
    mutable QMutex frameMutex_;
    std::shared_ptr<const CarPlayVideoFrame> frame_;
    bool night_ = false;
    double volume_ = 1.0;
    bool enabled_ = false;
    bool sessionActive_ = false;
    std::set<PhoneControls::Command> heldControls_;

    void binaryMessage(const QByteArray &data);
    void textMessage(const QString &data);
    void configureVideo(const QByteArray &data);
    void configureAudio(const QByteArray &data);
    void stopMedia();
    void stopAudio(int type);
    void updateAudioVolumes();
    void rebuildAudio();
    GstElement *audioOutput();
    bool attachAudioOutput(GstElement *pipeline);
    void stopSpeakerTest();
    void checkPipelines();
    void push(GstElement *source, const QByteArray &data, bool video = false, int offset = 0);
    static GstFlowReturn newSample(GstAppSink *sink, gpointer context);
};
#endif
