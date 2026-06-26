#pragma once

#include <QtCore/QString>
#include <QtCore/QVector>

#include "../model/Measurement.h"

namespace sensor {

class DataExporter {
public:
    [[nodiscard]] static bool exportCsv(const QString& path, const QVector<Measurement>& rows, QString* error);
    [[nodiscard]] static bool exportExcelXml(
        const QString& path,
        const QVector<Measurement>& rows,
        const QString& testName,
        const QString& delaySummary,
        QString* error);

private:
    [[nodiscard]] static QString xmlEscape(QString text);
};

}  // namespace sensor
