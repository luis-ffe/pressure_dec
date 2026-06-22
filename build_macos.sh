#!/bin/sh
set -eu
PYTHON_BIN="${PYTHON_BIN:-python3}"
if ! "$PYTHON_BIN" -c 'import tkinter' >/dev/null 2>&1; then
  echo "Error: $PYTHON_BIN does not include Tkinter."
  echo "Install Python from python.org, or install the matching Homebrew python-tk package."
  exit 1
fi
"$PYTHON_BIN" -m venv --clear .venv-build
. .venv-build/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
pyinstaller --clean --noconfirm SensorTester.spec
echo "Built dist/SensorTester.app"
