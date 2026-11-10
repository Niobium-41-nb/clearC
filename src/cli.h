/*
 * clearC - 命令行模式（便于脚本与自动化验证）
 */
#pragma once

#include "common.h"

#include <string>
#include <vector>

namespace clearc {

int RunCli(const std::vector<std::wstring>& args, HINSTANCE instance);

}  // namespace clearc
