@echo off
setlocal
set ROOT=%~dp0..\..
for %%I in ("%ROOT%") do set ROOT=%%~fI

set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
    echo ERROR: Visual Studio 2022 vcvars64.bat not found at %VCVARS%
    exit /b 1
)
call %VCVARS% >nul

set VCYD_DIR=%ROOT%\companion\virtual_cyd
set SHIM_INC=%VCYD_DIR%\shims
set FW_INC=%ROOT%\firmware\include
set FW_SRC=%ROOT%\firmware\src
set JSON_INC=%ROOT%\firmware\.pio\libdeps\e32r28t_ili9341\ArduinoJson\src
set TFT_INC=%ROOT%\firmware\.pio\libdeps\e32r28t_ili9341\TFT_eSPI
set OUT_DLL=%VCYD_DIR%\hydro_virtual_cyd.dll
set OBJ_DIR=%VCYD_DIR%\obj
if not exist "%OBJ_DIR%" mkdir "%OBJ_DIR%"

echo Building real firmware C++ Virtual CYD DLL...
cl.exe /nologo /LD /O2 /EHsc /std:c++17 /MD /D_CRT_SECURE_NO_WARNINGS /DHYDRO_VIRTUAL_CYD ^
  /I"%SHIM_INC%" ^
  /I"%FW_INC%" ^
  /I"%FW_SRC%" ^
  /I"%JSON_INC%" ^
  /I"%TFT_INC%" ^
  /Fo"%OBJ_DIR%\\" ^
  "%VCYD_DIR%\virtual_hw.cpp" ^
  "%VCYD_DIR%\TFT_eSPI_emu.cpp" ^
  "%FW_SRC%\display_ui.cpp" ^
  "%FW_SRC%\deck_store.cpp" ^
  "%FW_SRC%\touch_input.cpp" ^
  "%FW_SRC%\hw_feedback.cpp" ^
  "%VCYD_DIR%\virtual_cyd_main.cpp" ^
  /Fe:"%OUT_DLL%"

if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo Built OK: %OUT_DLL%

