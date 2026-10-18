#pragma once
#ifdef DASH_CARPLAY
#include "carplay/client.hpp"
#include <QWidget>

class CarPlaySurface : public QWidget {
    Q_OBJECT
 public:
    explicit CarPlaySurface(CarPlayClient &client, QWidget *parent = nullptr);
 protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
 private:
    CarPlayClient &client_;
    QRectF imageRect_;
    bool mouseDown_ = false;
    QMap<int, int> contacts_;
    QJsonObject contact(int id, const QPointF &position, bool down) const;
    void mouseContact(QMouseEvent *event, bool down);
};

#endif
