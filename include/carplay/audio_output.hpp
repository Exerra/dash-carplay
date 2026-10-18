#pragma once
#ifdef DASH_CARPLAY
#include <gst/gst.h>
#include <QList>
#include <QString>

namespace CarPlayAudio {
constexpr int DefaultBufferMs = 60;
int bufferMs();
struct Settings {
    QString output;
    int buffer = DefaultBufferMs;
    int music = 100;
    int voice = 100;
    QString microphoneMode = "silent";
    QString input;
};
struct Output { QString id; QString name; };
Settings loadSettings();
void saveSettings(const Settings &settings);
QList<Output> outputs();
QList<Output> inputs();
GstElement *createOutput(const QString &id);
GstElement *createInput(const QString &id);
bool isMusic(int streamType, const QString &audioType);
bool configureOutput(GstElement *element, int milliseconds = 0);
void configurePipeline(GstElement *pipeline, int milliseconds = 0);
}
#endif
