#ifdef DASH_CARPLAY
#include "carplay/audio_settings.hpp"
#include "carplay/touch_ui.hpp"
#include <QHBoxLayout>
#include <QShowEvent>
#include <QScrollArea>
#include <QThread>
#include <QVBoxLayout>
#include <memory>

CarPlayAudioSettings::CarPlayAudioSettings(CarPlayClient &client, QWidget *parent) : QWidget(parent), client_(client) {
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0,4,0,4); outer->setSpacing(8);
    auto *scroll = new QScrollArea(this); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); CarPlayTouch::scroll(scroll->viewport());
    auto *content = new QWidget(scroll); scroll->setWidget(content); outer->addWidget(scroll,1);
    auto *layout = new QVBoxLayout(content); layout->setContentsMargins(0,0,8,0); layout->setSpacing(12);
    const auto saved = client.audioSettings();
    auto choice = [&](const QString &title, const char *name) {
        auto *label = new QLabel(title, this); layout->addWidget(label);
        auto *control = new CarPlayTouchChoice(title, this); control->setObjectName(name); layout->addWidget(control); return control;
    };
    output_ = choice("Speakers", "carplayAudioOutput"); output_->addItem("System default", "");
    if (!saved.output.isEmpty()) { output_->addItem("Saved speakers", saved.output); output_->setCurrentIndex(1); }
    refresh_ = CarPlayTouch::button("Refresh devices", "refreshAudioDevices", this); layout->addWidget(refresh_);
    connect(refresh_, &QPushButton::clicked, this, &CarPlayAudioSettings::refresh);
    latency_ = choice("Playback response", "carplayAudioLatency");
    latency_->addItem("Responsive · recommended", 60); latency_->addItem("Minimum delay", 40);
    latency_->addItem("Balanced", 120); latency_->addItem("Extra buffering", 200);
    if (latency_->findData(saved.buffer) < 0) latency_->addItem(QString("Custom · %1 ms").arg(saved.buffer), saved.buffer);
    latency_->setCurrentIndex(latency_->findData(saved.buffer));
    auto *latencyHint = new QLabel("Use extra buffering if sound breaks up. Minimum delay needs a steady connection.", this);
    latencyHint->setWordWrap(true); latencyHint->setProperty("secondary",true); layout->addWidget(latencyHint);
    auto level = [&](const QString &title, const char *name, int value) {
        auto *label = new QLabel(title, this); layout->addWidget(label);
        auto *row = new QHBoxLayout(); auto *less = CarPlayTouch::button("−", "audioLess", this);
        auto *slider = new QSlider(Qt::Horizontal,this); slider->setObjectName(name); slider->setRange(0,100); slider->setValue(value); slider->setMinimumHeight(48);
        auto *more = CarPlayTouch::button("+", "audioMore", this); auto *percent = new QLabel(QString::number(value)+"%",this);
        less->setFixedWidth(56); more->setFixedWidth(56); percent->setMinimumWidth(54); percent->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
        row->addWidget(less); row->addWidget(slider,1); row->addWidget(more); row->addWidget(percent); layout->addLayout(row);
        connect(less,&QPushButton::clicked,slider,[slider]{slider->setValue(slider->value()-5);});
        connect(more,&QPushButton::clicked,slider,[slider]{slider->setValue(slider->value()+5);});
        connect(slider,&QSlider::valueChanged,percent,[percent](int value){percent->setText(QString::number(value)+"%");});
        return slider;
    };
    music_ = level("Music", "carplayMusicVolume", saved.music);
    voice_ = level("Voice & alerts", "carplayVoiceVolume", saved.voice);
    auto *volumeHint = new QLabel("Dash's volume bar controls the overall volume. Apps that mix directions into music share the music level.",this);
    volumeHint->setWordWrap(true); volumeHint->setProperty("secondary",true); layout->addWidget(volumeHint);
    mode_ = choice("Microphone", "carplayMicrophoneMode");
    mode_->addItem("Compatibility · silent microphone", "silent");
    mode_->addItem("Phone · no microphone offered", "phone");
    mode_->addItem("USB / system microphone", "real"); mode_->setCurrentIndex(mode_->findData(saved.microphoneMode));
    input_ = new CarPlayTouchChoice("Choose a microphone",this); input_->setObjectName("carplayMicrophoneInput");
    input_->addItem("System microphone", "");
    if (!saved.input.isEmpty()) { input_->addItem("Saved microphone",saved.input); input_->setCurrentIndex(1); }
    layout->addWidget(input_);
    auto *micHint = new QLabel(this); micHint->setProperty("secondary",true); micHint->setWordWrap(true); layout->addWidget(micHint);
    auto updateMicrophone = [this,micHint] {
        const auto mode = mode_->currentData().toString(); input_->setVisible(mode=="real");
        micHint->setText(mode=="silent" ? "Keeps CarPlay audio routing active without recording sound." : mode=="phone" ?
            "Dash offers no microphone. The iPhone decides how to handle voice and audio routing." :
            "Uses the selected microphone for Siri and calls. Microphone-mode changes reconnect CarPlay.");
    };
    connect(mode_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[updateMicrophone](int){updateMicrophone();}); updateMicrophone();
    status_ = new QLabel("Changes apply when you tap Apply audio.",this); status_->setWordWrap(true); status_->setProperty("secondary",true); status_->setObjectName("carplayAudioStatus"); outer->addWidget(status_);
    connect(&client_,&CarPlayClient::audioStatusChanged,status_,&QLabel::setText);
    auto *actions = new QHBoxLayout();
    auto *test = CarPlayTouch::button("Test speakers", "testCarPlaySpeakers", this);
    auto *applyButton = CarPlayTouch::button("Apply audio", "applyCarPlayAudio", this, true);
    actions->addWidget(test,1); actions->addWidget(applyButton,1); outer->addLayout(actions);
    connect(applyButton,&QPushButton::clicked,this,&CarPlayAudioSettings::apply);
    connect(test,&QPushButton::clicked,this,[this]{status_->setText("Testing the saved speakers. Apply audio first to use changed settings."); client_.testSpeakers();});
    connect(&client_,&CarPlayClient::speakerTestChanged,test,[test](bool active){test->setEnabled(!active);});
    layout->addStretch();
}

void CarPlayAudioSettings::showEvent(QShowEvent *event) {
    QWidget::showEvent(event); if (!discovered_) { discovered_=true; refresh(); }
}

void CarPlayAudioSettings::refresh() {
    if (scanning_) return;
    scanning_=true; refresh_->setEnabled(false); refresh_->setText("Looking for devices…");
    struct Devices { QList<CarPlayAudio::Output> outputs, inputs; };
    auto devices = std::make_shared<Devices>();
    auto *worker = QThread::create([devices] { devices->outputs=CarPlayAudio::outputs(); devices->inputs=CarPlayAudio::inputs(); });
    connect(worker,&QThread::finished,this,[this,devices] {
        auto populate = [](QComboBox *control,const QList<CarPlayAudio::Output> &devices,const QString &automatic,const QString &missing) {
            const auto selected=control->currentData().toString(); control->clear(); control->addItem(automatic,"");
            for (const auto &device:devices) if (control->findData(device.id)<0) control->addItem(device.name,device.id);
            if (!selected.isEmpty() && control->findData(selected)<0) control->addItem(missing,selected);
            control->setCurrentIndex(qMax(0,control->findData(selected)));
        };
        populate(output_,devices->outputs,"System default","Saved speakers · not connected");
        populate(input_,devices->inputs,"System microphone","Saved microphone · not connected");
        scanning_=false; refresh_->setEnabled(true); refresh_->setText("Refresh devices");
    });
    connect(worker,&QThread::finished,worker,&QObject::deleteLater); worker->start();
}

void CarPlayAudioSettings::apply() {
    auto settings=client_.audioSettings(); const auto previousMode=settings.microphoneMode;
    settings.output=output_->currentData().toString(); settings.buffer=latency_->currentData().toInt();
    settings.music=music_->value(); settings.voice=voice_->value(); settings.microphoneMode=mode_->currentData().toString(); settings.input=input_->currentData().toString();
    CarPlayAudio::saveSettings(settings); status_->setText("Audio settings saved. Speaker changes may briefly pause sound.");
    client_.setAudioSettings(settings);
    if (settings.microphoneMode!=previousMode) { status_->setText("Microphone mode saved. Reconnecting CarPlay…"); emit microphoneModeChanged(); }
}
#endif
