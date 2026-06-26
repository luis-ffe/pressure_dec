#pragma once

#include <QtSerialPort/QSerialPort>

#include "Transport.h"

namespace sensor {

class SerialTransport final : public Transport {
    Q_OBJECT

public:
    SerialTransport(QString portName, QObject* parent = nullptr);

    [[nodiscard]] Kind kind() const override { return Kind::Usb; }
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] bool isConnected() const override;

public slots:
    void connectToDevice() override;
    void disconnectFromDevice() override;
    void move(const QString& direction, int steps, int delayUs) override;
    void stop() override;
    void startCapture() override;
    void stopCapture() override;
    void pressureNudge(bool increasePressure, int steps, int delayUs) override;

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    void writeCommand(const QString& command);
    void processAsciiBuffer();
    void processCaptureBuffer();
    void decodePreviewLine(const QByteArray& line);
    void appendBinaryMeasurements(const QByteArray& payload, bool keepRemainder);
    void beginCapture();
    void finishCapture();

    QString portName_;
    QSerialPort serial_;
    QByteArray rxBuffer_;
    QByteArray binaryBuffer_;
    QVector<Measurement> captureSamples_;
    bool captureActive_ = false;
    int capturePairIndex_ = 0;
};

}  // namespace sensor
