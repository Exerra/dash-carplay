#ifdef DASH_CARPLAY
#include "carplay/audio_output.hpp"
#include <QDebug>
#include <QtGlobal>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

int CarPlayAudio::bufferMs() {
    bool valid = false;
    const int value = qEnvironmentVariableIntValue("CARPLAY_AUDIO_BUFFER_MS", &valid);
    return valid && value >= 40 && value <= 500 ? value : DefaultBufferMs;
}

CarPlayAudio::Settings CarPlayAudio::loadSettings() {
    QSettings saved;
    Settings result;
    result.output = saved.value("CarPlay/Audio/output").toString();
    const int buffer = saved.value("CarPlay/Audio/buffer_ms", bufferMs()).toInt();
    result.buffer = buffer >= 40 && buffer <= 500 ? buffer : DefaultBufferMs;
    result.music = qBound(0, saved.value("CarPlay/Audio/music", 100).toInt(), 100);
    result.voice = qBound(0, saved.value("CarPlay/Audio/voice", 100).toInt(), 100);
    result.microphoneMode = saved.value("CarPlay/Audio/microphone_mode", "silent").toString();
    if (result.microphoneMode != "silent" && result.microphoneMode != "phone" && result.microphoneMode != "real") result.microphoneMode = "silent";
    result.input = saved.value("CarPlay/Audio/input").toString();
    return result;
}

void CarPlayAudio::saveSettings(const Settings &settings) {
    QSettings saved;
    saved.setValue("CarPlay/Audio/output", settings.output);
    saved.setValue("CarPlay/Audio/buffer_ms", settings.buffer);
    saved.setValue("CarPlay/Audio/music", settings.music);
    saved.setValue("CarPlay/Audio/voice", settings.voice);
    saved.setValue("CarPlay/Audio/microphone_mode", settings.microphoneMode);
    saved.setValue("CarPlay/Audio/input", settings.input);
    saved.sync();
}

bool CarPlayAudio::isMusic(int streamType, const QString &audioType) {
    const auto kind = audioType.toLower();
    return kind == "media" || ((kind.isEmpty() || kind == "default" || kind == "compatibility") && streamType != 101);
}

namespace {
bool deviceValue(GstElement *sink, QString *value) {
    const auto *property = g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "device");
    if (!property || !(property->flags & G_PARAM_READABLE)) return false;
    if (property->value_type == G_TYPE_STRING) {
        gchar *device = nullptr; g_object_get(sink, "device", &device, nullptr);
        *value = QString::fromUtf8(device); g_free(device); return !value->isEmpty();
    }
    if (property->value_type == G_TYPE_UINT) {
        guint device = 0; g_object_get(sink, "device", &device, nullptr); *value = QString::number(device); return true;
    }
    if (property->value_type == G_TYPE_INT) {
        gint device = 0; g_object_get(sink, "device", &device, nullptr); *value = QString::number(device); return true;
    }
    return false;
}
}

namespace {
QList<CarPlayAudio::Output> devices(bool input) {
    QList<CarPlayAudio::Output> result;
    // Probe the platform's shared audio server rather than opening ALSA devices
    // exclusively. On the Pi, PipeWire exposes HDMI, USB and Bluetooth here.
#if defined(Q_OS_LINUX)
    const auto *providerName = "pulsedeviceprovider";
#elif defined(Q_OS_MACOS)
    const auto *providerName = "osxaudiodeviceprovider";
#else
    const auto *providerName = "wasapideviceprovider";
#endif
    auto *provider = gst_device_provider_factory_get_by_name(providerName);
    if (!provider) return result;
    auto *devices = gst_device_provider_get_devices(provider);
    for (auto *entry = devices; entry; entry = entry->next) {
        auto *device = GST_DEVICE(entry->data);
        auto *classes = gst_device_get_device_class(device);
        const bool matches = QString::fromUtf8(classes).contains(input ? "Source" : "Sink"); g_free(classes);
        if (!matches) continue;
        auto *properties = gst_device_get_properties(device);
        const auto *kind = properties ? gst_structure_get_string(properties, "device.class") : nullptr;
        const bool monitor = kind && QString::fromUtf8(kind) == "monitor";
        if (properties) gst_structure_free(properties);
        if (input && monitor) continue;
        auto *sink = gst_device_create_element(device, nullptr);
        if (!sink) continue;
        QString value;
        if (deviceValue(sink, &value)) {
            auto *factory = gst_element_get_factory(sink);
            const QString name = factory ? QString::fromUtf8(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory))) : QString();
            if (!name.isEmpty()) {
                const QString id = QString::fromUtf8(QJsonDocument(QJsonArray{name, value}).toJson(QJsonDocument::Compact));
                auto *label = gst_device_get_display_name(device);
                result.append({id, QString::fromUtf8(label)}); g_free(label);
            }
        }
        gst_object_unref(sink);
    }
    g_list_free_full(devices, reinterpret_cast<GDestroyNotify>(gst_object_unref));
    gst_object_unref(provider);
    return result;
}

GstElement *createDevice(const QString &id, bool input) {
    if (id.isEmpty()) return gst_element_factory_make(input ? "autoaudiosrc" : "autoaudiosink", nullptr);
    const auto fields = QJsonDocument::fromJson(id.toUtf8()).array();
    if (fields.size() != 2 || !fields[0].isString() || !fields[1].isString()) return nullptr;
    const auto factory = fields[0].toString();
    const QStringList allowed = input ? QStringList{"pulsesrc", "osxaudiosrc", "wasapisrc"} : QStringList{"pulsesink", "osxaudiosink", "wasapisink"};
    if (!allowed.contains(factory)) return nullptr;
    auto *sink = gst_element_factory_make(factory.toUtf8().constData(), nullptr);
    if (!sink) return nullptr;
    const auto *property = g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "device");
    const auto value = fields[1].toString();
    bool valid = property && (property->flags & G_PARAM_WRITABLE);
    if (valid && property->value_type == G_TYPE_STRING) g_object_set(sink, "device", value.toUtf8().constData(), nullptr);
    else if (valid && property->value_type == G_TYPE_UINT) {
        const auto device = value.toUInt(&valid); if (valid) g_object_set(sink, "device", guint(device), nullptr);
    } else if (valid && property->value_type == G_TYPE_INT) {
        const auto device = value.toInt(&valid); if (valid) g_object_set(sink, "device", gint(device), nullptr);
    } else valid = false;
    if (!valid) { gst_object_unref(sink); return nullptr; }
    return sink;
}
}

QList<CarPlayAudio::Output> CarPlayAudio::outputs() { return devices(false); }
QList<CarPlayAudio::Output> CarPlayAudio::inputs() { return devices(true); }
GstElement *CarPlayAudio::createOutput(const QString &id) { return createDevice(id, false); }
GstElement *CarPlayAudio::createInput(const QString &id) { return createDevice(id, true); }

bool CarPlayAudio::configureOutput(GstElement *element, int milliseconds) {
    if (!element) return false;
    // autoaudiosink selects its real output at runtime. Only audio base sinks
    // expose both properties; do not apply these to decoders, sources or video.
    auto *klass = G_OBJECT_GET_CLASS(element);
    const auto *buffer = g_object_class_find_property(klass, "buffer-time");
    const auto *period = g_object_class_find_property(klass, "latency-time");
    if (!buffer || !period || buffer->value_type != G_TYPE_INT64 || period->value_type != G_TYPE_INT64 ||
        !(buffer->flags & G_PARAM_WRITABLE) || !(period->flags & G_PARAM_WRITABLE)) return false;
    const gint64 bufferUs = gint64(milliseconds >= 40 && milliseconds <= 500 ? milliseconds : bufferMs()) * 1000;
    const gint64 periodUs = 10000;
    g_object_set(element, "buffer-time", bufferUs, "latency-time", periodUs, nullptr);
    qInfo() << "CarPlay audio output" << GST_OBJECT_NAME(element) << "bufferUs=" << bufferUs << "periodUs=" << periodUs;
    return true;
}

void CarPlayAudio::configurePipeline(GstElement *pipeline, int milliseconds) {
    if (!pipeline || !GST_IS_BIN(pipeline)) return;
    // In particular, catch the Pulse/ALSA/CoreAudio child created later by
    // autoaudiosink, before it prepares the audio ring buffer.
    const int target = milliseconds >= 40 && milliseconds <= 500 ? milliseconds : bufferMs();
    g_signal_connect(pipeline, "deep-element-added", G_CALLBACK(+[](GstBin *, GstBin *, GstElement *element, gpointer data) {
        configureOutput(element, GPOINTER_TO_INT(data));
    }), GINT_TO_POINTER(target));
    auto *iterator = gst_bin_iterate_recurse(GST_BIN(pipeline));
    GValue value = G_VALUE_INIT;
    bool done = false;
    while (!done) {
        switch (gst_iterator_next(iterator, &value)) {
            case GST_ITERATOR_OK: configureOutput(GST_ELEMENT(g_value_get_object(&value)), target); g_value_reset(&value); break;
            case GST_ITERATOR_RESYNC: gst_iterator_resync(iterator); break;
            default: done = true; break;
        }
    }
    if (G_IS_VALUE(&value)) g_value_unset(&value);
    gst_iterator_free(iterator);
}
#endif
