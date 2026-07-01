#include "FsrPlotWidget.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#include <algorithm>

namespace sensor::viewer {

namespace {
constexpr double WindowSeconds = 5.0;
constexpr int MaxAdcValue = 6000;
}

FsrPlotWidget::FsrPlotWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(760, 420);
    setAutoFillBackground(false);
}

void FsrPlotWidget::clear() {
    samples_.clear();
    latestTimeSeconds_ = 0.0;
    update();
}

void FsrPlotWidget::addSample(double timeSeconds, quint16 fsr1, quint16 fsr2) {
    latestTimeSeconds_ = std::max(latestTimeSeconds_, timeSeconds);
    samples_.push_back({timeSeconds, fsr1, fsr2});

    const double cutoff = latestTimeSeconds_ - WindowSeconds;
    while (!samples_.isEmpty() && samples_.front().timeSeconds < cutoff) {
        samples_.pop_front();
    }
    update();
}

void FsrPlotWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(12, 16, 24));

    const QRect plotRect = rect().adjusted(58, 24, -22, -46);
    painter.setPen(QPen(QColor(55, 65, 81), 1));
    painter.drawRect(plotRect);

    const double start = std::max(0.0, latestTimeSeconds_ - WindowSeconds);

    painter.setPen(QPen(QColor(38, 48, 65), 1));
    for (int i = 1; i < 10; ++i) {
        const int x = plotRect.left() + (plotRect.width() * i) / 10;
        painter.drawLine(x, plotRect.top(), x, plotRect.bottom());
    }
    for (int i = 1; i < 6; ++i) {
        const int y = plotRect.top() + (plotRect.height() * i) / 6;
        painter.drawLine(plotRect.left(), y, plotRect.right(), y);
    }

    painter.setPen(QColor(203, 213, 225));
    painter.drawText(10, plotRect.top() + 4, "6000");
    painter.drawText(20, plotRect.bottom(), "0");
    painter.drawText(plotRect.left(), height() - 18, "last 5 s");
    painter.drawText(plotRect.right() - 70, height() - 18, "now");

    if (samples_.size() >= 2) {
        QPainterPath path1;
        QPainterPath path2;
        bool started = false;
        for (const auto& sample : samples_) {
            const QPoint p1 = mapPoint(plotRect, start, sample.timeSeconds, sample.fsr1);
            const QPoint p2 = mapPoint(plotRect, start, sample.timeSeconds, sample.fsr2);
            if (!started) {
                path1.moveTo(p1);
                path2.moveTo(p2);
                started = true;
            } else {
                path1.lineTo(p1);
                path2.lineTo(p2);
            }
        }
        painter.setPen(QPen(QColor(96, 165, 250), 2));
        painter.drawPath(path1);
        painter.setPen(QPen(QColor(248, 113, 113), 2));
        painter.drawPath(path2);
    }

    painter.setPen(QColor(96, 165, 250));
    painter.drawText(plotRect.left(), 18, "FSR1");
    painter.setPen(QColor(248, 113, 113));
    painter.drawText(plotRect.left() + 55, 18, "FSR2");
    painter.setPen(QColor(148, 163, 184));
    painter.drawText(plotRect.left() + 118, 18, QString("samples kept: %1").arg(samples_.size()));
}

QPoint FsrPlotWidget::mapPoint(const QRect& plotRect, double startSeconds, double timeSeconds, quint16 value) const {
    const double xRatio = std::clamp((timeSeconds - startSeconds) / WindowSeconds, 0.0, 1.0);
    const double yRatio = std::clamp(static_cast<double>(value) / MaxAdcValue, 0.0, 1.0);
    const int x = plotRect.left() + static_cast<int>(xRatio * plotRect.width());
    const int y = plotRect.bottom() - static_cast<int>(yRatio * plotRect.height());
    return {x, y};
}

}  // namespace sensor::viewer
