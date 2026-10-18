#pragma once
#ifdef DASH_CARPLAY
#include "carplay/client.hpp"
#include <QWidget>
class CarPlayVideoWidget;

class CarPlaySurface : public QWidget {
    Q_OBJECT
 public:
    explicit CarPlaySurface(CarPlayClient &client, QWidget *parent = nullptr);
 protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool event(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
 private:
    CarPlayClient &client_;
    CarPlayVideoWidget *video_ = nullptr;
    QRectF imageRect_;
    bool mouseDown_ = false;
    QMap<int, int> contacts_;
    QMap<int, PhoneControls::Command> heldKeys_;
    QJsonObject contact(int id, const QPointF &position, bool down) const;
    void mouseContact(QMouseEvent *event, bool down);
};

#endif
