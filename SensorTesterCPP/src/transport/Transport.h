#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>

#include "../model/Measurement.h"

namespace sensor {

class Transport : public QObject {
    Q_OBJECT

public:
    enum class Kind { Usb, Wifi };

    explicit Transport(QObject* parent = nullptr) : QObject(parent) {}
    ~Transport() override = default;

    [[nodiscard]] virtual Kind kind() const = 0;
    [[nodiscard]] virtual QString displayName() const = 0;
    [[nodiscard]] virtual bool isConnected() const = 0;

public slots:
    virtual void connectToDevice() = 0;
    virtual void disconnectFromDevice() = 0;
    virtual void move(const QString& direction, int steps, int delayUs) = 0;
    virtual void stop() = 0;
    virtual void startCapture() = 0;
    virtual void stopCapture() = 0;
    virtual void pressureNudge(bool increasePressure, int steps, int delayUs) = 0;
    virtual void setFsr2ResistanceChannel(int channel) = 0;

signals:
    void connected(QString displayName);
    void disconnected();
    void statusMessage(QString message);
    void errorOccurred(QString message);
    void sampleReceived(sensor::SensorSample sample);
    void captureStarted();
    void captureFinished(QVector<sensor::Measurement> samples);
};

}  // namespace sensor
