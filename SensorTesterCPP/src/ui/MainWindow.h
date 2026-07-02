#pragma once

#include <memory>

#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSpinBox>

#include "../core/ClosedLoopController.h"
#include "../model/Measurement.h"
#include "../model/RecordingSettings.h"
#include "../transport/Transport.h"
#include "LivePlotWidget.h"

namespace sensor {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void refreshPorts();
    void toggleConnection();
    void moveUp();
    void moveDown();
    void stopMotor();
    void openMotorControls();
    void setFsr2Resistance();
    void openRecordingSetup();
    void startAutoTest();
    void toggleRecording();
    void clearData();
    void exportCsv();
    void exportExcel();

private:
    void buildUi();
    void setConnectedUi(bool connected, const QString& label = "Disconnected");
    void createTransport();
    void attachTransportSignals();
    void disconnectTransport();
    void sendMove(const QString& direction);
    void sendMove(const QString& direction, int steps, int delayUs);
    void startRecordingSession(const QString& label);
    void stopRecordingSession(const QString& label);
    void stopMotionAndRetract(const QString& label);
    void updateRunButtonLabels();
    void beginCapture();
    void finishCapture(const QVector<Measurement>& samples);
    void addDisplayedSample(const SensorSample& sample);
    void addDisplayedMeasurement(double graphBaseSeconds, const Measurement& row);
    QString defaultExportBaseName() const;
    QLabel* createReadingCard(const QString& title);
    void updateMotorSummary();
    void updateRecordingSummary();
    void applyRecordingSettings();
    void runClosedLoopStep();
    [[nodiscard]] double currentActualPressure(const SensorSample& sample) const;

    QRadioButton* usbRadio_ = nullptr;
    QRadioButton* wifiRadio_ = nullptr;
    QComboBox* portCombo_ = nullptr;
    QPushButton* refreshPortsButton_ = nullptr;
    QLineEdit* wifiEdit_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* motorSummaryLabel_ = nullptr;
    QPushButton* motorSettingsButton_ = nullptr;
    QPushButton* quickMoveUpButton_ = nullptr;
    QPushButton* quickMoveDownButton_ = nullptr;
    QPushButton* emergencyStopButton_ = nullptr;
    QComboBox* fsr2ResistanceCombo_ = nullptr;
    QPushButton* autoTestButton_ = nullptr;
    QLabel* motionLabel_ = nullptr;
    QPushButton* recordButton_ = nullptr;
    QPushButton* recordingSetupButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QLabel* recordLabel_ = nullptr;
    QLabel* recordingSummaryLabel_ = nullptr;
    QLabel* delayLabel_ = nullptr;
    QPushButton* exportCsvButton_ = nullptr;
    QPushButton* exportExcelButton_ = nullptr;
    QLabel* fsr1Value_ = nullptr;
    QLabel* fsr2Value_ = nullptr;
    LivePlotWidget* plot_ = nullptr;

    std::unique_ptr<Transport> transport_;
    QVector<Measurement> measurements_;
    RecordingSettings recordingSettings_;
    ClosedLoopController closedLoopController_;
    QTimer recordingLimitTimer_;
    QTimer closedLoopTimer_;
    QElapsedTimer recordingElapsed_;
    QElapsedTimer profileElapsed_;
    bool recording_ = false;
    bool waitingForCapture_ = false;
    bool stoppingRecording_ = false;
    bool autoTestArmed_ = false;
    int currentRecordingStartIndex_ = 0;
    int motorSteps_ = 3200;
    int motorDelayUs_ = 62;
    int fsr2ResistanceChannel_ = 4;
    double latestGraphTimeSeconds_ = 0.0;
    double recordingStartSampleSeconds_ = 0.0;
    double lastSavedWifiSampleSeconds_ = -1.0;
    double lastDisplayedSampleSeconds_ = -1.0;
    double latestActualPressure_ = 0.0;
    bool hasLatestActualPressure_ = false;
    QString currentDirection_;
};

}  // namespace sensor
