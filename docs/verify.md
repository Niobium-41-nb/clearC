# clearC 2.0.0 独立验收报告

- 验收人：verifier（独立验收，未参与本次重构实现）
- 验收时间：2026-11-10 11:30–11:48（UTC+8）
- 仓库：`D:\Program Files\clearC`
- 基线提交：`2bc5e38`（重构改动尚未提交；`clearC.cpp` 删除已暂存，`src/`、`docs/`、`.gitattributes`、`LICENSE` 为未跟踪文件）
- 验收方式：从非仓库 CWD 实机构建 + CLI 实测 + Win32 消息驱动的 GUI 冒烟 + 独立 junction 夹具 + 源码逐行核查；不采信 README 自述。
- 约束遵守：未执行任何真实清理，未点击“清理选中项”，未运行 `--clean`；仅执行 `--scan/--list/--self-test` 与 GUI 扫描。未修改源码/配置，仅新增本文件；`clearC.exe` 由 `build.bat` 重新生成（已被 `.gitignore` 忽略）；临时产物全部位于 `%TEMP%`。

## 1. 验收结论

**结论：通过（PASS），未发现阻断缺陷。**

全部必验项（构建、CLI、GUI、代码级安全、一致性/工程、额外独立验证）均为 PASS；`--self-test` 49/49 通过、0 失败、0 跳过；未发现可导致误删用户数据、构建误报成功或功能不可用的阻断问题。

另有 2 项低severity 加固建议、若干工程/文档类观察（见 §5、§6），均不影响本次验收结论。

## 2. 验收环境

| 项 | 值 |
| --- | --- |
| OS | Windows 10/11（中文，非管理员账户） |
| 屏幕 | 物理 2880×1800，系统缩放 200%，工作区 2880×1704 |
| 工具链 | `D:\msys64\ucrt64\bin\g++.exe`（MSYS2 g++ 16.1.0）、`windres` 2.46 |
| 磁盘 | C 盘可用约 57 GB |
| 构建 CWD | `C:\Users\14530\AppData\Local\Temp`（非仓库目录） |
| 证据目录 | `%TEMP%\cc-*.json/out`、`%TEMP%\gui_probe.out`、`%TEMP%\clearc_gui.png` |

## 3. 逐项验收结果

### 3.1 构建（PASS）

| # | 断言 | 结果 |
| --- | --- | --- |
| 1 | 从非仓库 CWD 执行 `build.bat`，退出码 0 | **PASS**（exit 0，12.2s） |
| 2 | `clearC.exe` 被更新（时间戳/哈希变化） | **PASS**（mtime 11:30:03→11:31:16；SHA256 `F6133E1F…`→`E7C209C7…`；大小 1,221,481 B 不变） |
| 3 | 版本资源 `FileVersion=2.0.0.0` | **PASS**（FileVersion/ProductVersion 均 2.0.0.0） |
| 4 | `build.bat` 全 ASCII | **PASS**（2400 B，non-ASCII=0） |
| 5 | `build.bat` CRLF | **PASS**（CRLF=94，裸 LF=0） |
| 6 | 含 `cd /d "%~dp0"` | **PASS** |
| 7 | 含 `if errorlevel 1 goto fail`（windres/g++ 两处） | **PASS** |
| 8 | LIBS = `-lcomctl32 -lshlwapi -lshell32 -static` | **PASS** |
| 9 | 沙盒故障注入：编译错误 → 退出码非 0、报错、不打印 `Build succeeded`、不产出 exe | **PASS**（exit 1；打印 g++ 错误与 `Build failed. See the errors above.`；`Build succeeded` 未出现；沙盒内无 `clearC.exe`） |

关键输出（从 `%TEMP%` 调用）：

```
Toolchain: g++
[1/2] Compiling resource...
[2/2] Compiling clearC.exe...
Build succeeded: D:\Program Files\clearC\clearC.exe
Size: 1221481 bytes
```

故障注入（`%TEMP%\clearC-sandbox`，向 `src\main.cpp` 追加 `void broken_func( { syntax error here`）：

```
src\main.cpp:24:6: error: variable or field 'broken_func' declared void
...
Build failed. See the errors above.
SANDBOX_EXITCODE=1   SuccessStringPresent=False   SandboxExeExists=False
```

### 3.2 CLI（PASS）

| # | 断言 | 结果 |
| --- | --- | --- |
| 1 | `--version` | **PASS**（`clearC 2.0.0`，exit 0） |
| 2 | `--help` | **PASS**（7 个选项 + 管理员提示，exit 0） |
| 3 | `--list --json`：JSON 合法 | **PASS** |
| 4 | `total=97` | **PASS**（`total=97`） |
| 5 | 分类数 = 7 | **PASS**（包管理器/构建缓存/编辑器IDE/浏览器/系统缓存/应用缓存/开发工具） |
| 6 | `kind=file` 条目 = 2 | **PASS**（缩略图缓存、图标缓存） |
| 7 | `defaultChecked=70` | **PASS**（70/97；其余 27 项为谨慎项） |
| 8 | `needsAdmin=14` | **PASS** |
| 9 | `--scan --json` JSON 可解析 | **PASS** |
| 10 | `totalBytes>0` | **PASS**（41,239,499,649 B ≈ 38.41 GB；且逐项 bytes 求和 == totalBytes） |
| 11 | `items>=30` | **PASS**（64 项存在目标；kind 分布 dir=61 / file=2 / recycle=1） |
| 12 | 每条含 `bytes/files/needsAdmin/denied/skippedLinks/kind/dirs/fileCount` | **PASS**（缺失字段数=0） |
| 13 | `--self-test` 退出码 0，无 FAIL/SKIP，通过数 49 | **PASS**（`自检结果: 49 通过 / 0 失败 / 0 跳过`，exit 0，0.2s） |
| 14 | 扫描耗时记录 | **10.3s**（CLI 单次）；GUI 内扫描 10.3s / 10.2s；与 README“约 12 秒”同一量级 |

`--list --json` 分类计数（与 README 完全一致）：包管理器 24、构建缓存 4、编辑器/IDE 23、浏览器 7、系统缓存 23、应用缓存 10、开发工具 6，合计 97。

`--scan --json` 片段（按大小降序）：

```
top: WPS 组件缓存 11.54 GB (126365 files) > VSCode 工作区状态 6.0 GB > npm 缓存 2.76 GB >
     Package Cache（全机） 1.94 GB(需管理员) > Puppeteer 浏览器缓存 1.77 GB
denied=false 的 6 项 / skippedLinks>0 的 2 项 / needsAdmin 的 11 项（未提权时 GUI 状态栏显示“7 需提权”，
差异原因：状态栏只统计 needsAdmin 且 bytes>0 的项，其余 4 项为 0 字节/不可读，语义可解释）
```

### 3.3 GUI 冒烟（PASS）

用自写的 Win32 消息夹具（`%TEMP%\clearc_gui_probe.cpp` → `clearc_gui_probe.exe`）启动被测 exe，按 `GetWindowThreadProcessId` 过滤仅操作本夹具启动的实例；**未点击“清理选中项”**。

| # | 断言 | 结果 |
| --- | --- | --- |
| 1 | 存在可见主窗口，标题含 `clearC 2.0.0` | **PASS**（`C盘清理工具 - clearC 2.0.0`，类名 `ClearCMainWindow`；扫描后 `IsWindowVisible=1`。注：窗口创建瞬间（235ms）尚未 ShowWindow，属夹具采样时机，非缺陷） |
| 2 | 控件齐全：Button 扫描缓存 / 全选 / 以管理员身份重启 / 清理选中项、ComboBox、SysListView32、msctls_progress32 | **PASS**（7/7 命中，控件 ID 1001/1003/1010/1002/1009/1005/1006） |
| 3 | 自动扫描并等待完成，状态栏静态文本含“可清理” | **PASS**（`扫描完成 · 可清理 64 项 · 38.30 GB · C: 57.29 GB 可用 · 7 需提权 · 6 不可读 · 2 链接跳过`；观察到“正在扫描”阶段，扫描等待 10.3s） |
| 4 | ListView 行数 > 0 | **PASS**（64 行） |
| 5 | “全选”初始为半选 `BM_GETCHECK==2` | **PASS**（=2；与“64 项存在、43 项默认勾选”一致） |
| 6 | `BM_CLICK` “扫描缓存”后行数稳定 | **PASS**（观察到“正在扫描”，再次完成，64→64） |
| 7 | ComboBox 内容 | **PASS**（8 项：全部分类 + 7 类） |
| 8 | `WM_CLOSE` 正常退出，进程退出码 0 | **PASS**（wait=OBJECT_0，exit=0） |
| 9 | 可选：DPI-aware PrintWindow 截图，无控件越界/裁切 | **PASS**（`OUT_OF_BOUNDS=0`；200% 缩放下窗口 1906×1391、客户区 1880×1320；截图见 `%TEMP%\clearc_gui.png`） |

截图目视：标题栏、扫描按钮、半选“全选”、分类下拉、提权按钮、4 列表格（缓存项/分类/大小/路径，路径列显示“等 N 个位置”）、已选择汇总、状态栏、进度条、清理按钮均完整显示，无重叠。

### 3.4 代码级安全核查（PASS）

逐行核查 `src/util.cpp`、`src/scan.cpp`、`src/catalog.cpp`、`src/ui.cpp`（行号对应验收时工作区现状）：

| 断言 | 代码证据 | 结果 |
| --- | --- | --- |
| DirSize 不跟随重解析点 | `util.cpp:98-100`（目录且 `FILE_ATTRIBUTE_REPARSE_POINT` → `++skippedReparse; continue`）；顶层 `util.cpp:479-482` 整体跳过 | **PASS** |
| DeleteChildren 不删除链接 | `util.cpp:136-139`（重解析点目录保留并计数，不递归）；只读文件先清属性再删（`util.cpp:151-163`） | **PASS** |
| ExpandGlob 不跟随链接 | `util.cpp:203`（通配匹配到重解析点直接跳过） | **PASS** |
| ResolveCatalog 对“目标本身是链接”的项跳过并计入 skippedLinks | `catalog.cpp:197-205` | **PASS**（独立夹具实测：uv 项 `resolved=0/skippedLinks=1/exists=true`） |
| IsSafeToClean 拒绝盘符根 / `C:` / `C:\` | `util.cpp:320-326`（原始写法先拒）+ `p.size()<=3`（`util.cpp:333`） | **PASS** |
| 拒绝 `C:\Windows`、System32 等 | `util.cpp:336-353` 字面量根 + `util.cpp:358-366` 环境变量根（SystemDrive/SystemRoot/ProgramFiles/ProgramData/USERPROFILE/LOCALAPPDATA/APPDATA/PUBLIC 等） | **PASS** |
| 拒绝用户目录根及桌面/文档/下载/OneDrive/AppData | `util.cpp:368-374` | **PASS** |
| 拒绝受保护目录的“上级” | `util.cpp:46-54`（`IsSelfOrAncestorOf`）+ `util.cpp:376-379` | **PASS** |
| `..` 归一化绕过被阻断 | `util.cpp:276-279` 绝对路径也走 `GetFullPathNameW` 归一化；自检“拒绝 `..` 归一化后的受保护目录”PASS | **PASS** |
| 文件通配只删命中文件、失败计数 | `util.cpp:169-208`（末段只收文件）+ `cli.cpp:483-497`；`scan.cpp:209-230`（逐文件 `DeleteFileForce`，失败 `++failed` 并记录首个错误） | **PASS**（自检“只删除命中的文件”PASS） |
| 需管理员项未提权时跳过并计数 | `scan.cpp:189-194`（`needsAdmin && !elevated` → `entriesSkippedAdmin`，不删除）；`catalog.cpp:182` 标记 `adminBlocked`；GUI 列表/状态栏标注“需管理员” | **PASS** |
| 删除失败累计 failed 与 firstError | `util.cpp:71-76`（`RecordFailure`）；`scan.cpp:224-247` 汇总到 `CleanOutcome`；`ui.cpp:874-893` 结果对话框显示失败数/跳过数/首个失败原因 | **PASS** |
| 长路径统一 `\\?\` | `util.cpp:283-291`；所有文件 API 调用经 `ToLongPath`（`grep` 核查：`FindFirstFileExW/DeleteFileW/GetFileAttributesW/CreateFileW/RemoveDirectoryW` 等均带 `ToLongPath`）；自检 >260 字符夹具统计/清理 PASS | **PASS** |
| 清理前确认、清理后自动重扫 | `ui.cpp:500-507`（确认框，默认按钮为“否”）、`ui.cpp:896`（`DoScan`） | **PASS** |
| 扫描期间关闭窗口有确认，取消则不退出 | `ui.cpp:900-909` | **PASS** |
| 提权重启走 `ShellExecuteW(..., L"runas", ...)` | `ui.cpp:452-463` | **PASS** |

### 3.5 一致性与工程（PASS）

| # | 断言 | 结果 |
| --- | --- | --- |
| 1 | README 的 97 项/7 类/默认 70 项与 `--list` 一致 | **PASS**（97；7 类且各类计数 24/4/23/7/23/10/6 逐项一致；70 默认勾选，27 谨慎项） |
| 2 | README 编译命令与 `build.bat` 一致 | **PASS**（同为 `-std=c++17 -O2 -DUNICODE -D_UNICODE -municode -mwindows -finput-charset=UTF-8 -I.`、同源文件顺序、同 `-lcomctl32 -lshlwapi -lshell32 -static`、同 `windres resource.rc -o build/resource.o`） |
| 3 | `git status` 无意外大文件 | **PASS**（未跟踪：`src/` 12 文件 125 KB、`docs/` 2 文件 141 KB、`.gitattributes` 175 B、`LICENSE` 1 KB；无二进制大文件） |
| 4 | `build/` 与 exe 被忽略 | **PASS**（`git check-ignore`：`.gitignore:2:build/`、`.gitignore:3:*.exe` 命中；`git status --ignored` 显示 `!! build/`、`!! clearC.exe`） |
| 5 | `.gitattributes` 存在且定义 `*.bat CRLF` | **PASS**（`*.bat text eol=crlf` 等；工作区 `build.bat` 实测 94 CRLF、0 裸 LF） |
| 6 | `g++ -std=c++17 -Wall -Wextra ... -fsyntax-only src/*.cpp` 无警告 | **PASS**（exit 0，警告 0） |
| 7 | 完整 `-c` 编译（`-Wall -Wextra`，比 `-fsyntax-only` 更严格）无警告 | **PASS**（6 个 .cpp 全部 exit 0，警告 0） |
| 8 | manifest：`asInvoker` / `PerMonitorV2` / `longPathAware` | **PASS**（`manifest.xml` 声明；exe 内嵌清单字节中检索到 `asInvoker`、`PerMonitorV2`、`longPathAware`、`ClearC.Cleaner`） |
| 9 | `resource.rc` 版本资源存在 | **PASS**（`VERSIONINFO FILEVERSION 2,0,0,0` + `StringFileInfo`；exe 的 VersionInfo 读取为 2.0.0.0） |

### 3.6 额外独立验证：junction 夹具（PASS）

不依赖被测程序的自检代码，使用 `cmd /c mklink /J` 创建真实 junction，并通过覆盖子进程 `LOCALAPPDATA` 让内置的 `uv 包缓存`/`pip 缓存` 指向夹具：

```
夹具：%TEMP%\cc_junction_fixture
  local\uv\cache  -> junction -> victim\        （victim\IMPORTANT.txt 500 B）
  local\pip\cache -> 真实目录（pkg.whl 1234 B）
```

`--list --json`（子进程 `LOCALAPPDATA`=夹具 `local`）：

```
UV : exists=True resolved=0 skippedLinks=1 defaultChecked=True
PIP: exists=True resolved=1 skippedLinks=0
victim\IMPORTANT.txt 仍存在
```

`--scan --json`（10.6s）：

```
UV : found=True bytes=0 files=0 skippedLinks=1      // 目标本身是 junction → 整体跳过，不统计指向内容
PIP: found=True bytes=1234 files=1                  // 普通目录正常统计
victim\IMPORTANT.txt 仍存在                         // 仅扫描，未删除任何真实数据
```

结论：`ResolveCatalog` 的目标级重解析点跳过在真实 OS junction 上可复现，且指向的用户数据未被统计/删除。夹具已安全删除（先 `rmdir` junction 本体，再删目录树）。

## 4. 发现的问题（按严重度）

无阻断（Blocker/High）问题。以下均为非阻断项：

| 级别 | 问题 | 说明/建议 |
| --- | --- | --- |
| 低 | `IsSafeToClean` 未拒绝 UNC 路径；设备前缀（`\\?\`、`\\.\`）路径在 `NormalizePath` 中被跳过 `GetFullPathName` 归一化，理论上 `\\?\C:\…\..\…` 可绕过字面量/环境变量根比较 | 当前内置 catalog 全部为本地盘符路径，且 `CleanDirectoryContents` 的入口路径来自已归一化的解析结果，**实际不可达**；审计 §9.1 期望 UNC 模板被拒。建议在 `IsSafeToClean` 中显式拒绝 UNC 与 `\\?\` 输入，或对设备前缀剥前缀后再做一次完整归一化/根比较 |
| 低 | `--scan/--list` JSON 与 GUI 列表/状态栏的 `skippedLinks` 只反映“解析阶段目标本身是链接”；`DirSize` 递归中跳过的重解析点只写入 `EntryState.skippedReparse`，CLI JSON 无该字段，GUI 也不展示（仅清理结果对话框汇总 `skippedReparse`） | 安全行为正确（跳过），但可观测性不足；建议 scan JSON 增加 `skippedReparse` 字段（或合并进 `skippedLinks` 并注明语义），GUI 列表副标题同步展示 |
| 信息 | 工作区 `build/` 残留大量测试产物（截图、`vtest.exe`/`clearC-test.exe` 等约 3 MB） | 已被 `.gitignore` 忽略，不影响仓库；交付前一并清理更整洁 |
| 信息 | 重构未提交：`clearC.cpp` 删除已 `git add`，而 `src/`、`docs/`、`.gitattributes`、`LICENSE` 仍未跟踪 | 验收基于工作区快照；建议主干合并前提交并跑一次干净 clone 构建 |
| 信息 | `.gitattributes` 用 `* text=auto`，未显式规定 `.gitignore` 行尾，`git` 提示 `.gitignore` 的 LF 将被转为 CRLF（cosmetic） | 可对 `*.gitignore`/`.gitignore` 补 `eol=crlf` 或保持现状 |
| 信息 | 审计 §9.2/§9.6 中提及的“取消按钮”“`--validate` / `--dry-run` / `--log`”未实现 | 不在本次验收必验范围；当前用 `WM_CLOSE` 确认框取消扫描/清理，CLI 仅提供 README 声明的 5 个选项 |

## 5. 残余风险与建议

1. **系统项清理的真实删除路径未做破坏性验证**（按验收要求未执行）。安全推断依据：`CleanDirectoryContents`/`DeleteFileForce` 的 `IsSafeToClean` + 重解析点检查 + 只删目录内容，加上 `--self-test` 与独立 junction 夹具。建议在干净测试机/虚拟机做一次“全选清理”演练，并对比 `GetDiskFreeSpaceExW` 前后差值。
2. **目录树遍历深度上限 128**（`util.cpp:19`）：异常深目录会被截断统计（不报错），清理同理；对真实缓存几乎无影响，可在结果中区分“深度截断”以便观测。
3. **`--scan` 只列出 exists 的项**（本机 64/97）：未安装的缓存项不出现，`totalBytes` 亦只含存在项；与 `--list.total=97` 语义不同，文档已分别说明，但脚本消费方需注意。
4. **并行扫描 8 线程对旋转盘/网络盘可能放大抖动**：当前仅 C 盘本地缓存，风险低；若后续支持自定义路径，建议按任务粒度限流。
5. **提权重启后勾选状态不跨进程保存**：`RelaunchElevated` 以新进程启动，用户已勾选项不保留（审计 §9.3 曾期望保留）；不影响安全，属体验项。
6. **UI 取消语义**：关闭窗口时置 `g.cancel` 并 `WaitForSingleObject(…,3000)`，若清理线程超时未退出则句柄被 `CloseHandle`（线程仍会因 `cancel` 自行结束）；极端慢盘下窗口可能先退出、线程短暂残留，无数据风险。

## 6. 证据与临时产物

| 产物 | 路径 |
| --- | --- |
| CLI `--list --json` 输出 | `%TEMP%\cc-list.json` |
| CLI `--scan --json` 输出 | `%TEMP%\cc-scan.json` |
| CLI `--self-test` 输出 | `%TEMP%\cc-selftest.out` |
| GUI 冒烟夹具源码/输出 | `%TEMP%\clearc_gui_probe.cpp`、`%TEMP%\gui_probe.out` |
| GUI 截图（200% DPI） | `%TEMP%\clearc_gui.png`（1906×1391） |
| junction 夹具输出 | `%TEMP%\cc-junction-list.json`、`%TEMP%\cc-junction-scan.json`（夹具目录已清理） |
| 故障注入沙盒 | `%TEMP%\clearC-sandbox`（已用坏源码构建失败） |
| `-Wall -Wextra` 完整编译对象 | `%TEMP%\clearc-obj\*.o` |

以上均为临时证据；仓库内唯一新增文件为本报告。
