/*
 * clearC - 清理项目录
 *
 * 设计约定：
 *   paths 支持 ';' 分隔多个模板，支持 %ENV% 变量与 * / ? 通配（任意一段均可）
 *   needsAdmin 为 true 的项目：普通权限下仍会统计大小，但清理时跳过并提示提权
 *   defaultChecked 只在 Risk::Safe 时可能为 true，谨慎项默认不勾选
 *   危险目录（聊天记录、扩展本体、Docker 镜像、WPS 版本目录等）不进入目录
 */
#pragma once

#include "scan.h"

#include <cstddef>
#include <string>
#include <vector>

namespace clearc {

enum class Category {
    Package,  // 包管理器
    Build,    // 构建缓存
    Ide,      // 编辑器 / IDE
    Browser,  // 浏览器
    System,   // 系统
    App,      // 应用
    Dev,      // 开发工具
    Count,
};

enum class Risk {
    Safe,     // 可随时删除，程序会自动重建 / 重新下载
    Caution,  // 可删，但需要重新下载或登录后重建，默认不勾选
};

enum class EntryKind {
    Directory,   // 按目录内容清理（目录本身保留）
    FileGlob,    // 文件通配：删除命中的文件本身
    RecycleBin,  // 通过 Shell API 清空回收站
};

struct EntryDef {
    const wchar_t* name;
    const wchar_t* paths;
    Category category;
    bool needsAdmin;
    bool defaultChecked;
    Risk risk;
    const wchar_t* note;          // 一句话说明这是什么缓存
    const wchar_t* consequence;   // 清理之后会发生什么（点击名称时展示）
    EntryKind kind;
};

const EntryDef* Catalog(std::size_t* count);
const wchar_t* CategoryName(Category category);
const wchar_t* ConsequenceOf(const EntryDef& def);

// 解析所有清理项：展开环境变量/通配、剔除不存在或不安全的目标
std::vector<EntryState> ResolveCatalog(bool elevated);

}  // namespace clearc
