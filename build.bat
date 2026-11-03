@echo off
chcp 65001 >nul 2>&1
echo ============================================
echo   clearC - C盘缓存清理工具 编译脚本
echo ============================================
echo.

REM 尝试 MSYS2 MinGW-w64 路径
set "MSYS2=D:\msys64\ucrt64\bin"

if exist "%MSYS2%\g++.exe" (
    set "PATH=%MSYS2%;%PATH%"

    echo [1/2] 编译资源文件...
    "%MSYS2%\windres" resource.rc -o resource.o
    if %ERRORLEVEL% NEQ 0 (
        echo 资源编译失败!
        pause
        exit /b 1
    )

    echo [2/2] 编译主程序...
    "%MSYS2%\g++" -std=c++17 -O2 -o clearC.exe clearC.cpp resource.o -mwindows -municode -lcomctl32 -lshlwapi -lole32 -luuid -static
    if %ERRORLEVEL% NEQ 0 (
        echo 编译失败!
        pause
        exit /b 1
    )

    del resource.o 2>nul
    echo.
    echo 编译成功: clearC.exe
    echo.
    start clearC.exe
) else (
    echo 未找到 MSYS2 MinGW-w64 编译器
    echo 请确认 D:\msys64\ucrt64\bin\g++.exe 存在
    echo.
    echo 或者手动在 Git Bash 中执行:
    echo   PATH="/d/msys64/ucrt64/bin:$PATH"
    echo   windres resource.rc -o resource.o
    echo   g++ -std=c++17 -O2 -o clearC.exe clearC.cpp resource.o -mwindows -municode -lcomctl32 -lshlwapi -lole32 -luuid -static
)

pause
