@echo off
setlocal
title D4AssetBrowser - Fix paths after the folder move
cd /d "%~dp0"

REM ---------------------------------------------------------------------------
REM  The project folder moved. Absolute paths recorded INSIDE it did not.
REM
REM    - the app's settings still name the OLD folder for d4data and the TACT
REM      keys, so it keeps working only because that old folder still exists
REM    - CMake's cache still records the old source directory, so a build from
REM      the new location fails until it is cleared
REM
REM  This fixes both. It does NOT touch build\release\data - d4data, the CASC
REM  index and every built cache stay where they are.
REM
REM  Thin launcher on purpose, same reason as "Release - Set Version.bat":
REM  the regexes below contain quotes and backslashes, and a powershell
REM  -Command string makes cmd re-parse them. -File means it never does.
REM
REM  Usage:  "Fix Paths After Move.bat"          apply
REM          "Fix Paths After Move.bat" /dry     report only, change nothing
REM ---------------------------------------------------------------------------

if not exist "%~dp0fix-paths-after-move.ps1" (
    echo.
    echo  [X] fix-paths-after-move.ps1 is missing - it must sit beside this file.
    echo.
    pause & exit /b 1
)

REM The app rewrites its settings file when it exits, which would undo this.
tasklist /fi "imagename eq D4AssetBrowser.exe" 2>nul | find /i "D4AssetBrowser.exe" >nul
if not errorlevel 1 (
    echo.
    echo  [X] D4AssetBrowser.exe is running. Close it first - it rewrites its
    echo      settings file on exit and would put the old paths straight back.
    echo.
    pause & exit /b 1
)

set "ARG="
if /i "%~1"=="/dry" set "ARG=-WhatIfOnly"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fix-paths-after-move.ps1" %ARG%
set "RC=%errorlevel%"

echo.
echo  Next: rebuild.bat   (CMake will reconfigure from this folder)
echo.
pause
exit /b %RC%
