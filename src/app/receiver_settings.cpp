#include "app/receiver_settings.hpp"
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QScroller>
#include <QVBoxLayout>
#include <utility>

bool Receivers::androidAutoEnabled() { return QSettings().value("Receivers/android_auto_enabled", true).toBool(); }
bool Receivers::carplayEnabled() { return QSettings().value("Receivers/carplay_enabled", true).toBool(); }

ReceiverSettings::ReceiverSettings(bool runningAndroidAuto, std::function<void(bool)> setCarplay, QWidget *parent)
    : QWidget(parent), runningAndroidAuto_(runningAndroidAuto), setCarplay_(std::move(setCarplay)) {
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(20,16,20,16); outer->setSpacing(12);
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *body = new QWidget(scroll); scroll->setWidget(body); outer->addWidget(scroll, 1);
    QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    auto *layout = new QVBoxLayout(body); layout->setContentsMargins(4,4,4,4); layout->setSpacing(16);
    auto *description = new QLabel("Choose which phones can connect to Dash. Turning a receiver off also stops automatic connections; hiding its page in Layout does not.", this);
    description->setWordWrap(true); layout->addWidget(description);
    androidAuto_ = new QCheckBox("Android Auto receiver", this); androidAuto_->setObjectName("androidAutoReceiverEnabled");
    androidAuto_->setMinimumHeight(56); androidAuto_->setChecked(Receivers::androidAutoEnabled()); layout->addWidget(androidAuto_);
#ifdef DASH_CARPLAY
    carplay_ = new QCheckBox("CarPlay receiver", this); carplay_->setObjectName("carplayReceiverEnabled");
    carplay_->setMinimumHeight(56); carplay_->setChecked(Receivers::carplayEnabled()); layout->addWidget(carplay_);
    connect(carplay_, &QCheckBox::toggled, this, [this] { updateAction(); });
#endif
    auto *hint = new QLabel("Changing Android Auto restarts Dash to apply the setting. CarPlay changes apply immediately. Saved phones and keybindings are kept.", this);
    hint->setWordWrap(true); layout->addWidget(hint);
    layout->addStretch();
    status_ = new QLabel(this); status_->setWordWrap(true); status_->setObjectName("receiverSettingsStatus"); outer->addWidget(status_);
    apply_ = new QPushButton("Apply", this); apply_->setMinimumHeight(56); apply_->setObjectName("applyReceivers"); outer->addWidget(apply_);
    connect(androidAuto_, &QCheckBox::toggled, this, [this] { updateAction(); });
    connect(apply_, &QPushButton::clicked, this, &ReceiverSettings::apply);
    updateAction();
}

void ReceiverSettings::updateAction() {
    const bool restart = androidAuto_->isChecked() != runningAndroidAuto_;
    apply_->setText(restart ? "Apply && restart Dash" : "Apply");
    status_->setText(restart ? "Dash will briefly close and reopen when you apply these changes." : QString());
}

void ReceiverSettings::apply() {
    QSettings settings;
    settings.setValue("Receivers/android_auto_enabled", androidAuto_->isChecked());
    if (carplay_) settings.setValue("Receivers/carplay_enabled", carplay_->isChecked());
    settings.sync();
    if (settings.status() != QSettings::NoError) { status_->setText("Settings could not be saved. Please try again."); return; }
    if (androidAuto_->isChecked() != runningAndroidAuto_) { emit restartRequested(); return; }
    if (carplay_ && setCarplay_) setCarplay_(carplay_->isChecked());
    status_->setText("Receiver settings applied.");
}
