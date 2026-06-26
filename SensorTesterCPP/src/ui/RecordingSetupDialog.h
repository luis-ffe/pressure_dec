#pragma once

#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpinBox>

#include "../model/RecordingSettings.h"
#include "ProfileCurveWidget.h"

namespace sensor {

class RecordingSetupDialog final : public QDialog {
    Q_OBJECT

public:
    explicit RecordingSetupDialog(RecordingSettings settings, QWidget* parent = nullptr);

    [[nodiscard]] RecordingSettings settings() const;

private:
    QButtonGroup* graphButtons_ = nullptr;
    QSpinBox* maxPressureSpin_ = nullptr;
    QCheckBox* loopCheck_ = nullptr;
    QSpinBox* timeLimitSpin_ = nullptr;
    QDoubleSpinBox* acquisitionIntervalSpin_ = nullptr;
    QSpinBox* displayIntervalSpin_ = nullptr;
    QComboBox* profileTypeCombo_ = nullptr;
    QDoubleSpinBox* peakForceSpin_ = nullptr;
    QDoubleSpinBox* durationSpin_ = nullptr;
    QDoubleSpinBox* frequencySpin_ = nullptr;
    QDoubleSpinBox* controlIntervalSpin_ = nullptr;
    QDoubleSpinBox* controlDeadbandSpin_ = nullptr;
    QSpinBox* commandStepsSpin_ = nullptr;
    QSpinBox* commandDelaySpin_ = nullptr;
    QLabel* sineDerivedLabel_ = nullptr;
    ProfileCurveWidget* profilePreview_ = nullptr;

    void updateProfilePreview();
    [[nodiscard]] TestProfileSettings profileSettings() const;
};

}  // namespace sensor
