#ifdef DASH_CARPLAY
#include "carplay/widget.hpp"
#include "carplay/surface.hpp"
#include "carplay/phone_wizard.hpp"
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
bool validAddress(const QString &value) {
    return QRegularExpression("^(?:[0-9A-F]{2}:){5}[0-9A-F]{2}$").match(value).hasMatch();
}
QPushButton *button(const QString &text, const char *name, QWidget *parent) {
    auto *result = new QPushButton(text, parent);
    result->setObjectName(name); result->setMinimumHeight(44); return result;
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
    pages_ = new QStackedWidget(this); pages_->setObjectName("carplayViews"); layout->addWidget(pages_);
    auto *scroll = new QScrollArea(pages_); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll); auto *body = new QVBoxLayout(content);
    body->setContentsMargins(24, 16, 24, 16); body->setSpacing(16);
    auto *heading = new QLabel("CarPlay", content);
    QFont title = heading->font(); title.setBold(true); heading->setFont(title); body->addWidget(heading);
    status_ = new QLabel("Choose a saved iPhone or add a new one. One phone connects at a time.", content);
    status_->setObjectName("carplayStatus"); status_->setWordWrap(true); body->addWidget(status_);
    settings_ = new QWidget(content); auto *settingsLayout = new QVBoxLayout(settings_);
    settingsLayout->setContentsMargins(0, 0, 0, 0); settingsLayout->setSpacing(16);

    auto *phoneGroup = new QGroupBox("Your iPhones", settings_); auto *phoneLayout = new QVBoxLayout(phoneGroup);
    phones_ = new QComboBox(phoneGroup); phones_->setObjectName("savedPhones"); phones_->setMinimumHeight(44);
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
    phoneActions->addWidget(add); phoneActions->addWidget(rename); phoneActions->addWidget(remove);
    phoneLayout->addLayout(phoneActions); settingsLayout->addWidget(phoneGroup);

    auto *display = new QGroupBox("Display", settings_); auto *displayForm = new QFormLayout(display);
    resolution_ = new QComboBox(display); resolution_->setObjectName("resolutionPreset");
    const QList<QSize> sizes = {{800,480}, {1024,600}, {1280,720}, {1920,1080}, {1920,720}};
    for (const auto &size : sizes)
        resolution_->addItem(QString("%1 × %2%3").arg(size.width()).arg(size.height()).arg(size == QSize(800,480) ? " · Low load" : ""), size);
    resolution_->addItem("Custom", QSize());
    width_ = new QSpinBox(display); width_->setObjectName("carplayWidth"); width_->setRange(480, 3840); width_->setSingleStep(2);
    height_ = new QSpinBox(display); height_->setObjectName("carplayHeight"); height_->setRange(480, 3840); height_->setSingleStep(2);
    width_->setValue(saved.value("CarPlay/width", 800).toInt()); height_->setValue(saved.value("CarPlay/height", 480).toInt());
    int preset = resolution_->findData(QSize(width_->value(), height_->value()));
    resolution_->setCurrentIndex(preset >= 0 ? preset : resolution_->count() - 1);
    auto *custom = new QWidget(display); auto *customLayout = new QHBoxLayout(custom); customLayout->setContentsMargins(0,0,0,0);
    customLayout->addWidget(width_); customLayout->addWidget(new QLabel("×", custom)); customLayout->addWidget(height_);
    custom->setVisible(preset < 0);
    auto *customLabel = new QLabel("Custom size", display); customLabel->setVisible(preset < 0);
    connect(resolution_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, custom, customLabel](int index) {
        const auto size = resolution_->itemData(index).toSize(); custom->setVisible(size.isEmpty());
        customLabel->setVisible(size.isEmpty());
        if (!size.isEmpty()) { width_->setValue(size.width()); height_->setValue(size.height()); }
    });
    fps_ = new QComboBox(display); fps_->setObjectName("carplayFps"); fps_->addItem("30 fps", 30); fps_->addItem("60 fps", 60);
    fps_->setCurrentIndex(saved.value("CarPlay/fps", 30).toInt() == 60 ? 1 : 0);
    displayForm->addRow("Resolution", resolution_); displayForm->addRow(customLabel, custom); displayForm->addRow("Frame rate", fps_);
    auto *hint = new QLabel("Higher resolutions and 60 fps need more processing power. Changes take effect when CarPlay reconnects.", display);
    hint->setWordWrap(true); displayForm->addRow(hint);
    auto *apply = button("Apply && reconnect", "applyDisplay", display); displayForm->addRow(apply);
    connect(apply, &QPushButton::clicked, this, [this] {
        if ((width_->value() % 2) || (height_->value() % 2)) {
            status_->setText("Use even pixel sizes, for example 1280 × 720."); return;
        }
        saveDisplay();
        if (process_.state() != QProcess::NotRunning) connectSelectedPhone();
        else status_->setText("Display settings saved. Connect an iPhone to use them.");
    });
    settingsLayout->addWidget(display);

    auto *advancedToggle = new QToolButton(settings_); advancedToggle->setText("Advanced setup");
    advancedToggle->setCheckable(true); advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); advancedToggle->setArrowType(Qt::RightArrow);
    settingsLayout->addWidget(advancedToggle);
    auto *advanced = new QWidget(settings_); advanced->setVisible(false); auto *form = new QFormLayout(advanced);
    ssid_ = new QLineEdit(saved.value("CarPlay/ssid", qEnvironmentVariable("CARPLAY_WIFI_SSID")).toString(), advanced);
    password_ = new QLineEdit(qEnvironmentVariable("CARPLAY_WIFI_PASSPHRASE"), advanced); password_->setEchoMode(QLineEdit::Password);
    identity_ = new QLineEdit(saved.value("CarPlay/identity_directory", QString(DASH_CARPLAY_IDENTITY_DIR)).toString(), advanced);
    auto *identityRow = new QWidget(advanced); auto *identityLayout = new QHBoxLayout(identityRow); identityLayout->setContentsMargins(0,0,0,0);
    identityLayout->addWidget(identity_); auto *browse = button("Choose folder", "chooseIdentity", identityRow); identityLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto folder = QFileDialog::getExistingDirectory(this, "CarPlay identity folder", identity_->text());
        if (!folder.isEmpty()) identity_->setText(folder);
    });
    form->addRow("Wi-Fi network", ssid_); form->addRow("Wi-Fi password", password_); form->addRow("Identity folder", identityRow);
    settingsLayout->addWidget(advanced);
    connect(advancedToggle, &QToolButton::toggled, advanced, &QWidget::setVisible);
    connect(advancedToggle, &QToolButton::toggled, this, [advancedToggle](bool open) { advancedToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow); });
    body->addWidget(settings_);

    auto *controls = new QHBoxLayout();
    start_ = button("Connect", "connectCarPlay", content); stop_ = button("Disconnect", "stopCarPlay", content);
    resume_ = button("Return to CarPlay", "resumeCarPlay", content); resume_->setVisible(false); stop_->setEnabled(false);
    controls->addWidget(start_); controls->addWidget(stop_); controls->addWidget(resume_); phoneLayout->addLayout(controls); body->addStretch();
    scroll->setWidget(content); setup_ = scroll; pages_->addWidget(setup_);
    surface_ = new CarPlaySurface(*client_, pages_); surface_->setObjectName("carplayProjection"); pages_->addWidget(surface_);
    connect(resume_, &QPushButton::clicked, this, [this] {
        managing_ = false;
        client_->sendCommand({{"type", "show-ui"}});
        if (projectionReady_) pages_->setCurrentWidget(surface_);
    });
    connect(client_, &CarPlayClient::hostUiRequested, this, [this] { showSetup(); emit managementRequested(); });
    connect(client_, &CarPlayClient::frameChanged, this, [this] {
        const bool ready = !client_->frame().isNull();
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
        bool ok = false;
        const auto name = QInputDialog::getText(this, "Phone name", "Name", QLineEdit::Normal, phones_->currentText(), &ok).trimmed();
        if (ok && !name.isEmpty()) { phones_->setItemText(phones_->currentIndex(), name.left(80)); savePhones(); }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (phones_->currentIndex() < 0) return;
        if (QMessageBox::question(this, "Remove saved iPhone", "Remove " + phones_->currentText() + " from Dash? Bluetooth pairing on the Pi is kept.") != QMessageBox::Yes) return;
        restartPending_ = false; stopReceiver(); phones_->removeItem(phones_->currentIndex()); savePhones();
    });
    connect(start_, &QPushButton::clicked, this, &CarPlayWidget::connectSelectedPhone);
    connect(stop_, &QPushButton::clicked, this, [this] { restartPending_ = false; stopReceiver(); });
    connect(client_, &CarPlayClient::statusChanged, status_, &QLabel::setText);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { process_.readAllStandardOutput(); });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] { process_.readAllStandardError(); });
    connect(&process_, &QProcess::started, this, [this] { start_->setEnabled(true); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            restartPending_ = false; status_->setText("CarPlay could not start. Check Advanced setup or the receiver installation.");
            start_->setEnabled(true); stop_->setEnabled(false); poll_.stop();
        }
    });
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
        poll_.stop(); client_->disconnectReceiver(); start_->setEnabled(true); stop_->setEnabled(false);
        if (restartPending_) { restartPending_ = false; QTimer::singleShot(0, this, &CarPlayWidget::startReceiver); }
        else status_->setText(code == 0 ? "CarPlay disconnected. Choose an iPhone to reconnect." : "CarPlay receiver stopped. Try connecting again; Advanced setup has the receiver details.");
    });
    poll_.setInterval(500);
    connect(&poll_, &QTimer::timeout, this, [this] { client_->connectFile(stateDirectory_ + "/connection.json"); });
    if (qEnvironmentVariableIntValue("CARPLAY_AUTOSTART") == 1 && phones_->count())
        QTimer::singleShot(0, this, &CarPlayWidget::startReceiver);
}

void CarPlayWidget::showSetup() { managing_ = true; pages_->setCurrentWidget(setup_); }

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
    if (androidAutoActive_) { status_->setText("Disconnect Android Auto before starting CarPlay."); return; }
    if (!validAddress(phones_->currentData().toString())) { status_->setText("Choose a saved iPhone or use Add iPhone first."); return; }
    if ((width_->value() % 2) || (height_->value() % 2)) { status_->setText("Resolution width and height must be even numbers."); return; }
    savePhones(); saveDisplay(); managing_ = false;
    if (process_.state() != QProcess::NotRunning) { restartPending_ = true; stopReceiver(); }
    else startReceiver();
}

void CarPlayWidget::startReceiver() {
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

void CarPlayWidget::setAndroidAutoActive(bool active) {
    androidAutoActive_ = active;
    if (active) { restartPending_ = false; if (process_.state() != QProcess::NotRunning) stopReceiver(); }
}
#endif
