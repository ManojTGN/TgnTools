@echo off
rem tcd wrapper for Windows cmd.exe.
rem
rem Resolution order for the binary:
rem   1. %TCD_BIN% if set
rem   2. tcd.exe in this script's parent directory (i.e. <shell-dir>\..\tcd.exe)
rem   3. tcd.exe on PATH
rem
rem cmd runs .bat files in the parent's own context, so `cd /d` here actually
rem changes the caller's directory.

setlocal enabledelayedexpansion

if defined TCD_BIN (
    set "_exe=!TCD_BIN!"
) else (
    for %%I in ("%~dp0..\tcd.exe") do set "_candidate=%%~fI"
    if exist "!_candidate!" (
        set "_exe=!_candidate!"
    ) else (
        set "_exe=tcd.exe"
    )
)

set "_target="
for /f "usebackq delims=" %%I in (`"!_exe!" %*`) do set "_target=%%I"

endlocal & set "_TCD=%_target%"

if not defined _TCD goto :_tcd_done
if exist "%_TCD%\" (
    cd /d "%_TCD%"
    goto :_tcd_done
)
echo %_TCD%
:_tcd_done
set "_TCD="
