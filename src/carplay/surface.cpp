#ifdef DASH_CARPLAY
#include "carplay/surface.hpp"
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTouchEvent>

namespace {
bool commandForKey(int key, PhoneControls::Command &command) {
    using Command = PhoneControls::Command;
    switch (key) {
        case Qt::Key_Left: command = Command::Left; break;
        case Qt::Key_Right: command = Command::Right; break;
        case Qt::Key_Up: command = Command::Up; break;
        case Qt::Key_Down: command = Command::Down; break;
        case Qt::Key_Return: case Qt::Key_Enter: command = Command::Enter; break;
        case Qt::Key_Escape: command = Command::Back; break;
        case Qt::Key_Home: command = Command::Home; break;
        case Qt::Key_MediaPlay: command = Command::Play; break;
        case Qt::Key_MediaPause: command = Command::Pause; break;
        case Qt::Key_MediaTogglePlayPause: command = Command::TogglePlay; break;
        case Qt::Key_MediaPrevious: command = Command::Previous; break;
        case Qt::Key_MediaNext: command = Command::Next; break;
        default: return false;
    }
    return true;
}
}

CarPlaySurface::CarPlaySurface(CarPlayClient &client, QWidget *parent) : QWidget(parent), client_(client) {
    setAttribute(Qt::WA_AcceptTouchEvents);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(160, 96);
    // Qt coalesces updates; an idle phone no longer repaints the whole display at 30 Hz.
    connect(&client_, &CarPlayClient::frameChanged, this, QOverload<>::of(&QWidget::update), Qt::QueuedConnection);
    connect(&client_, &CarPlayClient::connectedChanged, this, QOverload<>::of(&QWidget::update));
}

void CarPlaySurface::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    const auto image = client_.frame();
    if (image.isNull()) { imageRect_ = {}; contacts_.clear(); mouseDown_ = false; return; }
    const auto size = image.size().scaled(this->size(), Qt::KeepAspectRatio);
    imageRect_ = QRectF(QPointF((width() - size.width()) / 2.0, (height() - size.height()) / 2.0), size);
    painter.drawImage(imageRect_, image);
}

QJsonObject CarPlaySurface::contact(int id, const QPointF &position, bool down) const {
    return {{"id", id}, {"x", qBound(0.0, (position.x() - imageRect_.left()) / imageRect_.width(), 1.0)},
        {"y", qBound(0.0, (position.y() - imageRect_.top()) / imageRect_.height(), 1.0)}, {"down", down}};
}

bool CarPlaySurface::event(QEvent *event) {
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate ||
        event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
        if (imageRect_.isEmpty()) { event->accept(); return true; }
        QJsonArray values;
        if (event->type() == QEvent::TouchCancel) {
            for (const int id : contacts_) values.append(contact(id, imageRect_.center(), false));
            contacts_.clear();
        } else {
            auto *touch = static_cast<QTouchEvent *>(event);
            for (const auto &point : touch->touchPoints()) {
                if (!contacts_.contains(point.id())) {
                    if (contacts_.size() >= 2 || !imageRect_.contains(point.pos()) ||
                        point.state() == Qt::TouchPointReleased) continue;
                    contacts_.insert(point.id(), contacts_.values().contains(0) ? 1 : 0);
                }
                const bool down = point.state() != Qt::TouchPointReleased;
                values.append(contact(contacts_[point.id()], point.pos(), down));
                if (!down) contacts_.remove(point.id());
            }
        }
        client_.sendCommand({{"type", "touch"}, {"contacts", values}});
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

void CarPlaySurface::mouseContact(QMouseEvent *event, bool down) {
    if (!imageRect_.isEmpty())
        client_.sendCommand({{"type", "touch"}, {"contacts", QJsonArray{contact(0, event->localPos(), down)}}});
    event->accept();
}
void CarPlaySurface::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !imageRect_.contains(event->localPos())) return;
    mouseDown_ = true;
    setFocus();
    mouseContact(event, true);
}
void CarPlaySurface::mouseMoveEvent(QMouseEvent *event) { if (mouseDown_) mouseContact(event, true); }
void CarPlaySurface::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && mouseDown_) { mouseDown_ = false; mouseContact(event, false); }
}
void CarPlaySurface::keyPressEvent(QKeyEvent *event) {
    PhoneControls::Command command;
    if (!commandForKey(event->key(), command)) { QWidget::keyPressEvent(event); return; }
    if (!event->isAutoRepeat() && !heldKeys_.contains(event->key())) {
        heldKeys_.insert(event->key(), command);
        client_.sendControl(command, PhoneControls::State::Pressed);
    }
    event->accept();
}

void CarPlaySurface::keyReleaseEvent(QKeyEvent *event) {
    const auto found = heldKeys_.find(event->key());
    if (found == heldKeys_.end()) { QWidget::keyReleaseEvent(event); return; }
    if (!event->isAutoRepeat()) {
        client_.sendControl(found.value(), PhoneControls::State::Released);
        heldKeys_.erase(found);
    }
    event->accept();
}

void CarPlaySurface::focusOutEvent(QFocusEvent *event) {
    for (auto command : heldKeys_) client_.sendControl(command, PhoneControls::State::Released);
    heldKeys_.clear();
    QWidget::focusOutEvent(event);
}

#endif
