@echo off
setlocal enabledelayedexpansion

set DO_CLEAN=0
set CONFIGS=

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="clean" (
    set DO_CLEAN=1
) else if /i "%~1"=="debug" (
    set "CONFIGS=!CONFIGS! Debug"
) else if /i "%~1"=="release" (
    set "CONFIGS=!CONFIGS! Release"
) else if /i "%~1"=="both" (
    set "CONFIGS=Debug Release"
) else (
    echo Unknown argument: %~1
    echo Usage: %~nx0 [clean] [debug ^| release ^| both]
    exit /b 1
)
shift
goto parse
:parsed

if "%CONFIGS%"=="" set "CONFIGS=Debug"

REM Skip vcvars if we're already inside a developer prompt
if not defined VSCMD_ARG_TGT_ARCH (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    if errorlevel 1 goto fail
)

if "%DO_CLEAN%"=="1" (
    echo Cleaning build directory...
    if exist build rmdir /s /q build
)

cmake -B build -G "Ninja Multi-Config"
if errorlevel 1 goto fail

for %%C in (%CONFIGS%) do (
    echo Building %%C
    cmake --build build --config %%C
    if errorlevel 1 goto fail
)

echo Build succeeded: %CONFIGS%
pause
exit /b 0

:fail
echo BUILD FAILED
pause
exit /b 1
