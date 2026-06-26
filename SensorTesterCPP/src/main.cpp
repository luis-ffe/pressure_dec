#include <QtWidgets/QApplication>

#include "ui/AppStyle.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    sensor::AppStyle::apply(app);

    sensor::MainWindow window;
    window.show();

    return app.exec();
}
