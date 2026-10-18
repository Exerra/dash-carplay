#include <QApplication>
#include <QStringList>
#include <QWindow>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "app/window.hpp"
#include "app/action.hpp"
#include "app/receiver_settings.hpp"

static int runDash(int argc, char *argv[], QByteArray &executable)
{
    QApplication dash(argc, argv);
    executable = QCoreApplication::applicationFilePath().toLocal8Bit();

    dash.setOrganizationName("openDsh");
    dash.setApplicationName("dash");
    dash.installEventFilter(ActionEventFilter::get_instance());

    QSize size = dash.primaryScreen()->size();
    QPoint pos = dash.primaryScreen()->geometry().topLeft();
    bool fullscreen = true;

    QSettings settings;
    DASH_LOG(info) << "loaded config: " << settings.fileName().toStdString();

    QStringList args = dash.arguments();
    if (args.size() > 2) {
        size = QSize(args.at(1).toInt(), args.at(2).toInt());
        if (args.size() > 4)
            pos = QPoint(args.at(3).toInt(), args.at(4).toInt());
        fullscreen = false;
    }
    else {
        settings.beginGroup("Window");
        if (settings.contains("size")) {
            size = settings.value("size").toSize();
            if (settings.contains("pos"))
                pos = settings.value("pos").toPoint();
            fullscreen = false;
        }
    }

    QPixmap pixmap(QPixmap(":/splash.png").scaledToHeight(size.height() / 2));
    QSplashScreen splash(pixmap);
    splash.setMask(pixmap.mask());
    splash.move(pos.x() + ((size.width() / 2) - (splash.width() / 2)), pos.y() + ((size.height() / 2) - (splash.height() / 2)));
    splash.show();
    dash.processEvents();

    MainWindow window(QRect(pos, size));
    window.setWindowIcon(QIcon(":/logo.png"));
    window.setWindowFlags(Qt::FramelessWindowHint);
    if (fullscreen)
        window.setWindowState(Qt::WindowFullScreen);

    window.show();
    splash.finish(&window);

    return dash.exec();
}

int main(int argc, char *argv[])
{
    std::vector<std::string> arguments;
    for (int i = 0; i < argc; ++i) arguments.emplace_back(argv[i]);
    QByteArray executable;
    const int result = runDash(argc, argv, executable);
    if (result != Receivers::RestartExitCode) return result;

    // Qt and receiver workers have shut down. Keep the same PID for systemd and
    // preserve the launcher's environment, but never inherit old receiver sockets.
    if (DIR *directory = opendir("/proc/self/fd")) {
        while (auto *entry = readdir(directory)) {
            char *end = nullptr;
            const long descriptor = std::strtol(entry->d_name, &end, 10);
            if (*end || descriptor < 3) continue;
            const int flags = fcntl(int(descriptor), F_GETFD);
            if (flags >= 0) fcntl(int(descriptor), F_SETFD, flags | FD_CLOEXEC);
        }
        closedir(directory);
    } else {
        const long limit = sysconf(_SC_OPEN_MAX);
        for (int descriptor = 3; descriptor < limit; ++descriptor) {
            const int flags = fcntl(descriptor, F_GETFD);
            if (flags >= 0) fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC);
        }
    }
    arguments[0] = executable.constData();
    std::vector<char *> pointers;
    for (auto &argument : arguments) pointers.push_back(const_cast<char *>(argument.c_str()));
    pointers.push_back(nullptr);
    execv(executable.constData(), pointers.data());
    std::perror("Could not restart Dash");
    return EXIT_FAILURE;
}
