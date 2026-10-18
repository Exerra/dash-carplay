#ifdef DASH_CARPLAY
#include "carplay/microphone.hpp"
#include "carplay/audio_output.hpp"
#include <QMetaObject>

void CarPlayMicrophone::stop() {
    ++generation_;
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_); pipeline_ = nullptr;
        emit statusChanged("Microphone idle.");
    }
    pending_.clear();
}

void CarPlayMicrophone::start(const QJsonObject &request, const QString &device) {
    stop();
    const int rate = request.value("micRate").toInt(), channels = request.value("micChannels").toInt();
    const int samples = request.value("micSamplesPerPacket").toInt();
    const auto codec = request.value("micCodec").toString();
    opus_ = codec == "opus";
    if (rate < 8000 || rate > 48000 || channels < 1 || channels > 2 || samples < 1 || samples > 4096 ||
        (codec != "opus" && codec != "lpcm") || (opus_ && (rate != 48000 || channels != 1 || samples != 960))) {
        emit statusChanged("The phone requested an unsupported microphone format. Compatibility silence remains active."); return;
    }
    frameBytes_ = samples * channels * 2;
    const QString encoder = opus_ ? QString("opusenc audio-type=voice frame-size=20 bitrate=%1 ! ")
        .arg(qBound(6000, request.value("micBitrate").toInt(32000), 128000)) : QString();
    const auto description = QString("audioconvert name=convert ! audioresample ! audio/x-raw,format=S16LE,layout=interleaved,rate=%1,channels=%2 ! %3"
        "appsink name=capture sync=false max-buffers=3 drop=true").arg(rate).arg(channels).arg(encoder).toUtf8();
    GError *error = nullptr;
    pipeline_ = gst_parse_launch(description.constData(), &error);
    auto *source = CarPlayAudio::createInput(device);
    auto *convert = pipeline_ ? gst_bin_get_by_name(GST_BIN(pipeline_), "convert") : nullptr;
    auto *sink = pipeline_ ? gst_bin_get_by_name(GST_BIN(pipeline_), "capture") : nullptr;
    bool linked = false;
    if (source && pipeline_ && convert && sink) {
        CarPlayAudio::configureOutput(source, 60);
        if (gst_bin_add(GST_BIN(pipeline_), source)) {
            linked = gst_element_link(source, convert); source = nullptr;
        }
    }
    if (source) gst_object_unref(source);
    if (convert) gst_object_unref(convert);
    if (error || !linked) {
        if (error) g_error_free(error);
        if (sink) gst_object_unref(sink);
        stop(); emit statusChanged("Microphone unavailable. Compatibility silence remains active; refresh devices to retry."); return;
    }
    GstAppSinkCallbacks callbacks = {}; callbacks.new_sample = &CarPlayMicrophone::sample;
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, this, nullptr); gst_object_unref(sink);
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    emit statusChanged("Microphone active for CarPlay.");
}

GstFlowReturn CarPlayMicrophone::sample(GstAppSink *sink, gpointer data) {
    auto *self = static_cast<CarPlayMicrophone *>(data);
    const auto generation = self->generation_.load();
    auto *sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_EOS;
    auto *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        if (map.size <= 65536 && self->queued_.fetch_add(1) < 3) {
            const QByteArray bytes(reinterpret_cast<const char *>(map.data), int(map.size));
            QMetaObject::invokeMethod(self, [self, generation, bytes] {
                --self->queued_;
                if (generation != self->generation_.load()) return;
                if (self->opus_) emit self->packetReady(QByteArray(1, char(0x11)) + bytes);
                else {
                    self->pending_.append(bytes);
                    while (self->pending_.size() >= self->frameBytes_) {
                        emit self->packetReady(QByteArray(1, char(0x10)) + self->pending_.left(self->frameBytes_));
                        self->pending_.remove(0, self->frameBytes_);
                    }
                }
            }, Qt::QueuedConnection);
        } else if (map.size <= 65536) --self->queued_;
        gst_buffer_unmap(buffer, &map);
    }
    gst_sample_unref(sample); return GST_FLOW_OK;
}

void CarPlayMicrophone::check() {
    if (!pipeline_) return;
    auto *bus = gst_element_get_bus(pipeline_);
    auto *message = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
    if (message) {
        gst_message_unref(message); stop();
        emit statusChanged("Microphone disconnected or could not open. Compatibility silence remains active.");
    }
    gst_object_unref(bus);
}
#endif
