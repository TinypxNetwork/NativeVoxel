@echo off
cd /d "%~dp0"
if not exist "bin\NativeVoxel.exe" (
  echo NativeVoxel.exe is missing. Run powershell -ExecutionPolicy Bypass -File build.ps1
  pause
  exit /b 1
)
for %%F in (assets\assets.bin assets\atlas.rgba assets\font.rgba assets\runtime\first_person\models.bin assets\runtime\tiles\patterns.bin assets\runtime\equipment\report.json) do if not exist "%%F" goto prepareAssets
goto launch

:prepareAssets
python "tools\setup_assets.py"
if errorlevel 1 (
  echo Asset setup failed. Install Python and run: python -m pip install -r requirements-dev.txt
  pause
  exit /b 1
)

:launch
start "NativeVoxel" "bin\NativeVoxel.exe" %*
