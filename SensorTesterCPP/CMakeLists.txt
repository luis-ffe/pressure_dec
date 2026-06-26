cmake_minimum_required(VERSION 3.21)

project(SensorTesterCPP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort Network)

qt_add_executable(SensorTesterCPP
    MACOSX_BUNDLE
    src/main.cpp
    src/core/AppConstants.h
    src/core/CaptureDecoder.cpp
    src/core/CaptureDecoder.h
    src/core/ClosedLoopController.cpp
    src/core/ClosedLoopController.h
    src/core/DataExporter.cpp
    src/core/DataExporter.h
    src/core/DelayAnalyzer.cpp
    src/core/DelayAnalyzer.h
    src/core/TestProfile.cpp
    src/core/TestProfile.h
    src/model/Measurement.h
    src/model/RecordingSettings.h
    src/transport/SerialPortEnumerator.cpp
    src/transport/SerialPortEnumerator.h
    src/transport/SerialTransport.cpp
    src/transport/SerialTransport.h
    src/transport/Transport.h
    src/transport/WifiTransport.cpp
    src/transport/WifiTransport.h
    src/ui/GraphPreviewWidget.cpp
    src/ui/GraphPreviewWidget.h
    src/ui/LivePlotWidget.cpp
    src/ui/LivePlotWidget.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/MotorControlDialog.cpp
    src/ui/MotorControlDialog.h
    src/ui/ProfileCurveWidget.cpp
    src/ui/ProfileCurveWidget.h
    src/ui/RecordingSetupDialog.cpp
    src/ui/RecordingSetupDialog.h
)

target_link_libraries(SensorTesterCPP
    PRIVATE
        Qt6::Widgets
        Qt6::SerialPort
        Qt6::Network
)

set_target_properties(SensorTesterCPP PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.local.SensorTesterCPP"
    MACOSX_BUNDLE_BUNDLE_NAME "SensorTesterCPP"
    MACOSX_BUNDLE_DISPLAY_NAME "SensorTesterCPP"
    WIN32_EXECUTABLE TRUE
)

install(TARGETS SensorTesterCPP
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)
