#include "SerialTransport.h"

#include <QtCore/QtEndian>
#include <QtSerialPort/QSerialPortInfo>

#include <algorithm>

#include "../core/AppConstants.h"

namespace sensor {

SerialTransport::SerialTransport(QString portName, QObject* parent)
    : Transport(parent), portName_(std::move(portName)) {
    connect(&serial_, &QSerialPort::readyRead, this, &SerialTransport::onReadyRead);
    connect(&serial_, &QSerialPort::errorOccurred, this, &SerialTransport::onSerialError);
}

QString SerialTransport::displayName() const {
    return "USB";
}

bool SerialTransport::isConnected() const {
    return serial_.isOpen();
}

void SerialTransport::connectToDevice() {
    serial_.setPortName(portName_);
    serial_.setBaudRate(constants::UsbBaudRate);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial_.open(QIODevice::ReadWrite)) {
        emit errorOccurred(serial_.errorString());
        return;
    }

    rxBuffer_.clear();
    binaryBuffer_.clear();
    captureSamples_.clear();
    capturePairIndex_ = 0;
    captureActive_ = false;
    serial_.clear(QSerialPort::AllDirections);
    writeCommand("PING");
    emit connected(displayName());
}

void SerialTransport::disconnectFromDevice() {
    if (serial_.isOpen()) {
        writeCommand("RECORD,0");
        serial_.close();
    }
    captureActive_ = false;
    rxBuffer_.clear();
    binaryBuffer_.clear();
    captureSamples_.clear();
    capturePairIndex_ = 0;
    emit disconnected();
}

void SerialTransport::move(const QString& direction, int steps, int delayUs) {
    writeCommand(QString("MOVE,%1,%2,%3").arg(direction.toUpper()).arg(steps).arg(delayUs));
    emit statusMessage(QString("Commanded %1: %2 steps").arg(direction).arg(steps));
}

void SerialTransport::stop() {
    writeCommand("STOP");
    emit statusMessage("Motor stopped");
}

void SerialTransport::autoTest(int threshold, int downSpeed, int upSpeed, int retractSteps) {
    writeCommand(QString("AUTOTEST,%1,%2,%3,%4").arg(threshold).arg(downSpeed).arg(upSpeed).arg(retractSteps));
    emit statusMessage(QString("Automated Test: moving down until %1").arg(threshold));
}

void SerialTransport::startCapture() {
    rxBuffer_.clear();
    binaryBuffer_.clear();
    captureSamples_.clear();
    capturePairIndex_ = 0;
    writeCommand("RECORD,1");
}

void SerialTransport::stopCapture() {
    writeCommand("RECORD,0");
    emit statusMessage("Recording stopped");
}

void SerialTransport::pressureNudge(bool increasePressure, int steps, int delayUs) {
    writeCommand(QString("%1,%2,%3").arg(increasePressure ? "F" : "B").arg(steps).arg(delayUs));
}

void SerialTransport::onReadyRead() {
    if (captureActive_) {
        binaryBuffer_.append(serial_.readAll());
        processCaptureBuffer();
    } else {
        rxBuffer_.append(serial_.readAll());
        processAsciiBuffer();
    }
}

void SerialTransport::onSerialError(QSerialPort::SerialPortError error) {
    if (error != QSerialPort::NoError && serial_.isOpen()) {
        emit errorOccurred("USB error: " + serial_.errorString());
    }
}

void SerialTransport::writeCommand(const QString& command) {
    if (!serial_.isOpen()) {
        emit errorOccurred("USB is not connected");
        return;
    }
    serial_.write((command + "\n").toUtf8());
    serial_.flush();
}

void SerialTransport::processAsciiBuffer() {
    while (true) {
        const int lineEnd = rxBuffer_.indexOf('\n');
        if (lineEnd < 0) {
            return;
        }

        QByteArray line = rxBuffer_.left(lineEnd).trimmed();
        rxBuffer_.remove(0, lineEnd + 1);
        if (line == "BINARY_CAPTURE_START") {
            beginCapture();
            if (!rxBuffer_.isEmpty()) {
                binaryBuffer_.append(rxBuffer_);
                rxBuffer_.clear();
                processCaptureBuffer();
            }
            return;
        }
        decodePreviewLine(line);
    }
}

void SerialTransport::processCaptureBuffer() {
    static const QByteArray stopMarker = "BINARY_CAPTURE_STOP\n";

    const int stopIndex = binaryBuffer_.indexOf(stopMarker);
    if (stopIndex >= 0) {
        const QByteArray payload = binaryBuffer_.left(stopIndex);
        appendBinaryMeasurements(payload, false);
        binaryBuffer_.remove(0, stopIndex + stopMarker.size());
        finishCapture();
        if (!binaryBuffer_.isEmpty()) {
            rxBuffer_.append(binaryBuffer_);
            binaryBuffer_.clear();
            processAsciiBuffer();
        }
        return;
    }

    int safeBytes = binaryBuffer_.size() - (stopMarker.size() - 1);
    safeBytes -= safeBytes % constants::BytesPerCapturePair;
    if (safeBytes <= 0) {
        return;
    }
    appendBinaryMeasurements(binaryBuffer_.left(safeBytes), false);
    binaryBuffer_.remove(0, safeBytes);
}

void SerialTransport::decodePreviewLine(const QByteArray& line) {
    if (!line.startsWith("S,")) {
        return;
    }

    const QList<QByteArray> parts = line.split(',');
    if (parts.size() != 4) {
        return;
    }

    bool okTime = false;
    bool okFsr1 = false;
    bool okFsr2 = false;
    const double timeSeconds = parts[1].toDouble(&okTime) / 1000.0;
    const int fsr1 = parts[2].toInt(&okFsr1);
    const int fsr2 = parts[3].toInt(&okFsr2);
    if (!okTime || !okFsr1 || !okFsr2) {
        return;
    }

    emit sampleReceived({
        timeSeconds,
        static_cast<quint16>(std::clamp(fsr1, 0, constants::AdcMaxValue)),
        static_cast<quint16>(std::clamp(fsr2, 0, constants::AdcMaxValue)),
        false,
        false,
        0,
    });
}

void SerialTransport::appendBinaryMeasurements(const QByteArray& payload, bool keepRemainder) {
    int processBytes = payload.size() - (payload.size() % constants::BytesPerCapturePair);
    if (keepRemainder && processBytes == payload.size() && processBytes >= constants::BytesPerCapturePair) {
        processBytes -= constants::BytesPerCapturePair;
    }
    if (processBytes <= 0) {
        return;
    }

    for (int offset = 0; offset < processBytes; offset += constants::BytesPerCapturePair) {
        const auto* raw = reinterpret_cast<const uchar*>(payload.constData() + offset);
        const quint16 fsr1 = qFromLittleEndian<quint16>(raw);
        const quint16 fsr2 = qFromLittleEndian<quint16>(raw + 2);
        const double timeMs = capturePairIndex_ * constants::SampleIntervalMs;
        captureSamples_.push_back({timeMs, fsr1, fsr2});

        if (capturePairIndex_ % 128 == 0) {
            emit sampleReceived({
                timeMs / 1000.0,
                fsr1,
                fsr2,
                false,
                false,
                0,
            });
        }
        ++capturePairIndex_;
    }

    Q_UNUSED(keepRemainder);
}

void SerialTransport::beginCapture() {
    if (captureActive_) {
        return;
    }
    captureActive_ = true;
    binaryBuffer_.clear();
    captureSamples_.clear();
    capturePairIndex_ = 0;
    emit captureStarted();
}

void SerialTransport::finishCapture() {
    captureActive_ = false;
    emit captureFinished(captureSamples_);
    captureSamples_.clear();
    capturePairIndex_ = 0;
}

}  // namespace sensor
