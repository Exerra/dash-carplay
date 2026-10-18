#pragma once
#ifdef DASH_CARPLAY
#include <QObject>
#include <QJsonObject>
#include <gst/app/gstappsink.h>
#include <atomic>

class CarPlayMicrophone : public QObject {
    Q_OBJECT
 public:
    explicit CarPlayMicrophone(QObject *parent = nullptr) : QObject(parent) {}
    ~CarPlayMicrophone() override { stop(); }
    void start(const QJsonObject &request, const QString &device);
    void stop();
    void check();
 signals:
    void packetReady(const QByteArray &packet);
    void statusChanged(const QString &message);
 private:
    GstElement *pipeline_ = nullptr;
    std::atomic<unsigned> generation_{0};
    std::atomic<int> queued_{0};
    bool opus_ = false;
    int frameBytes_ = 0;
    QByteArray pending_;
    static GstFlowReturn sample(GstAppSink *sink, gpointer data);
};
#endif
