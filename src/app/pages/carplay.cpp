#ifdef DASH_CARPLAY
#include "app/pages/carplay.hpp"
#include "app/arbiter.hpp"
#include "carplay/widget.hpp"
#include <QVBoxLayout>

CarPlayPage::CarPlayPage(Arbiter &arbiter, QWidget *parent)
    : QWidget(parent), Page(arbiter, "CarPlay", "directions_car", true, this) { }

void CarPlayPage::init() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *widget = new CarPlayWidget(this);
    layout->addWidget(widget);
    connect(widget, &CarPlayWidget::managementRequested, this, [this] { arbiter.set_curr_page(this); });
    connect(&arbiter, &Arbiter::mode_changed, widget, [widget](Session::Theme::Mode mode) {
        widget->setNightMode(mode == Session::Theme::Dark);
    });
    connect(&arbiter, &Arbiter::volume_changed, widget, &CarPlayWidget::setVolume);
    connect(arbiter.layout().openauto_page, &QStackedWidget::currentChanged, widget, [widget](int index) {
        widget->setAndroidAutoActive(index == 1);
    });
    widget->setAndroidAutoActive(arbiter.layout().openauto_page->currentIndex() == 1);
    widget->setNightMode(arbiter.theme().mode == Session::Theme::Dark);
    widget->setVolume(arbiter.system().volume);
}
#endif
