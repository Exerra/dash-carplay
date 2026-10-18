#ifdef DASH_CARPLAY
#include "carplay/audio_output.hpp"
#include <QDebug>
#include <QtGlobal>

int CarPlayAudio::bufferMs() {
    bool valid = false;
    const int value = qEnvironmentVariableIntValue("CARPLAY_AUDIO_BUFFER_MS", &valid);
    return valid && value >= 40 && value <= 500 ? value : DefaultBufferMs;
}

bool CarPlayAudio::configureOutput(GstElement *element) {
    if (!element) return false;
    // autoaudiosink selects its real output at runtime. Only audio base sinks
    // expose both properties; do not apply these to decoders, sources or video.
    auto *klass = G_OBJECT_GET_CLASS(element);
    const auto *buffer = g_object_class_find_property(klass, "buffer-time");
    const auto *period = g_object_class_find_property(klass, "latency-time");
    if (!buffer || !period || buffer->value_type != G_TYPE_INT64 || period->value_type != G_TYPE_INT64 ||
        !(buffer->flags & G_PARAM_WRITABLE) || !(period->flags & G_PARAM_WRITABLE)) return false;
    const gint64 bufferUs = gint64(bufferMs()) * 1000;
    const gint64 periodUs = 10000;
    g_object_set(element, "buffer-time", bufferUs, "latency-time", periodUs, nullptr);
    qInfo() << "CarPlay audio output" << GST_OBJECT_NAME(element) << "bufferUs=" << bufferUs << "periodUs=" << periodUs;
    return true;
}

void CarPlayAudio::configurePipeline(GstElement *pipeline) {
    if (!pipeline || !GST_IS_BIN(pipeline)) return;
    // In particular, catch the Pulse/ALSA/CoreAudio child created later by
    // autoaudiosink, before it prepares the audio ring buffer.
    g_signal_connect(pipeline, "deep-element-added", G_CALLBACK(+[](GstBin *, GstBin *, GstElement *element, gpointer) {
        configureOutput(element);
    }), nullptr);
    auto *iterator = gst_bin_iterate_recurse(GST_BIN(pipeline));
    GValue value = G_VALUE_INIT;
    bool done = false;
    while (!done) {
        switch (gst_iterator_next(iterator, &value)) {
            case GST_ITERATOR_OK: configureOutput(GST_ELEMENT(g_value_get_object(&value))); g_value_reset(&value); break;
            case GST_ITERATOR_RESYNC: gst_iterator_resync(iterator); break;
            default: done = true; break;
        }
    }
    if (G_IS_VALUE(&value)) g_value_unset(&value);
    gst_iterator_free(iterator);
}
#endif
