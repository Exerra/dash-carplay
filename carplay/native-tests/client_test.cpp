#include "carplay/client.hpp"
#include "carplay/audio_output.hpp"
#include "carplay/audio_settings.hpp"
#include "carplay/surface.hpp"
#include "carplay/widget.hpp"
#include "carplay/pairing_agent.hpp"
#include "carplay/phone_wizard.hpp"
#include "carplay/touch_ui.hpp"
#include "app/receiver_settings.hpp"
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFocusEvent>
#include <QVBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSettings>
#include <QSlider>
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
        settings.remove("Receivers");
        settings.setValue("CarPlay/bluetooth_address", "00:11:22:33:44:55");
    }

    void audioSettingsPersistWithoutStartingAReceiverAndKeepVoiceSeparate() {
        CarPlayWidget widget;
        auto *client = widget.findChild<CarPlayClient *>(); QVERIFY(client);
        auto *mode = widget.findChild<QComboBox *>("carplayMicrophoneMode"); QVERIFY(mode);
        QCOMPARE(mode->count(), 3);
        mode->setCurrentIndex(mode->findData("real"));
        auto *latency = widget.findChild<QComboBox *>("carplayAudioLatency");
        latency->setCurrentIndex(latency->findData(120));
        widget.findChild<QSlider *>("carplayMusicVolume")->setValue(75);
        widget.findChild<QSlider *>("carplayVoiceVolume")->setValue(40);
        widget.findChild<QPushButton *>("applyCarPlayAudio")->click();
        const auto saved = CarPlayAudio::loadSettings();
        QCOMPARE(saved.music,75); QCOMPARE(saved.voice,40); QCOMPARE(saved.buffer,120);
        QCOMPARE(saved.microphoneMode,QString("real"));
        CarPlayWidget reopened;
        QCOMPARE(reopened.findChild<CarPlayClient *>()->audioSettings().voice,40);
        QVERIFY(CarPlayAudio::isMusic(102,"media"));
        QVERIFY(CarPlayAudio::isMusic(100,"default"));
        QVERIFY(!CarPlayAudio::isMusic(101,"default"));
        QVERIFY(!CarPlayAudio::isMusic(100,"speechRecognition"));
        QVERIFY(!CarPlayAudio::isMusic(100,"alert"));
        QVERIFY(!CarPlayAudio::createInput("[\"pulsesink\",\"wrong-direction\"]"));
        QVERIFY(!CarPlayAudio::createOutput("[\"fakesrc\",\"invalid\"]"));
    }

    void mappedFramesKeepDecoderMemoryAliveAndConvertPaddedPlanes() {
        gst_init(nullptr, nullptr);
        GstVideoInfo info;
        gst_video_info_set_format(&info, GST_VIDEO_FORMAT_I420, 162, 98);
        info.fps_n=30; info.fps_d=1;
        info.colorimetry.matrix=GST_VIDEO_COLOR_MATRIX_BT601;
        info.colorimetry.range=GST_VIDEO_COLOR_RANGE_16_235;
        auto *buffer = gst_buffer_new_allocate(nullptr, info.size, nullptr);
        GstVideoFrame writable;
        QVERIFY(gst_video_frame_map(&writable, &info, buffer, GST_MAP_WRITE));
        const int values[] = {81,90,240}; // Limited-range BT.601 red, with padded rows.
        for (int plane=0; plane<3; ++plane)
            memset(GST_VIDEO_FRAME_PLANE_DATA(&writable, plane), values[plane],
                GST_VIDEO_FRAME_PLANE_STRIDE(&writable, plane)*(plane ? 49 : 98));
        gst_video_frame_unmap(&writable);
        auto *caps = gst_video_info_to_caps(&info);
        auto *sample = gst_sample_new(buffer, caps, nullptr, nullptr);
        auto frame = CarPlayVideoFrame::map(sample);
        gst_sample_unref(sample); gst_caps_unref(caps); gst_buffer_unref(buffer);
        QVERIFY(frame); QCOMPARE(frame->size(), QSize(162,98));
        QVERIFY(frame->stride(0) > 162);
        const auto image = frame->image();
        QVERIFY(!image.isNull());
        frame.reset(); // The RGB fallback snapshot must also own its storage.
        for (const auto &point : {QPoint(0,0), QPoint(161,97)}) {
            const auto pixel = image.pixelColor(point);
            QVERIFY(pixel.red() >= 245); QVERIFY(pixel.green() <= 10); QVERIFY(pixel.blue() <= 10);
        }
    }

    void audioLatencyOnlyChangesOutputBufferingAndSupportsRollback() {
        gst_init(nullptr, nullptr);
        const bool hadOverride = qEnvironmentVariableIsSet("CARPLAY_AUDIO_BUFFER_MS");
        const auto previous = qgetenv("CARPLAY_AUDIO_BUFFER_MS");
        struct Restore { bool had; QByteArray value; ~Restore() {
            if (had) qputenv("CARPLAY_AUDIO_BUFFER_MS", value); else qunsetenv("CARPLAY_AUDIO_BUFFER_MS");
        }} restore{hadOverride, previous};
        qunsetenv("CARPLAY_AUDIO_BUFFER_MS");
        QCOMPARE(CarPlayAudio::bufferMs(), 60);
        for (const auto &invalid : {"0", "39", "501", "invalid"}) {
            qputenv("CARPLAY_AUDIO_BUFFER_MS", invalid); QCOMPARE(CarPlayAudio::bufferMs(), 60);
        }
        auto *fake = gst_element_factory_make("fakesink", nullptr);
        QVERIFY(fake); QVERIFY(!CarPlayAudio::configureOutput(fake)); gst_object_unref(fake);
        GstElement *sink = nullptr;
        for (const auto *factory : {"pulsesink", "alsasink", "osxaudiosink", "wasapisink"}) {
            sink = gst_element_factory_make(factory, nullptr); if (sink) break;
        }
        QVERIFY2(sink, "An audio sink plugin is required; no device is opened by this test");
        gboolean syncBefore = false; g_object_get(sink, "sync", &syncBefore, nullptr);
        for (const int target : {60, 200, 40}) {
            qputenv("CARPLAY_AUDIO_BUFFER_MS", QByteArray::number(target));
            QVERIFY(CarPlayAudio::configureOutput(sink));
            gint64 buffer = 0, period = 0; gboolean sync = false;
            g_object_get(sink, "buffer-time", &buffer, "latency-time", &period, "sync", &sync, nullptr);
            QCOMPARE(buffer, gint64(target)*1000); QCOMPARE(period, gint64(10000)); QCOMPARE(sync, syncBefore);
        }
        // autoaudiosink adds its selected output inside a nested bin at READY.
        // Exercise that late-child path without opening an audio device.
        g_object_set(sink, "buffer-time", gint64(200000), nullptr);
        qputenv("CARPLAY_AUDIO_BUFFER_MS", "60");
        auto *pipeline = gst_pipeline_new(nullptr);
        CarPlayAudio::configurePipeline(pipeline);
        auto *nested = gst_bin_new(nullptr);
        QVERIFY(gst_bin_add(GST_BIN(pipeline), nested));
        QVERIFY(gst_bin_add(GST_BIN(nested), sink));
        gint64 applied = 0; g_object_get(sink, "buffer-time", &applied, nullptr);
        QCOMPARE(applied, gint64(60000));
        gst_object_unref(pipeline);
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

    void receiverSettingsAreIndependentAndOnlyAndroidAutoNeedsRestart() {
        bool carplayEnabled = true;
        ReceiverSettings panel(true, [&](bool enabled) { carplayEnabled = enabled; });
        QSignalSpy restart(&panel, &ReceiverSettings::restartRequested);
        auto *android = panel.findChild<QCheckBox *>("androidAutoReceiverEnabled");
        auto *carplay = panel.findChild<QCheckBox *>("carplayReceiverEnabled");
        auto *apply = panel.findChild<QPushButton *>("applyReceivers");
        QVERIFY(android->isChecked()); QVERIFY(carplay->isChecked());
        carplay->setChecked(false); apply->click();
        QVERIFY(!carplayEnabled); QVERIFY(Receivers::androidAutoEnabled()); QVERIFY(!Receivers::carplayEnabled());
        QCOMPARE(restart.count(), 0);
        android->setChecked(false); apply->click();
        QVERIFY(!Receivers::androidAutoEnabled()); QVERIFY(!Receivers::carplayEnabled()); QCOMPARE(restart.count(), 1);
        ReceiverSettings restored(false, [&](bool enabled) { carplayEnabled = enabled; });
        QVERIFY(!restored.findChild<QCheckBox *>("androidAutoReceiverEnabled")->isChecked());
        restored.findChild<QCheckBox *>("carplayReceiverEnabled")->setChecked(true);
        restored.findChild<QPushButton *>("applyReceivers")->click();
        QVERIFY(carplayEnabled); QVERIFY(!Receivers::androidAutoEnabled()); QVERIFY(Receivers::carplayEnabled());
    }

    void disabledCarPlayBlocksAutostartAndManualStartAndStopsAnActiveReceiver() {
        const auto marker = settingsDirectory_.filePath("receiver-was-started"); QFile::remove(marker);
        const auto runner = settingsDirectory_.filePath("disabled-receiver");
        QFile executable(runner); QVERIFY(executable.open(QIODevice::WriteOnly));
        executable.write(("#!/bin/sh\ntouch '" + marker + "'\nexec sleep 30\n").toUtf8()); executable.close();
        QVERIFY(executable.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QSettings settings; settings.setValue("CarPlay/receiver_command", runner);
        settings.setValue("Receivers/carplay_enabled", false);
        const bool hadAutostart = qEnvironmentVariableIsSet("CARPLAY_AUTOSTART");
        const auto autostart = qgetenv("CARPLAY_AUTOSTART"); qputenv("CARPLAY_AUTOSTART", "1");
        CarPlayWidget widget;
        if (hadAutostart) qputenv("CARPLAY_AUTOSTART", autostart); else qunsetenv("CARPLAY_AUTOSTART");
        auto *start = widget.findChild<QPushButton *>("connectCarPlay");
        QVERIFY(!start->isEnabled()); start->click(); QTest::qWait(30); QVERIFY(!QFile::exists(marker));
        widget.setAndroidAutoActive(true); widget.setAndroidAutoActive(false);
        QVERIFY(!start->isEnabled());
        widget.setReceiverEnabled(true); start->click(); QTRY_VERIFY(QFile::exists(marker));
        widget.setReceiverEnabled(false); QVERIFY(!widget.phoneActive());
        QTRY_VERIFY(!widget.findChild<QPushButton *>("stopCarPlay")->isEnabled());
        QTest::qWait(100); QVERIFY(!start->isEnabled()); QVERIFY(!Receivers::carplayEnabled());
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

    void sharedPhoneActionsChooseOneBackendAndKeepReleasesWithIt() {
        using Command = PhoneControls::Command;
        using State = PhoneControls::State;
        bool android = false, carplay = true;
        std::vector<std::pair<Command, State>> aa, cp;
        PhoneControls::Router router([&] { return android; }, [&](Command c, State s) { aa.emplace_back(c,s); },
            [&] { return carplay; }, [&](Command c, State s) { cp.emplace_back(c,s); });
        const auto &bindings = PhoneControls::bindings();
        QCOMPARE(bindings.size(), size_t(17));
        QCOMPARE(bindings[1].command, Command::Enter); QCOMPARE(bindings[6].command, Command::Home);
        QCOMPARE(bindings[13].command, Command::TogglePlay); QCOMPARE(bindings[16].command, Command::ScrollDown);
        for (const auto &binding : bindings) router.dispatch(binding.command, State::Triggered);
        QCOMPARE(cp.size(), size_t(17)); QVERIFY(aa.empty());
        for (size_t i = 0; i < bindings.size(); ++i) QCOMPARE(cp[i].first, bindings[i].command);
        android = true; // Android Auto retains priority even during an overlapping disconnect.
        for (const auto &binding : bindings) router.dispatch(binding.command, State::Triggered);
        QCOMPARE(aa.size(), size_t(17)); QCOMPARE(cp.size(), size_t(17));
        android = false;
        router.dispatch(Command::Enter, State::Pressed); router.dispatch(Command::Enter, State::Pressed);
        QCOMPARE(cp.size(), size_t(18));
        router.dispatch(Command::Enter, State::Released); QCOMPARE(cp.size(), size_t(19));
        QCOMPARE(cp.back().second, State::Released);
        router.dispatch(Command::Voice, State::Pressed);
        android = true; carplay = false;
        router.dispatch(Command::Voice, State::Released);
        QCOMPARE(aa.size(), size_t(17)); // A CarPlay release must not invoke Android Auto.
        android = false;
        router.dispatch(Command::Play, State::Triggered); QCOMPARE(cp.size(), size_t(20));
        router.dispatch(Command::Home, State::Pressed); // Pressed while neither phone is connected.
        carplay = true; router.dispatch(Command::Home, State::Released);
        QCOMPARE(cp.size(), size_t(20));
    }

    void appearanceIsSentBeforePhoneConnectionAndReappliedForTheSession() {
        CarPlayClient client; client.setNightMode(true);
        QWebSocketServer server("appearance", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTemporaryDir directory; QFile file(directory.filePath("connection.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"url", QString("ws://127.0.0.1:%1/ws?token=fixture").arg(server.serverPort())}}).toJson()); file.close();
        QVERIFY(client.connectFile(file.fileName())); QTRY_VERIFY(server.hasPendingConnections());
        auto *peer = server.nextPendingConnection(); QSignalSpy input(peer, &QWebSocket::textMessageReceived);
        auto night = [&](int index) {
            const auto message = QJsonDocument::fromJson(input.at(index).at(0).toString().toUtf8()).object();
            return message.value("type").toString() == "night" && message.value("night").toBool();
        };
        peer->sendTextMessage("{\"type\":\"hello\",\"sessionActive\":false}");
        QTRY_COMPARE(input.size(), 1); QVERIFY(night(0)); QVERIFY(!client.sessionActive());
        client.setNightMode(false); QTRY_COMPARE(input.size(), 2); QVERIFY(!night(1));
        peer->sendTextMessage("{\"type\":\"session\",\"sessionActive\":true}");
        QTRY_COMPARE(input.size(), 3); QVERIFY(!night(2)); QVERIFY(client.sessionActive());
        client.setNightMode(true); QTRY_COMPARE(input.size(), 4); QVERIFY(night(3));
        client.disconnectReceiver(); peer->deleteLater();
    }

    void sharedControlsReachCarPlayWithHeldStatesAndNoStaleSessionButtons() {
        using Command = PhoneControls::Command;
        using State = PhoneControls::State;
        CarPlayClient client;
        client.sendControl(Command::Play, State::Triggered); // Receiver availability alone is insufficient.
        QWebSocketServer server("controls", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTemporaryDir directory; QFile file(directory.filePath("connection.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"url", QString("ws://127.0.0.1:%1/ws?token=fixture").arg(server.serverPort())}}).toJson()); file.close();
        QVERIFY(client.connectFile(file.fileName())); QTRY_VERIFY(server.hasPendingConnections());
        auto *peer = server.nextPendingConnection(); QSignalSpy input(peer, &QWebSocket::textMessageReceived);
        client.sendControl(Command::Play, State::Triggered); QTest::qWait(10); QVERIFY(input.isEmpty());
        peer->sendTextMessage("{\"type\":\"session\",\"sessionActive\":true}"); QTRY_VERIFY(client.sessionActive());
        QTRY_VERIFY(!input.isEmpty()); input.clear();
        auto message = [&](int index) { return QJsonDocument::fromJson(input.at(index).at(0).toString().toUtf8()).object(); };
        const std::array<std::pair<Command,int>,5> media = {{{Command::Play,1},{Command::Pause,2},
            {Command::TogglePlay,3},{Command::Next,4},{Command::Previous,5}}};
        for (const auto &key : media) client.sendControl(key.first, State::Triggered);
        QTRY_COMPARE(input.size(), 5);
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(message(i).value("type").toString(), QString("media"));
            QCOMPARE(message(i).value("media").toInt(), media[i].second);
            QVERIFY(!message(i).contains("pressed"));
        }
        input.clear();
        for (auto command : {Command::Phone, Command::CallEnd, Command::Voice}) {
            client.sendControl(command, State::Pressed); client.sendControl(command, State::Released);
        }
        QTRY_COMPARE(input.size(), 6);
        QCOMPARE(message(0).value("telephony").toInt(), 1); QCOMPARE(message(2).value("telephony").toInt(), 3);
        QCOMPARE(message(4).value("type").toString(), QString("siri"));
        for (int i = 0; i < 6; ++i) QCOMPARE(message(i).value("pressed").toBool(), i % 2 == 0);
        input.clear();
        client.sendControl(Command::Enter, State::Pressed);
        client.sendControl(Command::Right, State::Pressed);
        client.sendControl(Command::ScrollUp, State::Triggered);
        client.sendControl(Command::ScrollUp, State::Released);
        client.sendControl(Command::Home, State::Triggered);
        client.sendControl(Command::Right, State::Released);
        client.sendControl(Command::Enter, State::Released);
        QTRY_COMPARE(input.size(), 7);
        QVERIFY(message(1).value("knob").toObject().value("select").toBool());
        QCOMPARE(message(1).value("knob").toObject().value("x").toInt(), 127);
        QCOMPARE(message(2).value("knob").toObject().value("wheel").toInt(), 1);
        QVERIFY(message(2).value("knob").toObject().value("select").toBool());
        QVERIFY(message(3).value("knob").toObject().value("home").toBool());
        QVERIFY(!message(4).value("knob").toObject().value("home").toBool());
        QVERIFY(message(4).value("knob").toObject().value("select").toBool());
        QCOMPARE(message(5).value("knob").toObject().value("x").toInt(), 0);
        QVERIFY(!message(6).value("knob").toObject().value("select").toBool());
        input.clear();
        CarPlaySurface surface(client); surface.show();
        QTest::keyPress(&surface, Qt::Key_Left); QTest::keyRelease(&surface, Qt::Key_Left);
        QTest::keyPress(&surface, Qt::Key_Return);
        QFocusEvent focusOut(QEvent::FocusOut); QApplication::sendEvent(&surface, &focusOut);
        QTRY_COMPARE(input.size(), 4);
        QCOMPARE(message(0).value("knob").toObject().value("x").toInt(), -127);
        QCOMPARE(message(1).value("knob").toObject().value("x").toInt(), 0);
        QVERIFY(!message(3).value("knob").toObject().value("select").toBool());
        client.sendControl(Command::Enter, State::Pressed);
        peer->sendTextMessage("{\"type\":\"session\",\"sessionActive\":false}"); QTRY_VERIFY(!client.sessionActive());
        peer->sendTextMessage("{\"type\":\"session\",\"sessionActive\":true}"); QTRY_VERIFY(client.sessionActive());
        QTest::qWait(10); input.clear(); client.sendControl(Command::Down, State::Triggered);
        QTRY_COMPARE(input.size(), 2);
        QVERIFY(!message(0).value("knob").toObject().value("select").toBool());
        QCOMPARE(message(0).value("knob").toObject().value("y").toInt(), 127);
        client.disconnectReceiver(); QVERIFY(!client.sessionActive()); peer->deleteLater();
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

    void touchSelectorsAndKeyboardKeepEditsUntilConfirmed() {
        QWidget parent; parent.resize(800, 480); parent.show();
        CarPlayTouchChoice choice("Choose an iPhone", &parent);
        choice.addItems({"Alice", "Bob"});
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *list = dialog->findChild<QListWidget *>("touchChoices");
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(1)).center());
        });
        choice.showPopup(); QCOMPARE(choice.currentText(), QString("Bob"));
        QString value = "Original";
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            dialog->findChild<QLineEdit *>("keyboardValue")->setText("Discard this");
            dialog->findChild<QPushButton *>("cancelKeyboard")->click();
        });
        QVERIFY(!CarPlayTouch::editText(&parent, "Phone name", value));
        QCOMPARE(value, QString("Original"));
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            dialog->findChild<QPushButton *>("keyboardShift")->click();
            for (auto *key : dialog->findChildren<QPushButton *>("keyboardKey"))
                if (key->property("character") == "A") key->click();
            dialog->findChild<QPushButton *>("keyboardMode")->click();
            for (auto *key : dialog->findChildren<QPushButton *>("keyboardKey"))
                if (key->property("character") == "&") key->click();
            dialog->findChild<QPushButton *>("saveKeyboard")->click();
        });
        QVERIFY(CarPlayTouch::editText(&parent, "Phone name", value));
        QCOMPARE(value, QString("A&"));
    }

    void numberPadRejectsOddAndOutOfRangeResolutions() {
        QWidget parent; parent.resize(800, 480); parent.show();
        CarPlayTouchNumber number(&parent); number.setRange(480, 3840); number.setValue(800);
        bool rejectsOdd = false, rejectsSmall = false, acceptsEven = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *save = dialog->findChild<QPushButton *>("saveNumber");
            auto enter = [&](const QString &digits) {
                const auto keys = dialog->findChildren<QPushButton *>("numberKey");
                for (auto *key : keys) if (key->text() == "Clear") key->click();
                for (auto digit : digits) for (auto *key : keys) if (key->text() == QString(digit)) key->click();
            };
            enter("1279"); rejectsOdd = !save->isEnabled();
            enter("12"); rejectsSmall = !save->isEnabled();
            enter("1280"); acceptsEven = save->isEnabled();
            if (acceptsEven) save->click(); else dialog->reject();
        });
        QTest::mouseClick(number.findChild<QLineEdit *>(), Qt::LeftButton);
        QVERIFY(rejectsOdd); QVERIFY(rejectsSmall); QVERIFY(acceptsEven);
        QCOMPARE(number.value(), 1280);
    }

    void exportTouchPreviewsWhenRequested() {
        const auto directory = qEnvironmentVariable("CARPLAY_UI_PREVIEW_DIR");
        if (directory.isEmpty()) QSKIP("Set CARPLAY_UI_PREVIEW_DIR to export UI screenshots");
        QDir().mkpath(directory);
        CarPlayWidget widget; widget.resize(800,480); widget.show();
        auto *phones = widget.findChild<QComboBox *>("savedPhones"); phones->setItemText(0, "Sithis");
        auto capture = [&](QWidget *view, const QString &name) { QTest::qWait(30); view->grab().save(directory + "/" + name + ".png"); };
        capture(&widget, "phones-800");
        for (auto *tab : widget.findChildren<QPushButton *>("carplaySection")) {
            if (tab->text() == "Phones") continue;
            tab->click(); capture(&widget, tab->text().toLower() + "-800");
        }
        auto selectSection = [&](const QString &title) {
            for (auto *tab : widget.findChildren<QPushButton *>("carplaySection")) if (tab->text() == title) tab->click();
        };
        selectSection("Display");
        auto *resolution = widget.findChild<QComboBox *>("resolutionPreset"); resolution->setCurrentIndex(resolution->count()-1);
        capture(&widget, "display-custom-800");
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            capture(dialog, "keyboard-800"); dialog->reject();
        });
        QString value = "My iPhone"; CarPlayTouch::editText(&widget, "Phone name", value);
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            capture(dialog, "number-800"); dialog->reject();
        });
        QTest::mouseClick(widget.findChild<QSpinBox *>("carplayWidth")->findChild<QLineEdit *>(), Qt::LeftButton);
        auto *bluetooth = new CarPlayPhones(nullptr, false);
        CarPlayPhoneWizard wizard("OpenDash-CarPlay", "example-password", &widget, bluetooth);
        wizard.resize(760,440); wizard.show();
        emit bluetooth->found("Sithis", "00:11:22:33:44:55", true);
        wizard.findChild<QListWidget *>("discoveredPhones")->setCurrentRow(0);
        capture(&wizard, "wizard-phone-800"); wizard.next(); capture(&wizard, "wizard-wifi-800");
        wizard.next(); capture(&wizard, "wizard-finish-800"); wizard.reject();
        selectSection("Setup");
        widget.setNightMode(false); capture(&widget, "setup-light-800");
        widget.resize(1920,1080); selectSection("Phones"); capture(&widget, "phones-1920");
        QDialog receiverHost; receiverHost.resize(800,480);
        auto *receiverLayout = new QVBoxLayout(&receiverHost); receiverLayout->setContentsMargins(0,0,0,0);
        receiverLayout->addWidget(new ReceiverSettings(true, {}, &receiverHost)); receiverHost.show();
        CarPlayTouch::theme(&receiverHost, true); capture(&receiverHost, "receivers-dark-800");
        CarPlayTouch::theme(&receiverHost, false); capture(&receiverHost, "receivers-light-800");
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
