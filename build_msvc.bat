@echo off
setlocal

REM Load MSVC environment
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

set CONFIG=Debug

if "%1"=="clean" (
    echo Cleaning build directory...
    if exist build rmdir /s /q build
)

cmake -B build -G "Ninja Multi-Config" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --config %CONFIG%

pause
endlocal
