#include "carplay/client.hpp"
#include "carplay/surface.hpp"
#include "carplay/widget.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWebSocketServer>
#include <QtEndian>
#include <gst/app/gstappsink.h>

class CarPlayNativeTest : public QObject {
    Q_OBJECT
 private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("OpenDashCarPlayTests");
        QCoreApplication::setApplicationName("native-test");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory_.path());
    }

    void androidAutoBlocksAndStopsTheReceiver() {
        const auto runner = settingsDirectory_.filePath("receiver");
        QFile executable(runner);
        QVERIFY(executable.open(QIODevice::WriteOnly));
        executable.write("#!/bin/sh\nexec sleep 30\n"); executable.close();
        QVERIFY(executable.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QSettings settings;
        settings.setValue("CarPlay/receiver_command", runner);
        CarPlayWidget widget;
        QPushButton *start = nullptr, *stop = nullptr;
        for (auto *button : widget.findChildren<QPushButton *>()) {
            if (button->text() == "Start CarPlay") start = button;
            if (button->text() == "Stop") stop = button;
        }
        QVERIFY(start && stop);
        widget.setAndroidAutoActive(true);
        start->click();
        QVERIFY(start->isEnabled());
        QVERIFY(!stop->isEnabled());
        widget.setAndroidAutoActive(false);
        start->click();
        QVERIFY(!start->isEnabled());
        QVERIFY(stop->isEnabled());
        widget.setAndroidAutoActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 4000);
        QVERIFY(!stop->isEnabled());
    }
    void rejectsNonLocalConnectionFiles() {
        CarPlayClient client;
        QTemporaryDir directory;
        const auto fileName = directory.filePath("connection.json");
        QFile file(fileName);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"url\":\"ws://192.0.2.1:8080/ws?token=test\"}");
        file.close();
        QVERIFY(!client.connectFile(fileName));
    }

    void decodesVideoAndMapsTouchInsideLetterboxing() {
        CarPlayClient client;
        QWebSocketServer server("fixture", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTemporaryDir directory;
        const auto fileName = directory.filePath("connection.json");
        QFile file(fileName);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"url", QString("ws://127.0.0.1:%1/ws?token=fixture").arg(server.serverPort())}}).toJson());
        file.close();
        QSignalSpy connected(&server, &QWebSocketServer::newConnection);
        QVERIFY(client.connectFile(fileName));
        QVERIFY(connected.wait(2000));
        auto *peer = server.nextPendingConnection();
        QVERIFY(peer);
        QCOMPARE(peer->requestUrl().query(), QString("token=fixture"));
        QSignalSpy input(peer, &QWebSocket::textMessageReceived);
        QSignalSpy status(&client, &CarPlayClient::statusChanged);
        QSignalSpy decoded(&client, &CarPlayClient::frameChanged);
        peer->sendTextMessage("{\"type\":\"receiver\",\"message\":\"Pair the iPhone first\"}");
        QTRY_VERIFY(!status.isEmpty() && status.last().at(0).toString() == "Pair the iPhone first");

        // Generate our own test pattern; this validates the real decode path without an iPhone.
        GError *error = nullptr;
        auto *generator = gst_parse_launch(
            "videotestsrc num-buffers=60 pattern=ball ! video/x-raw,format=I420,width=160,height=96,framerate=30/1 "
            "! x264enc tune=zerolatency key-int-max=60 ! video/x-h264,stream-format=avc,alignment=au "
            "! appsink name=encoded sync=false", &error);
        QVERIFY2(!error && generator, error ? error->message : "fixture pipeline failed");
        auto *sink = gst_bin_get_by_name(GST_BIN(generator), "encoded");
        gst_element_set_state(generator, GST_STATE_PLAYING);
        // Send a burst with dependent P-frames; a leaky encoded queue loses references.
        for (int i = 0; i < 60; ++i) {
            auto *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 5 * GST_SECOND);
            QVERIFY(sample);
            if (i == 0) {
                auto *caps = gst_sample_get_caps(sample);
                const auto *value = gst_structure_get_value(gst_caps_get_structure(caps, 0), "codec_data");
                QVERIFY(value);
                auto *config = gst_value_get_buffer(value);
                GstMapInfo mapped;
                QVERIFY(gst_buffer_map(config, &mapped, GST_MAP_READ));
                QByteArray message(11, '\0');
                message[0] = 1; message[1] = 110; message[2] = 1;
                qToLittleEndian<quint32>(160, reinterpret_cast<uchar *>(message.data() + 3));
                qToLittleEndian<quint32>(96, reinterpret_cast<uchar *>(message.data() + 7));
                message.append(reinterpret_cast<const char *>(mapped.data), int(mapped.size));
                gst_buffer_unmap(config, &mapped);
                peer->sendBinaryMessage(message);
            }
            GstMapInfo mapped;
            QVERIFY(gst_buffer_map(gst_sample_get_buffer(sample), &mapped, GST_MAP_READ));
            QByteArray frame(11, '\0');
            frame[0] = 2; frame[1] = 110; frame[2] = 1;
            size_t offset = 0;
            while (offset + 4 <= mapped.size) {
                const auto length = qFromBigEndian<quint32>(mapped.data + offset);
                offset += 4;
                QVERIFY(length <= mapped.size - offset);
                frame.append("\0\0\0\1", 4);
                frame.append(reinterpret_cast<const char *>(mapped.data + offset), int(length));
                offset += length;
            }
            peer->sendBinaryMessage(frame);
            gst_buffer_unmap(gst_sample_get_buffer(sample), &mapped);
            gst_sample_unref(sample);
        }
        gst_element_set_state(generator, GST_STATE_NULL);
        gst_object_unref(sink);
        gst_object_unref(generator);
        QTRY_VERIFY_WITH_TIMEOUT(!client.frame().isNull(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(decoded.count(), 60, 5000);
        QCOMPARE(client.frame().size(), QSize(160, 96));

        CarPlaySurface surface(client);
        surface.resize(500, 200);
        surface.show();
        QTest::qWait(100);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(250, 100));
        QTRY_VERIFY(input.count() >= 2);
        const auto press = QJsonDocument::fromJson(input.at(0).at(0).toString().toUtf8()).object();
        const auto contact = press.value("contacts").toArray().at(0).toObject();
        QCOMPARE(contact.value("x").toDouble(), 0.5);
        QCOMPARE(contact.value("y").toDouble(), 0.5);
        QVERIFY(contact.value("down").toBool());
        const int before = input.count();
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(1, 100));
        QTest::qWait(50);
        QCOMPARE(input.count(), before);

        peer->sendBinaryMessage(QByteArray::fromHex("010100")); // truncated config must be ignored
        client.disconnectReceiver();
        QVERIFY(client.frame().isNull());
        peer->deleteLater();
    }
 private:
    QTemporaryDir settingsDirectory_;
};

QTEST_MAIN(CarPlayNativeTest)
#include "client_test.moc"
