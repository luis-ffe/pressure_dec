#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

echo
echo "Built: $(pwd)/build/bin/SensorTesterCPP.app"
