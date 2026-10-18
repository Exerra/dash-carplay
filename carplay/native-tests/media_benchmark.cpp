#include "carplay/client.hpp"
#include "carplay/surface.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QWebSocketServer>
#include <QPainter>
#include <QtEndian>
#include <gst/app/gstappsink.h>
#include <ctime>
#include <cstdio>
#include <algorithm>

// Generate before timing: the measured process does not encode or transcode.
// Replay the same deterministic H.264 frames through IPC, decode and display.
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const int width = argc > 1 ? QByteArray(argv[1]).toInt() : 800;
    const int height = argc > 2 ? QByteArray(argv[2]).toInt() : 480;
    const int count = argc > 3 ? QByteArray(argv[3]).toInt() : 300;
    if (width < 16 || height < 16 || count < 30 || count > 1800) return 2;
    gst_init(nullptr, nullptr);
    GError *error = nullptr;
    const auto pattern = qEnvironmentVariable("CARPLAY_BENCHMARK_PATTERN", "ball");
    const auto pipeline = QString("videotestsrc num-buffers=%1 pattern=%4 ! video/x-raw,format=I420,width=%2,height=%3,framerate=30/1 "
        "! x264enc tune=zerolatency speed-preset=ultrafast bitrate=6000 key-int-max=60 "
        "! video/x-h264,stream-format=avc,alignment=au ! appsink name=encoded sync=false").arg(count).arg(width).arg(height).arg(pattern);
    auto *generator = gst_parse_launch(pipeline.toUtf8().constData(), &error);
    if (error || !generator) { qCritical("Fixture generation failed"); return 2; }
    auto *sink = gst_bin_get_by_name(GST_BIN(generator), "encoded");
    gst_element_set_state(generator, GST_STATE_PLAYING);
    QVector<QByteArray> frames;
    QByteArray config(11, '\0'); config[0] = 1; config[1] = 110; config[2] = 1;
    qToLittleEndian<quint32>(width, reinterpret_cast<uchar *>(config.data() + 3));
    qToLittleEndian<quint32>(height, reinterpret_cast<uchar *>(config.data() + 7));
    for (int i = 0; i < count; ++i) {
        auto *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 10 * GST_SECOND);
        if (!sample) { qCritical("Fixture generation timed out"); return 2; }
        GstMapInfo mapped;
        if (i == 0) {
            const auto *value = gst_structure_get_value(gst_caps_get_structure(gst_sample_get_caps(sample), 0), "codec_data");
            if (!value || !gst_buffer_map(gst_value_get_buffer(value), &mapped, GST_MAP_READ)) return 2;
            config.append(reinterpret_cast<const char *>(mapped.data), int(mapped.size));
            gst_buffer_unmap(gst_value_get_buffer(value), &mapped);
        }
        auto *buffer = gst_sample_get_buffer(sample);
        if (!gst_buffer_map(buffer, &mapped, GST_MAP_READ)) return 2;
        QByteArray frame(11, '\0'); frame[0] = 2; frame[1] = 110;
        frame[2] = !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
        size_t offset = 0;
        while (offset + 4 <= mapped.size) {
            const auto length = qFromBigEndian<quint32>(mapped.data + offset); offset += 4;
            if (length > mapped.size - offset) return 2;
            frame.append("\0\0\0\1", 4);
            frame.append(reinterpret_cast<const char *>(mapped.data + offset), int(length)); offset += length;
        }
        frames.append(frame);
        gst_buffer_unmap(buffer, &mapped); gst_sample_unref(sample);
    }
    gst_element_set_state(generator, GST_STATE_NULL); gst_object_unref(sink); gst_object_unref(generator);

    CarPlayClient client;
    CarPlaySurface surface(client); surface.resize(1920, 1080); surface.show();
    QWebSocketServer server("local-media-benchmark", QWebSocketServer::NonSecureMode);
    if (!server.listen(QHostAddress::LocalHost, 0)) return 2;
    QTemporaryDir directory;
    QFile connection(directory.filePath("connection.json"));
    if (!connection.open(QIODevice::WriteOnly)) return 2;
    connection.write(QJsonDocument(QJsonObject{{"url", QString("ws://127.0.0.1:%1/ws").arg(server.serverPort())}}).toJson()); connection.close();
    QWebSocket *peer = nullptr;
    QObject::connect(&server, &QWebSocketServer::newConnection, [&] { peer = server.nextPendingConnection(); });
    if (!client.connectFile(connection.fileName())) return 2;
    QElapsedTimer timeout; timeout.start();
    while (!peer && timeout.elapsed() < 5000) app.processEvents(QEventLoop::AllEvents, 10);
    if (!peer) return 2;
    int decoded = 0, sent = 0;
    QVector<qint64> sentAt, latency;
    QElapsedTimer elapsed;
    QObject::connect(&client, &CarPlayClient::frameChanged, [&] {
        if (!client.videoFrame()) return;
        if (decoded < sentAt.size()) latency.append(elapsed.nsecsElapsed() - sentAt[decoded]);
        ++decoded;
    });
    QTimer tick; tick.setTimerType(Qt::PreciseTimer); tick.setInterval(1);
    const auto cpuStart = std::clock(); elapsed.start();
    peer->sendBinaryMessage(config);
    QObject::connect(&tick, &QTimer::timeout, [&] {
        if (sent < count && elapsed.nsecsElapsed() >= qint64(sent) * 1000000000 / 30) {
            sentAt.append(elapsed.nsecsElapsed()); peer->sendBinaryMessage(frames[sent++]);
        }
        if ((sent == count && decoded == count && elapsed.elapsed() >= count * 1000 / 30 + 200) || elapsed.elapsed() > count * 1000 / 30 + 5000) app.quit();
    });
    tick.start(); app.exec();
    const double wall = elapsed.nsecsElapsed() / 1e9;
    const double cpu = double(std::clock() - cpuStart) / CLOCKS_PER_SEC;
    const auto screenshot = qEnvironmentVariable("CARPLAY_BENCHMARK_SCREENSHOT");
    double imageError = -1;
    if (!screenshot.isEmpty()) {
        const auto actual = surface.grab().toImage().convertToFormat(QImage::Format_RGB32);
        actual.save(screenshot);
        QImage reference(actual.size(), QImage::Format_RGB32); reference.fill(Qt::black);
        const auto image = client.frame();
        const auto fitted = image.size().scaled(reference.size(), Qt::KeepAspectRatio);
        QPainter painter(&reference); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRect(QPoint((reference.width()-fitted.width())/2, (reference.height()-fitted.height())/2), fitted), image); painter.end();
        qint64 sum=0, samples=0;
        for (int y=4; y<actual.height(); y+=8) for (int x=4; x<actual.width(); x+=8) {
            const auto a=actual.pixel(x,y), b=reference.pixel(x,y);
            sum+=qAbs(qRed(a)-qRed(b))+qAbs(qGreen(a)-qGreen(b))+qAbs(qBlue(a)-qBlue(b)); samples+=3;
        }
        imageError = samples ? double(sum)/samples : 255;
    }
    std::sort(latency.begin(), latency.end());
    const auto percentile = [&](int percent) { return latency.isEmpty() ? 0.0 : latency[qMin(latency.size()-1, latency.size()*percent/100)] / 1e6; };
    QJsonObject result{{"width", width}, {"height", height}, {"displayWidth", 1920}, {"displayHeight", 1080},
        {"sent", sent}, {"decoded", decoded}, {"wallSeconds", wall}, {"cpuSeconds", cpu},
        {"cpuPercentOneCore", 100.0 * cpu / wall}, {"decodeP50ms", percentile(50)}, {"decodeP95ms", percentile(95)}};
    if (imageError >= 0) result.insert("gpuVsSoftwareMeanChannelError", imageError);
    std::puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
    return sent == count && decoded == count && imageError <= 5 ? 0 : 1;
}
