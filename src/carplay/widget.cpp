#ifdef DASH_CARPLAY
#include "carplay/widget.hpp"
#include "carplay/surface.hpp"
#include "carplay/phone_wizard.hpp"
#include "carplay/touch_ui.hpp"
#include "carplay/audio_settings.hpp"
#include "app/receiver_settings.hpp"
#include <QButtonGroup>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>

namespace {
bool validAddress(const QString &value) {
    return QRegularExpression("^(?:[0-9A-F]{2}:){5}[0-9A-F]{2}$").match(value).hasMatch();
}
QPushButton *button(const QString &text, const char *name, QWidget *parent) {
    return CarPlayTouch::button(text, name, parent);
}
}

CarPlayWidget::CarPlayWidget(QWidget *parent) : QWidget(parent) { init(); }

CarPlayWidget::~CarPlayWidget() {
    restartPending_ = false;
    stopReceiver();
    if (!process_.waitForFinished(3000)) { process_.kill(); process_.waitForFinished(1000); }
}

void CarPlayWidget::init() {
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    client_ = new CarPlayClient(this);
    stateDirectory_ = qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + "/.local/state") + "/opendash/carplay";
    QSettings saved;
    receiverEnabled_ = Receivers::carplayEnabled();
    pages_ = new QStackedWidget(this); pages_->setObjectName("carplayViews"); layout->addWidget(pages_);
    auto *content = new QWidget(pages_); content->setObjectName("carplayManagement");
    setup_ = content; setProperty("carplayDark", true);
    auto *outer = new QHBoxLayout(content); outer->setContentsMargins(0,0,0,0);
    auto *canvas = new QWidget(content); canvas->setMaximumSize(1040,720);
    outer->addStretch(); outer->addWidget(canvas, 1); outer->addStretch();
    auto *body = new QVBoxLayout(canvas); body->setContentsMargins(20,16,20,16); body->setSpacing(12);
    auto *header = new QHBoxLayout(); auto *titles = new QVBoxLayout(); titles->setSpacing(4);
    auto *heading = new QLabel("CarPlay", content); heading->setObjectName("touchTitle"); titles->addWidget(heading);
    status_ = new QLabel("Choose an iPhone to get started.", content);
    status_->setObjectName("carplayStatus"); status_->setProperty("secondary", true); status_->setWordWrap(true); titles->addWidget(status_);
    header->addLayout(titles, 1);
    resume_ = button("Return to CarPlay", "resumeCarPlay", content); resume_->setProperty("primary", true); resume_->setVisible(false); header->addWidget(resume_);
    body->addLayout(header);
    auto *navigation = new QHBoxLayout(); navigation->setSpacing(8); body->addLayout(navigation);
    auto *sections = new QStackedWidget(content); sections->setObjectName("carplaySections"); settings_ = sections; body->addWidget(sections, 1);
    auto *tabs = new QButtonGroup(this); tabs->setExclusive(true);
    for (const auto &title : {QString("Phones"), QString("Display"), QString("Audio"), QString("Setup")}) {
        auto *tab = button(title, "carplaySection", content); tab->setProperty("navigation", true); tab->setCheckable(true);
        const int index = tabs->buttons().size(); tabs->addButton(tab, index); navigation->addWidget(tab, 1);
        if (index == 0) tab->setChecked(true);
        if (title == "Audio") audioTab_ = tab;
        connect(tab, &QPushButton::clicked, sections, [sections, index] { sections->setCurrentIndex(index); });
    }
    auto addSection = [sections](QWidget *panel) {
        auto *scroll = new QScrollArea(sections); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setWidget(panel);
        CarPlayTouch::scroll(scroll->viewport()); sections->addWidget(scroll);
    };
    auto *phoneGroup = new QWidget(sections); auto *phoneLayout = new QVBoxLayout(phoneGroup);
    phoneLayout->setContentsMargins(0, 4, 0, 4); phoneLayout->setSpacing(12);
    auto *phoneHint = new QLabel("Your saved iPhones. One connects at a time.", phoneGroup); phoneHint->setWordWrap(true); phoneHint->setProperty("secondary", true); phoneLayout->addWidget(phoneHint);
    phones_ = new CarPlayTouchChoice("Choose an iPhone", phoneGroup); phones_->setObjectName("savedPhones");
    const int count = saved.beginReadArray("CarPlay/phones");
    for (int i = 0; i < count; ++i) {
        saved.setArrayIndex(i);
        const auto address = saved.value("address").toString().toUpper();
        if (validAddress(address) && phones_->findData(address) < 0)
            phones_->addItem(saved.value("name", "iPhone").toString(), address);
    }
    saved.endArray();
    const auto previous = saved.value("CarPlay/bluetooth_address", qEnvironmentVariable("CARPLAY_BT_ADDRESS")).toString().toUpper();
    if (validAddress(previous) && phones_->findData(previous) < 0) phones_->addItem("iPhone", previous);
    if (phones_->findData(previous) >= 0) phones_->setCurrentIndex(phones_->findData(previous));
    phoneLayout->addWidget(phones_);
    auto *phoneActions = new QHBoxLayout();
    auto *add = button("Add iPhone", "addPhone", phoneGroup);
    auto *rename = button("Rename", "renamePhone", phoneGroup);
    auto *remove = button("Remove", "removePhone", phoneGroup);
    auto *options = button("Phone options", "phoneOptions", phoneGroup);
    options->setEnabled(phones_->count() > 0);
    connect(phones_, QOverload<int>::of(&QComboBox::currentIndexChanged), options, [options](int index) { options->setEnabled(index >= 0); });
    phoneActions->addWidget(add, 1); phoneActions->addWidget(options, 1); rename->hide(); remove->hide();
    connect(options, &QPushButton::clicked, this, [this, rename, remove] {
        if (phones_->currentIndex() < 0) return;
        QDialog dialog(this); dialog.setWindowTitle(phones_->currentText()); CarPlayTouch::theme(&dialog, CarPlayTouch::dark(this));
        dialog.resize(qMin(600, window()->width() - 40), 280); auto *layout = new QVBoxLayout(&dialog); layout->setSpacing(12);
        auto *renameAction = button("Rename iPhone", "renameAction", &dialog);
        auto *removeAction = button("Remove saved iPhone", "removeAction", &dialog); removeAction->setProperty("danger", true);
        auto *cancel = button("Cancel", "cancelPhoneOptions", &dialog);
        layout->addWidget(renameAction); layout->addWidget(removeAction); layout->addWidget(cancel);
        int action = 0;
        connect(renameAction, &QPushButton::clicked, &dialog, [&] { action = 1; dialog.accept(); });
        connect(removeAction, &QPushButton::clicked, &dialog, [&] { action = 2; dialog.accept(); });
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) { if (action == 1) rename->click(); else if (action == 2) remove->click(); }
    });
    phoneLayout->addLayout(phoneActions); phoneLayout->addStretch(1); addSection(phoneGroup);

    auto *display = new QWidget(sections); auto *displayForm = new QVBoxLayout(display); displayForm->setContentsMargins(0,4,0,4); displayForm->setSpacing(12);
    resolution_ = new CarPlayTouchChoice("Resolution", display); resolution_->setObjectName("resolutionPreset");
    const QList<QSize> sizes = {{800,480}, {1024,600}, {1280,720}, {1920,1080}, {1920,720}};
    for (const auto &size : sizes)
        resolution_->addItem(QString("%1 × %2%3").arg(size.width()).arg(size.height()).arg(size == QSize(800,480) ? " · Low load" : ""), size);
    resolution_->addItem("Custom", QSize());
    width_ = new CarPlayTouchNumber(display); width_->setObjectName("carplayWidth"); width_->setRange(480, 3840); width_->setSingleStep(2);
    height_ = new CarPlayTouchNumber(display); height_->setObjectName("carplayHeight"); height_->setRange(480, 3840); height_->setSingleStep(2);
    width_->setValue(saved.value("CarPlay/width", 800).toInt()); height_->setValue(saved.value("CarPlay/height", 480).toInt());
    int preset = resolution_->findData(QSize(width_->value(), height_->value()));
    resolution_->setCurrentIndex(preset >= 0 ? preset : resolution_->count() - 1);
    auto *custom = new QWidget(display); auto *customLayout = new QVBoxLayout(custom); customLayout->setContentsMargins(0,0,0,0); customLayout->setSpacing(6);
    auto *customLabel = new QLabel("Width × height", custom); customLayout->addWidget(customLabel);
    auto *times = new QLabel("×", custom); times->setFixedWidth(24); times->setAlignment(Qt::AlignCenter);
    auto *dimensions = new QHBoxLayout(); dimensions->addWidget(width_, 1); dimensions->addWidget(times); dimensions->addWidget(height_, 1); customLayout->addLayout(dimensions);
    custom->setVisible(preset < 0);
    connect(resolution_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, custom](int index) {
        const auto size = resolution_->itemData(index).toSize(); custom->setVisible(size.isEmpty());
        if (!size.isEmpty()) { width_->setValue(size.width()); height_->setValue(size.height()); }
    });
    fps_ = new CarPlayTouchChoice("Frame rate", display); fps_->setObjectName("carplayFps"); fps_->addItem("30 fps", 30); fps_->addItem("60 fps", 60);
    fps_->setCurrentIndex(saved.value("CarPlay/fps", 30).toInt() == 60 ? 1 : 0);
    auto *displayChoices = new QHBoxLayout(); displayChoices->setSpacing(16);
    auto *resolutionColumn = new QVBoxLayout(); resolutionColumn->setSpacing(8);
    resolutionColumn->addWidget(new QLabel("Resolution", display)); resolutionColumn->addWidget(resolution_);
    auto *fpsColumn = new QVBoxLayout(); fpsColumn->setSpacing(8);
    fpsColumn->addWidget(new QLabel("Frame rate", display)); fpsColumn->addWidget(fps_);
    displayChoices->addLayout(resolutionColumn, 2); displayChoices->addLayout(fpsColumn, 1);
    displayForm->addLayout(displayChoices); displayForm->addWidget(custom);
    auto *hint = new QLabel("Higher resolutions and 60 fps need more processing power. Applying changes reconnects CarPlay.", display);
    hint->setWordWrap(true); hint->setProperty("secondary", true); displayForm->addWidget(hint); displayForm->addStretch();
    auto *apply = button("Apply && reconnect", "applyDisplay", display); displayForm->addWidget(apply);
    connect(apply, &QPushButton::clicked, this, [this] {
        if ((width_->value() % 2) || (height_->value() % 2)) {
            status_->setText("Use even pixel sizes, for example 1280 × 720."); return;
        }
        saveDisplay();
        if (process_.state() != QProcess::NotRunning) connectSelectedPhone();
        else status_->setText("Display settings saved. Connect an iPhone to use them.");
    });
    apply->setProperty("primary", true); addSection(display);

    auto *audio = new CarPlayAudioSettings(*client_, sections); sections->addWidget(audio);
    connect(audio, &CarPlayAudioSettings::microphoneModeChanged, this, [this] {
        if (process_.state() != QProcess::NotRunning) { restartPending_ = true; stopReceiver(); }
    });

    auto *advanced = new QWidget(sections); auto *form = new QFormLayout(advanced);
    form->setContentsMargins(0,4,0,4); form->setSpacing(12); form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    auto *setupHint = new QLabel("Connection details are already configured on this Pi. Change them only when using another Wi-Fi network or receiver installation.", advanced);
    setupHint->setWordWrap(true); setupHint->setProperty("secondary", true); form->addRow(setupHint);
    ssid_ = new CarPlayTouchEdit(saved.value("CarPlay/ssid", qEnvironmentVariable("CARPLAY_WIFI_SSID")).toString(), "Wi-Fi network", advanced);
    ssid_->setMaxLength(32);
    password_ = new CarPlayTouchEdit(qEnvironmentVariable("CARPLAY_WIFI_PASSPHRASE"), "Wi-Fi password", advanced); password_->setEchoMode(QLineEdit::Password);
    identity_ = new CarPlayTouchEdit(saved.value("CarPlay/identity_directory", QString(DASH_CARPLAY_IDENTITY_DIR)).toString(), "Identity folder", advanced);
    auto *identityRow = new QWidget(advanced); auto *identityLayout = new QHBoxLayout(identityRow); identityLayout->setContentsMargins(0,0,0,0);
    identityLayout->addWidget(identity_); auto *browse = button("Choose folder", "chooseIdentity", identityRow); identityLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto folder = QFileDialog::getExistingDirectory(this, "CarPlay identity folder", identity_->text());
        if (!folder.isEmpty()) identity_->setText(folder);
    });
    form->addRow("Wi-Fi network", ssid_); form->addRow("Wi-Fi password", password_); form->addRow("Identity folder", identityRow);
    addSection(advanced);

    auto *controls = new QHBoxLayout();
    start_ = button("Connect", "connectCarPlay", content); stop_ = button("Disconnect", "stopCarPlay", content);
    stop_->setEnabled(false); start_->setProperty("primary", true);
    controls->addWidget(start_, 1); controls->addWidget(stop_, 1); phoneLayout->addLayout(controls);
    pages_->addWidget(setup_); CarPlayTouch::theme(setup_, true);
    surface_ = new CarPlaySurface(*client_, pages_); surface_->setObjectName("carplayProjection"); pages_->addWidget(surface_);
    connect(resume_, &QPushButton::clicked, this, [this] {
        managing_ = false;
        client_->sendCommand({{"type", "show-ui"}});
        if (projectionReady_) pages_->setCurrentWidget(surface_);
    });
    connect(client_, &CarPlayClient::hostUiRequested, this, [this] { showSetup(); emit managementRequested(); });
    connect(client_, &CarPlayClient::frameChanged, this, [this] {
        const bool ready = bool(client_->videoFrame());
        if (ready != projectionReady_) {
            projectionReady_ = ready;
            if (!ready) managing_ = false;
            pages_->setCurrentWidget(ready && !managing_ ? surface_ : setup_);
            resume_->setVisible(ready);
        }
    }, Qt::QueuedConnection);
    connect(add, &QPushButton::clicked, this, [this] {
        CarPlayPhoneWizard wizard(ssid_->text(), password_->text(), this);
        if (wizard.exec() != QDialog::Accepted) return;
        ssid_->setText(wizard.ssid()); password_->setText(wizard.password());
        addPhone(wizard.phoneName(), wizard.phoneAddress()); connectSelectedPhone();
    });
    connect(rename, &QPushButton::clicked, this, [this] {
        if (phones_->currentIndex() < 0) return;
        auto name = phones_->currentText();
        if (CarPlayTouch::editText(this, "Phone name", name, false, 80) && !name.trimmed().isEmpty()) {
            phones_->setItemText(phones_->currentIndex(), name.trimmed()); savePhones();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (phones_->currentIndex() < 0) return;
        if (!CarPlayTouch::confirm(this, "Remove saved iPhone?", "Remove " + phones_->currentText() + " from Dash? You can add it again without pairing.", "Remove iPhone")) return;
        restartPending_ = false; stopReceiver(); phones_->removeItem(phones_->currentIndex()); savePhones();
    });
    connect(start_, &QPushButton::clicked, this, &CarPlayWidget::connectSelectedPhone);
    connect(stop_, &QPushButton::clicked, this, [this] { restartPending_ = false; stopReceiver(); });
    connect(client_, &CarPlayClient::statusChanged, this, [this](const QString &message) {
        if (receiverEnabled_) status_->setText(message);
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { process_.readAllStandardOutput(); });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] { process_.readAllStandardError(); });
    connect(&process_, &QProcess::started, this, [this] {
        start_->setEnabled(receiverEnabled_);
        if (!receiverEnabled_) stopReceiver();
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            restartPending_ = false;
            status_->setText(receiverEnabled_ ? "CarPlay could not start. Check Setup or the receiver installation." :
                "CarPlay receiver is disabled. Enable it in Settings → Receivers.");
            start_->setEnabled(receiverEnabled_); stop_->setEnabled(false); poll_.stop();
        }
    });
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
        poll_.stop(); client_->disconnectReceiver(); start_->setEnabled(receiverEnabled_); stop_->setEnabled(false);
        if (!receiverEnabled_) { status_->setText("CarPlay receiver is disabled. Enable it in Settings → Receivers."); return; }
        if (restartPending_) { restartPending_ = false; QTimer::singleShot(0, this, &CarPlayWidget::startReceiver); }
        else status_->setText(code == 0 ? "CarPlay disconnected. Choose an iPhone to reconnect." : "CarPlay receiver stopped. Try connecting again; Setup has the receiver details.");
    });
    poll_.setInterval(500);
    connect(&poll_, &QTimer::timeout, this, [this] { client_->connectFile(stateDirectory_ + "/connection.json"); });
    if (!receiverEnabled_) {
        start_->setEnabled(false);
        status_->setText("CarPlay receiver is disabled. Enable it in Settings → Receivers.");
    }
    if (receiverEnabled_ && qEnvironmentVariableIntValue("CARPLAY_AUTOSTART") == 1 && phones_->count())
        QTimer::singleShot(0, this, &CarPlayWidget::startReceiver);
}

void CarPlayWidget::showSetup() { managing_ = true; pages_->setCurrentWidget(setup_); }
void CarPlayWidget::showAudioSettings() { showSetup(); audioTab_->click(); }

void CarPlayWidget::setNightMode(bool night) {
    client_->setNightMode(night); setProperty("carplayDark", night); updateTheme();
}

void CarPlayWidget::setAccentColor(const QColor &color) {
    if (!color.isValid()) return;
    setProperty("carplayAccent", color); updateTheme();
}

void CarPlayWidget::updateTheme() {
    const bool night = CarPlayTouch::dark(this);
    CarPlayTouch::theme(setup_, night);
    // Dialogs have their own stylesheet. Keep an open wizard, picker or keyboard
    // in sync as well as the management page; new dialogs inherit this accent.
    for (auto *dialog : findChildren<QDialog *>()) CarPlayTouch::theme(dialog, night);
}

void CarPlayWidget::addPhone(const QString &name, const QString &address) {
    const auto normalized = address.toUpper(); if (!validAddress(normalized)) return;
    int index = phones_->findData(normalized);
    if (index < 0) { phones_->addItem(name, normalized); index = phones_->count() - 1; }
    else phones_->setItemText(index, name);
    phones_->setCurrentIndex(index); savePhones();
}

void CarPlayWidget::savePhones() {
    QSettings saved; saved.remove("CarPlay/phones"); saved.beginWriteArray("CarPlay/phones", phones_->count());
    for (int i = 0; i < phones_->count(); ++i) {
        saved.setArrayIndex(i); saved.setValue("name", phones_->itemText(i)); saved.setValue("address", phones_->itemData(i));
    }
    saved.endArray(); saved.setValue("CarPlay/bluetooth_address", phones_->currentData().toString()); saved.sync();
}

void CarPlayWidget::saveDisplay() {
    QSettings saved; saved.setValue("CarPlay/width", width_->value()); saved.setValue("CarPlay/height", height_->value());
    saved.setValue("CarPlay/fps", fps_->currentData()); saved.sync();
}

void CarPlayWidget::connectSelectedPhone() {
    if (!receiverEnabled_) { status_->setText("Enable the CarPlay receiver in Settings → Receivers first."); return; }
    if (androidAutoActive_) { status_->setText("Disconnect Android Auto before starting CarPlay."); return; }
    if (!validAddress(phones_->currentData().toString())) { status_->setText("Choose a saved iPhone or use Add iPhone first."); return; }
    if ((width_->value() % 2) || (height_->value() % 2)) { status_->setText("Resolution width and height must be even numbers."); return; }
    savePhones(); saveDisplay(); managing_ = false;
    if (process_.state() != QProcess::NotRunning) { restartPending_ = true; stopReceiver(); }
    else startReceiver();
}

void CarPlayWidget::startReceiver() {
    if (!receiverEnabled_) return;
    if (process_.state() != QProcess::NotRunning) return;
    if (androidAutoActive_) { status_->setText("Disconnect Android Auto before starting CarPlay."); return; }
    if (!validAddress(phones_->currentData().toString())) { status_->setText("Add an iPhone to start CarPlay."); return; }
    if ((width_->value() % 2) || (height_->value() % 2)) { status_->setText("Resolution width and height must be even numbers."); return; }
    QSettings saved;
    saved.setValue("CarPlay/ssid", ssid_->text()); saved.setValue("CarPlay/bluetooth_address", phones_->currentData());
    saved.setValue("CarPlay/identity_directory", identity_->text());
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("CARPLAY_IDENTITY_DIR", identity_->text()); environment.insert("CARPLAY_STATE_DIR", stateDirectory_);
    environment.insert("CARPLAY_WIFI_SSID", ssid_->text()); environment.insert("CARPLAY_WIFI_PASSPHRASE", password_->text());
    environment.insert("CARPLAY_BT_ADDRESS", phones_->currentData().toString());
    environment.insert("CARPLAY_WIDTH", QString::number(width_->value())); environment.insert("CARPLAY_HEIGHT", QString::number(height_->value()));
    environment.insert("CARPLAY_FPS", QString::number(fps_->currentData().toInt()));
    environment.insert("CARPLAY_MIC_MODE", client_->audioSettings().microphoneMode);
    process_.setProcessEnvironment(environment);
    process_.setProgram(saved.value("CarPlay/receiver_command", QString(DASH_CARPLAY_RUNNER)).toString()); process_.setArguments(QStringList());
    start_->setEnabled(false); stop_->setEnabled(true);
    status_->setText("Connecting to " + phones_->currentText() + "… Approve CarPlay on the iPhone if asked.");
    process_.start(); poll_.start();
}

void CarPlayWidget::stopReceiver() {
    poll_.stop(); if (client_) client_->disconnectReceiver();
    const auto pid = process_.processId(); process_.terminate();
    if (pid) QTimer::singleShot(3000, this, [this, pid] {
        if (process_.processId() == pid && process_.state() != QProcess::NotRunning) process_.kill();
    });
}

void CarPlayWidget::setReceiverEnabled(bool enabled) {
    const bool wasEnabled = receiverEnabled_; receiverEnabled_ = enabled;
    QSettings saved; saved.setValue("Receivers/carplay_enabled", enabled); saved.sync();
    if (!enabled) {
        restartPending_ = false; stopReceiver(); start_->setEnabled(false); stop_->setEnabled(false);
        status_->setText("CarPlay receiver is disabled. Enable it in Settings → Receivers.");
    } else if (!wasEnabled) {
        start_->setEnabled(true); status_->setText("CarPlay receiver enabled. Choose an iPhone to connect.");
        if (qEnvironmentVariableIntValue("CARPLAY_AUTOSTART") == 1 && phones_->count()) {
            if (process_.state() != QProcess::NotRunning) restartPending_ = true;
            else QTimer::singleShot(0, this, &CarPlayWidget::startReceiver);
        }
    }
}

void CarPlayWidget::setAndroidAutoActive(bool active) {
    androidAutoActive_ = active;
    if (active) { restartPending_ = false; if (process_.state() != QProcess::NotRunning) stopReceiver(); }
}
#endif
