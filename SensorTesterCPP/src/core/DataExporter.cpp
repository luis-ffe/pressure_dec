#include "DataExporter.h"

#include <QtCore/QFile>
#include <QtCore/QTextStream>

namespace sensor {

bool DataExporter::exportCsv(const QString& path, const QVector<Measurement>& rows, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    QTextStream out(&file);
    out << "time_ms,fsr1,fsr2\n";
    for (const Measurement& row : rows) {
        out << QString::number(row.timeMs, 'f', 3) << ',' << row.fsr1 << ',' << row.fsr2 << '\n';
    }
    return true;
}

bool DataExporter::exportExcelXml(
    const QString& path,
    const QVector<Measurement>& rows,
    const QString& testName,
    const QString& delaySummary,
    QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    QTextStream out(&file);
    out << R"(<?xml version="1.0"?>)" << '\n';
    out << R"(<?mso-application progid="Excel.Sheet"?>)" << '\n';
    out << R"(<Workbook xmlns="urn:schemas-microsoft-com:office:spreadsheet" xmlns:ss="urn:schemas-microsoft-com:office:spreadsheet">)" << '\n';
    out << R"(<Worksheet ss:Name="Measurements"><Table>)" << '\n';
    out << R"(<Row><Cell><Data ss:Type="String">time_ms</Data></Cell><Cell><Data ss:Type="String">fsr1</Data></Cell><Cell><Data ss:Type="String">fsr2</Data></Cell></Row>)" << '\n';
    for (const Measurement& row : rows) {
        out << "<Row>"
            << "<Cell><Data ss:Type=\"Number\">" << QString::number(row.timeMs, 'f', 3) << "</Data></Cell>"
            << "<Cell><Data ss:Type=\"Number\">" << row.fsr1 << "</Data></Cell>"
            << "<Cell><Data ss:Type=\"Number\">" << row.fsr2 << "</Data></Cell>"
            << "</Row>\n";
    }
    out << R"(</Table></Worksheet>)" << '\n';
    out << R"(<Worksheet ss:Name="Summary"><Table>)" << '\n';
    out << "<Row><Cell><Data ss:Type=\"String\">Test</Data></Cell><Cell><Data ss:Type=\"String\">" << xmlEscape(testName) << "</Data></Cell></Row>\n";
    out << "<Row><Cell><Data ss:Type=\"String\">Samples</Data></Cell><Cell><Data ss:Type=\"Number\">" << rows.size() << "</Data></Cell></Row>\n";
    out << "<Row><Cell><Data ss:Type=\"String\">Delay</Data></Cell><Cell><Data ss:Type=\"String\">" << xmlEscape(delaySummary) << "</Data></Cell></Row>\n";
    out << R"(</Table></Worksheet></Workbook>)" << '\n';
    return true;
}

QString DataExporter::xmlEscape(QString text) {
    text.replace("&", "&amp;");
    text.replace("<", "&lt;");
    text.replace(">", "&gt;");
    text.replace("\"", "&quot;");
    return text;
}

}  // namespace sensor
