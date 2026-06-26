#include "ProfileCurveWidget.h"

#include <QtGui/QPainter>
#include <QtGui/QPen>

#include <algorithm>

#include "../core/AppConstants.h"

namespace sensor {

ProfileCurveWidget::ProfileCurveWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 210);
}

void ProfileCurveWidget::setProfile(TestProfileSettings settings) {
    settings_ = settings;
    update();
}

void ProfileCurveWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#ffffff"));

    const QRect plot = rect().adjusted(54, 28, -18, -42);
    painter.setPen(QPen(QColor("#d7dce2"), 1));
    painter.drawRoundedRect(plot, 6, 6);

    painter.setPen(QColor("#111827"));
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(12, 20, "Target pressure profile");
    painter.setFont(QFont());
    painter.setPen(QColor("#6b7280"));
    painter.drawText(plot.left(), height() - 14, "10 second theoretical window");

    const double peak = std::max(1.0, settings_.peakForce);
    for (int i = 0; i <= 4; ++i) {
        const int y = plot.top() + i * plot.height() / 4;
        painter.setPen(QPen(QColor("#edf0f3"), 1));
        painter.drawLine(plot.left(), y, plot.right(), y);
        painter.setPen(QColor("#6b7280"));
        painter.drawText(8, y + 4, QString::number(static_cast<int>(peak - i * peak / 4.0)));
    }

    TestProfileGenerator generator(settings_);
    QPolygon curve;
    constexpr int samples = 240;
    curve.reserve(samples);
    for (int i = 0; i < samples; ++i) {
        const double t = 10.0 * i / static_cast<double>(samples - 1);
        const double target = std::clamp(generator.targetPressureAt(t), 0.0, peak);
        const double xNorm = t / 10.0;
        const double yNorm = target / peak;
        curve << QPoint(
            plot.left() + static_cast<int>(xNorm * plot.width()),
            plot.bottom() - static_cast<int>(yNorm * plot.height()));
    }

    painter.setClipRect(plot.adjusted(1, 1, -1, -1));
    painter.setPen(QPen(QColor("#6366f1"), 2));
    painter.drawPolyline(curve);
    painter.setClipping(false);

    painter.setPen(QColor("#6366f1"));
    painter.drawText(plot.right() - 170, plot.top() + 22, "● " + profileTypeName(settings_.type));
}

}  // namespace sensor
