#include <QtWidgets/QApplication>

#include "../ui/AppStyle.h"
#include "FsrViewerWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    sensor::AppStyle::apply(app);
    sensor::viewer::FsrViewerWindow window;
    window.show();
    return app.exec();
}
