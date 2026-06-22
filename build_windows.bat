@echo off
setlocal
py -c "import tkinter" || (echo Error: this Python installation does not include Tkinter. & exit /b 1)
py -m venv --clear .venv-build
call .venv-build\Scripts\activate.bat
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
if exist build rmdir /s /q build
if exist dist\SensorTester.exe del /q dist\SensorTester.exe
pyinstaller --clean --noconfirm --onefile --windowed --name SensorTester ^
  --hidden-import=_tkinter ^
  --hidden-import=matplotlib.backends.backend_tkagg ^
  --hidden-import=serial.tools.list_ports ^
  --hidden-import=openpyxl ^
  --hidden-import=requests ^
  run_app.py
if errorlevel 1 (
  echo Build failed. Review the error shown above.
  exit /b 1
)
echo.
echo Built one-file application: dist\SensorTester.exe
echo Send that single file to your friend.
pause
