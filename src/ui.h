/*
 * clearC - 图形界面
 */
#pragma once

#include "common.h"

namespace clearc {

// 启动 GUI 消息循环，返回进程退出码
int RunGui(HINSTANCE instance, int showCmd);

}  // namespace clearc
