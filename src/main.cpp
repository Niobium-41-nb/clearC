/*
 * clearC - 程序入口
 * 无参数启动 GUI；带参数进入命令行模式（便于脚本 / 自动化验证）
 */
#include "cli.h"
#include "ui.h"

#include <shellapi.h>

#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCmd) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (argv) {
        for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
        LocalFree(argv);
    }
    if (!args.empty()) return clearc::RunCli(args, instance);
    return clearc::RunGui(instance, showCmd);
}
