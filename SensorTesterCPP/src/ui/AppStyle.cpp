#include "AppStyle.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QWidget>

namespace sensor {

namespace {

constexpr const char* StyleSheet = R"(
    QMainWindow, QWidget { background: #f5f7fb; color: #111827; }
    QGroupBox {
        background: white;
        border: 1px solid #dfe4ea;
        border-radius: 10px;
        margin-top: 12px;
        padding: 10px;
        font-weight: 600;
    }
    QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }
    QPushButton {
        background: #eef2ff;
        border: 1px solid #c7d2fe;
        border-radius: 7px;
        padding: 7px 10px;
    }
    QPushButton:hover { background: #e0e7ff; }
    QPushButton#dangerButton {
        background: #fee2e2;
        border-color: #fca5a5;
        font-weight: 700;
        padding: 10px;
    }
    QLabel#subtitle { color: #6b7280; }
    QLabel#statusPill {
        background: #f3f4f6;
        border: 1px solid #e5e7eb;
        border-radius: 9px;
        padding: 4px 8px;
        color: #374151;
    }
    QLabel[reading="true"] {
        background: white;
        border: 1px solid #dfe4ea;
        border-radius: 12px;
        padding: 16px;
    }
)";

}  // namespace

void AppStyle::apply(QApplication& app) {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    app.setStyleSheet(StyleSheet);
}

void AppStyle::applyTo(QWidget& widget) {
    widget.setStyleSheet(StyleSheet);
}

}  // namespace sensor
