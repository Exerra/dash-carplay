#pragma once
#ifdef DASH_CARPLAY
#include <gst/gst.h>

namespace CarPlayAudio {
constexpr int DefaultBufferMs = 60;
int bufferMs();
bool configureOutput(GstElement *element);
void configurePipeline(GstElement *pipeline);
}
#endif
