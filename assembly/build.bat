@echo off
REM build.bat - Build mini_nano on Windows (MSYS2 / MinGW-w64)


















pauseecho To run: .\mini_nano.exe [filename]echo Build succeeded: mini_nano.exe)    exit /b %ERRORLEVEL%    echo Build failed with exit code %ERRORLEVEL%.if %ERRORLEVEL% NEQ 0 (gcc -Wall -Wextra -std=c11 mini_nano.c -o mini_nano.exe
:: Compile (MSYS2/MinGW provides POSIX support required by this source))    exit /b 1    echo Alternatively, build inside WSL with:  gcc mini_nano.c -o mini_nano    echo Install MSYS2 or MinGW-w64 and open the MSYS2/MinGW shell, or add MinGW's bin to PATH.    echo ERROR: gcc not found in PATH.if %ERRORLEVEL% NEQ 0 (where gcc >nul 2>&1:: Check for gcc on PATH