# Build with: pyinstaller --clean --noconfirm SensorTester.spec
from PyInstaller.utils.hooks import collect_data_files

datas = collect_data_files("matplotlib")

a = Analysis(
    ["run_app.py"],
    pathex=[],
    binaries=[],
    datas=datas,
    hiddenimports=[
        "_tkinter",
        "matplotlib.backends.backend_tkagg",
        "serial.tools.list_ports",
        "openpyxl",
        "requests",
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="SensorTester",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
)
coll = COLLECT(exe, a.binaries, a.datas, strip=False, upx=True, name="SensorTester")

import platform
if platform.system() == "Darwin":
    app = BUNDLE(
        coll,
        name="SensorTester.app",
        icon=None,
        bundle_identifier="com.sensortester.desktop",
        info_plist={"NSHighResolutionCapable": "True"},
    )
