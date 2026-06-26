#include "RecordingSetupDialog.h"

#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "../core/AppConstants.h"
#include "GraphPreviewWidget.h"

namespace sensor {

namespace {

int graphModeId(GraphMode mode) {
    switch (mode) {
        case GraphMode::DualLines:
            return 0;
        case GraphMode::FilledComparison:
            return 1;
        case GraphMode::Difference:
            return 2;
    }
    return 0;
}

GraphMode graphModeFromId(int id) {
    switch (id) {
        case 1:
            return GraphMode::FilledComparison;
        case 2:
            return GraphMode::Difference;
        case 0:
        default:
            return GraphMode::DualLines;
    }
}

int profileTypeIndex(TestProfileType type) {
    switch (type) {
        case TestProfileType::StepHold:
            return 0;
        case TestProfileType::LinearRampTriangle:
            return 1;
        case TestProfileType::CyclicSinusoidal:
            return 2;
    }
    return 0;
}

TestProfileType profileTypeFromIndex(int index) {
    switch (index) {
        case 1:
            return TestProfileType::LinearRampTriangle;
        case 2:
            return TestProfileType::CyclicSinusoidal;
        case 0:
        default:
            return TestProfileType::StepHold;
    }
}

}  // namespace

RecordingSetupDialog::RecordingSetupDialog(RecordingSettings settings, QWidget* parent) : QDialog(parent) {
    setWindowTitle("Recording setup");
    resize(820, 760);

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel("Choose how recorded data is saved and how the low-rate UI graph is displayed.");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* graphBox = new QGroupBox("Graph preview");
    auto* graphLayout = new QGridLayout(graphBox);
    graphButtons_ = new QButtonGroup(this);

    const QVector<QPair<GraphMode, QString>> modes = {
        {GraphMode::DualLines, "Dual lines"},
        {GraphMode::FilledComparison, "Filled comparison"},
        {GraphMode::Difference, "Difference"},
    };
    for (int i = 0; i < modes.size(); ++i) {
        auto* optionLayout = new QVBoxLayout();
        auto* preview = new GraphPreviewWidget();
        preview->setMode(modes[i].first);
        auto* radio = new QRadioButton(modes[i].second);
        graphButtons_->addButton(radio, graphModeId(modes[i].first));
        optionLayout->addWidget(preview, 0, Qt::AlignCenter);
        optionLayout->addWidget(radio, 0, Qt::AlignCenter);
        graphLayout->addLayout(optionLayout, 0, i);
    }
    if (auto* button = graphButtons_->button(graphModeId(settings.graphMode))) {
        button->setChecked(true);
    }
    layout->addWidget(graphBox);

    auto* profileBox = new QGroupBox("Custom curve / test profile");
    auto* profileLayout = new QHBoxLayout(profileBox);
    auto* profileForm = new QFormLayout();

    profileTypeCombo_ = new QComboBox();
    profileTypeCombo_->addItems({
        profileTypeName(TestProfileType::StepHold),
        profileTypeName(TestProfileType::LinearRampTriangle),
        profileTypeName(TestProfileType::CyclicSinusoidal),
    });
    profileTypeCombo_->setCurrentIndex(profileTypeIndex(settings.testProfile.type));

    peakForceSpin_ = new QDoubleSpinBox();
    peakForceSpin_->setRange(0.0, constants::AdcMaxValue);
    peakForceSpin_->setDecimals(1);
    peakForceSpin_->setSingleStep(50.0);
    peakForceSpin_->setValue(settings.testProfile.peakForce);
    peakForceSpin_->setSuffix(" ADC");

    durationSpin_ = new QDoubleSpinBox();
    durationSpin_->setRange(0.1, 3600.0);
    durationSpin_->setDecimals(2);
    durationSpin_->setSingleStep(0.5);
    durationSpin_->setValue(settings.testProfile.durationSeconds);
    durationSpin_->setSuffix(" s");

    frequencySpin_ = new QDoubleSpinBox();
    frequencySpin_->setRange(0.01, 50.0);
    frequencySpin_->setDecimals(2);
    frequencySpin_->setSingleStep(0.1);
    frequencySpin_->setValue(settings.testProfile.frequencyHz);
    frequencySpin_->setSuffix(" Hz");

    controlIntervalSpin_ = new QDoubleSpinBox();
    controlIntervalSpin_->setRange(1.0, 1000.0);
    controlIntervalSpin_->setDecimals(1);
    controlIntervalSpin_->setSingleStep(5.0);
    controlIntervalSpin_->setValue(settings.testProfile.controlIntervalMs);
    controlIntervalSpin_->setSuffix(" ms");

    controlDeadbandSpin_ = new QDoubleSpinBox();
    controlDeadbandSpin_->setRange(0.0, 1000.0);
    controlDeadbandSpin_->setDecimals(1);
    controlDeadbandSpin_->setSingleStep(5.0);
    controlDeadbandSpin_->setValue(settings.testProfile.controlDeadband);
    controlDeadbandSpin_->setSuffix(" ADC");

    commandStepsSpin_ = new QSpinBox();
    commandStepsSpin_->setRange(1, 10000);
    commandStepsSpin_->setValue(settings.testProfile.commandSteps);

    commandDelaySpin_ = new QSpinBox();
    commandDelaySpin_->setRange(20, 5000);
    commandDelaySpin_->setValue(settings.testProfile.commandDelayUs);
    commandDelaySpin_->setSuffix(" µs");

    sineDerivedLabel_ = new QLabel();
    sineDerivedLabel_->setObjectName("subtitle");
    sineDerivedLabel_->setWordWrap(true);

    profileForm->addRow("Profile type", profileTypeCombo_);
    profileForm->addRow("Peak / max force", peakForceSpin_);
    profileForm->addRow("Duration", durationSpin_);
    profileForm->addRow("Frequency", frequencySpin_);
    profileForm->addRow("Control interval", controlIntervalSpin_);
    profileForm->addRow("Deadband", controlDeadbandSpin_);
    profileForm->addRow("Command steps", commandStepsSpin_);
    profileForm->addRow("Command delay", commandDelaySpin_);
    profileForm->addRow("Cyclic derived values", sineDerivedLabel_);

    profilePreview_ = new ProfileCurveWidget();
    profileLayout->addLayout(profileForm, 1);
    profileLayout->addWidget(profilePreview_, 1);
    layout->addWidget(profileBox);

    auto* form = new QFormLayout();
    maxPressureSpin_ = new QSpinBox();
    maxPressureSpin_->setRange(100, constants::AdcMaxValue);
    maxPressureSpin_->setValue(settings.maxPressure);
    maxPressureSpin_->setSuffix(" ADC");

    acquisitionIntervalSpin_ = new QDoubleSpinBox();
    acquisitionIntervalSpin_->setRange(0.1, 500.0);
    acquisitionIntervalSpin_->setSingleStep(0.1);
    acquisitionIntervalSpin_->setDecimals(1);
    acquisitionIntervalSpin_->setValue(settings.acquisitionIntervalMs);
    acquisitionIntervalSpin_->setSuffix(" ms");

    displayIntervalSpin_ = new QSpinBox();
    displayIntervalSpin_->setRange(100, 500);
    displayIntervalSpin_->setSingleStep(50);
    displayIntervalSpin_->setValue(settings.displayIntervalMs);
    displayIntervalSpin_->setSuffix(" ms");

    loopCheck_ = new QCheckBox("Loop captures until stopped");
    loopCheck_->setChecked(settings.loopEnabled);
    timeLimitSpin_ = new QSpinBox();
    timeLimitSpin_->setRange(0, 3600);
    timeLimitSpin_->setValue(settings.timeLimitSeconds);
    timeLimitSpin_->setSuffix(" s");
    timeLimitSpin_->setSpecialValueText("No limit");

    form->addRow("Maximum pressure shown", maxPressureSpin_);
    form->addRow("Saved acquisition interval", acquisitionIntervalSpin_);
    form->addRow("UI graph display interval", displayIntervalSpin_);
    form->addRow("Loop", loopCheck_);
    form->addRow("Time limit", timeLimitSpin_);
    layout->addLayout(form);

    const auto refreshPreview = [this] { updateProfilePreview(); };
    connect(profileTypeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, refreshPreview);
    connect(peakForceSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, refreshPreview);
    connect(durationSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, refreshPreview);
    connect(frequencySpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, refreshPreview);
    connect(controlIntervalSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, refreshPreview);
    connect(controlDeadbandSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, refreshPreview);
    connect(commandStepsSpin_, qOverload<int>(&QSpinBox::valueChanged), this, refreshPreview);
    connect(commandDelaySpin_, qOverload<int>(&QSpinBox::valueChanged), this, refreshPreview);
    updateProfilePreview();

    auto* note = new QLabel(
        "Note: USB still receives the ESP32 DMA block at 100 µs internally. If you choose a slower saved interval, "
        "the app downsamples before storing/exporting. Wi‑Fi acquisition is limited to the low-rate display interval.");
    note->setWordWrap(true);
    note->setObjectName("subtitle");
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

RecordingSettings RecordingSetupDialog::settings() const {
    RecordingSettings value;
    value.graphMode = graphModeFromId(graphButtons_->checkedId());
    value.maxPressure = maxPressureSpin_->value();
    value.loopEnabled = loopCheck_->isChecked();
    value.timeLimitSeconds = timeLimitSpin_->value();
    value.acquisitionIntervalMs = acquisitionIntervalSpin_->value();
    value.displayIntervalMs = displayIntervalSpin_->value();
    value.testProfile = profileSettings();
    return value;
}

TestProfileSettings RecordingSetupDialog::profileSettings() const {
    return {
        profileTypeFromIndex(profileTypeCombo_->currentIndex()),
        peakForceSpin_->value(),
        durationSpin_->value(),
        frequencySpin_->value(),
        controlIntervalSpin_->value(),
        controlDeadbandSpin_->value(),
        commandStepsSpin_->value(),
        commandDelaySpin_->value(),
    };
}

void RecordingSetupDialog::updateProfilePreview() {
    const TestProfileSettings profile = profileSettings();
    profilePreview_->setProfile(profile);
    sineDerivedLabel_->setText(
        QString("Amplitude %1 ADC, offset %2 ADC; cyclic target stays between 0 and max.")
            .arg(QString::number(profile.amplitude(), 'f', 1))
            .arg(QString::number(profile.offset(), 'f', 1)));
    maxPressureSpin_->setValue(std::max(maxPressureSpin_->value(), static_cast<int>(std::ceil(profile.peakForce))));
}

}  // namespace sensor
