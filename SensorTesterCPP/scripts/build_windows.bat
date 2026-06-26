@echo off
setlocal
cd /d "%~dp0\.."

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b %errorlevel%

cmake --build build --config Release
if errorlevel 1 exit /b %errorlevel%

echo.
echo Built: %cd%\build\bin\Release\SensorTesterCPP.exe
