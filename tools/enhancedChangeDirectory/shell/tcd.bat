@echo off
rem tcd wrapper for Windows cmd.exe.
rem
rem cmd runs .bat files in the parent's own context, so `cd /d` here actually
rem changes the caller's directory.
rem
rem Resolution order for the binary:
rem   1. %TCD_BIN% if set
rem   2. tcd.exe in this script's parent directory (<shell-dir>\..\tcd.exe)
rem   3. tcd.exe on PATH
rem
rem Setup: put this folder on PATH (User PATH via sysdm.cpl works). Either
rem also put tcd.exe on PATH in a different folder, or set TCD_BIN to its
rem full path.

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

rem If the first arg looks like a flag (starts with -), pass straight through
rem so multi-line output (e.g. --help, --config, --list-themes) flows to the
rem console verbatim instead of being eaten by `for /f`.
set "_first=%~1"
if defined _first if "!_first:~0,1!"=="-" (
    "!_exe!" %*
    exit /b !errorlevel!
)

rem TUI / cd path: capture stdout, then either cd to it (directory) or echo it.
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
