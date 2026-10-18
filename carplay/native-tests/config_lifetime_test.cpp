#include <QApplication>
#include <QTemporaryDir>
#include "app/config.hpp"

// Exercise real QApplication teardown: Qt must not free static singleton storage.
int main(int argc, char **argv) {
    QTemporaryDir settings;
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
    QApplication app(argc, argv);
    app.setOrganizationName("opendash-lifetime-test");
    app.setApplicationName("config-lifetime");
    Config::get_instance();
    return 0;
}
