#ifdef DASH_CARPLAY
#include "carplay/phone_wizard.hpp"
#include <QCheckBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

CarPlayPhoneWizard::CarPlayPhoneWizard(const QString &ssid, const QString &password, QWidget *parent, CarPlayPhones *bluetooth)
    : QWizard(parent), bluetooth_(bluetooth ? bluetooth : new CarPlayPhones(this)) {
    bluetooth_->setParent(this);
    setWindowTitle("Connect an iPhone");
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage);
    setButtonText(QWizard::FinishButton, "Save && connect");
    resize(760, 560);
    auto *phone = new QWizardPage();
    phone->setTitle("1. Choose your iPhone");
    phone->setSubTitle("Open Settings → Bluetooth on your iPhone and keep it open.");
    auto *phoneLayout = new QVBoxLayout(phone);
    devices_ = new QListWidget(phone); devices_->setObjectName("discoveredPhones");
    phoneLayout->addWidget(devices_, 1);
    status_ = new QLabel(phone); status_->setWordWrap(true); phoneLayout->addWidget(status_);
    auto *search = new QPushButton("Search again", phone); phoneLayout->addWidget(search);
    addPage(phone);

    auto *wifi = new QWizardPage();
    wifi->setTitle("2. Join the CarPlay Wi-Fi");
    wifi->setSubTitle("On the iPhone, open Settings → Wi-Fi and join this network. Keep Wi-Fi and Bluetooth switched on.");
    auto *wifiLayout = new QFormLayout(wifi);
    ssid_ = new QLineEdit(ssid, wifi); ssid_->setObjectName("wizardSsid");
    password_ = new QLineEdit(password, wifi); password_->setEchoMode(QLineEdit::Password);
    wifiLayout->addRow("Wi-Fi network", ssid_); wifiLayout->addRow("Wi-Fi password", password_);
    auto *show = new QCheckBox("Show password", wifi); wifiLayout->addRow(show);
    connect(show, &QCheckBox::toggled, password_, [this](bool visible) {
        password_->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
    });
    addPage(wifi);

    auto *finish = new QWizardPage();
    finish->setTitle("3. Start CarPlay");
    finish->setSubTitle("Choose Save & connect, then approve Use CarPlay on the iPhone. You can add more phones later; one connects at a time.");
    auto *finishLayout = new QFormLayout(finish);
    name_ = new QLineEdit(finish); name_->setMaxLength(80); finishLayout->addRow("Phone name", name_);
    addPage(finish);
    for (auto *control : findChildren<QAbstractButton *>()) control->setMinimumHeight(44);

    connect(search, &QPushButton::clicked, bluetooth_, &CarPlayPhones::scan);
    connect(bluetooth_, &CarPlayPhones::status, status_, &QLabel::setText);
    connect(bluetooth_, &CarPlayPhones::found, this, [this](const QString &name, const QString &address, bool paired) {
        QListWidgetItem *row = nullptr;
        for (int i = 0; i < devices_->count(); ++i)
            if (devices_->item(i)->data(Qt::UserRole).toString() == address) row = devices_->item(i);
        if (!row) row = new QListWidgetItem(devices_);
        row->setText(name + (paired ? " · Paired" : ""));
        row->setData(Qt::UserRole, address); row->setData(Qt::UserRole + 1, paired); row->setData(Qt::UserRole + 2, name);
    });
    connect(bluetooth_, &CarPlayPhones::confirmation, this, [this](const QString &code) {
        bluetooth_->confirm(QMessageBox::question(this, "Confirm pairing", "Does your iPhone show " + code + "?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes);
    });
    connect(bluetooth_, &CarPlayPhones::paired, this, [this](const QString &address) {
        auto *item = devices_->currentItem();
        if (item && item->data(Qt::UserRole).toString() == address) {
            item->setData(Qt::UserRole + 1, true); name_->setText(item->data(Qt::UserRole + 2).toString());
            if (currentId() == 0) next();
        }
    });
    QTimer::singleShot(0, bluetooth_, &CarPlayPhones::scan);
}

bool CarPlayPhoneWizard::validateCurrentPage() {
    if (currentId() == 0) {
        auto *item = devices_->currentItem();
        if (!item) { status_->setText("Select your iPhone from the list first."); return false; }
        if (!item->data(Qt::UserRole + 1).toBool()) { bluetooth_->pair(item->data(Qt::UserRole).toString()); return false; }
        name_->setText(item->data(Qt::UserRole + 2).toString());
    }
    if (currentId() == 1 && ssid_->text().trimmed().isEmpty()) {
        QMessageBox::information(this, "Wi-Fi network", "Enter the Wi-Fi network shared by the Pi and iPhone."); return false;
    }
    return true;
}
QString CarPlayPhoneWizard::phoneName() const { return name_->text().trimmed().isEmpty() ? "iPhone" : name_->text().trimmed(); }
QString CarPlayPhoneWizard::phoneAddress() const { return devices_->currentItem() ? devices_->currentItem()->data(Qt::UserRole).toString() : QString(); }
QString CarPlayPhoneWizard::ssid() const { return ssid_->text().trimmed(); }
QString CarPlayPhoneWizard::password() const { return password_->text(); }
#endif
