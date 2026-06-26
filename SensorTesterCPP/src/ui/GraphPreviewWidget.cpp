#include "GraphPreviewWidget.h"

#include <QtGui/QPainter>
#include <QtGui/QPen>

namespace sensor {

GraphPreviewWidget::GraphPreviewWidget(QWidget* parent) : QWidget(parent) {
    setFixedSize(150, 82);
}

void GraphPreviewWidget::setMode(GraphMode mode) {
    mode_ = mode;
    update();
}

void GraphPreviewWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#ffffff"));
    const QRect plot = rect().adjusted(10, 10, -10, -14);
    painter.setPen(QPen(QColor("#dfe4ea"), 1));
    painter.drawRoundedRect(plot, 6, 6);

    const QVector<QPoint> fsr1 = {
        {plot.left() + 4, plot.bottom() - 8},
        {plot.left() + plot.width() / 3, plot.bottom() - 12},
        {plot.left() + plot.width() / 2, plot.top() + 12},
        {plot.right() - 4, plot.top() + 8},
    };
    const QVector<QPoint> fsr2 = {
        {plot.left() + 4, plot.bottom() - 5},
        {plot.left() + plot.width() / 3, plot.bottom() - 7},
        {plot.left() + plot.width() / 2, plot.bottom() - 15},
        {plot.right() - 4, plot.top() + 18},
    };

    if (mode_ == GraphMode::FilledComparison) {
        QPolygon area1(fsr1);
        area1 << QPoint(plot.right() - 4, plot.bottom()) << QPoint(plot.left() + 4, plot.bottom());
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 168, 143, 45));
        painter.drawPolygon(area1);
        QPolygon area2(fsr2);
        area2 << QPoint(plot.right() - 4, plot.bottom()) << QPoint(plot.left() + 4, plot.bottom());
        painter.setBrush(QColor(226, 59, 98, 45));
        painter.drawPolygon(area2);
    }

    if (mode_ == GraphMode::Difference) {
        painter.setPen(QPen(QColor("#9ca3af"), 1, Qt::DashLine));
        painter.drawLine(plot.left() + 4, plot.center().y(), plot.right() - 4, plot.center().y());
        QPolygon diff;
        diff << QPoint(plot.left() + 4, plot.center().y())
             << QPoint(plot.left() + plot.width() / 3, plot.center().y() - 3)
             << QPoint(plot.left() + plot.width() / 2, plot.top() + 17)
             << QPoint(plot.right() - 4, plot.center().y() - 14);
        painter.setPen(QPen(QColor("#6366f1"), 2));
        painter.drawPolyline(diff);
        return;
    }

    painter.setPen(QPen(QColor("#00a88f"), 2));
    painter.drawPolyline(QPolygon(fsr1));
    painter.setPen(QPen(QColor("#e23b62"), 2));
    painter.drawPolyline(QPolygon(fsr2));
}

}  // namespace sensor
