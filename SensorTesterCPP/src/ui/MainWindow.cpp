#include "MainWindow.h"

#include <QtCore/QDateTime>
#include <QtCore/QStringList>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "../core/AppConstants.h"
#include "../core/DataExporter.h"
#include "../core/DelayAnalyzer.h"
#include "../transport/SerialPortEnumerator.h"
#include "../transport/TransportFactory.h"
#include "../transport/WifiTransport.h"
#include "MotorControlDialog.h"
#include "RecordingSetupDialog.h"

namespace sensor {

namespace {
constexpr int RetractAfterStopSteps = 200;

const QStringList Fsr2ResistanceLabels = {
    "C0 · 330 Ω",
    "C1 · 1 kΩ",
    "C2 · 2.2 kΩ",
    "C3 · 4.7 kΩ",
    "C4 · 10 kΩ",
    "C5 · 20 kΩ",
    "C6 · 47 kΩ",
    "C7 · 68 kΩ",
    "C8 · 100 kΩ",
    "C9 · 220 kΩ",
    "C10 · 300 kΩ",
    "C11 · 470 kΩ",
    "C12 · 680 kΩ",
    "C13 · 1 MΩ",
    "C14 · 4.7 MΩ",
    "C15 · 5.6 MΩ",
};
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("ESP32 Sensor Tester CPP");
    resize(1180, 740);
    recordingLimitTimer_.setSingleShot(true);
    connect(&recordingLimitTimer_, &QTimer::timeout, this, [this] {
        stopRecordingSession("Recording stopped — time limit reached");
    });
    connect(&closedLoopTimer_, &QTimer::timeout, this, &MainWindow::runClosedLoopStep);
    buildUi();
    applyRecordingSettings();
    updateMotorSummary();
    updateRecordingSummary();
    refreshPorts();
}

MainWindow::~MainWindow() {
    disconnectTransport();
}

void MainWindow::buildUi() {
    auto* root = new QWidget(this);
    setCentralWidget(root);

    auto* outer = new QVBoxLayout(root);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("ESP32 Sensor Tester");
    QFont titleFont = title->font();
    titleFont.setPointSize(22);
    titleFont.setBold(true);
    title->setFont(titleFont);
    auto* subtitle = new QLabel("ADS1256 USB capture · motor control · export");
    subtitle->setObjectName("subtitle");
    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(subtitle);
    outer->addLayout(titleRow);

    auto* body = new QHBoxLayout();
    body->setSpacing(14);
    outer->addLayout(body, 1);

    auto* controls = new QWidget();
    controls->setFixedWidth(340);
    auto* controlsLayout = new QVBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(12);
    body->addWidget(controls);

    auto* connectionBox = new QGroupBox("Connection");
    auto* connectionLayout = new QVBoxLayout(connectionBox);
    auto* modeRow = new QHBoxLayout();
    usbRadio_ = new QRadioButton("USB");
    wifiRadio_ = new QRadioButton("Wi‑Fi");
    usbRadio_->setChecked(true);
    modeRow->addWidget(usbRadio_);
    modeRow->addWidget(wifiRadio_);
    modeRow->addStretch();
    connectionLayout->addLayout(modeRow);

    portCombo_ = new QComboBox();
    refreshPortsButton_ = new QPushButton("Refresh ports");
    wifiEdit_ = new QLineEdit("http://192.168.4.1");
    wifiEdit_->hide();
    connectionLayout->addWidget(portCombo_);
    connectionLayout->addWidget(refreshPortsButton_);
    connectionLayout->addWidget(wifiEdit_);

    auto* connectRow = new QHBoxLayout();
    connectButton_ = new QPushButton("Connect");
    statusLabel_ = new QLabel("Disconnected");
    statusLabel_->setObjectName("statusPill");
    connectRow->addWidget(connectButton_, 1);
    connectRow->addWidget(statusLabel_);
    connectionLayout->addLayout(connectRow);
    controlsLayout->addWidget(connectionBox);

    auto* testBox = new QGroupBox("Motor and test setup");
    auto* testLayout = new QVBoxLayout(testBox);
    auto* motorHeader = new QHBoxLayout();
    auto* motorHint = new QLabel("Motor settings");
    motorSettingsButton_ = new QPushButton("Motor…");
    motorSettingsButton_->setToolTip("Open motor steps, pulse delay, move up/down, and stop controls");
    motorHeader->addWidget(motorHint);
    motorHeader->addStretch();
    motorHeader->addWidget(motorSettingsButton_);
    testLayout->addLayout(motorHeader);

    motorSummaryLabel_ = new QLabel();
    motorSummaryLabel_->setWordWrap(true);
    motorSummaryLabel_->setObjectName("subtitle");
    motionLabel_ = new QLabel("Idle");
    motionLabel_->setAlignment(Qt::AlignCenter);
    testLayout->addWidget(motorSummaryLabel_);

    auto* quickMoveRow = new QHBoxLayout();
    quickMoveUpButton_ = new QPushButton("▲ Move up");
    quickMoveDownButton_ = new QPushButton("▼ Move down");
    quickMoveRow->addWidget(quickMoveUpButton_);
    quickMoveRow->addWidget(quickMoveDownButton_);
    testLayout->addLayout(quickMoveRow);

    emergencyStopButton_ = new QPushButton("EMERGENCY STOP");
    emergencyStopButton_->setObjectName("dangerButton");
    testLayout->addWidget(emergencyStopButton_);

    fsr2ResistanceCombo_ = new QComboBox();
    for (int channel = 0; channel < Fsr2ResistanceLabels.size(); ++channel) {
        fsr2ResistanceCombo_->addItem(Fsr2ResistanceLabels[channel], channel);
    }
    fsr2ResistanceCombo_->setCurrentIndex(fsr2ResistanceChannel_);
    fsr2ResistanceCombo_->setToolTip("Selects the analog mux channel that chooses FSR2's resistor");
    testLayout->addWidget(new QLabel("FSR2 resistor"));
    testLayout->addWidget(fsr2ResistanceCombo_);

    testLayout->addWidget(motionLabel_);
    controlsLayout->addWidget(testBox);

    auto* recordingBox = new QGroupBox("Recording");
    auto* recordingLayout = new QVBoxLayout(recordingBox);
    auto* recordRow = new QHBoxLayout();
    recordButton_ = new QPushButton("Start recording");
    recordingSetupButton_ = new QPushButton("Setup…");
    clearButton_ = new QPushButton("Clear");
    recordRow->addWidget(recordButton_, 1);
    recordRow->addWidget(recordingSetupButton_);
    recordRow->addWidget(clearButton_);
    recordingLayout->addLayout(recordRow);
    autoTestButton_ = new QPushButton("Automated Test");
    recordingLayout->addWidget(autoTestButton_);
    recordLabel_ = new QLabel("Not recording");
    recordingSummaryLabel_ = new QLabel();
    recordingSummaryLabel_->setWordWrap(true);
    recordingSummaryLabel_->setObjectName("subtitle");
    delayLabel_ = new QLabel("Response delay: not calculated");
    delayLabel_->setWordWrap(true);
    recordingLayout->addWidget(recordLabel_);
    recordingLayout->addWidget(recordingSummaryLabel_);
    recordingLayout->addWidget(delayLabel_);
    auto* exportRow = new QHBoxLayout();
    exportCsvButton_ = new QPushButton("Export CSV");
    exportExcelButton_ = new QPushButton("Export Excel");
    exportRow->addWidget(exportCsvButton_);
    exportRow->addWidget(exportExcelButton_);
    recordingLayout->addLayout(exportRow);
    controlsLayout->addWidget(recordingBox);
    controlsLayout->addStretch();

    auto* right = new QWidget();
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);
    body->addWidget(right, 1);

    auto* readingsRow = new QHBoxLayout();
    readingsRow->setSpacing(12);
    fsr1Value_ = createReadingCard("FSR 1 · ADS1256 AIN1");
    fsr2Value_ = createReadingCard("FSR 2 · ADS1256 AIN2");
    readingsRow->addWidget(fsr1Value_->parentWidget());
    readingsRow->addWidget(fsr2Value_->parentWidget());
    rightLayout->addLayout(readingsRow);

    plot_ = new LivePlotWidget();
    rightLayout->addWidget(plot_, 1);

    connect(usbRadio_, &QRadioButton::toggled, this, [this](bool checked) {
        portCombo_->setVisible(checked);
        refreshPortsButton_->setVisible(checked);
        wifiEdit_->setVisible(!checked);
    });
    connect(refreshPortsButton_, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(connectButton_, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    connect(motorSettingsButton_, &QPushButton::clicked, this, &MainWindow::openMotorControls);
    connect(quickMoveUpButton_, &QPushButton::clicked, this, &MainWindow::moveUp);
    connect(quickMoveDownButton_, &QPushButton::clicked, this, &MainWindow::moveDown);
    connect(emergencyStopButton_, &QPushButton::clicked, this, &MainWindow::stopMotor);
    connect(fsr2ResistanceCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::setFsr2Resistance);
    connect(autoTestButton_, &QPushButton::clicked, this, &MainWindow::startAutoTest);
    connect(recordButton_, &QPushButton::clicked, this, &MainWindow::toggleRecording);
    connect(recordingSetupButton_, &QPushButton::clicked, this, &MainWindow::openRecordingSetup);
    connect(clearButton_, &QPushButton::clicked, this, &MainWindow::clearData);
    connect(exportCsvButton_, &QPushButton::clicked, this, &MainWindow::exportCsv);
    connect(exportExcelButton_, &QPushButton::clicked, this, &MainWindow::exportExcel);
}

QLabel* MainWindow::createReadingCard(const QString& title) {
    auto* box = new QGroupBox(title);
    auto* layout = new QVBoxLayout(box);
    auto* value = new QLabel("—");
    QFont font = value->font();
    font.setPointSize(26);
    font.setBold(true);
    value->setFont(font);
    value->setAlignment(Qt::AlignCenter);
    auto* unit = new QLabel("raw ADC counts");
    unit->setAlignment(Qt::AlignCenter);
    unit->setObjectName("subtitle");
    layout->addWidget(value);
    layout->addWidget(unit);
    return value;
}

void MainWindow::updateMotorSummary() {
    if (!motorSummaryLabel_) {
        return;
    }
    motorSummaryLabel_->setText(QString("Steps: %1 · Pulse delay: %2 µs").arg(motorSteps_).arg(motorDelayUs_));
}

void MainWindow::updateRecordingSummary() {
    if (!recordingSummaryLabel_) {
        return;
    }
    recordingSummaryLabel_->setText(
        QString("%1 profile · peak %2 ADC · %3 · save every %4 ms · graph every %5 ms · display max %6 ADC%7%8")
            .arg(profileTypeName(recordingSettings_.testProfile.type))
            .arg(QString::number(recordingSettings_.testProfile.peakForce, 'f', 0))
            .arg(graphModeName(recordingSettings_.graphMode))
            .arg(QString::number(recordingSettings_.acquisitionIntervalMs, 'f', 1))
            .arg(recordingSettings_.displayIntervalMs)
            .arg(recordingSettings_.maxPressure)
            .arg(recordingSettings_.loopEnabled ? " · loop" : "")
            .arg(recordingSettings_.timeLimitSeconds > 0
                     ? QString(" · %1 s limit").arg(recordingSettings_.timeLimitSeconds)
                     : ""));
}

void MainWindow::applyRecordingSettings() {
    if (plot_) {
        plot_->setMaxValue(recordingSettings_.maxPressure);
        plot_->setGraphMode(recordingSettings_.graphMode);
    }
    if (auto* wifi = dynamic_cast<WifiTransport*>(transport_.get())) {
        wifi->setPollIntervalMs(recordingSettings_.displayIntervalMs);
    }
}

void MainWindow::refreshPorts() {
    portCombo_->clear();
    for (const SerialPortDescriptor& port : SerialPortEnumerator::availablePorts()) {
        portCombo_->addItem(port.label, port.systemLocation);
    }
}

void MainWindow::toggleConnection() {
    if (transport_ && transport_->isConnected()) {
        disconnectTransport();
        return;
    }
    createTransport();
    if (!transport_) {
        return;
    }
    attachTransportSignals();
    statusLabel_->setText("Connecting…");
    transport_->connectToDevice();
}

void MainWindow::createTransport() {
    if (usbRadio_->isChecked()) {
        if (portCombo_->currentIndex() < 0) {
            QMessageBox::warning(this, "No serial port", "Connect the ESP32, then click Refresh ports.");
            return;
        }
        transport_ = TransportFactory::create({
            Transport::Kind::Usb,
            portCombo_->currentData().toString(),
            {},
            recordingSettings_.displayIntervalMs,
        });
    } else {
        const QString url = wifiEdit_->text().trimmed();
        if (url.isEmpty()) {
            QMessageBox::warning(this, "No Wi‑Fi address", "Enter the ESP32 address, normally http://192.168.4.1");
            return;
        }
        transport_ = TransportFactory::create({
            Transport::Kind::Wifi,
            {},
            url,
            recordingSettings_.displayIntervalMs,
        });
    }
}

void MainWindow::attachTransportSignals() {
    connect(transport_.get(), &Transport::connected, this, [this](const QString& label) {
        setConnectedUi(true, "Connected (" + label + ")");
        setFsr2Resistance();
    });
    connect(transport_.get(), &Transport::disconnected, this, [this] {
        setConnectedUi(false);
    });
    connect(transport_.get(), &Transport::statusMessage, this, [this](const QString& message) {
        motionLabel_->setText(message);
    });
    connect(transport_.get(), &Transport::errorOccurred, this, [this](const QString& message) {
        statusLabel_->setText("Error");
        QMessageBox::warning(this, "ESP32 communication", message);
    });
    connect(transport_.get(), &Transport::sampleReceived, this, &MainWindow::addDisplayedSample);
    connect(transport_.get(), &Transport::captureStarted, this, &MainWindow::beginCapture);
    connect(transport_.get(), &Transport::captureFinished, this, &MainWindow::finishCapture);
}

void MainWindow::disconnectTransport() {
    if (transport_) {
        transport_->disconnectFromDevice();
        transport_.reset();
    }
    recordingLimitTimer_.stop();
    closedLoopTimer_.stop();
    recording_ = false;
    waitingForCapture_ = false;
    stoppingRecording_ = false;
    autoTestArmed_ = false;
    setConnectedUi(false);
}

void MainWindow::setConnectedUi(bool connected, const QString& label) {
    statusLabel_->setText(label);
    connectButton_->setText(connected ? "Disconnect" : "Connect");
    recordButton_->setEnabled(true);
    updateRunButtonLabels();
}

void MainWindow::moveUp() {
    sendMove("up");
}

void MainWindow::moveDown() {
    sendMove("down");
}

void MainWindow::openMotorControls() {
    auto* dialog = new MotorControlDialog(motorSteps_, motorDelayUs_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &MotorControlDialog::settingsChanged, this, [this](int steps, int delayUs) {
        motorSteps_ = steps;
        motorDelayUs_ = delayUs;
        updateMotorSummary();
    });
    connect(dialog, &MotorControlDialog::moveRequested, this, [this](const QString& direction, int steps, int delayUs) {
        motorSteps_ = steps;
        motorDelayUs_ = delayUs;
        updateMotorSummary();
        sendMove(direction, steps, delayUs);
    });
    connect(dialog, &MotorControlDialog::stopRequested, this, &MainWindow::stopMotor);
    dialog->show();
}

void MainWindow::setFsr2Resistance() {
    if (!fsr2ResistanceCombo_) {
        return;
    }
    fsr2ResistanceChannel_ = fsr2ResistanceCombo_->currentData().toInt();
    if (transport_ && transport_->isConnected()) {
        transport_->setFsr2ResistanceChannel(fsr2ResistanceChannel_);
    }
}

void MainWindow::openRecordingSetup() {
    RecordingSetupDialog dialog(recordingSettings_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    recordingSettings_ = dialog.settings();
    applyRecordingSettings();
    updateRecordingSummary();
}

void MainWindow::sendMove(const QString& direction) {
    sendMove(direction, motorSteps_, motorDelayUs_);
}

void MainWindow::sendMove(const QString& direction, int steps, int delayUs) {
    if (!transport_ || !transport_->isConnected()) {
        QMessageBox::warning(this, "Not connected", "Connect to the ESP32 first.");
        return;
    }
    currentDirection_ = direction;
    transport_->move(direction, steps, delayUs);
}

void MainWindow::stopMotor() {
    closedLoopTimer_.stop();
    recordingLimitTimer_.stop();
    autoTestArmed_ = false;
    stoppingRecording_ = false;
    waitingForCapture_ = false;
    recording_ = false;
    recordButton_->setEnabled(true);
    recordLabel_->setText("Emergency stop — motor stopped and automated test cancelled");
    if (transport_ && transport_->isConnected()) {
        if (transport_->kind() == Transport::Kind::Usb) {
            transport_->stopCapture();
        }
        transport_->stop();
    }
    updateRunButtonLabels();
    motionLabel_->setText("EMERGENCY STOP");
}

void MainWindow::startAutoTest() {
    if (autoTestArmed_) {
        stopRecordingSession("Automated test stopped");
        stopMotionAndRetract("Automated test stopped — retracting");
        return;
    }
    if (!transport_ || !transport_->isConnected()) {
        QMessageBox::warning(this, "Not connected", "Connect to the ESP32 first.");
        return;
    }
    if (!recording_) {
        startRecordingSession("Automated test armed — waiting for sensor data…");
    }
    autoTestArmed_ = true;
    updateRunButtonLabels();

    closedLoopController_.setProfile(recordingSettings_.testProfile);
    profileElapsed_.restart();
    hasLatestActualPressure_ = false;
    closedLoopTimer_.setInterval(std::max(1, static_cast<int>(std::lround(recordingSettings_.testProfile.controlIntervalMs))));
    closedLoopTimer_.start();
    motionLabel_->setText(QString("Automated curve active: %1").arg(profileTypeName(recordingSettings_.testProfile.type)));

    if (transport_->kind() == Transport::Kind::Usb) {
        transport_->startCapture();
    }
}

void MainWindow::toggleRecording() {
    if (!recording_) {
        if (!transport_ || !transport_->isConnected()) {
            QMessageBox::warning(this, "Not connected", "Connect to the ESP32 before recording.");
            return;
        }
        startRecordingSession(
            transport_->kind() == Transport::Kind::Usb
                ? "Recording armed — waiting for ADS1256 USB stream…"
                : "Recording low-rate Wi‑Fi samples…");
        if (transport_->kind() == Transport::Kind::Usb) {
            transport_->startCapture();
        }
        return;
    }

    stopRecordingSession("Recording stopped");
    stopMotionAndRetract("Recording stopped — retracting");
}

void MainWindow::startRecordingSession(const QString& label) {
    recording_ = true;
    waitingForCapture_ = false;
    stoppingRecording_ = false;
    currentRecordingStartIndex_ = measurements_.size();
    recordingElapsed_.restart();
    recordingStartSampleSeconds_ = latestGraphTimeSeconds_;
    lastSavedWifiSampleSeconds_ = -1.0;
    updateRunButtonLabels();
    recordButton_->setEnabled(true);
    recordLabel_->setText(label);
    delayLabel_->setText("Response delay: capturing…");

    if (recordingSettings_.timeLimitSeconds > 0) {
        recordingLimitTimer_.start(recordingSettings_.timeLimitSeconds * 1000);
    } else {
        recordingLimitTimer_.stop();
    }
}

void MainWindow::stopRecordingSession(const QString& label) {
    closedLoopTimer_.stop();
    if (transport_ && transport_->isConnected() && transport_->kind() == Transport::Kind::Usb) {
        transport_->stopCapture();
        if (recording_) {
            stoppingRecording_ = true;
            autoTestArmed_ = false;
            recordingLimitTimer_.stop();
            recordButton_->setEnabled(false);
            recordLabel_->setText(label + " — finalizing USB capture…");
            updateRunButtonLabels();
            return;
        }
    }
    recordingLimitTimer_.stop();
    recording_ = false;
    waitingForCapture_ = false;
    stoppingRecording_ = false;
    autoTestArmed_ = false;
    recordButton_->setEnabled(true);
    recordLabel_->setText(label);
    updateRunButtonLabels();

    const QVector<Measurement> latest(measurements_.begin() + currentRecordingStartIndex_, measurements_.end());
    if (!latest.isEmpty()) {
        const auto delay = DelayAnalyzer::calculate(latest);
        delayLabel_->setText(delay ? delay->summary : "Response delay: no clear force change detected");
    }
}

void MainWindow::stopMotionAndRetract(const QString& label) {
    if (!transport_ || !transport_->isConnected()) {
        return;
    }
    transport_->stop();
    transport_->move("up", RetractAfterStopSteps, motorDelayUs_);
    currentDirection_ = "up";
    motionLabel_->setText(QString("%1 · moving up %2 steps").arg(label).arg(RetractAfterStopSteps));
}

void MainWindow::updateRunButtonLabels() {
    if (recordButton_) {
        recordButton_->setText(recording_ && !autoTestArmed_ ? "Stop recording" : "Start recording");
    }
    if (autoTestButton_) {
        autoTestButton_->setText(autoTestArmed_ ? "Stop automated test" : "Automated Test");
    }
}

void MainWindow::beginCapture() {
    if (!recording_) {
        recordLabel_->setText("Capture ignored — press Start recording or Automated Test first");
        return;
    }
    waitingForCapture_ = true;
    recordingStartSampleSeconds_ = latestGraphTimeSeconds_;
    lastDisplayedSampleSeconds_ = recordingStartSampleSeconds_ - (recordingSettings_.displayIntervalMs / 1000.0);
    updateRunButtonLabels();
    recordLabel_->setText("Receiving ADS1256 USB stream…");
    delayLabel_->setText("Response delay: capturing…");
}

void MainWindow::finishCapture(const QVector<Measurement>& samples) {
    if (!recording_) {
        waitingForCapture_ = false;
        return;
    }

    const double graphBase = latestGraphTimeSeconds_;
    const int acquisitionStride = recordingSettings_.acquisitionStrideForUsbCapture();
    const bool replayGraphFromFinishedCapture = !(transport_ && transport_->kind() == Transport::Kind::Usb);
    for (int index = 0; index < samples.size(); ++index) {
        if (index % acquisitionStride == 0 || index == samples.size() - 1) {
            measurements_.push_back(samples[index]);
        }
        if (replayGraphFromFinishedCapture && (index % recordingSettings_.displayStrideForUsbCapture() == 0 || index == samples.size() - 1)) {
            addDisplayedMeasurement(graphBase, samples[index]);
        }
    }

    latestGraphTimeSeconds_ = std::max(
        latestGraphTimeSeconds_,
        graphBase + (samples.isEmpty() ? 0.0 : samples.back().timeMs / 1000.0));

    waitingForCapture_ = false;

    const int recorded = measurements_.size() - currentRecordingStartIndex_;
    recordLabel_->setText(QString("Capture complete — %1 saved samples").arg(recorded));
    const QVector<Measurement> latest(measurements_.begin() + currentRecordingStartIndex_, measurements_.end());
    const auto delay = DelayAnalyzer::calculate(latest);
    delayLabel_->setText(delay ? delay->summary : "Response delay: no clear force change detected");

    if (!stoppingRecording_ && recordingSettings_.loopEnabled && recording_ && transport_ && transport_->isConnected() && transport_->kind() == Transport::Kind::Usb) {
        recordLabel_->setText(QString("Loop capture complete — %1 saved samples; starting next capture…").arg(recorded));
        transport_->startCapture();
        updateRunButtonLabels();
        return;
    }

    recordingLimitTimer_.stop();
    recording_ = false;
    stoppingRecording_ = false;
    autoTestArmed_ = false;
    recordButton_->setEnabled(true);
    updateRunButtonLabels();
}

void MainWindow::addDisplayedSample(const SensorSample& sample) {
    SensorSample displaySample = sample;
    if (transport_ && transport_->kind() == Transport::Kind::Usb && waitingForCapture_) {
        displaySample.timeSeconds = recordingStartSampleSeconds_ + sample.timeSeconds;
    }

    latestGraphTimeSeconds_ = std::max(latestGraphTimeSeconds_, displaySample.timeSeconds);
    latestActualPressure_ = currentActualPressure(displaySample);
    hasLatestActualPressure_ = true;

    if (recording_ && transport_ && transport_->kind() == Transport::Kind::Wifi) {
        const bool firstSavedSample = lastSavedWifiSampleSeconds_ < 0.0;
        const bool saveDue = firstSavedSample ||
                             displaySample.timeSeconds - lastSavedWifiSampleSeconds_ >= recordingSettings_.acquisitionIntervalSeconds();
        if (saveDue) {
            measurements_.push_back({
                std::max(0.0, (displaySample.timeSeconds - recordingStartSampleSeconds_) * 1000.0),
                displaySample.fsr1,
                displaySample.fsr2,
            });
            lastSavedWifiSampleSeconds_ = displaySample.timeSeconds;
            const int recorded = measurements_.size() - currentRecordingStartIndex_;
            recordLabel_->setText(QString("Recording low-rate Wi‑Fi samples… %1 saved").arg(recorded));
        }
    }

    const double displayIntervalSeconds = recordingSettings_.displayIntervalMs / 1000.0;
    const bool displayDue = lastDisplayedSampleSeconds_ < 0.0 ||
                            displaySample.timeSeconds - lastDisplayedSampleSeconds_ >= displayIntervalSeconds;
    if (!displayDue) {
        return;
    }

    lastDisplayedSampleSeconds_ = displaySample.timeSeconds;
    fsr1Value_->setText(QString::number(displaySample.fsr1));
    fsr2Value_->setText(QString::number(displaySample.fsr2));
    if (displaySample.motionStatusKnown) {
        motionLabel_->setText(displaySample.moving
                                  ? QString("Moving %1 — %2 steps left").arg(currentDirection_).arg(displaySample.stepsRemaining)
                                  : "Idle");
    }
    plot_->addPoint(displaySample.timeSeconds, displaySample.fsr1, displaySample.fsr2);
}

double MainWindow::currentActualPressure(const SensorSample& sample) const {
    return std::max<double>(sample.fsr1, sample.fsr2);
}

void MainWindow::runClosedLoopStep() {
    if (!autoTestArmed_ || !recording_ || !transport_ || !transport_->isConnected() || !hasLatestActualPressure_) {
        return;
    }

    const double elapsedSeconds = profileElapsed_.elapsed() / 1000.0;
    const PressureControlDecision decision = closedLoopController_.decide(elapsedSeconds, latestActualPressure_);
    if (!decision.shouldMove()) {
        motionLabel_->setText(QString("Curve target %1 ADC · actual %2 ADC · hold")
                                  .arg(QString::number(decision.targetPressure, 'f', 0))
                                  .arg(QString::number(decision.actualPressure, 'f', 0)));
        return;
    }

    transport_->pressureNudge(
        decision.increasesPressure(),
        decision.commandSteps,
        decision.commandDelayUs);
    motionLabel_->setText(QString("Curve target %1 ADC · actual %2 ADC · %3")
                              .arg(QString::number(decision.targetPressure, 'f', 0))
                              .arg(QString::number(decision.actualPressure, 'f', 0))
                              .arg(decision.increasesPressure() ? "press" : "retract"));
}

void MainWindow::addDisplayedMeasurement(double graphBaseSeconds, const Measurement& row) {
    addDisplayedSample({
        graphBaseSeconds + row.timeMs / 1000.0,
        row.fsr1,
        row.fsr2,
        false,
        false,
        0,
    });
}

void MainWindow::clearData() {
    if (recording_) {
        QMessageBox::warning(this, "Recording active", "Stop recording before clearing data.");
        return;
    }
    measurements_.clear();
    plot_->clear();
    latestGraphTimeSeconds_ = 0.0;
    recordingStartSampleSeconds_ = 0.0;
    lastSavedWifiSampleSeconds_ = -1.0;
    lastDisplayedSampleSeconds_ = -1.0;
    recordLabel_->setText("Not recording — 0 samples");
    delayLabel_->setText("Response delay: not calculated");
}

QString MainWindow::defaultExportBaseName() const {
    QString name = profileTypeName(recordingSettings_.testProfile.type);
    for (QChar& ch : name) {
        if (!ch.isLetterOrNumber() && ch != '-' && ch != '_') {
            ch = '_';
        }
    }
    return QString("%1_%2").arg(name, QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
}

void MainWindow::exportCsv() {
    if (measurements_.isEmpty()) {
        QMessageBox::information(this, "No data", "Record some measurements before exporting.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Export CSV", defaultExportBaseName() + ".csv", "CSV files (*.csv)");
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!DataExporter::exportCsv(path, measurements_, &error)) {
        QMessageBox::critical(this, "Export failed", error);
        return;
    }
    QMessageBox::information(this, "Export complete", QString("Saved %1 measurements.").arg(measurements_.size()));
}

void MainWindow::exportExcel() {
    if (measurements_.isEmpty()) {
        QMessageBox::information(this, "No data", "Record some measurements before exporting.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Export Excel", defaultExportBaseName() + ".xls", "Excel XML files (*.xls)");
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!DataExporter::exportExcelXml(path, measurements_, profileTypeName(recordingSettings_.testProfile.type), delayLabel_->text(), &error)) {
        QMessageBox::critical(this, "Export failed", error);
        return;
    }
    QMessageBox::information(this, "Export complete", QString("Saved %1 measurements.").arg(measurements_.size()));
}

}  // namespace sensor
