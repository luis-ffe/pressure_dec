#include "CaptureDecoder.h"

#include <QtCore/QtEndian>

#include "AppConstants.h"

namespace sensor {

void CaptureDecoder::reset() {
    buffer_.clear();
}

void CaptureDecoder::append(QByteArray bytes) {
    buffer_.append(std::move(bytes));
}

bool CaptureDecoder::hasCompleteCapture() const {
    return buffer_.size() >= constants::CaptureBytes;
}

int CaptureDecoder::pendingByteCount() const {
    return buffer_.size();
}

QVector<Measurement> CaptureDecoder::takeCapture() {
    QVector<Measurement> samples;
    if (!hasCompleteCapture()) {
        return samples;
    }

    const QByteArray payload = buffer_.left(constants::CaptureBytes);
    buffer_.remove(0, constants::CaptureBytes);

    samples.reserve(constants::CapturePairs);
    for (int index = 0; index < constants::CapturePairs; ++index) {
        const auto* raw = reinterpret_cast<const uchar*>(payload.constData() + index * constants::BytesPerCapturePair);
        samples.push_back({
            index * constants::SampleIntervalMs,
            qFromLittleEndian<quint16>(raw),
            qFromLittleEndian<quint16>(raw + 2),
        });
    }
    return samples;
}

}  // namespace sensor
