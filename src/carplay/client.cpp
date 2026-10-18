#ifdef DASH_CARPLAY
#include "carplay/client.hpp"
#include "carplay/audio_output.hpp"
#include <QFile>
#include <QDebug>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QtEndian>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>

namespace {
quint32 u32(const QByteArray &data, int offset) {
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + offset));
}

QByteArray parameterSets(const QByteArray &avcc) {
    if (avcc.size() < 7 || quint8(avcc[0]) != 1) return {};
    QByteArray result;
    int offset = 6;
    auto append = [&](int count) {
        for (int i = 0; i < count; ++i) {
            if (offset + 2 > avcc.size()) return false;
            int length = (quint8(avcc[offset]) << 8) | quint8(avcc[offset + 1]);
            offset += 2;
            if (length == 0 || length > avcc.size() - offset) return false;
            result.append("\0\0\0\1", 4);
            result.append(avcc.constData() + offset, length);
            offset += length;
        }
        return true;
    };
    if (!append(quint8(avcc[5]) & 31) || offset >= avcc.size()) return {};
    const int ppsCount = quint8(avcc[offset++]);
    return append(ppsCount) ? result : QByteArray();
}

void stopPipeline(GstElement *&pipeline, GstElement *&source) {
    if (pipeline) gst_element_set_state(pipeline, GST_STATE_NULL);
    if (source) gst_object_unref(source);
    if (pipeline) gst_object_unref(pipeline);
    source = nullptr;
    pipeline = nullptr;
}
}

CarPlayClient::CarPlayClient(QObject *parent) : QObject(parent) {
    GError *error = nullptr;
    enabled_ = gst_init_check(nullptr, nullptr, &error);
    if (error) g_error_free(error);
    connect(&socket_, &QWebSocket::binaryMessageReceived, this, &CarPlayClient::binaryMessage);
    connect(&socket_, &QWebSocket::textMessageReceived, this, &CarPlayClient::textMessage);
    connect(&socket_, &QWebSocket::connected, this, [this] {
        emit statusChanged("Receiver ready. Waiting for iPhone.");
        emit connectedChanged(true);
    });
    connect(&socket_, &QWebSocket::disconnected, this, [this] {
        sessionActive_ = false; heldControls_.clear();
        stopMedia();
        emit connectedChanged(false);
    });
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, &CarPlayClient::checkPipelines);
    timer_.start();
}

CarPlayClient::~CarPlayClient() {
    socket_.abort();
    stopMedia();
}

bool CarPlayClient::connectFile(const QString &path) {
    if (!enabled_ || socket_.state() != QAbstractSocket::UnconnectedState) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8192) return false;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    const QUrl url(object.value("url").toString());
    // The token-bearing native API is restricted to the same machine.
    if (url.scheme() != "ws" || url.host() != "127.0.0.1" || url.path() != "/ws" || url.port() <= 0) return false;
    socket_.open(url);
    return true;
}

void CarPlayClient::disconnectReceiver() {
    sessionActive_ = false; heldControls_.clear();
    socket_.abort();
    stopMedia();
}

void CarPlayClient::sendCommand(const QJsonObject &message) {
    if (socket_.state() == QAbstractSocket::ConnectedState)
        socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

void CarPlayClient::setNightMode(bool night) {
    night_ = night;
    sendCommand({{"type", "night"}, {"night", night}});
}

void CarPlayClient::setVolume(int percent) {
    volume_ = qBound(0, percent, 100) / 100.0;
    for (const auto &audio : audio_) {
        GstElement *volume = gst_bin_get_by_name(GST_BIN(audio.pipeline), "volume");
        if (volume) { g_object_set(volume, "volume", volume_, nullptr); gst_object_unref(volume); }
    }
}

QImage CarPlayClient::frame() const {
    const auto current = videoFrame();
    return current ? current->image() : QImage();
}

std::shared_ptr<const CarPlayVideoFrame> CarPlayClient::videoFrame() const {
    QMutexLocker guard(&frameMutex_);
    return frame_;
}

void CarPlayClient::textMessage(const QString &data) {
    const auto object = QJsonDocument::fromJson(data.toUtf8()).object();
    const auto type = object.value("type").toString();
    if (type == "hello" || type == "session") {
        const bool active = object.value("sessionActive").toBool();
        sessionActive_ = active;
        if (!active) heldControls_.clear();
        if (!active) stopMedia();
        else setNightMode(night_);
        emit statusChanged(active ? "CarPlay connected" : "Receiver ready. Waiting for iPhone.");
    } else if (type == "host-ui") {
        emit hostUiRequested();
    } else if (type == "mic" && object.value("micActive").toBool()) {
        emit statusChanged("Microphone uplink is not enabled in this prototype.");
    } else if (type == "receiver") {
        emit statusChanged(object.value("message").toString());
    }
}

void CarPlayClient::configureVideo(const QByteArray &data) {
    if (data.size() < 12) return;
    const quint32 width = u32(data, 3), height = u32(data, 7);
    if (width < 16 || height < 16 || width > 4096 || height > 4096) return;
    if (quint8(data[2]) != 1) { emit statusChanged("Select H.264 for the native CarPlay prototype."); return; }
    const auto config = parameterSets(data.mid(11));
    if (config.isEmpty()) { emit statusChanged("Invalid CarPlay H.264 configuration."); return; }
    stopPipeline(video_, videoSource_);
    videoConfig_ = data;
    waitingVideoKeyframe_ = false;
    videoReceived_ = 0;
    videoDecoded_ = 0;
    videoStats_.start();
    GError *error = nullptr;
    video_ = gst_parse_launch(
        "appsrc name=source is-live=true format=time do-timestamp=true block=false "
        // appsrc already queues input. Dropping encoded P-frames here corrupts
        // their dependants; keep them and discard only decoded display frames.
        "! h264parse config-interval=-1 ! avdec_h264 max-threads=2 thread-type=slice "
        // Keep the decoder's native planes; the GPU converts and scales them.
        "! videoconvert ! video/x-raw,format=I420 "
        "! appsink name=screen sync=false max-buffers=1 drop=true", &error);
    if (error || !video_) {
        emit statusChanged(error ? QString::fromUtf8(error->message) : "Video pipeline unavailable");
        if (error) g_error_free(error);
        stopPipeline(video_, videoSource_);
        return;
    }
    videoSource_ = gst_bin_get_by_name(GST_BIN(video_), "source");
    GstElement *sink = gst_bin_get_by_name(GST_BIN(video_), "screen");
    if (!videoSource_ || !sink) {
        if (sink) gst_object_unref(sink);
        stopPipeline(video_, videoSource_);
        return;
    }
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = &CarPlayClient::newSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, this, nullptr);
    gst_object_unref(sink);
    GstCaps *caps = gst_caps_new_simple("video/x-h264", "stream-format", G_TYPE_STRING, "byte-stream",
        "alignment", G_TYPE_STRING, "au", nullptr);
    gst_app_src_set_caps(GST_APP_SRC(videoSource_), caps);
    gst_caps_unref(caps);
    videoType_ = quint8(data[1]);
    gst_element_set_state(video_, GST_STATE_PLAYING);
    push(videoSource_, config, true);
}

GstFlowReturn CarPlayClient::newSample(GstAppSink *sink, gpointer context) {
    auto *self = static_cast<CarPlayClient *>(context);
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_EOS;
    const auto frame = CarPlayVideoFrame::map(sample);
    if (frame) {
        {
            QMutexLocker guard(&self->frameMutex_);
            self->frame_ = frame;
        }
        ++self->videoDecoded_;
        emit self->frameChanged();
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

void CarPlayClient::configureAudio(const QByteArray &data) {
    if (data.size() < 9) return;
    const int type = quint8(data[1]), codec = quint8(data[2]);
    const int rate = int(u32(data, 3)), channels = quint8(data[7]);
    const int description = 9 + quint8(data[8]);
    if (rate < 8000 || rate > 192000 || channels < 1 || channels > 2 || description > data.size()) return;
    stopAudio(type);
    const char *capsName = codec == 1 ? "audio/mpeg" : codec == 2 ? "audio/x-opus" : codec == 3 ? "audio/x-raw" : nullptr;
    if (!capsName) return;
    GError *error = nullptr;
    AudioPipeline audio;
    audio.pipeline = gst_parse_launch(
        "appsrc name=source is-live=true format=time do-timestamp=true block=false "
        "! queue max-size-time=200000000 ! decodebin ! audioconvert ! audioresample "
        "! volume name=volume ! autoaudiosink", &error);
    if (error || !audio.pipeline) {
        emit statusChanged(error ? QString::fromUtf8(error->message) : "Audio pipeline unavailable");
        if (error) g_error_free(error);
        stopPipeline(audio.pipeline, audio.source);
        return;
    }
    CarPlayAudio::configurePipeline(audio.pipeline);
    audio.source = gst_bin_get_by_name(GST_BIN(audio.pipeline), "source");
    if (!audio.source) { stopPipeline(audio.pipeline, audio.source); return; }
    GstCaps *caps = gst_caps_new_simple(capsName, "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, channels, nullptr);
    if (codec == 1) {
        gst_caps_set_simple(caps, "mpegversion", G_TYPE_INT, 4, "stream-format", G_TYPE_STRING, "raw", nullptr);
        if (description < data.size()) {
            GstBuffer *asc = gst_buffer_new_allocate(nullptr, data.size() - description, nullptr);
            gst_buffer_fill(asc, 0, data.constData() + description, data.size() - description);
            gst_caps_set_simple(caps, "codec_data", GST_TYPE_BUFFER, asc, nullptr);
            gst_buffer_unref(asc);
        }
    } else if (codec == 2) gst_caps_set_simple(caps, "channel-mapping-family", G_TYPE_INT, 0, nullptr);
    else gst_caps_set_simple(caps, "format", G_TYPE_STRING, "S16LE", "layout", G_TYPE_STRING, "interleaved", nullptr);
    gst_app_src_set_caps(GST_APP_SRC(audio.source), caps);
    gst_caps_unref(caps);
    audio_.insert(type, audio);
    setVolume(int(volume_ * 100));
    gst_element_set_state(audio.pipeline, GST_STATE_PLAYING);
}

void CarPlayClient::push(GstElement *source, const QByteArray &data, bool video, int offset) {
    if (!source || offset < 0 || offset >= data.size()) return;
    if (gst_app_src_get_current_level_bytes(GST_APP_SRC(source)) > 2 * 1024 * 1024) {
        if (video) {
            // Recover at a fresh keyframe rather than continuing with missing references.
            stopPipeline(video_, videoSource_);
            waitingVideoKeyframe_ = true;
            sendCommand({{"type", "keyframe"}});
        }
        return;
    }
    // QByteArray's shared storage outlives the WebSocket callback. GStreamer
    // releases this reference after consuming the payload, without mid()/memcpy.
    auto *storage = new QByteArray(data);
    GstBuffer *buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY,
        const_cast<char *>(storage->constData()), storage->size(), offset, storage->size() - offset,
        storage, [](gpointer value) { delete static_cast<QByteArray *>(value); });
    if (!buffer) { delete storage; return; }
    gst_app_src_push_buffer(GST_APP_SRC(source), buffer);
}

void CarPlayClient::binaryMessage(const QByteArray &data) {
    if (data.size() < 2 || data.size() > 8 * 1024 * 1024) return;
    const int type = quint8(data[1]);
    switch (quint8(data[0])) {
        case 1: configureVideo(data); break;
        case 2:
            if (data.size() >= 11 && type == videoType_) {
                if (waitingVideoKeyframe_) {
                    if (!data[2]) break;
                    configureVideo(videoConfig_);
                }
                ++videoReceived_;
                push(videoSource_, data, true, 11);
            }
            break;
        case 3: configureAudio(data); break;
        case 4: if (data.size() >= 10 && audio_.contains(type)) push(audio_[type].source, data, false, 10); break;
        case 5: if (data.size() >= 3 && type == videoType_ && !data[2]) stopPipeline(video_, videoSource_); break;
        case 6: stopAudio(type); break;
        default: break;
    }
}

void CarPlayClient::stopAudio(int type) {
    auto it = audio_.find(type);
    if (it == audio_.end()) return;
    stopPipeline(it->pipeline, it->source);
    audio_.erase(it);
}

void CarPlayClient::stopMedia() {
    stopPipeline(video_, videoSource_);
    waitingVideoKeyframe_ = false;
    videoConfig_.clear();
    videoStats_.invalidate();
    for (const int type : audio_.keys()) stopAudio(type);
    {
        QMutexLocker guard(&frameMutex_);
        frame_ = {};
    }
    emit frameChanged();
}

void CarPlayClient::checkPipelines() {
    if (videoSource_ && videoStats_.isValid() && videoStats_.elapsed() >= 5000) {
        qInfo() << "CarPlay video" << videoStats_.restart() << "ms received=" << videoReceived_
                << "decoded=" << videoDecoded_.exchange(0)
                << "queuedBytes=" << gst_app_src_get_current_level_bytes(GST_APP_SRC(videoSource_));
        videoReceived_ = 0;
    }
    QList<GstElement *> pipelines;
    if (video_) pipelines.append(video_);
    for (const auto &audio : audio_) pipelines.append(audio.pipeline);
    for (auto *pipeline : pipelines) {
        GstBus *bus = gst_element_get_bus(pipeline);
        GstMessage *message = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
        if (message) {
            GError *error = nullptr;
            gst_message_parse_error(message, &error, nullptr);
            emit statusChanged(QString("CarPlay media: %1").arg(error ? error->message : "unknown error"));
            if (error) g_error_free(error);
            gst_message_unref(message);
        }
        gst_object_unref(bus);
    }
}
#endif
