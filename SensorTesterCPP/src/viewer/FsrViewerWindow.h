#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QTimer>
#include <QtSerialPort/QSerialPort>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QPushButton>

#include "FsrPlotWidget.h"

namespace sensor::viewer {

class FsrViewerWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit FsrViewerWindow(QWidget* parent = nullptr);
    ~FsrViewerWindow() override;

private slots:
    void refreshPorts();
    void toggleConnection();
    void requestRawSnapshot();
    void copyRawSnapshot();
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    void buildUi();
    void connectSerial();
    void disconnectSerial();
    void beginCapture();
    void processAsciiBuffer();
    void processBinaryBuffer();
    void setStatus(const QString& text);
    void sendCommand(const QString& command);
    void appendPair(quint16 fsr1, quint16 fsr2);

    QComboBox* portCombo_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* rawSnapshotButton_ = nullptr;
    QPushButton* copyRawButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* fsr1Label_ = nullptr;
    QLabel* fsr2Label_ = nullptr;
    QLabel* rawSnapshotLabel_ = nullptr;
    FsrPlotWidget* plot_ = nullptr;

    QSerialPort serial_;
    QByteArray asciiBuffer_;
    QByteArray binaryBuffer_;
    bool binaryActive_ = false;
    bool pendingRawSnapshot_ = false;
    bool resumeAfterRawSnapshot_ = false;
    QString lastRawSnapshotLine_;
    quint64 sampleIndex_ = 0;
};

}  // namespace sensor::viewer
