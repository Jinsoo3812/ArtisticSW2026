@echo off
REM ==============================================================================
REM ArtisticSW 2026 - Kelvin Wake with Analytical Gradients Baker Launcher
REM ==============================================================================

set "UE_PYTHON=C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"

if not exist "%UE_PYTHON%" (
    echo [ERROR] Unreal Engine Python was not found at: %UE_PYTHON%
    pause
    exit /b 1
)

echo [INFO] Running Kelvin Wake Gradient Baker Pipeline using UE 5.7 Python...
"%UE_PYTHON%" "%~dp0run_pipeline.py"

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Pipeline failed with error code %ERRORLEVEL%
    pause
    exit /b %ERRORLEVEL%
)

echo [SUCCESS] Pipeline execution finished!
pause
