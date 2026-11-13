@echo off
rem Builds core\cyd_core.dll (the CYD board peripherals) with MSVC.
setlocal
set HERE=%~dp0
set VCVARS=
for %%E in (Community Professional Enterprise BuildTools) do (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if "%VCVARS%"=="" (
    echo ERROR: Visual Studio 2022 C++ tools not found
    exit /b 1
)
call "%VCVARS%" >nul
if not exist "%HERE%obj" mkdir "%HERE%obj"
cl.exe /nologo /LD /O2 /W3 /MT /D_CRT_SECURE_NO_WARNINGS /Fo"%HERE%obj\\" /Fe:"%HERE%cyd_core.dll" "%HERE%cyd_core.c" /link /IMPLIB:"%HERE%obj\cyd_core.lib"
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo Built %HERE%cyd_core.dll
