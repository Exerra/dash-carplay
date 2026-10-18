#ifdef DASH_CARPLAY
#include "carplay/touch_ui.hpp"
#include <QApplication>
#include <QDialog>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QScroller>
#include <QScrollerProperties>
#include <QVBoxLayout>
#include <cmath>

namespace {
void sizeDialog(QDialog &dialog, QWidget *parent, int preferredHeight) {
    auto size = parent ? parent->window()->size() : QSize(800, 480);
    if (auto *screen = QGuiApplication::primaryScreen()) size = size.boundedTo(screen->availableGeometry().size());
    dialog.resize(qMin(760, qMax(360, size.width() - 32)), qMin(preferredHeight, qMax(360, size.height() - 32)));
    CarPlayTouch::theme(&dialog, CarPlayTouch::dark(parent));
}
QLabel *titleLabel(const QString &text, QWidget *parent) {
    auto *title = new QLabel(text, parent); title->setObjectName("touchTitle"); title->setWordWrap(true); return title;
}
QColor accentText(const QColor &color) {
    // Compare WCAG contrast against black and white so arbitrary user accents
    // remain readable, including very light colours and saturated blue.
    auto linear = [](double channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    const double luminance = 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
    return (luminance + 0.05) / 0.05 >= 1.05 / (luminance + 0.05) ? QColor(Qt::black) : QColor(Qt::white);
}
}

bool CarPlayTouch::dark(const QWidget *widget) {
    for (auto *current = widget; current; current = current->parentWidget())
        if (current->property("carplayDark").isValid()) return current->property("carplayDark").toBool();
    return true;
}

QColor CarPlayTouch::accent(const QWidget *widget) {
    for (auto *current = widget; current; current = current->parentWidget()) {
        const auto color = current->property("carplayAccent").value<QColor>();
        if (color.isValid()) return color;
    }
    // Dash stores its current accent in Base; standalone dialogs use that same
    // palette role until their host explicitly supplies an accent.
    return (widget ? widget->palette() : QApplication::palette()).color(QPalette::Base);
}

void CarPlayTouch::theme(QWidget *widget, bool night) {
    widget->setProperty("carplayDark", night);
    const QString background = night ? "#121416" : "#f3f4f5";
    const QString panel = night ? "#1d2024" : "#ffffff";
    const QString border = night ? "#383d43" : "#d2d6dc";
    const QString text = night ? "#f3f4f6" : "#1b2027";
    const QString muted = night ? "#aab2bd" : "#586373";
    const auto color = accent(widget);
    const auto foreground = accentText(color);
    const auto pressed = QColor::fromRgbF(
        color.redF() * 0.88 + foreground.redF() * 0.12,
        color.greenF() * 0.88 + foreground.greenF() * 0.12,
        color.blueF() * 0.88 + foreground.blueF() * 0.12);
    auto palette = widget->palette();
    palette.setColor(QPalette::Highlight, color);
    palette.setColor(QPalette::HighlightedText, foreground);
    widget->setPalette(palette);
    widget->setStyleSheet(QString(
        "QWidget { color:%1; font-size:18px; }"
        "QDialog, QWizard, QWizardPage, QWidget#carplayManagement { background:%2; }"
        "QLabel { background:transparent; } QLabel#touchTitle { font-size:26px; font-weight:600; }"
        "QLabel[secondary=true] { color:%5; font-size:16px; }"
        "QFrame[panel=true] { background:%3; border:1px solid %4; border-radius:14px; }"
        "QPushButton, QToolButton { background:%3; border:1px solid %4; border-radius:10px; padding:8px 16px; }"
        "QPushButton:pressed, QToolButton:pressed { background:%4; }"
        "QPushButton:disabled { color:%5; background:%2; }"
        "QPushButton:focus, QToolButton:focus { border:2px solid %7; }"
        "QPushButton[primary=true] { background:%7; color:%8; border-color:%7; font-weight:600; }"
        "QPushButton[primary=true]:pressed { background:%9; }"
        "QPushButton[primary=true]:disabled { background:%4; border-color:%4; color:%5; }"
        "QPushButton[navigation=true] { background:transparent; border:0; color:%5; }"
        "QPushButton[navigation=true]:checked { background:%7; color:%8; border:1px solid %7; }"
        "QPushButton[danger=true] { color:%6; }"
        "QLineEdit, QComboBox, QSpinBox { background:%3; color:%1; border:1px solid %4; border-radius:10px; padding:8px 14px; }"
        "QLineEdit, QSpinBox { selection-background-color:%7; selection-color:%8; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border:2px solid %7; }"
        "QComboBox::drop-down { width:42px; border:0; }"
        "QListWidget { background:transparent; border:0; outline:0; }"
        "QListWidget::item { background:%3; border:1px solid %4; border-radius:10px; padding:10px 16px; margin:4px 0; }"
        "QListWidget::item:selected { background:%7; border:2px solid %7; color:%8; }"
        "QScrollArea { border:0; background:transparent; } QScrollArea > QWidget > QWidget { background:transparent; }"
        "QScrollBar:vertical { background:transparent; width:14px; margin:0; }"
        "QScrollBar::handle:vertical { background:%4; min-height:50px; border-radius:7px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QCheckBox { spacing:14px; padding:10px 0; } QCheckBox::indicator { width:28px; height:28px; }"
        "QSlider::groove:horizontal { background:%4; height:6px; border-radius:3px; }"
        "QSlider::sub-page:horizontal { background:%7; border-radius:3px; }"
        "QSlider::handle:horizontal { background:%7; width:28px; margin:-11px 0; border-radius:14px; }"
    ).arg(text, background, panel, border, muted, night ? "#edaaa2" : "#ac3028", color.name(), foreground.name(), pressed.name()));
}

void CarPlayTouch::scroll(QWidget *viewport) {
    QScroller::grabGesture(viewport, QScroller::TouchGesture);
    auto *scroller = QScroller::scroller(viewport);
    auto properties = scroller->scrollerProperties();
    properties.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, QScrollerProperties::OvershootAlwaysOff);
    properties.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, QScrollerProperties::OvershootAlwaysOff);
    scroller->setScrollerProperties(properties);
}

QPushButton *CarPlayTouch::button(const QString &text, const char *name, QWidget *parent, bool primary) {
    auto *result = new QPushButton(text, parent); result->setObjectName(name);
    result->setMinimumHeight(56); result->setAutoDefault(false); result->setProperty("primary", primary); return result;
}

void CarPlayTouchChoice::paintEvent(QPaintEvent *event) {
    QComboBox::paintEvent(event);
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(CarPlayTouch::dark(this) ? QColor("#aab2bd") : QColor("#586373"), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const int x = width() - 27, y = height() / 2;
    painter.drawPolyline(QPolygonF({QPointF(x-6,y-3), QPointF(x,y+3), QPointF(x+6,y-3)}));
}

void CarPlayTouchChoice::showPopup() {
    QDialog dialog(this); dialog.setObjectName("carplayChoiceDialog"); dialog.setWindowTitle(title_);
    sizeDialog(dialog, this, 560);
    auto *layout = new QVBoxLayout(&dialog); layout->setContentsMargins(20,16,20,16); layout->setSpacing(12);
    layout->addWidget(titleLabel(title_, &dialog));
    auto *list = new QListWidget(&dialog); list->setObjectName("touchChoices");
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (int i = 0; i < count(); ++i) {
        auto *item = new QListWidgetItem(itemText(i), list); item->setSizeHint(QSize(0, 64));
    }
    list->setCurrentRow(currentIndex()); list->scrollToItem(list->currentItem());
    CarPlayTouch::scroll(list->viewport()); layout->addWidget(list, 1);
    auto *cancel = CarPlayTouch::button("Cancel", "cancelChoice", &dialog); layout->addWidget(cancel);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemClicked, &dialog, [this, &dialog, list](QListWidgetItem *item) {
        setCurrentIndex(list->row(item)); dialog.accept();
    });
    dialog.exec(); QComboBox::hidePopup();
}

bool CarPlayTouch::editText(QWidget *parent, const QString &title, QString &value, bool password, int maximum) {
    QDialog dialog(parent); dialog.setObjectName("carplayKeyboard"); dialog.setWindowTitle(title);
    sizeDialog(dialog, parent, 560);
    const bool compact = dialog.height() < 500;
    auto *layout = new QVBoxLayout(&dialog); layout->setContentsMargins(16,8,16,8); layout->setSpacing(compact ? 6 : 8);
    layout->addWidget(titleLabel(title, &dialog));
    auto *field = new QLineEdit(value, &dialog); field->setObjectName("keyboardValue"); field->setMinimumHeight(compact ? 48 : 52);
    field->setMaxLength(maximum); field->setEchoMode(password ? QLineEdit::Password : QLineEdit::Normal);
    field->selectAll(); layout->addWidget(field);
    bool symbols = false, upper = false;
    auto *keys = new QWidget(&dialog); auto *grid = new QGridLayout(keys); grid->setContentsMargins(0,0,0,0); grid->setSpacing(6);
    QList<QPushButton *> letters;
    for (int i = 0; i < 40; ++i) {
        auto *key = button("", "keyboardKey", keys); key->setMinimumSize(0, 44); key->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        letters.append(key); grid->addWidget(key, i / 10, i % 10);
        key->setFocusPolicy(Qt::NoFocus);
        QObject::connect(key, &QPushButton::clicked, field, [field, key] { field->insert(key->property("character").toString()); });
    }
    auto updateKeys = [&] {
        const QString characters = symbols ? "1234567890!@#$%^&*()_+-=[]{}\\|;:'\"<>?`~" : "1234567890qwertyuiopasdfghjkl'zxcvbnm,./";
        for (int i = 0; i < letters.size(); ++i) {
            const auto character = i < characters.size() ? QString(characters[i]) : QString();
            const auto value = upper && !symbols ? character.toUpper() : character;
            letters[i]->setProperty("character", value);
            letters[i]->setText(value == "&" ? "&&" : value); letters[i]->setEnabled(!character.isEmpty());
        }
    };
    updateKeys(); layout->addWidget(keys, 1);
    auto *editing = new QHBoxLayout();
    auto *mode = button("Symbols", "keyboardMode", &dialog); auto *shift = button("Shift", "keyboardShift", &dialog);
    auto *space = button("Space", "keyboardSpace", &dialog); auto *erase = button("Delete", "keyboardDelete", &dialog);
    for (auto *control : {mode, shift, space, erase}) {
        control->setFocusPolicy(Qt::NoFocus);
        if (compact) control->setMinimumHeight(44);
    }
    editing->addWidget(mode); editing->addWidget(shift); editing->addWidget(space, 1); editing->addWidget(erase); layout->addLayout(editing);
    QObject::connect(mode, &QPushButton::clicked, &dialog, [&] { symbols = !symbols; mode->setText(symbols ? "Letters" : "Symbols"); updateKeys(); });
    QObject::connect(shift, &QPushButton::clicked, &dialog, [&] { upper = !upper; updateKeys(); });
    QObject::connect(space, &QPushButton::clicked, field, [field] { field->insert(" "); });
    QObject::connect(erase, &QPushButton::clicked, field, &QLineEdit::backspace);
    auto *actions = new QHBoxLayout(); auto *cancel = button("Cancel", "cancelKeyboard", &dialog);
    auto *save = button("Done", "saveKeyboard", &dialog, true); actions->addWidget(cancel); actions->addWidget(save); layout->addLayout(actions);
    if (compact) { cancel->setMinimumHeight(48); save->setMinimumHeight(48); }
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(field, &QLineEdit::returnPressed, &dialog, &QDialog::accept);
    field->setFocus();
    if (dialog.exec() != QDialog::Accepted) return false;
    value = field->text(); return true;
}

bool CarPlayTouch::confirm(QWidget *parent, const QString &title, const QString &message, const QString &action) {
    QDialog dialog(parent); dialog.setObjectName("carplayConfirmation"); sizeDialog(dialog, parent, 320);
    auto *layout = new QVBoxLayout(&dialog); layout->setContentsMargins(24,20,24,20); layout->setSpacing(16);
    layout->addWidget(titleLabel(title, &dialog));
    auto *description = new QLabel(message, &dialog); description->setWordWrap(true); layout->addWidget(description, 1);
    auto *actions = new QHBoxLayout(); auto *cancel = button("Cancel", "cancelConfirmation", &dialog);
    auto *accept = button(action, "acceptConfirmation", &dialog, true); actions->addWidget(cancel); actions->addWidget(accept); layout->addLayout(actions);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject); QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
    return dialog.exec() == QDialog::Accepted;
}

CarPlayTouchEdit::CarPlayTouchEdit(const QString &value, const QString &title, QWidget *parent)
    : QLineEdit(value, parent), title_(title) { setMinimumHeight(56); }
void CarPlayTouchEdit::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || isReadOnly()) { QLineEdit::mouseReleaseEvent(event); return; }
    auto value = text();
    if (CarPlayTouch::editText(this, title_, value, echoMode() == QLineEdit::Password, maxLength())) setText(value);
    event->accept();
}

CarPlayTouchNumber::CarPlayTouchNumber(QWidget *parent) : QSpinBox(parent) {
    setMinimumHeight(56); setButtonSymbols(QAbstractSpinBox::NoButtons); lineEdit()->installEventFilter(this);
}
bool CarPlayTouchNumber::eventFilter(QObject *watched, QEvent *event) {
    if (watched != lineEdit() || event->type() != QEvent::MouseButtonRelease) return QSpinBox::eventFilter(watched, event);
    QDialog dialog(this); dialog.setObjectName("carplayNumberPad"); sizeDialog(dialog, this, 480);
    auto *layout = new QVBoxLayout(&dialog); layout->setContentsMargins(20,12,20,12); layout->setSpacing(8);
    layout->addWidget(titleLabel("Custom resolution", &dialog));
    auto *field = new QLineEdit(QString::number(value()), &dialog); field->setReadOnly(true); field->setMinimumHeight(48); layout->addWidget(field);
    auto *hint = new QLabel(QString("%1–%2 pixels · even numbers").arg(minimum()).arg(maximum()), &dialog); hint->setProperty("secondary", true); layout->addWidget(hint);
    auto *keys = new QGridLayout(); keys->setSpacing(6); bool replace = true;
    for (int i = 0; i < 12; ++i) {
        const QString label = i < 9 ? QString::number(i + 1) : i == 9 ? "Clear" : i == 10 ? "0" : "Delete";
        auto *key = CarPlayTouch::button(label, "numberKey", &dialog); key->setMinimumHeight(44); keys->addWidget(key, i / 3, i % 3);
        connect(key, &QPushButton::clicked, field, [field, label, &replace] {
            auto text = field->text();
            if (label == "Clear") text.clear();
            else if (label == "Delete") text.chop(1);
            else { if (replace) text.clear(); if (text.size() < 4) text += label; }
            replace = false; field->setText(text);
        });
    }
    layout->addLayout(keys, 1);
    auto *actions = new QHBoxLayout(); auto *cancel = CarPlayTouch::button("Cancel", "cancelNumber", &dialog);
    auto *save = CarPlayTouch::button("Done", "saveNumber", &dialog, true); actions->addWidget(cancel); actions->addWidget(save); layout->addLayout(actions);
    auto validate = [this, field, save] { bool ok = false; const int number = field->text().toInt(&ok); save->setEnabled(ok && number >= minimum() && number <= maximum() && number % 2 == 0); };
    connect(field, &QLineEdit::textChanged, &dialog, validate); validate();
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject); connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() == QDialog::Accepted) setValue(field->text().toInt());
    return true;
}
#endif
