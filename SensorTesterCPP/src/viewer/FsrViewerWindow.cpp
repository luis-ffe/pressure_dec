#include "FsrViewerWindow.h"

#include <QtCore/QThread>
#include <QtCore/QtEndian>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtSerialPort/QSerialPortInfo>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>

#include "../core/AppConstants.h"

namespace sensor::viewer {

FsrViewerWindow::FsrViewerWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("FSR Live Viewer");
    resize(980, 620);
    connect(&serial_, &QSerialPort::readyRead, this, &FsrViewerWindow::onReadyRead);
    connect(&serial_, &QSerialPort::errorOccurred, this, &FsrViewerWindow::onSerialError);
    buildUi();
    refreshPorts();
}

FsrViewerWindow::~FsrViewerWindow() {
    disconnectSerial();
}

void FsrViewerWindow::buildUi() {
    auto* root = new QWidget(this);
    setCentralWidget(root);
    auto* layout = new QVBoxLayout(root);

    auto* title = new QLabel("FSR1 / FSR2 live output");
    QFont titleFont = title->font();
    titleFont.setPointSize(22);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* controls = new QHBoxLayout();
    portCombo_ = new QComboBox();
    refreshButton_ = new QPushButton("Refresh");
    connectButton_ = new QPushButton("Connect");
    rawSnapshotButton_ = new QPushButton("Raw snapshot");
    rawSnapshotButton_->setEnabled(false);
    copyRawButton_ = new QPushButton("Copy raw");
    copyRawButton_->setEnabled(false);
    statusLabel_ = new QLabel("Disconnected");
    controls->addWidget(portCombo_, 1);
    controls->addWidget(refreshButton_);
    controls->addWidget(connectButton_);
    controls->addWidget(rawSnapshotButton_);
    controls->addWidget(copyRawButton_);
    controls->addWidget(statusLabel_, 2);
    layout->addLayout(controls);

    auto* readings = new QHBoxLayout();
    fsr1Label_ = new QLabel("FSR1: —");
    fsr2Label_ = new QLabel("FSR2: —");
    rawSnapshotLabel_ = new QLabel("Raw: —");
    rawSnapshotLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    QFont readingFont = fsr1Label_->font();
    readingFont.setPointSize(20);
    readingFont.setBold(true);
    fsr1Label_->setFont(readingFont);
    fsr2Label_->setFont(readingFont);
    rawSnapshotLabel_->setFont(readingFont);
    readings->addWidget(fsr1Label_);
    readings->addWidget(fsr2Label_);
    readings->addWidget(rawSnapshotLabel_);
    readings->addStretch();
    layout->addLayout(readings);

    auto* graphBox = new QGroupBox("Rolling graph · last 5 seconds · Y axis 0–6000 ADC");
    auto* graphLayout = new QVBoxLayout(graphBox);
    plot_ = new FsrPlotWidget();
    graphLayout->addWidget(plot_);
    layout->addWidget(graphBox, 1);

    connect(refreshButton_, &QPushButton::clicked, this, &FsrViewerWindow::refreshPorts);
    connect(connectButton_, &QPushButton::clicked, this, &FsrViewerWindow::toggleConnection);
    connect(rawSnapshotButton_, &QPushButton::clicked, this, &FsrViewerWindow::requestRawSnapshot);
    connect(copyRawButton_, &QPushButton::clicked, this, &FsrViewerWindow::copyRawSnapshot);
}

void FsrViewerWindow::refreshPorts() {
    const QString previous = portCombo_->currentData().toString();
    portCombo_->clear();
    for (const QSerialPortInfo& port : QSerialPortInfo::availablePorts()) {
        const QString label = QString("%1 · %2").arg(port.portName(), port.description());
        portCombo_->addItem(label, port.systemLocation());
        if (port.systemLocation() == previous) {
            portCombo_->setCurrentIndex(portCombo_->count() - 1);
        }
    }
}

void FsrViewerWindow::toggleConnection() {
    if (serial_.isOpen()) {
        disconnectSerial();
    } else {
        connectSerial();
    }
}

void FsrViewerWindow::connectSerial() {
    if (portCombo_->currentIndex() < 0) {
        setStatus("No serial port selected");
        return;
    }

    serial_.setPortName(portCombo_->currentData().toString());
    serial_.setBaudRate(constants::UsbBaudRate);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial_.open(QIODevice::ReadWrite)) {
        setStatus("Open failed: " + serial_.errorString());
        return;
    }

    serial_.setDataTerminalReady(false);
    serial_.setRequestToSend(false);
    QThread::msleep(1200);
    serial_.clear(QSerialPort::AllDirections);

    asciiBuffer_.clear();
    binaryBuffer_.clear();
    binaryActive_ = false;
    sampleIndex_ = 0;
    plot_->clear();

    pendingRawSnapshot_ = false;
    resumeAfterRawSnapshot_ = false;
    lastRawSnapshotLine_.clear();
    rawSnapshotLabel_->setText("Raw: —");

    sendCommand("PING");
    beginCapture();
    connectButton_->setText("Disconnect");
    rawSnapshotButton_->setEnabled(true);
    copyRawButton_->setEnabled(false);
    setStatus("Connected · streaming 2 ms binary FSR pairs");
}

void FsrViewerWindow::disconnectSerial() {
    if (serial_.isOpen()) {
        sendCommand("RECORD,0");
        serial_.flush();
        serial_.close();
    }
    binaryActive_ = false;
    pendingRawSnapshot_ = false;
    resumeAfterRawSnapshot_ = false;
    lastRawSnapshotLine_.clear();
    connectButton_->setText("Connect");
    rawSnapshotButton_->setEnabled(false);
    copyRawButton_->setEnabled(false);
    setStatus("Disconnected");
}

void FsrViewerWindow::beginCapture() {
    if (!serial_.isOpen()) {
        return;
    }
    sendCommand("RECORD,1");
}

void FsrViewerWindow::requestRawSnapshot() {
    if (!serial_.isOpen() || pendingRawSnapshot_) {
        return;
    }

    pendingRawSnapshot_ = true;
    rawSnapshotButton_->setEnabled(false);
    rawSnapshotLabel_->setText("Raw: reading…");

    if (binaryActive_) {
        resumeAfterRawSnapshot_ = true;
        sendCommand("RECORD,0");
        setStatus("Pausing stream for raw ADS snapshot…");
    } else {
        resumeAfterRawSnapshot_ = false;
        sendCommand("RAW");
        setStatus("Requesting raw ADS snapshot…");
    }
}

void FsrViewerWindow::copyRawSnapshot() {
    if (lastRawSnapshotLine_.isEmpty()) {
        setStatus("No raw snapshot to copy yet");
        return;
    }
    QGuiApplication::clipboard()->setText(lastRawSnapshotLine_);
    setStatus("Copied raw snapshot to clipboard");
}

void FsrViewerWindow::onReadyRead() {
    if (binaryActive_) {
        binaryBuffer_.append(serial_.readAll());
        processBinaryBuffer();
    } else {
        asciiBuffer_.append(serial_.readAll());
        processAsciiBuffer();
    }
}

void FsrViewerWindow::onSerialError(QSerialPort::SerialPortError error) {
    if (error == QSerialPort::NoError || error == QSerialPort::TimeoutError) {
        return;
    }
    if (serial_.isOpen()) {
        setStatus("USB error: " + serial_.errorString());
    }
}

void FsrViewerWindow::processAsciiBuffer() {
    while (true) {
        const int lineEnd = asciiBuffer_.indexOf('\n');
        if (lineEnd < 0) {
            return;
        }

        const QByteArray line = asciiBuffer_.left(lineEnd).trimmed();
        asciiBuffer_.remove(0, lineEnd + 1);
        if (line == "BINARY_CAPTURE_START") {
            binaryActive_ = true;
            binaryBuffer_.append(asciiBuffer_);
            asciiBuffer_.clear();
            processBinaryBuffer();
            return;
        }

        if (line.startsWith("PONG") || line.startsWith("BOOT") || line.startsWith("WARN") || line.startsWith("OK,")) {
            setStatus(QString::fromLatin1(line));
            continue;
        }

        if (line.startsWith("RAW,")) {
            lastRawSnapshotLine_ = QString::fromLatin1(line);
            const QList<QByteArray> parts = line.split(',');
            if (parts.size() >= 7) {
                const QString text = QString("Raw: %1  %2  scaled:%3")
                                         .arg(QString::fromLatin1(parts[2]))
                                         .arg(QString::fromLatin1(parts[3]))
                                         .arg(QString::fromLatin1(parts[4]));
                rawSnapshotLabel_->setText(text);
                rawSnapshotLabel_->setToolTip(lastRawSnapshotLine_);
                setStatus(QString("ADS raw snapshot at %1 ms · ADCON %2")
                              .arg(QString::fromLatin1(parts[1]), QString::fromLatin1(parts[6])));
                copyRawButton_->setEnabled(true);
            } else {
                rawSnapshotLabel_->setText("Raw: parse error");
                setStatus(QString::fromLatin1(line));
                copyRawButton_->setEnabled(true);
            }
            pendingRawSnapshot_ = false;
            rawSnapshotButton_->setEnabled(serial_.isOpen());
            if (resumeAfterRawSnapshot_) {
                resumeAfterRawSnapshot_ = false;
                beginCapture();
            }
            continue;
        }

        if (line.startsWith("S,")) {
            const QList<QByteArray> parts = line.split(',');
            if (parts.size() == 4) {
                appendPair(static_cast<quint16>(parts[2].toUShort()), static_cast<quint16>(parts[3].toUShort()));
            }
        }
    }
}

void FsrViewerWindow::processBinaryBuffer() {
    static const QByteArray stopMarker = "BINARY_CAPTURE_STOP\n";
    const int stopIndex = binaryBuffer_.indexOf(stopMarker);
    const int availableBytes = stopIndex >= 0 ? stopIndex : binaryBuffer_.size();
    int processBytes = availableBytes - (availableBytes % constants::BytesPerCapturePair);

    for (int offset = 0; offset < processBytes; offset += constants::BytesPerCapturePair) {
        const auto* raw = reinterpret_cast<const uchar*>(binaryBuffer_.constData() + offset);
        appendPair(qFromLittleEndian<quint16>(raw), qFromLittleEndian<quint16>(raw + 2));
    }
    binaryBuffer_.remove(0, processBytes);

    if (stopIndex >= 0) {
        binaryBuffer_.remove(0, stopMarker.size());
        binaryActive_ = false;
        setStatus("Stream stopped");
        if (!binaryBuffer_.isEmpty()) {
            asciiBuffer_.append(binaryBuffer_);
            binaryBuffer_.clear();
            processAsciiBuffer();
        }
        if (pendingRawSnapshot_) {
            sendCommand("RAW");
            setStatus("Requesting raw ADS snapshot…");
        }
    }
}

void FsrViewerWindow::setStatus(const QString& text) {
    statusLabel_->setText(text);
}

void FsrViewerWindow::sendCommand(const QString& command) {
    if (!serial_.isOpen()) {
        return;
    }
    serial_.write((command + "\n").toUtf8());
    serial_.flush();
}

void FsrViewerWindow::appendPair(quint16 fsr1, quint16 fsr2) {
    const double timeSeconds = static_cast<double>(sampleIndex_) * constants::SampleIntervalMs / 1000.0;
    ++sampleIndex_;
    fsr1Label_->setText(QString("FSR1: %1").arg(fsr1));
    fsr2Label_->setText(QString("FSR2: %1").arg(fsr2));
    plot_->addSample(timeSeconds, fsr1, fsr2);
}

}  // namespace sensor::viewer
