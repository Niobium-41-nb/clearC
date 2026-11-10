@echo off
setlocal
cd /d "%~dp0"

echo ============================================
echo   clearC - Windows cache cleaner build
echo ============================================
echo.

set "ACTION=%~1"
if /i "%ACTION%"=="clean" goto clean

REM Locate MinGW-w64 toolchain
set "GPP="
set "WINDRES="

where g++.exe >nul 2>&1
if not errorlevel 1 set "GPP=g++"
where windres.exe >nul 2>&1
if not errorlevel 1 set "WINDRES=windres"

if not defined GPP (
    if defined MSYS2_ROOT if exist "%MSYS2_ROOT%\ucrt64\bin\g++.exe" (
        set "GPP=%MSYS2_ROOT%\ucrt64\bin\g++.exe"
        set "WINDRES=%MSYS2_ROOT%\ucrt64\bin\windres.exe"
    )
)

if not defined GPP (
    for %%D in ("D:\msys64" "C:\msys64" "%ProgramFiles%\msys64" "%LOCALAPPDATA%\msys64") do (
        if exist "%%~D\ucrt64\bin\g++.exe" (
            set "GPP=%%~D\ucrt64\bin\g++.exe"
            set "WINDRES=%%~D\ucrt64\bin\windres.exe"
        )
    )
)

if not defined GPP (
    echo [ERROR] MinGW-w64 g++ was not found.
    echo         Install MSYS2 or add g++.exe to PATH, or set MSYS2_ROOT.
    echo.
    pause
    exit /b 1
)
if not defined WINDRES (
    echo [ERROR] windres.exe was not found. Check your MinGW-w64 installation.
    pause
    exit /b 1
)
echo Toolchain: %GPP%
echo.

if not exist "build" mkdir "build"

set "CXXFLAGS=-std=c++17 -O2 -DUNICODE -D_UNICODE -municode -mwindows -finput-charset=UTF-8 -I."
set "LIBS=-lcomctl32 -lshlwapi -lshell32 -static"
set "SOURCES=src\main.cpp src\ui.cpp src\scan.cpp src\catalog.cpp src\util.cpp src\cli.cpp"
if /i "%ACTION%"=="release" set "CXXFLAGS=%CXXFLAGS% -s"

echo [1/2] Compiling resource...
"%WINDRES%" resource.rc -o "build\resource.o"
if errorlevel 1 goto fail

echo [2/2] Compiling clearC.exe...
"%GPP%" %CXXFLAGS% -o clearC.exe %SOURCES% "build\resource.o" %LIBS%
if errorlevel 1 goto fail

echo.
echo Build succeeded: %CD%\clearC.exe
for %%F in (clearC.exe) do echo Size: %%~zF bytes
echo.

if /i "%ACTION%"=="run" (
    echo Starting clearC...
    start "" clearC.exe
)

if /i not "%ACTION%"=="run" pause
exit /b 0

:fail
echo.
echo Build failed. See the errors above.
pause
exit /b 1

:clean
echo Cleaning build artifacts...
if exist "build" rmdir /s /q "build"
if exist "clearC.exe" del /q "clearC.exe"
if exist "resource.o" del /q "resource.o"
echo Done.
pause
exit /b 0
