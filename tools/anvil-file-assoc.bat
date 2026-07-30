@echo off
REM ============================================================================
REM  Anvil-CAD file associations (per-user, no administrator rights required)
REM
REM    anvil-file-assoc.bat            register .anvil_pro/.anvil_sch/.anvil_pcb
REM    anvil-file-assoc.bat remove     unregister them again
REM
REM  Writes under HKCU\Software\Classes, so it only affects the current user and
REM  never fights a machine-wide KiCad install.
REM ============================================================================

setlocal

set "APPDIR=%~dp0..\build\install\msvc-win64-release\bin"
set "EXE=%APPDIR%\anvilcad.exe"

if /I "%1"=="remove" goto :remove

if not exist "%EXE%" (
    echo [ERROR] anvilcad.exe not found at:
    echo         %EXE%
    echo         Build/install first ^(build-all.bat^), or edit APPDIR in this script.
    pause
    exit /b 1
)

echo === Registering Anvil-CAD file associations ===
echo     using %EXE%

call :progid Anvil.Project    "Anvil-CAD Project"   1
call :progid Anvil.Schematic  "Anvil-CAD Schematic" 2
call :progid Anvil.Board      "Anvil-CAD Board"     3

call :assoc .anvil_pro Anvil.Project
call :assoc .anvil_sch Anvil.Schematic
call :assoc .anvil_pcb Anvil.Board

REM Ask Explorer to pick up the new icons/handlers immediately.
ie4uinit.exe -show >nul 2>&1

echo.
echo === DONE ===
echo Double-clicking a .anvil_pro / .anvil_sch / .anvil_pcb file now opens Anvil-CAD.
pause
exit /b 0


:progid
REM %1 = ProgID, %2 = friendly name, %3 = icon index in the exe
reg add "HKCU\Software\Classes\%~1" /ve /t REG_SZ /d %2 /f >nul
reg add "HKCU\Software\Classes\%~1\DefaultIcon" /ve /t REG_SZ /d "\"%EXE%\",%~3" /f >nul
reg add "HKCU\Software\Classes\%~1\shell\open\command" /ve /t REG_SZ /d "\"%EXE%\" \"%%1\"" /f >nul
echo   + ProgID %~1
exit /b 0

:assoc
REM %1 = extension, %2 = ProgID
reg add "HKCU\Software\Classes\%~1" /ve /t REG_SZ /d "%~2" /f >nul
reg add "HKCU\Software\Classes\%~1" /v "PerceivedType" /t REG_SZ /d "document" /f >nul
echo   + %~1 -^> %~2
exit /b 0


:remove
echo === Removing Anvil-CAD file associations ===
for %%E in (.anvil_pro .anvil_sch .anvil_pcb) do (
    reg delete "HKCU\Software\Classes\%%E" /f >nul 2>&1
    echo   - %%E
)
for %%P in (Anvil.Project Anvil.Schematic Anvil.Board) do (
    reg delete "HKCU\Software\Classes\%%P" /f >nul 2>&1
    echo   - %%P
)
ie4uinit.exe -show >nul 2>&1
echo === DONE ===
pause
exit /b 0
