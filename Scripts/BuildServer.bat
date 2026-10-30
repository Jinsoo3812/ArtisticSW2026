@echo off
setlocal DisableDelayedExpansion

rem Paths for this workstation. Update these when moving the engine.
set "SourceEngine=C:\UnrealSource\UE5.7.4"
set "LauncherEngine=C:\Program Files\Epic Games\UE_5.7"
set "ArchiveBase=C:\UnrealBuilds\ArtisticSW2026Server-UE574"
for %%I in ("%~dp0..") do set "ProjectRoot=%%~fI"
set "ProjectFile=%ProjectRoot%\ArtisticSW2026.uproject"
set "Mode=%~1"
if not defined Mode set "Mode=package"
if /i "%Mode%"=="package" goto prerequisites
if /i "%Mode%"=="build" goto prerequisites
if /i "%Mode%"=="check" goto prerequisites
echo Usage: BuildServer.bat [package^|build^|check]
exit /b 2

:prerequisites
if not exist "%ProjectFile%" goto missing
if not exist "%SourceEngine%\Engine\Build\BatchFiles\Build.bat" goto missing
if not exist "%SourceEngine%\Engine\Build\BatchFiles\RunUAT.bat" goto missing
if not exist "%LauncherEngine%\Engine\Build\BatchFiles\Build.bat" goto missing
set "CompilerConfig=%SourceEngine%\Engine\Saved\UnrealBuildTool\BuildConfiguration.xml"
if not exist "%CompilerConfig%" goto missing
findstr /L /C:"<Compiler>VisualStudio2026</Compiler>" "%CompilerConfig%" >nul
if errorlevel 1 goto compilerconfig
findstr /L /C:"<CompilerVersion>14.44.35228</CompilerVersion>" "%CompilerConfig%" >nul
if errorlevel 1 goto compilerconfig
echo Project: %ProjectFile%
echo Source engine: %SourceEngine%
if /i "%Mode%"=="check" (
    echo Path and compiler configuration checks passed. No build was run.
    exit /b 0
)

set "RunStamp="
for /f %%T in ('powershell.exe -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "RunStamp=%%T"
if not defined RunStamp (
    echo ERROR: Could not create the run timestamp.
    exit /b 1
)
set "LogDir=%ProjectRoot%\Saved\Logs\ServerBuilds\%RunStamp%_%RANDOM%"
mkdir "%LogDir%"
if errorlevel 1 exit /b 1
echo Close this project's Unreal Editor before building.
echo Logs: %LogDir%
if /i "%Mode%"=="build" goto buildonly

set "ArchiveDir=%ArchiveBase%\%RunStamp%_%RANDOM%"
echo Packaging Development Server. Please wait; output is written to package.log.
call "%SourceEngine%\Engine\Build\BatchFiles\RunUAT.bat" "-ScriptsForProject=%ProjectFile%" BuildCookRun "-project=%ProjectFile%" -noP4 -unattended -utf8output -server -noclient -serverplatform=Win64 -serverconfig=Development "-unrealexe=%SourceEngine%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -build -cook -stage -pak -iostore -package -archive "-archivedirectory=%ArchiveDir%" > "%LogDir%\package.log" 2>&1
set "PackageExit=%ERRORLEVEL%"
echo Restoring project Editor modules for the Launcher engine.
call "%LauncherEngine%\Engine\Build\BatchFiles\Build.bat" ArtisticSW2026Editor Win64 Development "-Project=%ProjectFile%" -WaitMutex -NoHotReload -2026 "-CompilerVersion=14.44.35228" > "%LogDir%\launcher-editor.log" 2>&1
set "RestoreExit=%ERRORLEVEL%"
set "Result=%PackageExit%"
if not "%PackageExit%"=="0" (
    echo ERROR: Packaging failed with exit code %PackageExit%. See package.log.
) else (
    echo SUCCESS: Server package: %ArchiveDir%\WindowsServer
)
if not "%RestoreExit%"=="0" (
    echo ERROR: Launcher Editor rebuild failed with exit code %RestoreExit%. See launcher-editor.log.
    if "%PackageExit%"=="0" set "Result=%RestoreExit%"
)
goto finish

:buildonly
echo Compiling Development Server only. No Cook or package is produced.
call "%SourceEngine%\Engine\Build\BatchFiles\Build.bat" ArtisticSW2026Server Win64 Development "-Project=%ProjectFile%" -WaitMutex -NoHotReload -2026 "-CompilerVersion=14.44.35228" > "%LogDir%\server-build.log" 2>&1
set "Result=%ERRORLEVEL%"
if "%Result%"=="0" (
    echo SUCCESS: Server compilation completed. This is not a distributable package.
) else (
    echo ERROR: Server compilation failed with exit code %Result%. See server-build.log.
)

:finish
echo Logs: %LogDir%
echo Final exit code: %Result%
pause
exit /b %Result%

:missing
echo ERROR: A project, engine, or compiler configuration file is missing.
echo Check the paths at the top of this script. See Scripts\SERVER_BUILD.md.
if /i "%Mode%"=="check" exit /b 1
pause
exit /b 1

:compilerconfig
echo ERROR: Source engine must use VisualStudio2026 and MSVC 14.44.35228.
echo Check: %CompilerConfig%
if /i "%Mode%"=="check" exit /b 1
pause
exit /b 1
