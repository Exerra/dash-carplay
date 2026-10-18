#pragma once
#ifdef DASH_CARPLAY
#include "app/pages/page.hpp"
#include <QWidget>
class CarPlayPage : public QWidget, public Page {
    Q_OBJECT
 public:
    explicit CarPlayPage(Arbiter &arbiter, QWidget *parent = nullptr);
    void init() override;
};
#endif
