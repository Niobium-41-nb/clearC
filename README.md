# clearC - C盘缓存清理工具

Windows 原生桌面应用，一键清理 C 盘各类缓存，释放磁盘空间。

## 功能

- 扫描并清理 19 类常见缓存：
  - **包管理器**：uv、pip、Conda、NuGet、npm、yarn、pnpm、vcpkg
  - **VSCode**：Cache、CachedData、CachedExtensions、CachedProfileData、GPUCache、logs、C++ cpptools
  - **构建工具**：CMake
  - **系统**：临时文件、Windows 更新缓存、预取文件、错误报告
- 复选框勾选要清理的项目，实时显示已选总大小
- 清理前弹窗确认 + 进度条
- 支持窗口缩放、DPI 感知

## 编译

### 环境要求

- MSYS2 + MinGW-w64 (ucrt64)
- 或 Visual Studio 2019+

### MSYS2 MinGW-w64

```bash
PATH="/d/msys64/ucrt64/bin:$PATH"
windres resource.rc -o resource.o
g++ -std=c++17 -O2 -o clearC.exe clearC.cpp resource.o \
    -mwindows -municode -lcomctl32 -lshlwapi -lole32 -luuid -static
```

### Visual Studio

```cmd
rc resource.rc
cl /std:c++17 /O2 clearC.cpp resource.res /link ^
    user32.lib comctl32.lib shlwapi.lib shell32.lib ole32.lib ^
    /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup
```

### 一键编译

双击 `build.bat`（需要 MSYS2 安装在 `D:\msys64`）。

## 技术栈

- C++17
- Win32 API
- Common Controls v6
- 静态链接，无外部依赖

## 许可证

MIT License
