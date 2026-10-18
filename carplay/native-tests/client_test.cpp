#include "carplay/client.hpp"
#include "carplay/surface.hpp"
#include "carplay/widget.hpp"
#include "carplay/pairing_agent.hpp"
#include "carplay/phone_wizard.hpp"
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

    void init() {
        QSettings settings;
        settings.remove("CarPlay");
        settings.setValue("CarPlay/bluetooth_address", "00:11:22:33:44:55");
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
        auto *start = widget.findChild<QPushButton *>("connectCarPlay");
        auto *stop = widget.findChild<QPushButton *>("stopCarPlay");
        QVERIFY(start && stop);
        widget.setAndroidAutoActive(true);
        start->click();
        QVERIFY(start->isEnabled());
        QVERIFY(!stop->isEnabled());
        widget.setAndroidAutoActive(false);
        start->click();
        QVERIFY(stop->isEnabled());
        widget.setAndroidAutoActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(!stop->isEnabled(), 4000);
        QVERIFY(start->isEnabled());
    }
    void selectedPhoneAndResolutionReachTheReceiverAndPersist() {
        const auto capture = settingsDirectory_.filePath("environment");
        const auto runner = settingsDirectory_.filePath("capture-receiver");
        QFile executable(runner);
        QVERIFY(executable.open(QIODevice::WriteOnly));
        executable.write(("#!/bin/sh\nprintf '%s\\n' \"$CARPLAY_BT_ADDRESS\" \"$CARPLAY_WIDTH\" \"$CARPLAY_HEIGHT\" \"$CARPLAY_FPS\" > '" + capture + "'\nexec sleep 30\n").toUtf8());
        executable.close();
        QVERIFY(executable.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QSettings settings;
        settings.setValue("CarPlay/receiver_command", runner);
        settings.beginWriteArray("CarPlay/phones", 2);
        settings.setArrayIndex(0); settings.setValue("name", "First iPhone"); settings.setValue("address", "00:11:22:33:44:55");
        settings.setArrayIndex(1); settings.setValue("name", "Second iPhone"); settings.setValue("address", "AA:BB:CC:DD:EE:FF");
        settings.endArray();
        {
            CarPlayWidget widget;
            auto *phones = widget.findChild<QComboBox *>("savedPhones");
            QCOMPARE(phones->count(), 2);
            phones->setCurrentIndex(1);
            auto *resolution = widget.findChild<QComboBox *>("resolutionPreset");
            resolution->setCurrentIndex(resolution->findData(QSize(1280,720)));
            auto *fps = widget.findChild<QComboBox *>("carplayFps"); fps->setCurrentIndex(1);
            widget.findChild<QPushButton *>("connectCarPlay")->click();
            QTRY_VERIFY(QFile::exists(capture));
            QFile result(capture); QVERIFY(result.open(QIODevice::ReadOnly));
            QCOMPARE(result.readAll(), QByteArray("AA:BB:CC:DD:EE:FF\n1280\n720\n60\n"));
            widget.setAndroidAutoActive(true);
            QTRY_VERIFY(!widget.findChild<QPushButton *>("stopCarPlay")->isEnabled());
        }
        CarPlayWidget restored;
        QCOMPARE(restored.findChild<QComboBox *>("savedPhones")->currentText(), QString("Second iPhone"));
        QCOMPARE(restored.findChild<QSpinBox *>("carplayWidth")->value(), 1280);
        QCOMPARE(restored.findChild<QSpinBox *>("carplayHeight")->value(), 720);
        QCOMPARE(restored.findChild<QComboBox *>("carplayFps")->currentData().toInt(), 60);
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
    void pairingAgentOnlyAcceptsTheChosenPhone() {
        CarPlayPairingAgent agent;
        const QDBusObjectPath selected("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF");
        QVERIFY(!agent.accepts(selected));
        agent.select("aa:bb:cc:dd:ee:ff");
        QVERIFY(agent.accepts(selected));
        QVERIFY(!agent.accepts(QDBusObjectPath("/org/bluez/hci0/dev_00_11_22_33_44_55")));
        agent.select(QString());
        QVERIFY(!agent.accepts(selected));
    }
    void wizardGuidesPairedPhonesAndDeduplicatesDiscovery() {
        auto *bluetooth = new CarPlayPhones(nullptr, false);
        CarPlayPhoneWizard wizard("Test Wi-Fi", "test-password", nullptr, bluetooth);
        wizard.show(); QTest::qWait(20);
        QCOMPARE(wizard.currentId(), 0);
        wizard.next(); QCOMPARE(wizard.currentId(), 0); // cannot proceed with no phone
        emit bluetooth->found("Alice", "AA:BB:CC:DD:EE:FF", true);
        emit bluetooth->found("Alice", "AA:BB:CC:DD:EE:FF", true);
        emit bluetooth->found("Bob", "00:11:22:33:44:55", true);
        auto *list = wizard.findChild<QListWidget *>("discoveredPhones");
        QCOMPARE(list->count(), 2);
        list->setCurrentRow(1);
        wizard.next(); QCOMPARE(wizard.currentId(), 1);
        QCOMPARE(wizard.ssid(), QString("Test Wi-Fi"));
        wizard.next(); QCOMPARE(wizard.currentId(), 2);
        QCOMPARE(wizard.phoneName(), QString("Bob"));
        QCOMPARE(wizard.phoneAddress(), QString("00:11:22:33:44:55"));
        wizard.reject();
        QVERIFY(!QSettings().contains("CarPlay/wifi_password"));
    }

    void decodesVideoAndMapsTouchInsideLetterboxing() {
        CarPlayWidget widget;
        widget.resize(1000, 600); widget.show();
        auto &client = *widget.findChild<CarPlayClient *>();
        auto *views = widget.findChild<QStackedWidget *>("carplayViews");
        auto *projection = widget.findChild<QWidget *>("carplayProjection");
        QVERIFY(views->currentWidget() != projection);
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
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
        auto *peer = server.nextPendingConnection();
        QVERIFY(peer);
        QCOMPARE(peer->requestUrl().query(), QString("token=fixture"));
        QSignalSpy input(peer, &QWebSocket::textMessageReceived);
        QSignalSpy status(&client, &CarPlayClient::statusChanged);
        QSignalSpy decoded(&client, &CarPlayClient::frameChanged);
        peer->sendTextMessage("{\"type\":\"session\",\"sessionActive\":true}");
        QTest::qWait(30);
        QVERIFY(views->currentWidget() != projection); // authenticated control is not video readiness
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
        QTRY_COMPARE(views->currentWidget(), projection);
        QVERIFY(!widget.findChild<QPushButton *>("addPhone")->isVisible());
        widget.showSetup();
        QVERIFY(views->currentWidget() != projection);
        QVERIFY(!client.frame().isNull()); // settings do not stop the session
        widget.findChild<QPushButton *>("resumeCarPlay")->click();
        QCOMPARE(views->currentWidget(), projection);
        QTRY_VERIFY(!input.isEmpty());
        QTRY_VERIFY([&] {
            for (const auto &entry : input)
                if (QJsonDocument::fromJson(entry.at(0).toString().toUtf8()).object().value("type") == "show-ui") return true;
            return false;
        }());
        QSignalSpy management(&widget, &CarPlayWidget::managementRequested);
        peer->sendTextMessage("{\"type\":\"host-ui\"}");
        QTRY_COMPARE(management.count(), 1);
        QVERIFY(views->currentWidget() != projection);
        QVERIFY(!client.frame().isNull());
        QCOMPARE(client.frame().size(), QSize(160, 96));

        CarPlaySurface surface(client);
        surface.resize(500, 200);
        surface.show();
        QTest::qWait(100);
        input.clear();
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
        QTRY_VERIFY(views->currentWidget() != projection);
        peer->deleteLater();
    }
 private:
    QTemporaryDir settingsDirectory_;
};

QTEST_MAIN(CarPlayNativeTest)
#include "client_test.moc"
