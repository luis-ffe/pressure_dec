#include "WifiTransport.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>

#include "../core/AppConstants.h"

namespace sensor {

WifiTransport::WifiTransport(QString baseUrl, int pollIntervalMs, QObject* parent)
    : Transport(parent), baseUrl_(std::move(baseUrl)), pollIntervalMs_(std::clamp(pollIntervalMs, 100, 500)) {
    baseUrl_ = baseUrl_.trimmed();
    while (baseUrl_.endsWith('/')) {
        baseUrl_.chop(1);
    }
    pollTimer_.setInterval(pollIntervalMs_);
    connect(&pollTimer_, &QTimer::timeout, this, &WifiTransport::requestSensors);
}

QString WifiTransport::displayName() const {
    return "Wi-Fi";
}

bool WifiTransport::isConnected() const {
    return connected_;
}

void WifiTransport::setPollIntervalMs(int pollIntervalMs) {
    pollIntervalMs_ = std::clamp(pollIntervalMs, 100, 500);
    pollTimer_.setInterval(pollIntervalMs_);
}

void WifiTransport::connectToDevice() {
    connected_ = true;
    sampleClockSeconds_ = 0.0;
    emit connected(displayName());
    pollTimer_.start();
    requestSensors();
}

void WifiTransport::disconnectFromDevice() {
    pollTimer_.stop();
    connected_ = false;
    emit disconnected();
}

void WifiTransport::move(const QString& direction, int steps, int delayUs) {
    const QString firmwareDirection = direction == "up" ? "forward" : "backward";
    get("/move", {{"dir", firmwareDirection}, {"steps", QString::number(steps)}, {"speed", QString::number(delayUs)}});
    emit statusMessage(QString("Commanded %1: %2 steps").arg(direction).arg(steps));
}

void WifiTransport::stop() {
    get("/stop");
    emit statusMessage("Motor stopped");
}

void WifiTransport::startCapture() {
    emit errorOccurred("The 10 kHz DMA capture is available only through USB.");
}

void WifiTransport::stopCapture() {
    // High-rate capture is intentionally USB-only.
}

void WifiTransport::pressureNudge(bool increasePressure, int steps, int delayUs) {
    const QString firmwareDirection = increasePressure ? "backward" : "forward";
    get("/move", {{"dir", firmwareDirection}, {"steps", QString::number(steps)}, {"speed", QString::number(delayUs)}});
}

void WifiTransport::requestSensors() {
    if (!connected_) {
        return;
    }
    get("/sensors", {}, [this](QNetworkReply* reply) {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            return;
        }

        const QJsonObject obj = doc.object();
        sampleClockSeconds_ += pollIntervalMs_ / 1000.0;
        emit sampleReceived({
            sampleClockSeconds_,
            static_cast<quint16>(obj.value("fsr1").toInt(obj.value("f1").toInt())),
            static_cast<quint16>(obj.value("fsr2").toInt(obj.value("f2").toInt())),
            obj.contains("moving") || obj.contains("steps_remaining"),
            obj.value("moving").toBool(false),
            obj.value("steps_remaining").toInt(0),
        });
    });
}

void WifiTransport::get(
    const QString& path,
    std::initializer_list<QPair<QString, QString>> params,
    ReplyHandler handler) {
    QUrl url(baseUrl_ + path);
    QUrlQuery query;
    for (const auto& param : params) {
        query.addQueryItem(param.first, param.second);
    }
    url.setQuery(query);

    QNetworkReply* reply = network_.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, handler]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit statusMessage("Wi-Fi error");
        } else if (handler) {
            handler(reply);
        }
        reply->deleteLater();
    });
}

}  // namespace sensor
