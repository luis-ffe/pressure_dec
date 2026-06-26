#include <QtWidgets/QApplication>

#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    sensor::MainWindow window;
    window.show();

    return app.exec();
}
