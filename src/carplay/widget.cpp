#ifdef DASH_CARPLAY
#include "carplay/widget.hpp"
#include "carplay/surface.hpp"
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QProcessEnvironment>
#include <QSettings>
#include <QVBoxLayout>

CarPlayWidget::CarPlayWidget(QWidget *parent) : QWidget(parent) { init(); }

CarPlayWidget::~CarPlayWidget() {
    stopReceiver();
    if (!process_.waitForFinished(3000)) { process_.kill(); process_.waitForFinished(1000); }
}

void CarPlayWidget::init() {
    auto *layout = new QVBoxLayout(this);
    client_ = new CarPlayClient(this);
    stateDirectory_ = qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + "/.local/state") + "/opendash/carplay";
    QSettings saved;
    settings_ = new QGroupBox("CarPlay connection", this);
    auto *form = new QFormLayout(settings_);
    ssid_ = new QLineEdit(saved.value("CarPlay/ssid", qEnvironmentVariable("CARPLAY_WIFI_SSID")).toString(), settings_);
    password_ = new QLineEdit(qEnvironmentVariable("CARPLAY_WIFI_PASSPHRASE"), settings_);
    password_->setEchoMode(QLineEdit::Password);
    address_ = new QLineEdit(saved.value("CarPlay/bluetooth_address").toString(), settings_);
    address_->setPlaceholderText("Auto-detect a paired iPhone");
    identity_ = new QLineEdit(saved.value("CarPlay/identity_directory", QString(DASH_CARPLAY_IDENTITY_DIR)).toString(), settings_);
    auto *identityRow = new QWidget(settings_);
    auto *identityLayout = new QHBoxLayout(identityRow);
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->addWidget(identity_);
    auto *browse = new QPushButton("Choose folder", identityRow);
    identityLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto folder = QFileDialog::getExistingDirectory(this, "CarPlay identity folder", identity_->text());
        if (!folder.isEmpty()) identity_->setText(folder);
    });
    form->addRow("Wi-Fi network", ssid_);
    form->addRow("Wi-Fi password", password_);
    form->addRow("iPhone Bluetooth address", address_);
    form->addRow("Identity folder", identityRow);
    layout->addWidget(settings_);
    status_ = new QLabel("Experimental CarPlay. Pair the iPhone in Bluetooth settings and join the same Wi-Fi network.", this);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *controls = new QHBoxLayout();
    start_ = new QPushButton("Start CarPlay", this);
    stop_ = new QPushButton("Stop", this);
    stop_->setEnabled(false);
    controls->addWidget(start_);
    controls->addWidget(stop_);
    auto *home = new QPushButton("CarPlay Home", this);
    controls->addWidget(home);
    connect(home, &QPushButton::clicked, this, [this] {
        client_->sendCommand({{"type", "knob"}, {"knob", QJsonObject{{"home", true}}}});
    });
    layout->addLayout(controls);
    layout->addWidget(new CarPlaySurface(*client_, this), 1);
    connect(start_, &QPushButton::clicked, this, &CarPlayWidget::startReceiver);
    connect(stop_, &QPushButton::clicked, this, &CarPlayWidget::stopReceiver);
    connect(client_, &CarPlayClient::statusChanged, status_, &QLabel::setText);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { process_.readAllStandardOutput(); });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] { process_.readAllStandardError(); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            status_->setText("CarPlay receiver could not start. Build the receiver component first.");
            settings_->setEnabled(true); start_->setEnabled(true); stop_->setEnabled(false); poll_.stop();
        }
    });
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus) {
            poll_.stop(); client_->disconnectReceiver(); settings_->setEnabled(true);
            start_->setEnabled(true); stop_->setEnabled(false);
            status_->setText(code == 0 ? "CarPlay stopped" : "CarPlay receiver exited. Check the identity folder and receiver setup.");
        });
    poll_.setInterval(500);
    connect(&poll_, &QTimer::timeout, this, [this] { client_->connectFile(stateDirectory_ + "/connection.json"); });
    if (qEnvironmentVariableIntValue("CARPLAY_AUTOSTART") == 1)
        QTimer::singleShot(0, this, &CarPlayWidget::startReceiver);
}

void CarPlayWidget::startReceiver() {
    if (process_.state() != QProcess::NotRunning) return;
    if (androidAutoActive_) {
        status_->setText("Disconnect Android Auto before starting CarPlay."); return;
    }
    QSettings saved;
    saved.setValue("CarPlay/ssid", ssid_->text());
    saved.setValue("CarPlay/bluetooth_address", address_->text());
    saved.setValue("CarPlay/identity_directory", identity_->text());
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("CARPLAY_IDENTITY_DIR", identity_->text());
    environment.insert("CARPLAY_STATE_DIR", stateDirectory_);
    environment.insert("CARPLAY_WIFI_SSID", ssid_->text());
    environment.insert("CARPLAY_WIFI_PASSPHRASE", password_->text());
    if (!address_->text().isEmpty()) environment.insert("CARPLAY_BT_ADDRESS", address_->text());
    process_.setProcessEnvironment(environment);
    process_.setProgram(saved.value("CarPlay/receiver_command", QString(DASH_CARPLAY_RUNNER)).toString());
    process_.setArguments(QStringList());
    settings_->setEnabled(false); start_->setEnabled(false); stop_->setEnabled(true);
    status_->setText("Starting CarPlay receiver…");
    process_.start();
    poll_.start();
}

void CarPlayWidget::stopReceiver() {
    poll_.stop();
    if (client_) client_->disconnectReceiver();
    const auto pid = process_.processId();
    process_.terminate();
    if (pid) QTimer::singleShot(3000, this, [this, pid] {
        if (process_.processId() == pid && process_.state() != QProcess::NotRunning) process_.kill();
    });
}

void CarPlayWidget::setAndroidAutoActive(bool active) {
    androidAutoActive_ = active;
    if (active && process_.state() != QProcess::NotRunning) stopReceiver();
}
#endif
