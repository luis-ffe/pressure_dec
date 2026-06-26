#include "LivePlotWidget.h"

#include <QtGui/QPainter>
#include <QtGui/QPen>

#include <algorithm>

#include "../core/AppConstants.h"

namespace sensor {

LivePlotWidget::LivePlotWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(360);
    setAutoFillBackground(true);
}

void LivePlotWidget::clear() {
    points_.clear();
    update();
}

void LivePlotWidget::addPoint(double timeSeconds, quint16 fsr1, quint16 fsr2) {
    points_.push_back({timeSeconds, fsr1, fsr2});
    const double cutoff = timeSeconds - constants::GraphWindowSeconds;
    while (points_.size() > 1 && points_.front().timeSeconds < cutoff) {
        points_.pop_front();
    }
    update();
}

void LivePlotWidget::setMaxValue(int maxValue) {
    maxValue_ = std::clamp(maxValue, 100, constants::AdcMaxValue);
    update();
}

void LivePlotWidget::setGraphMode(GraphMode mode) {
    graphMode_ = mode;
    update();
}

void LivePlotWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#ffffff"));

    const QRect plotRect = rect().adjusted(62, 34, -22, -48);
    painter.setPen(QPen(QColor("#d7dce2"), 1));
    painter.drawRoundedRect(plotRect, 6, 6);

    painter.setPen(QColor("#111827"));
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(14, 22, "Live sensor measurements");
    painter.setFont(QFont());
    painter.setPen(QColor("#6b7280"));
    painter.drawText(plotRect.left(), height() - 16, "Fixed window: last 30 seconds");

    painter.save();
    painter.translate(18, plotRect.center().y() + 48);
    painter.rotate(-90);
    painter.drawText(0, 0, "Raw ADC value");
    painter.restore();

    for (int i = 0; i <= 4; ++i) {
        const int y = plotRect.top() + i * plotRect.height() / 4;
        painter.setPen(QPen(QColor("#edf0f3"), 1));
        painter.drawLine(plotRect.left(), y, plotRect.right(), y);
        painter.setPen(QColor("#6b7280"));
        painter.drawText(10, y + 4, QString::number(maxValue_ - i * maxValue_ / 4));
    }

    if (points_.isEmpty()) {
        painter.setPen(QColor("#9ca3af"));
        painter.drawText(plotRect, Qt::AlignCenter, "No sensor data yet");
        return;
    }

    const double newest = points_.back().timeSeconds;
    const double start = newest - constants::GraphWindowSeconds;
    QPolygon line1;
    QPolygon line2;
    QPolygon diffLine;
    line1.reserve(points_.size());
    line2.reserve(points_.size());
    diffLine.reserve(points_.size());

    for (const PlotPoint& point : points_) {
        line1 << mapPoint(plotRect, start, point.timeSeconds, point.fsr1);
        line2 << mapPoint(plotRect, start, point.timeSeconds, point.fsr2);
        const int difference = static_cast<int>(point.fsr1) - static_cast<int>(point.fsr2);
        const int centered = std::clamp(maxValue_ / 2 + difference / 2, 0, maxValue_);
        diffLine << mapPoint(plotRect, start, point.timeSeconds, static_cast<quint16>(centered));
    }

    painter.setClipRect(plotRect.adjusted(1, 1, -1, -1));
    if (graphMode_ == GraphMode::Difference) {
        painter.setPen(QPen(QColor("#9ca3af"), 1, Qt::DashLine));
        painter.drawLine(plotRect.left(), plotRect.center().y(), plotRect.right(), plotRect.center().y());
        painter.setPen(QPen(QColor("#6366f1"), 2));
        painter.drawPolyline(diffLine);
    } else {
        if (graphMode_ == GraphMode::FilledComparison) {
            QPolygon area1(line1);
            area1 << QPoint(plotRect.right(), plotRect.bottom()) << QPoint(plotRect.left(), plotRect.bottom());
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 168, 143, 45));
            painter.drawPolygon(area1);
            QPolygon area2(line2);
            area2 << QPoint(plotRect.right(), plotRect.bottom()) << QPoint(plotRect.left(), plotRect.bottom());
            painter.setBrush(QColor(226, 59, 98, 45));
            painter.drawPolygon(area2);
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor("#00a88f"), 2));
        painter.drawPolyline(line1);
        painter.setPen(QPen(QColor("#e23b62"), 2));
        painter.drawPolyline(line2);
    }
    painter.setClipping(false);

    if (graphMode_ == GraphMode::Difference) {
        painter.setPen(QColor("#6366f1"));
        painter.drawText(plotRect.right() - 130, plotRect.top() + 22, "● FSR1 - FSR2");
    } else {
        painter.setPen(QColor("#00a88f"));
        painter.drawText(plotRect.right() - 136, plotRect.top() + 22, "● FSR 1");
        painter.setPen(QColor("#e23b62"));
        painter.drawText(plotRect.right() - 76, plotRect.top() + 22, "● FSR 2");
    }
}

QPoint LivePlotWidget::mapPoint(const QRect& plotRect, double startSeconds, double seconds, quint16 value) const {
    const double xNorm = std::clamp((seconds - startSeconds) / constants::GraphWindowSeconds, 0.0, 1.0);
    const double yNorm = std::clamp(static_cast<double>(value) / maxValue_, 0.0, 1.0);
    return {
        plotRect.left() + static_cast<int>(xNorm * plotRect.width()),
        plotRect.bottom() - static_cast<int>(yNorm * plotRect.height()),
    };
}

}  // namespace sensor
