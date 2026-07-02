#pragma once

#include <functional>

#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

#include "Transport.h"

namespace sensor {

class WifiTransport final : public Transport {
    Q_OBJECT

public:
    explicit WifiTransport(QString baseUrl, int pollIntervalMs, QObject* parent = nullptr);

    [[nodiscard]] Kind kind() const override { return Kind::Wifi; }
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] bool isConnected() const override;
    void setPollIntervalMs(int pollIntervalMs);

public slots:
    void connectToDevice() override;
    void disconnectFromDevice() override;
    void move(const QString& direction, int steps, int delayUs) override;
    void stop() override;
    void startCapture() override;
    void stopCapture() override;
    void pressureNudge(bool increasePressure, int steps, int delayUs) override;
    void setFsr2ResistanceChannel(int channel) override;

private slots:
    void requestSensors();

private:
    using ReplyHandler = std::function<void(QNetworkReply*)>;

    void get(
        const QString& path,
        std::initializer_list<QPair<QString, QString>> params = {},
        ReplyHandler handler = {});

    QString baseUrl_;
    QNetworkAccessManager network_;
    QTimer pollTimer_;
    bool connected_ = false;
    double sampleClockSeconds_ = 0.0;
    int pollIntervalMs_ = 200;
};

}  // namespace sensor
