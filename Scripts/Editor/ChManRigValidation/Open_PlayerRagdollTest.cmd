@echo off
for %%I in ("%~dp0..\..\..") do set "PROJECT_ROOT=%%~fI"
start "Player corpse test - Project" "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJECT_ROOT%\ArtisticSW2026.uproject" "/Game/Developers/PlayerRagdoll/L_PlayerRagdollTest" -NoCompile -NoCompileEditor -NoLiveCoding -windowed -ResX=1440 -ResY=900 -ExecCmds="t.MaxFPS 60"
