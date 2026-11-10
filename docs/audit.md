# clearC 现有实现代码审计报告

> 审计人：code-auditor（独立审计）  
> 审计日期：2026-11-10  
> 审计基线：git HEAD `2bc5e384de19f305a1a5084019611d2d020138e3`（"feat: 支持 Ctrl+鼠标滚轮调整界面字号"）  
> 对象文件（均已核对 blob 哈希）：
>
> | 文件 | 行数 | HEAD blob | 审计期间工作区状态 |
> | --- | --- | --- | --- |
> | `clearC.cpp` | 747 | `40bb4927…` | 审计期间未修改；审计结束时 Lead 已从工作区删除（本报告行号用 `git show HEAD:clearC.cpp` 复现） |
> | `build.bat` | 45 | `4117848b…` | ⚠ 审计期间被 Lead 修改（新版另见 §10） |
> | `manifest.xml` | 16 | `54086a99…` | ⚠ 审计期间被 Lead 修改（新版另见 §10） |
> | `resource.rc` | 4 | `398d9759…` | ⚠ 审计期间被 Lead 修改（新版另见 §10） |
> | `resource.h` | 6 | — | 未修改 |
> | `README.md` | 71 | `459f9913…` | 未修改 |
> | `.gitignore` | 18 | — | 未修改 |
>
> 说明：审计开始时工作区是干净的；审计过程中 Lead 并行开始了重构（新增未跟踪的 `src/`、`build/`，并修改了 `build.bat`/`manifest.xml`/`resource.rc`）。**本报告第 3 节的 F 组结论针对审计基线的 HEAD 版本**，行号以 HEAD 文件为准；新版差异集中记录在 §10，避免误判。
>
> 审计结束时（11:22）工作区已大幅演进：`clearC.cpp` 已被删除、新增 `src/` 新实现、`README.md` 已重写为 69 项/7 类、新增 `LICENSE`、`.gitignore` 已忽略 `build/`。本报告正文仍以 HEAD 基线为准（审计对象是"现有实现"），新旧实现的差异与遗留项见 §10。
>
> 工具链：MSYS2 UCRT64 `g++ 16.1.0 (Rev5, MSYS2 project)`、`windres`；本机**没有** MSVC、clang++、clang-tidy、cppcheck。  
> 权限限制：本机普通权限无法创建目录符号链接（`mklink /D` 报"权限不足"），因此符号链接结论依据 Win32 文档；**目录 junction 已实测**（§6.1）。MSVC 编译命令未实测。

---

## 0. 结论摘要

共记录 **56 项**问题：**严重 3 项、高 9 项、中 23 项、低 21 项**。

**最危险的三个结论（必须先修）**

1. **删除原语会跟随 junction / 符号链接 / 重解析点**：`CalcDirSize`、`DeleteRecursive`、`CleanDirContents` 只用 `dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY` 决定是否递归，从不检查 `FILE_ATTRIBUTE_REPARSE_POINT`/reparse tag。**已实测**：在缓存目录内放一个指向测试目录的 junction 后，`CalcDirSize` 把缓存目录之外的 19 字节计入；`CleanDirContents` 把 junction 目标里的用户文件真实删除（§6.1）。
2. **清理路径完全来自环境变量与硬编码，无任何危险路径护栏**：空值、相对路径（相对进程 CWD 解析）、盘符根、`%USERPROFILE%` 根、`C:\Windows`、UNC 网络路径、被截断的路径都可以成为 `CleanDirContents` 的目标；一旦 `%TEMP%` 异常或缓存项是重定向 junction，就会清空目标位置（A-02/A-03）。
3. **所有删除失败被完全吞掉，UI 仍弹"清理完成！"**：`DeleteRecursive`/`CleanDirContents` 返回 `void`，`CleanThread` 不看结果（`clearC.cpp:313`），`WM_CLEAN_DONE` 无条件提示成功（`clearC.cpp:673-679`）。普通权限下系统项、被占用文件、只读/ACL 失败时用户会以为空间已释放（A-05/C-01）。

**其余最高优先级**：任务生命周期无守卫导致按钮永久禁用（B-01）、关闭窗口时线程失控/进程中途被杀（B-02）、`MAX_PATH` 静默截断到"另一个合法路径"（A-04）、无管理员检测导致系统项永远静默失败（E-01）、`build.bat` 三个缺陷使"编译失败也报成功"（F-01/F-02/F-03）。

**编译警告**：按任务要求执行 `g++ -std=c++17 -Wall -Wextra -fsyntax-only clearC.cpp` 得到 **0 条警告**；完整编译（`-c`/`-o`，同样是 `-Wall -Wextra`）只报 **1 条** `g_hBtnNone` 未使用（`clearC.cpp:93`）。本项目的真实缺陷几乎全部落在编译器警告盲区（Win32 API 误用、并发、路径安全、错误吞掉），**不能用"编译无警告"作为质量依据**（§5）。

**非问题（已核验，无需改）**：资源 ID 无冲突（`manifest=1/type 24`、`ICON=101`、控件 ID 1001–1008，1004 为空号）；`WM_SCAN_UPDATE` 对 ListView 的访问发生在 UI 线程（线程归属正确）；`FindFirstFile` 句柄在各返回路径上都有 `FindClose`；扫描进度的除法不会除零；`.gitignore` 确实忽略了 `clearC.exe`。详见 §3.7。

---

## 1. 审计方法与实测手段

1. **逐行静态审计**：通读 `clearC.cpp`（747 行）、`build.bat`、`manifest.xml`、`README.md`、`resource.h/rc`、`.gitignore`；对每条结论标注文件与行号。
2. **编译器复核**（§5）：按任务要求跑 `-fsyntax-only`；再补完整编译、`-Wshadow`、`-Wformat=2`、`-Wconversion`、`-Wsign-conversion`、`-fanalyzer`。
3. **针对删除原语的可执行复现**（§6.1）：把 `CalcDirSize`/`DeleteRecursive`/`CleanDirContents` **逐字复制**到独立 harness，在 `%TEMP%` 创建 junction 做破坏性实验（不触碰任何真实缓存）。
4. **构建脚本行为复现**（§6.2）：把 HEAD 版 `build.bat` 复制到空临时目录运行（不写仓库、不覆盖 `clearC.exe`），再分别做 CRLF/LF、UTF-8/ASCII 对照实验。
5. **产物一致性核验**（§6.3）：在 `%TEMP%\clearC_audit\` 完整重建 exe，与仓库内 `clearC.exe` 比较 SHA-256/逐字节差异；验证 `-municode` 缺失确实链接失败（§6.4）。
6. 所有临时产物均在 `%TEMP%` 下（`clearC_audit\`、`clearC_junction_*\`、`bat_probe*\`），未修改仓库源码、未覆盖 `clearC.exe`。

**限制**：无 MSVC，故 README 的 cl.exe 命令只做静态判断；符号链接无权限创建；UI 布局/DPI 无截图自动化，结论来自坐标计算与代码路径（`S()` 缩放 + `MoveWindow` 参数）。

---

## 2. 严重度定义与编号

| 级别 | 定义 | 处置 |
| --- | --- | --- |
| **严重** | 可直接造成缓存目录之外的数据丢失、或删除系统/用户关键目录；一旦触发不可逆 | 发版前必须修，且要有自动化测试 |
| **高** | 用户可见的错误结果（谎报成功、按钮卡死）、安全护栏缺失但需特定条件、构建/发布链路不可信 | 本次重构必须修 |
| **中** | 可靠性/性能/可维护性明显受损，或特定场景下出错 | 建议本次修，至少排期 |
| **低** | 代码卫生、文档漂移、边缘体验 | 可选/顺手修 |

编号规则：`A` 删除安全、`B` 线程与消息、`C` 状态与错误处理、`D` 性能、`E` UI/UX、`F` 构建与一致性；`R` 为重构建议；`非问题` 单列。

---

## 3. 问题清单

### 3.1 A 组：删除安全（12 项）

#### A-01｜严重｜递归统计与删除跟随 junction / 符号链接 / 重解析点，会删除缓存目录之外的内容
- **位置**：`clearC.cpp:146-162`（`CalcDirSize`）、`164-186`（`DeleteRecursive`）、`188-198`（`CleanDirContents`）
- **现象**：三个函数都只判断 `fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY`（155、168、194 行）就继续递归；`FILE_ATTRIBUTE_REPARSE_POINT`、`fd.dwReserved0`（reparse tag）从未检查。缓存目录内只要存在 junction/目录符号链接（包管理器、用户重定向、OneDrive 很常见），扫描会把**目标位置**的大小算进来，清理会把**目标位置**里的文件真实删除。
- **根因**：没有区分"普通目录"与"重解析点"，把"目录"等同于"可递归的真实目录"；`FindFirstFileW`/`GetFileAttributesW` 返回的属性里已经带 `0x400` 标志（实测 junction 为 `0x410`，tag `0xA0000003`），代码视而不见。
- **修复**：抽一个安全遍历原语：枚举时若 `attr & FILE_ATTRIBUTE_REPARSE_POINT`（或 `dwReserved0 != 0`）→ **不递归、不统计其内部**；策略二选一：①默认跳过并计入"已跳过重解析点"；②仅删除链接本身（文件符号链接 `DeleteFileW`，目录链接/junction `RemoveDirectoryW`，均不进入目标）。根路径（缓存项本身）也要用 `CreateFileW(..., FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS)` + `GetFileInformationByHandleEx(FileAttributeTagInfo)` 复核；云占位符（`IO_REPARSE_TAG_CLOUD*`/`FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS`）默认跳过。若改用 `IFileOperation`，加 `FOF_NORECURSEREPARSE`。
- **实测**：§6.1（junction 内 19 字节被计入、目标文件被删除）。

#### A-02｜严重｜清理项自身是 junction/重定向时（如 `%TEMP%`、npm/yarn 缓存被重定向），会清空链接目标
- **位置**：`clearC.cpp:188-198`（`CleanDirContents` 对根目录直接 `FindFirstFile`）、`396-422`（`DoScan` 仅用 `IsDir` 判断存在）、`216-249`（条目来源为环境变量/硬编码）
- **现象**：`IsDir()`（141-144）对目录 junction/符号链接返回 true（实测 `IsDir`=1），`CleanDirContents` 随后枚举 `link\*`，删除的是**目标目录的内容**；junction 本身还在（实测 §6.1 CASE 2）。典型场景：用户把 `%TEMP%`、`%LOCALAPPDATA%\npm-cache`、`%USERPROFILE%\.nuget` 通过 junction 迁到 D 盘/内存盘/其他目录；或企业策略把 `%LOCALAPPDATA%` 重定向到网络盘。结果：跨卷删除、甚至删到与"缓存"无关的用户数据；UI 仍宣称"清理 C 盘"。
- **根因**：把"缓存项路径"当成可信的普通目录；没有对根路径做重解析点检测，也没有限定"必须位于本机固定磁盘的预期父目录下"。
- **修复**：缓存项解析后先做根路径校验（§A-03）；根路径是重解析点 → 默认禁用该项并在 UI 标注"已重定向/链接，默认不清理"，仅允许用户在明确知情后勾选（或用"清理链接目标"独立开关）；同时显示解析后的真实路径与卷（`GetVolumePathNameW`）。条目来源改用 `SHGetKnownFolderPath`（`FOLDERID_LocalAppData`/`RoamingAppData`/`Profile`）+ `GetTempPath2W`，而不是直接读环境变量。
- **实测**：§6.1 CASE 2（`victim2\DATA2.txt` 被删除，junction 保留）。

#### A-03｜严重｜没有任何危险路径护栏：空值/相对路径/盘根/用户目录根/Windows/UNC 都可能成为清理目标
- **位置**：`clearC.cpp:216-249`（`InitEntries` 直接拼环境变量）、`204-214`（`AddEntry` 不做校验）、`188-198`（`CleanDirContents` 直接清空子项）、`427-451`（`DoClean` 只做大小判空）
- **现象与根因**（逐条）：
  - `GetEnv` 返回空串时（服务/SYSTEM 上下文、精简环境），路径变成 `"\uv\cache"`、`"\npm-cache"` 之类**相对当前盘根**的路径，恰好存在就会被处理；
  - `TEMP` 等变量若为**相对路径**（`.`、`..\x`），`CleanDirContents` 会相对进程 CWD 解析，可能清空**程序自身安装目录**；
  - 若 `%TEMP%` 被配成盘根（`C:\`、`D:\`）或用户目录根（`C:\Users\x`），`CleanDirContents` 会删除该根下**所有子项**——这是不可逆的灾难（盘根下所有用户可见目录、用户目录下的 Desktop/Documents/Downloads）；
  - 完全没有 `PathIsRoot`、`GetDriveType`、`SHGetKnownFolderPath`、受保护目录清单（`%SystemRoot%`、`System32`、`Program Files`、`ProgramData`、用户目录根、桌面/文档/下载、`$Recycle.Bin`、`System Volume Information`）检查；
  - `%USERPROFILE%` 为 UNC（漫游/重定向）时，路径成为 `\\server\share\...`，会**跨网络删除**；
  - `AddEntry` 的 `wcsncpy` 截断（见 A-04）可能把路径截成**另一个真实存在的路径**。
- **修复**：新增 `ValidateTarget()`（所有删除/统计入口唯一必经）：
  1. `GetFullPathNameW`/`PathCchCanonicalizeEx` 规范化（必须绝对路径；拒绝 `.`/`..` 解析结果）；
  2. 拒绝 `PathIsRoot()`（含 UNC 根）、`\\?\` 原始设备路径、非固定卷（默认）；
  3. 拒绝落在受保护集合内的路径（Windows/System32/Program Files/ProgramData/用户目录根/桌面/文档/下载/OneDrive 根/回收站/System Volume Information）；
  4. 要求目标**必须是允许父目录的后代**（如 `%LOCALAPPDATA%`、`%APPDATA%`、`%TEMP%`、`%SystemRoot%\Temp`、`%USERPROFILE%\.<pkg>`）且**不能等于父目录本身**（例如系统临时文件项只允许"清空 `%TEMP%` 的内容"，绝不允许把 `%TEMP%` 解析成 `%USERPROFILE%` 后仍放行）；
  5. 目标必须存在、是目录、且根不是重解析点（A-02）；
  6. 清理前**再次**校验（TOCTOU，见 A-08）；删除过程中每个子项同样过"非重解析"检查；
  7. UI 显示规范化后的绝对路径 + 卷 + 风险等级；危险项默认不勾选、二次确认文案写明具体路径。
- **修复优先级**：**最高**。这是本次重构的安全底线。

#### A-04｜高｜`MAX_PATH` 定长缓冲静默截断，会指向"另一个合法路径"；长路径静默失败
- **位置**：`clearC.cpp:75`（`wchar_t path[MAX_PATH]`）、`209-210`（`wcsncpy(..., MAX_PATH-1)` 截断）、`141-162`/`164-198` 全部路径拼接与 Win32 调用
- **现象**：
  - 路径 ≥ 260 字符时被静默截断到 259 字符。截断点可能落在目录名中间，于是得到一个**不同的、可能真实存在的路径**：例如目标 `...\VeryLongName\cache` 截成 `...\VeryLongNam`；更糟的是截断到某个**祖先目录**（若后续字符恰好被切掉），`CleanDirContents` 会把祖先目录的子项当缓存清掉。没有任何"截断即报错"的检查。
  - 递归拼接 `dir + L"\\" + name`（154、174、195）很容易超过 260；一旦超过，`FindFirstFileW`/`GetFileAttributesW` 失败，`CalcDirSize` 静默返回 0（表现为"该缓存很小"），`DeleteRecursive` 静默失败（表现为"清理完成"但文件还在）。
  - 未加 `\\?\` 前缀，也没有 manifest `longPathAware`（HEAD 版）。
- **修复**：删掉固定数组，统一用 `std::wstring` 存路径；新增 `ToLongPath()`（绝对路径加 `\\?\`，UNC 转 `\\?\UNC\`）；所有文件 API 用长路径形式；根路径或子路径规范化失败/超长时**显式报错**并计入失败统计；manifest 增加 `<longPathAware>true</longPathAware>`，并做 >260 字符的自动化夹具（新版 `src/` 已出现相关夹具，见 §10）。

#### A-05｜高｜文件操作全部 `void` 返回，删除失败被完全吞掉
- **位置**：`clearC.cpp:164-186`（`DeleteRecursive` 返回 void，`DeleteFileW`/`RemoveDirectoryW`/`SetFileAttributesW` 返回值全部忽略）、`188-198`（`CleanDirContents` 返回 void）、`313`（`CleanThread` 调用后不看结果）
- **现象**：什么失败都不可见：只读清属性失败、ACL 拒绝、文件被占用、路径超长、重解析点、目录非空……调用方无法区分"删了 1 GB"和"一个都没删"。
- **根因**：错误处理设计缺失；函数签名没有结果出口。
- **修复**：定义统一结果结构（例：`struct OpResult { uint64_t files, dirs, skipped, failed; ULONGLONG bytesRemoved; std::vector<Failure> failures; }`），每个失败带 `path + Win32 error + 阶段`；清理线程汇总后回传 UI；UI 逐项状态列 + 汇总"释放 X，失败 N 项（原因分布）"；同时写诊断日志（§C-08）。
- **说明**：这是 A-05/C-01 共同的核心；只修 UI 文案不修返回值是无效修复。

#### A-06｜中｜只读/隐藏/系统/离线/云占位符属性处理不完整
- **位置**：`clearC.cpp:178-184`
- **现象**：只清 `FILE_ATTRIBUTE_READONLY`，且 `SetFileAttributesW` 失败不重试、不报错；`FILE_ATTRIBUTE_HIDDEN/SYSTEM` 未归一化（多数情况可删，但目录删除失败的常见原因之一）；OneDrive 等云占位符只按普通文件删/递归，可能触发不必要的水合（下载）或失败。
- **修复**：删除前统一归一化 `READONLY|HIDDEN|SYSTEM`（检查失败并计入失败）；`FILE_ATTRIBUTE_OFFLINE`/`RECALL_ON_DATA_ACCESS`/`IO_REPARSE_TAG_CLOUD*` **默认跳过**；清属性后重试一次删除；对目录删除失败记录原因。

#### A-07｜中｜被占用文件只尝试一次，无重试/延迟删除/占用进程提示
- **位置**：`clearC.cpp:164-186`、`288-319`
- **现象**：VSCode/浏览器/杀软/索引服务持有句柄时 `DeleteFileW` 失败（`ERROR_SHARING_VIOLATION`/`ACCESS_DENIED`），代码直接放弃，父目录 `RemoveDirectoryW` 随之失败，全部静默。清理提示还要求用户"确保 VSCode 已关闭"，但从不检测、也不报告哪些文件被谁占用。
- **修复**：①`ERROR_SHARING_VIOLATION` 做有限重试（如 3 次 × 50–100 ms 退避）；②可选 `MoveFileExW(..., MOVEFILE_DELAY_UNTIL_REBOOT)`（需管理员，且必须在 UI 明示）；③确认框里用 Restart Manager（`RmStartSession`/`RmRegisterResources`/`RmGetList`）列出占用进程；④失败明细写入日志并在结果中展示。

#### A-08｜中｜TOCTOU：扫描时只验证"是目录"，清理前不复核，目录可被替换/重定向
- **位置**：`clearC.cpp:408`（`IsDir` 判定）、`430`（用旧大小做确认）、`313`（直接清理，无复核）
- **现象**：扫描与清理间隔可达数分钟；期间缓存目录可能被删除重建、被替换成 junction/符号链接，或路径本身被重定向。清理时按当时看到的名字递归，可能跟随到新的目标。
- **修复**：清理前对每个条目重新执行完整 `ValidateTarget()`（含重解析点、受保护目录、规范化）；更好的是清理时用目录句柄（`FILE_FLAG_OPEN_REPARSE_POINT|BACKUP_SEMANTICS`）+ 句柄相对枚举（`GetFileInformationByHandleEx`/`NtQueryDirectoryFile`），避免"按路径重新解析"；无法保证时宁可不删并报错。

#### A-09｜中｜硬编码 `C:\Windows`；Prefetch / SoftwareDistribution 的语义与服务状态未处理
- **位置**：`clearC.cpp:246-247`
- **现象**：①系统装在非 C 盘或自定义 `%SystemRoot%` 时条目永远不存在/无效；②`Prefetch` 需要管理员，且删除会降低启动性能（微软不建议常规清理），条目却无任何风险标注；③`SoftwareDistribution\Download` 需要管理员，且 `wuauserv` 运行中删除会大量失败、可能导致更新状态不一致（半删的下载目录）；④没有检测服务状态、没有官方清理接口（Disk Cleanup/`IFileOperation`/`Dism`）。
- **修复**：用 `GetWindowsDirectoryW` 生成路径；给条目表加 `adminRequired`/`risk`/`defaultChecked`/`needsServiceStop` 元数据；`Prefetch` 默认不勾选或移除；`SoftwareDistribution` 要么先停 `wuauserv`/`bits` 再删（显著增加复杂度和风险），要么改用系统提供的清理入口；清理前检查服务状态并提示。

#### A-10｜中｜递归无深度上限；junction 环路会导致无限递归/栈溢出
- **位置**：`clearC.cpp:146-162`、`164-186`
- **现象**：`DeleteRecursive`/`CalcDirSize` 是纯递归，无深度保护；junction 互相指向（A→B、B→A）时形成环，直接无限递归 → 栈溢出 → 工作线程异常终止，由于没有 DONE 守卫（B-01）按钮永久禁用。极深目录树（如超深 `node_modules`）同样可能耗尽 1 MB 默认线程栈。
- **修复**：改成显式栈迭代遍历（顺带解决 depth 问题）；加最大深度（如 512）与最大条目数守卫；核心仍是"不跟随重解析点"（A-01）——这会天然消除环。

#### A-11｜低｜"大小"语义不准确：逻辑大小 ≠ 实际可释放空间
- **位置**：`clearC.cpp:158`（累加 `nFileSizeLow/High`）、`128-139`（格式化）、`437-438`（"将释放约 X"）
- **现象**：硬链接被重复计数（如 `pnpm`/`conda` 硬链接缓存）、稀疏/压缩文件按逻辑大小计、OneDrive 占位符按完整逻辑大小计；用户看到"释放 20 GB"但磁盘只多了 3 GB（或失败）。
- **修复**：同时统计**磁盘分配大小**（`GetFileInformationByHandleEx(FileStandardInfo)`/`GetCompressedFileSizeW`）并在 UI 区分"文件大小/占用空间"；确认文案改为"预计释放（估算）"；清理前后用 `GetDiskFreeSpaceExW` 展示**实际**变化。

#### A-12｜低｜清理中断/部分删除无任何记录，退出不警告
- **位置**：`clearC.cpp:683-687`（`WM_DESTROY` 直接 `PostQuitMessage`）、`673-681`
- **现象**：清理到一半关闭窗口/注销/关机，进程被终止，部分文件已删除且无日志；也无法知道哪次清理做了什么。
- **修复**：`WM_CLOSE` 时若 busy → 弹出"正在清理，是否停止并退出？"；设置停止标志、`WaitForSingleObject` 等待线程收尾（带超时）；写清理日志（开始/每项结果/退出原因）；支持 `WM_QUERYENDSESSION`。

### 3.2 B 组：线程与消息（8 项）

#### B-01｜高｜提前 return / 线程创建失败 / 线程异常都不发送 DONE，按钮永久禁用且无恢复
- **位置**：`clearC.cpp:263`（`if (!g_scanning) return 0;`）、`311`（`if (!g_cleaning) return 0;`）、`396-425`/`427-451`（`CreateThread` 返回值未检查，句柄未关闭）、`662-663`/`675-676`（只有 DONE 处理器会重新启用按钮）
- **现象**：`g_scanning`/`g_cleaning` 只在 `WM_SCAN_DONE`/`WM_CLEAN_DONE` 里置 false；这两个"提前 return"今天没有 UI 入口能触发，但下列路径会让按钮永久禁用：
  - `CreateThread` 失败（资源耗尽/注入环境）→ 标志已置 true、没有 DONE；
  - 线程因异常终止（A-10 栈溢出、`std::bad_alloc`、未捕获 SEH）→ 没有 DONE；
  - 未来一旦加"取消"功能并在线程内把标志置 false，就会命中 263/311 的 return 分支（典型的"半成品取消逻辑"陷阱）。
- **修复**：①RAII 完成守卫（线程函数对象析构时无条件 `PostMessage(WM_*_DONE, generation)`，包括异常路径）；②检查 `CreateThread` 返回值并在失败时回滚状态；③用 `std::thread` + 受控 join，或 `std::jthread`（C++20 不可用时自写 join 守卫）；④统一状态机 `SetBusy(State)` 管理按钮；⑤增加"取消"并保证取消路径也发 DONE。
- **实测**：按当前代码静态分析可达路径见上；`CreateThread` 失败未处理是确定的（424/450 行）。

#### B-02｜高｜关闭窗口时工作线程继续运行：不置停止标志、不等待、线程句柄泄漏，进程退出时可能"删到一半"
- **位置**：`clearC.cpp:683-687`（`WM_DESTROY` 只删字体 + `PostQuitMessage`）、`739-746`（消息循环结束即 `return`，工作线程被 `ExitProcess` 强杀）、`424`/`450`（`CreateThread` 句柄未 `CloseHandle`）
- **现象**：扫描/清理中关窗口 → 窗口销毁；工作线程继续 `PostMessage` 到已销毁的 HWND（无害但无意义），随后进程退出被强杀；清理线程可能在删除目录半途终止，留下部分删除状态且无日志；线程句柄泄漏（每次任务 1 个）。
- **修复**：`WM_CLOSE`/`WM_DESTROY`：若 busy → 提示并设置 `g_stop`；等待线程在文件边界检查 `g_stop` 后退出；`WaitForSingleObject(hThread, timeout)`；`CloseHandle`；只有在确认线程结束后再销毁窗口/退出；如果等待超时，明确提示"仍有后台删除在继续"再退出。

#### B-03｜中｜`g_mtx` 覆盖不完整、保护对象与临界区不匹配；持锁调用窗口函数
- **位置**：`clearC.cpp:258-259`（不加锁读 `lvIndex`）、`264-267`（不加锁读 `lvIndex`/`path`）、`269-273`（加锁写 `size`）、`313`（不加锁读 `path`）、`409-413`（UI 线程不加锁写 `lvIndex`）、`646-654`（持锁调用 `ListView_SetItemText`）、`364-380`（持锁调用 `ListView_GetCheckState`）
- **现象**：
  - 互斥量只护住 `size` 的读写，`g_entries[i].path/lvIndex/exists` 与 `g_count` 的跨线程访问没有统一约束；锁更像"装饰"。
  - UI 线程在持有 `g_mtx` 时调用 `ListView_SetItemText`/`ListView_GetCheckState`（都会同步派发窗口消息）；如果将来控件/通知在消息处理里回调进 `GetSelectedSize`（或任何要拿同一把非递归锁的路径），会**自死锁**。
  - 今天的时序"碰巧安全"，但一旦加"取消/重扫/后台重试"等改动就会变成真实数据竞争。
- **修复**：明确并发模型：工作线程只写"条目结果槽"（按索引，不共享可变容器），通过一次 `PostMessage` 把 `{generation, index, size, status}` 交给 UI；UI 线程独占 `EntryModel` 与 ListView；如必须共享，锁只保护纯数据（不调用 Win32 窗口 API），锁外更新 UI。消息里带 generation/序号，丢弃过期更新（见 B-07）。

#### B-04｜中｜清理线程从非所属线程直接读取 ListView 状态
- **位置**：`clearC.cpp:291-293`（`CleanThread` 里 `ListView_GetItemCount`/`ListView_GetCheckState`）
- **现象**：虽然跨线程 `SendMessage`（LVM_* 宏）通常能工作，但这是 Win32 明确不推荐的线程模型；控件若有 owner-data/通知/未来改动，行为不可预期；与 UI 线程的勾选变更（B-06）也没有同步。
- **修复**：在 `DoClean()`（UI 线程、确认框之前）一次性把"选中项索引快照"取好，作为参数传给 `CleanThread`（`CreateThread` 的 `lpParameter` 或 `std::thread` 捕获）；工作线程完全不接触 HWND/ListView。

#### B-05｜中｜`lvIndex` 双重语义 + 选择映射与 `GetSelectedSize` 重复实现，O(n²) 且易漂移
- **位置**：`clearC.cpp:78`（`int lvIndex`，"ListView 索引，-1 表示不显示"）、`295-300`（`CleanThread` 线性反查）、`371-375`（`GetSelectedSize` 同样的线性反查）、`404-422`（每次重扫重建映射）
- **现象**：同一映射逻辑写了两份；ListView 行 ↔ 条目索引靠 O(n) 搜索；`GetSelectedSize` 在每次 `LVN_ITEMCHANGED`（627-641）都会遍历全部行 × 全部条目。现在 20 项无感，但目标要扩到几十项且动态加载，重复实现极易漂移（一处改了另一处忘改）。
- **修复**：`ListView_InsertItem` 时写 `LVIF_PARAM` 存条目索引（`item.lParam`）；提供唯一 `EntryIndexFromLv(int lv)`（直接 `ListView_GetItemData`）；`GetSelectedSize`、`CleanThread`、全选同步都调用同一函数；模型与视图分离。

#### B-06｜中｜扫描/清理期间只禁用两个按钮，列表与"全选"仍可操作，快照与实时状态不一致
- **位置**：`clearC.cpp:399-400`、`445-446`（只禁用 Scan/Clean）、`616-621`（`ID_BTN_ALL` 始终可用）、`514-519`（ListView 未禁用）
- **现象**：清理线程在开始时对勾选做快照（291-302），但用户可以在清理过程中继续改勾选/点"全选"：UI 的"已选择/共 N 项"与实际清理对象不一致；扫描过程中点"全选"会触发大量通知与重复计算。
- **修复**：busy 时禁用 ListView（或至少禁用勾选交互）与"全选"；或者明确把选择快照显示为"正在清理以下 N 项（清理期间不可修改）"；`SetBusy()` 统一管理所有控件。

#### B-07｜低｜`PostMessage` 返回值不检查、无 generation 版本号、消息量随条目增长
- **位置**：`clearC.cpp:275-278`、`314-315`、`424`/`450`
- **现象**：`PostMessage` 失败（队列满/窗口销毁）时静默丢失，进度/完成消息可能丢；若未来加入"取消后立刻重扫"，旧线程的排队消息可能在新扫描开始后到达，用旧索引污染新列表（当前靠 `g_mtx` 内的 `lvIndex` 校验勉强规避，但不是显式设计）。
- **修复**：消息参数携带 `generation`（每次任务自增），UI 侧校验 generation 不匹配即丢弃；检查 `PostMessage` 返回值；进度消息做合并/节流（例如 50–100 ms 一次，或按字节阈值），不要每个文件一条。

#### B-08｜低｜没有任务状态机与取消/暂停入口；按钮状态在 5 处手工设置
- **位置**：`clearC.cpp:399-400`、`445-446`、`557`、`662-663`、`675-676`
- **现象**：按钮 Enable/Disable 分散在创建、扫描开始、扫描完成、清理开始、清理完成；没有显式 `Idle/Scanning/Cleaning` 状态，任何新增路径（取消、失败、重试）都要再改一遍，容易漏。
- **修复**：`enum class TaskState { Idle, Scanning, Cleaning, Stopping }` + `SetState()` 集中更新按钮/列表/进度/状态文本；取消按钮在 Scanning/Cleaning 时可用。

### 3.3 C 组：状态与错误处理（8 项）

#### C-01｜高｜`WM_CLEAN_DONE` 无条件弹"清理完成！"，没有任何失败统计
- **位置**：`clearC.cpp:673-681`（无条件 `MessageBoxW(..., L"清理完成！")`）、`313`（结果丢弃）
- **现象**：普通权限下清理系统项、文件被占用、只读/ACL 拒绝、路径超长时可能**一个文件都没删**，用户仍看到"清理完成！"，随后自动重扫大小几乎没变，也无法知道原因。这是用户信任度的致命问题，也会掩盖真实 bug。
- **根因**：没有结果模型（A-05），UI 文案与事实无关。
- **修复**：`CleanThread` 回传结构化结果（释放字节、删除文件/目录数、失败列表、跳过列表）；`WM_CLEAN_DONE` 根据结果分三态展示：
  - 全部成功："清理完成：释放 X（Y 个文件）"；
  - 部分成功："释放 X，**N 项失败**（展开查看/写日志）"；
  - 全部失败："未能清理：权限不足/N 个文件被占用…（建议以管理员身份重启）"。
  失败明细持久化到日志（C-08）。

#### C-02｜中｜用"已选大小 == 0"当作"未选择"，0 字节项的合法清理被拒绝且提示错误
- **位置**：`clearC.cpp:430-435`
- **现象**：`ULONGLONG sz = GetSelectedSize(); if (sz == 0) { MessageBox("请先扫描并勾选...") }`。用户勾选了若干**空缓存目录**（或扫描失败显示 0 B 的项）时，点清理被拒绝，提示"请先扫描并勾选"，与事实不符；反过来，扫描前误点"清理"也会得到同一句无指导性的提示。
- **修复**：用"勾选条目数"判断（`ListView_GetSelectedCount` 不行，需自行统计 check state 或用 `LVM_GETITEMSTATE` 计数），0 字节项允许清理（确认文案显示"0 B，仅删除空目录/残留"）；区分"尚未扫描"/"未勾选"/"勾选项不可访问"三种提示。

#### C-03｜中｜确认框"将释放约 X"基于扫描时的逻辑大小，与实际可释放量不一致
- **位置**：`clearC.cpp:437-439`、`158`、`128-139`
- **现象**：`GetSelectedSize` 用的是扫描时的逻辑大小（A-11），且可能包含随后被占用的文件；清理失败时差额没有解释。用户预期"释放 10 GB"，实际可能 0。
- **修复**：文案改为"预计释放（估算）"；同时展示"占用空间/文件大小"两项；清理后展示实际释放（`GetDiskFreeSpaceExW` 前后差 + 结果字节）；无法释放的项在确认前就标注风险。

#### C-04｜中｜清理完成后立即自动重扫，覆盖"清理完成"状态，且没有前后对比
- **位置**：`clearC.cpp:678-680`（`DoScan()` 紧跟 `MessageBox`）
- **现象**：`WM_CLEAN_DONE` 先 `SetWindowText(L"清理完成！")`、弹框，然后在同一处理器里 `DoScan()` 立刻改状态、清空并重建列表；用户错过状态，且重扫会重新统计（可能把刚被系统重建的缓存又算进来）；若重扫卡住，用户看不到清理结果。没有"清理前/后大小"对比。
- **修复**：先从 `CleanThread` 结果生成"清理报告"（释放字节、失败数、跳过数），用户关闭报告后再决定是否重扫（或自动重扫前先显示结果）；重扫复用已有模型做增量更新，而不是全删重建。

#### C-05｜中｜不存在/不可访问/需管理员/已重定向的条目没有状态区分，全部静默隐藏或显示 0
- **位置**：`clearC.cpp:406-422`（只用 `IsDir`，不存在就 `lvIndex=-1` 隐藏）、`271`（只要扫描就 `exists=true`，不区分错误）
- **现象**：不存在的缓存项完全不可见，用户不知道"没装"还是"解析失败"；`GetFileAttributes` 返回 `ERROR_ACCESS_DENIED` 时同样当作不存在；系统项（需管理员）与用户项看起来一样；重定向（junction）项看起来正常（A-02）。排障与信任都受影响。
- **修复**：条目模型增加状态：`Available / Missing / Inaccessible(error) / NeedsAdmin / Redirected(reparseTag) / Protected(skip)`；UI 灰色显示并提供"显示不存在的项"开关、错误 tooltip；对 `NeedsAdmin` 标盾牌图标；对 `Redirected` 标注真实目标路径。

#### C-06｜低｜状态机缺失：状态栏被字号提示覆盖，扫描中无当前项/耗时/ETA
- **位置**：`clearC.cpp:599`（字号提示覆盖状态栏）、`401`、`447`、`665`、`678`（状态文本分散设置）、`277-278`（只有百分比）
- **现象**：Ctrl+滚轮调字号时 `SetWindowTextW(g_hStatus, "字号 N px")` 会覆盖"正在扫描/正在清理"，任务状态丢失；扫描中看不到当前正在统计哪个条目、已处理文件数、耗时/剩余时间。
- **修复**：状态栏拆成两段（任务状态 / 临时提示），或用单独 toast 控件显示字号；扫描中显示"正在统计：<条目名>（已处理 N 个文件 / M MB）"；记录开始时间并显示耗时。

#### C-07｜低｜`exists` 是只写不读的死字段；`lvIndex` 在失败场景残留
- **位置**：`clearC.cpp:77`（字段）、`212`（初始化）、`271`（唯一写入点）；全文件无读取（grep 验证）
- **现象**：`exists` 没有任何消费方；条目状态靠 `lvIndex` 是否为 -1 隐式表达，重扫失败/中途退出时状态不一致；`size` 在清理后到重扫前保持旧值。
- **修复**：删掉死字段；用显式状态枚举（C-05）替代 `lvIndex` 魔法值；模型在清理后立即用结果更新（C-04）。

#### C-08｜中｜无日志、无诊断、无退出码、无 dry-run，线上问题不可复盘
- **位置**：整个文件（没有日志设施）；`696-747`（入口无参数解析）
- **现象**：用户报告"清不掉/删错了"时，没有日志、没有失败路径、没有可复现的扫描输出；开发也无法自测（只能点 UI）。
- **修复**（与 CLI 建议合并）：增加 `--dry-run`（只统计与校验，不删除）、`--scan --json`（机器可读的条目/大小/状态/失败原因）、`--clean <id>`、`--log <path>`；日志写 `%LOCALAPPDATA%\clearC\logs\`；退出码区分成功/部分失败/全部失败/参数错误；这是重构后做自动化验收的基础。

### 3.4 D 组：性能（6 项）

#### D-01｜中｜单线程串行扫描，20 个条目完全串行；大缓存目录会长时间无整体进度
- **位置**：`clearC.cpp:255-282`（单线程 `ScanThread` 顺序遍历）、`216-249`（20 个条目）
- **现象**：`%TEMP%`、`npm-cache`、`NuGet`、`Conda pkgs` 等常含数十万至数百万文件；单线程逐文件递归需要几十秒到数分钟，期间只有"项级"进度。多核机器完全没利用。
- **修复**：按条目并行（线程池，并发度 = min(hardware_concurrency, 4–8)，并按卷限流避免 IO 抖动）；每个条目独立汇报（B-03 的结果槽模式）；保留取消能力。

#### D-02｜中｜每个目录一次 `FindFirstFileW`，未使用 `FindFirstFileExW`/`FIND_FIRST_EX_LARGE_FETCH`/`FindExInfoBasic`；路径字符串反复拼接分配
- **位置**：`clearC.cpp:149`、`170`、`191`；`154`、`174`、`195`
- **现象**：大目录枚举慢（返回短名 8.3 信息、无大缓冲）；每层 `dir + L"\\" + name` 都新建 `std::wstring`，深目录/海量文件时分配压力大。
- **修复**：`FindFirstFileExW(FindExInfoBasic, FindExSearchNameMatch, FIND_FIRST_EX_LARGE_FETCH)`（Win7+）；路径用可复用缓冲（push/pop 后缀，容量预留）；或 `NtQueryDirectoryFile` 大批量获取 `FILE_BOTH_DIR_INFORMATION`，一次拿到属性+大小；统计时顺便累积文件数/分配大小。

#### D-03｜中｜进度按"项"而不是"字节/文件"：单个大项让进度条看起来卡死
- **位置**：`clearC.cpp:277-278`（`scanned*100/existCount`）、`314-315`（`(k+1)*100/total`）
- **现象**：20 项中若 `%TEMP%` 占 90% 文件，进度会 0→5→10…长时间停在个位数；用户以为程序无响应（实际在扫描）。清理时同样一项内部完全没有进度。
- **修复**：组合进度：`(已完成项权重之和 + 当前项内部进度)/总权重`；当前项内部按已处理文件数/字节计（未知总量时用 `PBM_SETMARQUEE` 或"已扫描 N 个文件/M MB"文字）；节流 UI 更新（B-07）。

#### D-04｜中｜清理逐文件串行、无并行、无重试队列、无细粒度进度
- **位置**：`clearC.cpp:288-319`、`164-198`
- **现象**：几十万文件的删除串行执行，且每项只报"完成百分比"；失败文件没有重试队列（A-07），整个清理时间可能数分钟且中止后无法续做（A-12）。
- **修复**：按条目并行（注意同卷写入限流）；文件级进度+可取消；失败项进重试队列（延迟/重启删除）；清理结果实时汇总。

#### D-05｜低｜每次扫描全量重算，清理后整表 `DeleteAllItems` 重建，无增量
- **位置**：`clearC.cpp:404-422`
- **现象**：重扫成本与首次相同；清理完成刚删掉的文件又被重新枚举（如果程序重建缓存）；`CleanThread` 明明知道删了多少，却不用来更新模型。
- **修复**：清理结果直接更新模型（大小差异、状态），重扫作为可选项；扫描结果按条目缓存，带时间戳/增量刷新。

#### D-06｜低｜条目间没有重叠/去重检测，可能重复统计和重复遍历
- **位置**：`clearC.cpp:216-249`
- **现象**：条目可能互相包含（例如把 `%TEMP%` 与位于 `%TEMP%` 下的某个缓存同时列为条目；或用户新增通配条目覆盖已有条目），总大小会重复计算，清理时第二次遍历是空转。
- **修复**：构建条目表时做祖先/后代检测，警告或自动折叠；UI 显示"与某项重叠"；统计只对最具体的条目计数。

### 3.5 E 组：UI/UX（11 项）

#### E-01｜高｜没有管理员检测/提示/提权重启，系统项在普通权限下永远静默失败
- **位置**：`clearC.cpp:216-249`（系统项与用户项混在一起）、`427-451`（清理无权限预检）、`manifest.xml:1-16`（HEAD 版没有 `requestedExecutionLevel`）
- **现象**：`C:\Windows\SoftwareDistribution\Download`、`C:\Windows\Prefetch` 等需要管理员/系统权限；普通权限下删除失败被吞（A-05），UI 报"清理完成"（C-01）。用户以为清理了，实际没动；也没有任何"以管理员身份重启"的入口。HEAD 版 manifest 甚至没有显式声明 `asInvoker`，依赖默认行为（可能触发 installer detection/虚拟化等边缘行为）。
- **修复**：manifest 显式 `asInvoker`（新版已加，见 §10）；启动时 `IsUserAnAdmin()` 检测；条目表标 `adminRequired`；普通权限下系统项置灰+盾牌+说明，或提供"以管理员身份重启"按钮（`ShellExecuteW(L"runas", ...)` 带 `--elevated` 参数，重启后恢复扫描/勾选状态）；提权后仍要处理"服务占用"（A-09）；无论结果如何都要报告失败（C-01）。

#### E-02｜中｜没有最小窗口尺寸，小窗口/大字号下控件重叠、负高度、列宽 ≤ 0
- **位置**：`clearC.cpp:457-485`（`LayoutControls` 无 clamp）、`579-581`（`WM_SIZE` 直接调用，无最小尺寸）、无 `WM_GETMINMAXINFO`
- **现象**：`lvBottom = h - S(110)` 在窗口高度不足时可小于 `lvTop = S(48)` → `MoveWindow` 得到负高度；`col.cx = w - S(350)` 在窄窗口为负；字号放大到 26 px 时 `S()` 翻倍（`26/13`），上述问题更早出现；状态栏/品牌区域固定宽度，窄窗口被裁剪。
- **修复**：加 `WM_GETMINMAXINFO` 返回按 DPI/字号换算的最小尺寸（如 `S(560)×S(430)`）；`LayoutControls` 内对所有宽高做 `std::max(min, ...)` 且列宽下限 `S(80)`；字号变化后重新 clamp 窗口尺寸。

#### E-03｜中｜manifest 声明 PerMonitorV2，但代码完全没有 DPI 处理，"DPI 感知"名不副实
- **位置**：`manifest.xml:12-13`（`dpiAware=true/pm` + `dpiAwareness=PerMonitorV2`）、`clearC.cpp:67`（`FONT_BASE=13` 物理像素）、`326`（`S()` 只按字号缩放，不含 DPI）、`349-354`（字体固定 px）、`706-708`/`723-724`（`GetSystemMetrics`）、`730`（`AdjustWindowRect`，非 `ForDpi` 版本）；无 `WM_DPICHANGED`
- **现象**：在 150%/200% 缩放屏上窗口与字体显得很小；跨显示器拖动不重新缩放（PMv2 要求处理 `WM_DPICHANGED`）；窗口居中用系统指标。按 README"支持 DPI 感知"的说法会误导测试/用户。
- **修复**：用 `GetDpiForWindow` 计算 `scale = dpi/96`，所有尺寸/字体 = 逻辑值 × scale × (字号/FONT_BASE)；处理 `WM_DPICHANGED`（`SetWindowPos` 到建议矩形、重新 `ApplyFonts`、重新布局、更新图标尺寸）；`AdjustWindowRectExForDpi`；`GetSystemMetricsForDpi`/`GetSystemMetrics(SM_CXICON)` 按 DPI 取图标（或 `LoadIconWithScaleDown`）；字号改存逻辑值（DIP）而非 px。

#### E-04｜低｜`WM_CTLCOLORSTATIC` 无条件返回 `COLOR_WINDOW` 画刷 + 透明背景：暗色/高对比度不适配
- **位置**：`clearC.cpp:606-610`；窗口背景 `717`（`(HBRUSH)(COLOR_WINDOW+1)`）
- **现象**：当前默认浅色主题下与窗口背景一致（不算错），但颜色是硬编码系统窗口色：高对比度主题、暗色模式、或未来换父窗口背景时会文字/背景不一致；对所有 STATIC（包括 `SS_ICON` 的品牌图标）都返回画刷；`SS_ICON` 用透明背景本身无必要。没有 `WM_CTLCOLORBTN` 处理（视觉样式下通常无碍）。
- **修复**：保留 `SetBkMode(TRANSPARENT)` 但用 `GetSysColorBrush(COLOR_WINDOW)` 与 `SetBkColor(GetSysColor(COLOR_WINDOW))` 成对；或按控件 ID 区分（图标返回 `NULL`/不处理）；若要支持暗色模式，监听 `WM_SETTINGCHANGE`/`WM_THEMECHANGED` 并重建画刷（或 `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` + 自绘背景）。

#### E-05｜低｜默认无勾选、无分类/排序/分组/反选/实际路径 tooltip/右键菜单
- **位置**：`clearC.cpp:493-537`（列表三列、无排序/分组）、`514-519`（无 `LVS_EX_INFOTIP`/`LVS_EX_DOUBLEBUFFER`）、`625-644`（无 `LVN_COLUMNCLICK`）、无右键
- **现象**：默认全部不勾选，用户必须逐个点（保守但首用体验差）；无法按大小排序/按类别分组；长路径被列宽截断且无 tooltip；没有"反选""仅清理此项""打开所在目录"等操作。
- **修复**：条目表带分类与风险；默认勾选"安全且可再生"的项（临时文件、包管理器缓存），危险/系统项默认不勾；支持点列头排序、`LVS_EX_GROUPVIEW` 分组、"全选/反选"；路径列 tooltip 显示规范化后的真实路径；右键菜单（打开目录、仅清理此项、复制路径）。

#### E-06｜低｜`WM_MOUSEWHEEL` 子类化：固定 16 上限、无 `SetWindowSubclass` 链、未恢复、O(n) 查找
- **位置**：`clearC.cpp:70`（`MAX_SUBCLASS=16`）、`107-109`、`328-343`、`562-566`（当前 9 个控件）
- **现象**：超过 16 个控件后静默不再转发 Ctrl+滚轮；手写子类化没有链式 `SetWindowSubclass` 的健壮性；窗口销毁不恢复（进程退出无影响，但复用于其他窗口时会乱）；每次滚轮 O(n) 查找。
- **修复**：改用 `SetWindowSubclass`/`DefSubclassProc`（comctl32，自动链式）；或用一个 `std::unordered_map<HWND, WNDPROC>` + 动态上限；对未登记的 HWND 直接 `DefWindowProc`。

#### E-07｜低｜"全选"用 `BS_AUTOCHECKBOX` 却设置 `BST_INDETERMINATE`，三态外观不保证
- **位置**：`clearC.cpp:497-499`（`BS_AUTOCHECKBOX`）、`637-640`（`BST_INDETERMINATE`）
- **现象**：`BS_AUTOCHECKBOX` 是两态控件，代码在"部分勾选"时 `BM_SETCHECK(BST_INDETERMINATE)`；视觉样式下可能显示为未勾选，与"部分选中"语义不符（`BS_AUTO3STATE`/`BS_3STATE` 才保证三态显示）。
- **修复**：改为 `BS_AUTO3STATE` 并正确处理 `BST_INDETERMINATE` 的点击语义（点击半选 → 全选或全不选，需自定义）；或干脆不用三态，改为"全选"文字/图标按钮。

#### E-08｜低｜ListView 无 `LVS_EX_DOUBLEBUFFER`（扫描刷新闪烁），大小列未右对齐/无列宽记忆
- **位置**：`clearC.cpp:514-537`
- **现象**：扫描时频繁 `LVM_SETITEMTEXT` 会闪烁；"大小"列左对齐不便于比较；列宽每次按窗口重算，用户手动调整后会被 `LayoutControls` 覆盖。
- **修复**：加 `LVS_EX_DOUBLEBUFFER`；`LVCFMT_RIGHT` 右对齐大小列；列宽存配置，只有在窗口尺寸变化超过阈值时才重算。

#### E-09｜低｜无键盘快捷键、无 `IsDialogMessage`（Tab 无法遍历控件）、无焦点顺序/可访问性
- **位置**：`clearC.cpp:739-743`（消息循环只有 `TranslateMessage`/`DispatchMessage`，没有 `IsDialogMessage`）、按钮无 `&` 助记符
- **现象**：纯键盘用户无法操作（Tab 不移动焦点、按钮无访问键），Esc/Enter 无行为；自动化测试也难。
- **修复**：`IsDialogMessage` 或 `SetFocus`/Tab 处理；按钮加助记符（`&扫描`）；`WM_KEYDOWN` 处理 Enter/Esc/F5；给控件设置 `AccessibleName`（或用标准控件默认）。

#### E-10｜低｜窗口固定 760×540、不记忆位置/大小、多显示器时总在主屏居中
- **位置**：`clearC.cpp:62-63`、`723-734`
- **现象**：高 DPI/小屏/大字号时窗口尺寸不合适；每次启动位置固定；多显示器/不同工作区下居中到主屏，可能跑到屏幕外（`SM_CXSCREEN` 虚拟化语义也不准）。
- **修复**：用 `MonitorFromWindow`/`GetMonitorInfo` 的工作区居中；记忆窗口位置/大小/字号到配置；`WM_GETMINMAXINFO` 约束在当前显示器范围内。

#### E-11｜低｜品牌图标以 24 px STATIC 承载 32 px 图标，可能被裁剪；状态/品牌 Static 固定宽度
- **位置**：`clearC.cpp:467-469`（`BRAND_ICON=24`）、`502-511`（`LoadImage` 用 `SM_CXICON=32`）、`540-546`（固定宽度）
- **现象**：`SS_ICON` 以图标原始尺寸绘制，24 px 控件里放 32 px 图标会裁边/错位；窗口变窄时状态文本/品牌文本被截断。
- **修复**：按控件尺寸加载对应图标（`LoadImage(..., 24, 24)` 或 `LoadIconWithScaleDown`）；品牌文本/状态用自适应宽度或右对齐布局。

### 3.6 F 组：构建、文档与一致性（11 项，行号基于 HEAD 版本）

#### F-01｜高｜`build.bat` 不 cd 到脚本目录、硬编码 `D:\msys64`、无编译器探测
- **位置**：`build.bat:9`（`set "MSYS2=D:\msys64\ucrt64\bin"`）、`11-15`、`23`
- **现象**：从任意其他目录运行（双击快捷方式、IDE 任务、CI）时：`windres resource.rc`/`g++ clearC.cpp` 找不到文件，`clearC.exe`/`resource.o` 落在调用者 CWD；机器未装在 `D:\msys64`（C 盘安装、自定义路径、只有 PATH 中的编译器）时直接失败；没有 `setlocal`，`PATH` 修改会泄漏到调用者 shell。
- **实测**：把 HEAD 版 `build.bat` 复制到空临时目录（无源码）运行 → `cc1.exe: fatal error: resource.rc: No such file`、`cc1plus.exe: fatal error: clearC.cpp: No such file`（§6.2）。
- **修复**：`@echo off` + `setlocal` + `cd /d "%~dp0"`；编译器探测（`where g++`/`windres` → `%MSYS2_ROOT%` → 常见根目录），失败时给明确指引；产物统一放 `build\`；新版 build.bat 已修复该项（§10），但请保留验收。

#### F-02｜高｜`%ERRORLEVEL%` 在括号块内是"解析期展开"，编译失败仍打印"编译成功"并启动旧 exe
- **位置**：`build.bat:11-28`（外层 `if exist ... (` 块内的 `if %ERRORLEVEL% NEQ 0`）
- **现象**：整个 `if exist (...)` 块是**一条复合命令**，`%ERRORLEVEL%` 在块解析时就被替换（此时还没跑 windres/g++）。所以两个错误检查永远不会命中真实失败；脚本继续 `del resource.o`、打印"编译成功: clearC.exe"、`start clearC.exe`——可能启动的是**上一次构建的旧 exe**，用户以为新代码已生效。
- **实测**：CRLF 化后的 `build.bat` 副本在缺源码的空目录运行，windres 与 g++ 均 fatal error，但 stdout 输出"编译成功: clearC.exe"并尝试启动（§6.2）。附独立对照实验：`cmd /c exit 5` 后 `if %ERRORLEVEL% NEQ 0` 在块内被判定为"0，未失败"。
- **修复**：用 `if errorlevel 1`（执行期判定）或 `命令 || goto :fail`；或开启 `setlocal EnableDelayedExpansion` 用 `!ERRORLEVEL!`；把每步封装成 `call :step` 子过程。新版 build.bat 已改为 `if errorlevel 1 goto fail`（§10），请用"故意让编译失败"的方法验收。

#### F-03｜高｜`build.bat` 是 LF-only 行尾 + UTF-8 中文 + `chcp 65001`，cmd.exe 会解析错位
- **位置**：`build.bat`（仓库 index 与工作区均为 LF；`.gitattributes` 不存在；`core.autocrlf=false`）
- **现象**：cmd 在处理"UTF-8 无 BOM + LF-only + `chcp 65001`"的 batch 时会行位置错乱：中文行被拆成片段当命令执行（实测错误：`'MSYS2' is not recognized`、`'中文行' is not recognized`、`'otepad.exe" (' is not recognized`），括号块/分支控制流被打乱——HEAD 版脚本在不同 CWD 下的实际输出既乱码又跳过/混合执行分支。
- **实测**：§6.2（CRLF 版输出干净但误报成功；LF 版解析错乱；最小 UTF-8 中文 batch 对照实验 CRLF 正常 / LF 报错）。
- **修复**：①加 `.gitattributes`：`*.bat text eol=crlf`（并 `git add --renormalize .`）；②batch 内只用 ASCII 文本（错误信息用英文或调用 PowerShell 显示中文），这样 LF 下也能解析（已实测 ASCII + LF + goto/括号块正常）；③或用 PowerShell/cmake 替代 batch。新版 build.bat 已改为全英文、LF-only（当前实测可用），但 `.gitattributes` 仍缺，未来一旦加中文或按 autocrlf 签出就会复发，建议一并修掉。

#### F-04｜中｜源码头注释的编译命令缺 `-municode`，按注释编译会链接失败
- **位置**：`clearC.cpp:4-6`（注释里的 `g++ ... -mwindows -lcomctl32 ...`），对比 `build.bat:23` 与 `README.md:31-32` 都有 `-municode`
- **现象**：源码用 `wWinMain`（`clearC.cpp:696`）；MinGW 未加 `-municode` 时按 `WinMain` 入口链接。
- **实测**：按头注释命令链接 → `undefined reference to 'WinMain'`（§6.4）。
- **修复**：统一三处编译命令（头注释补 `-municode`）；或干脆移除头注释里的命令，只指向 README/build 脚本，避免第三份真相。

#### F-05｜中｜README 的 MSVC 命令缺 `/utf-8`、`/EHsc`，且未验证；源文件 UTF-8 无 BOM
- **位置**：`README.md:37-42`（`cl /std:c++17 /O2 clearC.cpp resource.res /link ...`）；源文件无 BOM（审计已确认）
- **现象**：MSVC 在没有 `/utf-8` 时会用系统 ANSI 代码页（中文 Windows 为 GBK）解释源码，源码里的 UTF-8 中文宽字符串会变成乱码（界面显示"锟斤拷"之类）；`std::mutex`/异常相关代码缺 `/EHsc` 会报 C4530。该命令也从未被验证（仓库没有 CI/MSVC 工程）。
- **修复**：README 命令补 `/utf-8 /EHsc /W4`（或 `/source-charset:utf-8 /execution-charset:utf-8`），并给出"仅支持 MinGW 构建"的明确说明或加 CMake/vcxproj + CI；源文件加 BOM 或 `.editorconfig` 统一 UTF-8。

#### F-06｜低｜README 声称"19 类"，实际 20 条；源码头注释遗漏 `CachedProfileData`
- **位置**：`README.md:9-13`；`clearC.cpp:8-11`
- **现象**：`AddEntry` 实际 20 次调用（`clearC.cpp:223-248`：包管理器 8 + VSCode 7 + CMake 1 + 系统 4 = 20），README 写"19 类"，其自身列表也是 8+7+1+4=20；源码头注释的 VSCode 列表只写 6 项（漏 `CachedProfileData`，`clearC.cpp:236`）。
- **修复**：README 改为"20 类"或改为不写死数量（指向条目表）；源码注释同步；重构后条目数量应自动统计（动态条目表），避免再次漂移。

#### F-07｜低｜未使用的头文件与链接库（实测无相关导入符号）
- **位置**：`clearC.cpp:23-24`（`shlobj.h`/`shlwapi.h` 引入但未使用）、`build.bat:23`（`-lshlwapi -lole32 -luuid`）、`README.md:39-41`（`shlwapi.lib shell32.lib ole32.lib`）
- **现象**：实际逻辑没有调用 SHLWAPI/OLE/Shell API；链接这些库只是增加噪音（不影响体积，静态链接器会丢弃）。新版若无 COM 调用也不需要。
- **实测**：`objdump -p` 显示新构建产物只导入 `COMCTL32/GDI32/KERNEL32/USER32/UCRT api-set`，无 SHLWAPI/OLE32/SHELL32 符号（§6.3）。
- **修复**：移除未使用的 include/lib；如果后续用 `SHGetKnownFolderPath`/`IFileOperation` 再按需引入，并同步 README/build。

#### F-08｜中｜manifest：HEAD 版缺 `requestedExecutionLevel`/`longPathAware`/`supportedOS`；DPI 声明与实现不一致
- **位置**：`manifest.xml:1-16`（HEAD 版）；`clearC.cpp` 无 DPI 处理（E-03）
- **现象**：HEAD 版没有显式 `asInvoker`（依赖默认行为，可能触发 installer detection/虚拟化等边缘策略），没有 `longPathAware`（A-04 的长路径在系统层仍会受限），没有 `<compatibility>` 的 supportedOS（`GetVersionEx`/DPI 相关行为在旧系统上不一致）；`dpiAware=true/pm` + `dpiAwareness=PerMonitorV2` 本身是微软推荐的**回退组合**（不是矛盾），但代码没有处理 `WM_DPICHANGED`，实际表现为"声明了 PMv2 却没有 PMv2 行为"；HEAD 版也没有版本资源。
- **修复**：显式 `asInvoker` + `longPathAware` + `supportedOS`（Win10/11 GUID）+ `VERSIONINFO`；实现 E-03 或把 README 的"DPI 感知"改成准确描述。新版 manifest 已加 `asInvoker`/`longPathAware`/版本号（§10），仍需补 supportedOS/版本资源（新版 `resource.rc` 已加 VERSIONINFO，见 §10）。

#### F-09｜低｜仓库根目录存在未跟踪的 `clearC.exe`；产物与源码目录混放
- **位置**：仓库根 `clearC.exe`（962,063 B，`.gitignore:2` 已忽略，未被 git 跟踪）；`build.bat` 输出到仓库根
- **现象**：二进制易与源码混淆/过期；审计期间实测该 exe 与"当前源码 + 本机工具链完整重建"的产物**只差 5 字节**（PE TimeDateStamp 3 字节 + Checksum 2 字节），说明它确实对应这份源码（不是可疑的旧版本），但把产物放在源码目录仍不符合工程化习惯。
- **修复**：构建输出统一到 `build\`（可在 `.gitignore` 加 `build/`）；发布产物另行归档；CI 里校验"源码→二进制"可复现性（`-ffile-prefix-map`/`SOURCE_DATE_EPOCH` 或至少记录工具链版本）。

#### F-10｜低｜无 `LICENSE` 文件（README 声称 MIT）、无 `.gitattributes`/`.editorconfig`/CI、无版本资源
- **位置**：`README.md:69-71`；仓库根缺失文件
- **现象**：README 的 MIT 声明没有对应 LICENSE 文本；行尾/编码没有版本控制约束（直接导致 F-03）；没有 CI 编译/告警门禁；HEAD 版 exe 无版本信息（属性页空白），新版 `resource.rc` 已补 VERSIONINFO（§10）。
- **修复**：补 LICENSE、`.gitattributes`（bat CRLF、cpp/README LF、`* text=auto`）、`.editorconfig`；加 GitHub Actions/MinGW 构建 + `-Werror`；补版本资源并让版本号与 tag 同步。

#### F-11｜低｜资源 ID 冲突核查：**未发现冲突**（此项为非问题，列出以回答审计要点）
- **位置**：`resource.h:4`（`IDI_APPICON 101`）、`resource.rc:3`（`1 24 "manifest.xml"`）、`clearC.cpp:50-59`（控件 ID 1001–1008）
- **结论**：`RT_MANIFEST` 用资源 ID 1、类型 24；图标组用 ID 101；`.ico` 的多尺寸 `RT_ICON` 由 windres 生成、类型 3，与 RT_MANIFEST 不同类型空间；控件 ID 1001–1008（1004 空号）与资源 ID 不在同一命名空间。**无冲突，无需修改。**

### 3.7 已核验的非问题清单（避免误改）

| 位置 | 结论 |
| --- | --- |
| `clearC.cpp:646-654` | `WM_SCAN_UPDATE` 中操作 ListView 的是 **UI 线程**（消息由扫描线程 `PostMessage`），线程归属正确；相关问题在别处（B-03/B-07） |
| `clearC.cpp:149-160`、`170-177`、`191-197` | `FindFirstFile` 句柄在正常/提前返回路径上都有 `FindClose`；但异常/`bad_alloc` 路径没有 RAII（并入 B-01/A-10） |
| `clearC.cpp:277-278` | `scanned*100/existCount` 不会除零：`existCount==0` 时循环体不会执行（`lvIndex<0` 全部 continue） |
| `clearC.cpp:683-687`、`744-745` | 字体/图标资源在 `WM_DESTROY`/退出时释放，无长期泄漏（每次 `ApplyFonts` 会删旧建新，可接受） |
| `resource.*` / 控件 ID | 资源 ID 无冲突（F-11） |
| `.gitignore:2` | `*.exe` 确实覆盖根目录 `clearC.exe`（`git check-ignore` 验证），二进制未被误提交 |
| `manifest.xml` 依赖 + `clearC.cpp:697-700` | Common Controls v6 依赖与 `InitCommonControlsEx(ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES)` 配对正确 |
| `build.bat:23` / `README.md:31-32` | `-mwindows -municode -static` 组合本身正确（完整的 exe 已成功构建，仅头注释漏写 `-municode`） |

---

## 4. 审计要点逐项对照

| 任务重点 | 结论 | 条目 |
| --- | --- | --- |
| 1) 删除安全（junction/链接/重解析点、危险路径、MAX_PATH、属性、占用、FileID 重定向） | **有严重缺陷**：确实跟随 junction（实测）、无路径护栏、MAX_PATH 截断、错误全吞；属性处理不完整 | A-01…A-12 |
| 2) 线程与消息（数据竞争、g_entries/lvIndex、提前 return 无 DONE、窗口销毁、WM_SCAN_UPDATE 线程归属） | 有 1 高 2 中：生命周期守卫缺失、关闭窗口线程失控、锁覆盖不完整、工作线程读 ListView；`WM_SCAN_UPDATE` 线程归属正确 | B-01…B-08 |
| 3) 状态与错误处理（失败被吞、前后统计不一致、重复实现、空选择、重扫时机） | 失败全吞 + 谎报成功是最严重；`GetSelectedSize`/`CleanThread` 重复；0 字节选择被拒；清理后立即重扫 | C-01…C-08、B-05 |
| 4) 性能（串行、每文件 FindFirstFile、无 LARGE_FETCH、无并行清理、按项进度） | 全部存在：单线程、无 `FindFirstFileExW`、无并行、进度按项 | D-01…D-06 |
| 5) UI/UX（默认不勾选、排序分类、失败提示、管理员、CTLCOLOR、缩放/最小尺寸、滚轮上限） | 全部存在（管理员缺失为高）；`WM_CTLCOLORSTATIC` 当前与背景一致但硬编码；无最小尺寸/DPI 处理 | E-01…E-11 |
| 6) 构建与一致性（硬编码、未 cd、-municode、README 19 类、.gitignore/exe、manifest dpi、资源 ID） | build.bat 三处高（路径/错误检查/行尾）；-municode 三处不一致；README 19 vs 实际 20；资源 ID 无冲突；manifest 声明过度 | F-01…F-11 |
| 7) 编译警告实测 | `-fsyntax-only` 0 条；完整 `-Wall -Wextra` 1 条；附加告警 6 条 | §5 |
| 8) 重构建议清单 | 见 §7（必须修/建议/可选）与 §9 验收清单 | R-01… |

---

## 5. 编译器警告实测

### 5.1 环境与命令（按要求）

```bat
set "PATH=D:\msys64\ucrt64\bin;%PATH%"
g++ --version
:: g++.exe (Rev5, Built by MSYS2 project) 16.1.0

g++ -std=c++17 -Wall -Wextra -fsyntax-only "D:\Program Files\clearC\clearC.cpp"
```

**结果：exit code 0，无任何输出（0 条警告）。**

说明：为排除路径/编码干扰，另外做了两组对照，结论一致：
- 直接把源文件复制到 `%TEMP%\clearC_audit_src.cpp`、加 `-I "D:\Program Files\clearC"`（提供 `resource.h`）跑同一命令 → **exit 0，0 条警告**；
- 该副本与仓库源逐字节一致（`clearC.cpp` 未修改，blob 哈希 `40bb4927…`），行号与原文件相同，因此**结论对原文件有效**。路径中的空格/中文没有造成问题，无需担心。

### 5.2 完整编译（同 `-Wall -Wextra`，在 `%TEMP%\clearC_audit\` 中）

```bat
windres resource.rc -o resource.o
g++ -std=c++17 -O2 -Wall -Wextra -o clearC_audit.exe clearC.cpp resource.o ^
    -mwindows -municode -lcomctl32 -lshlwapi -lole32 -luuid -static
```

`windres` exit 0；`g++` exit 0，输出 **1 条**警告（原文）：

```text
clearC.cpp:93:13: warning: 'g_hBtnNone' defined but not used [-Wunused-variable]
   93 | static HWND g_hBtnNone   = nullptr;
      |             ^~~~~~~~~~
```

归因：`clearC.cpp:93` 的 `g_hBtnNone` 是早期"全不选"按钮的残留变量（当前只有 `g_hBtnAll`，`clearC.cpp:93`/`497-499`），属于死代码。修复：删除该变量（或加 `[[maybe_unused]]`，但更好是删掉）。

### 5.3 附加静态检查（用于判断"是否需要更高门禁"）

| 命令 | 结果 |
| --- | --- |
| `g++ -std=c++17 -Wall -Wextra -Wshadow -Wformat=2 -fsyntax-only …` | exit 0，**0 条** |
| `g++ -std=c++17 -Wall -Wextra -Wconversion -Wsign-conversion -fsyntax-only …` | **6 条**（见下） |
| `g++ -std=c++17 -O0 -Wall -Wextra -fanalyzer -c …` | exit 0，**0 条**（GCC 分析器对 Win32 调用建模有限，未发现额外问题；不代表无缺陷） |

`-Wconversion/-Wsign-conversion` 的 6 条原文（均已归因）：

```text
clearC.cpp:133:39: warning: conversion from 'ULONGLONG' to 'double' may change value [-Wconversion]
clearC.cpp:135:39: warning: conversion from 'ULONGLONG' to 'double' may change value [-Wconversion]
clearC.cpp:137:39: warning: conversion from 'ULONGLONG' to 'double' may change value [-Wconversion]
clearC.cpp:179:53: warning: unsigned conversion from 'int' to 'long unsigned int' changes value from '-2' to '4294967294' [-Wsign-conversion]
clearC.cpp:183:53: warning: unsigned conversion from 'int' to 'long unsigned int' changes value from '-2' to '4294967294' [-Wsign-conversion]
clearC.cpp:312:29: warning: conversion to 'std::vector<int>::size_type' from 'int' may change the sign of the result [-Wsign-conversion]
```

- 133/135/137：`FormatSize` 的 `b / 1024.0` 把 `ULONGLONG` 转 `double`，显示用途可接受；若要严格，改用整数除法 + 余数格式化（不必修）。
- 179/183：`attr & ~FILE_ATTRIBUTE_READONLY` 中 `~` 是 `int` 取反产生负值，再转 DWORD 得到正确位掩码；**行为正确但类型不严谨**，建议 `attr & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY)`（低优先级）。
- 312：`int i = selEntryIdx[k]` 的 `int`/`size_t` 转换，建议 `size_t` 或 `static_cast<int>`（低优先级）。

### 5.4 结论

- 按任务要求的命令：**0 条警告**；完整编译：**1 条**（死变量 `g_hBtnNone`）；附加严格告警：6 条（均非致命）。
- `-fsyntax-only` 不生成部分后端相关警告（如 `-Wunused-variable`），所以"0 警告"有误导性。**建议 CI 门禁用 `-c`（而非 `-fsyntax-only`）+ `-Wall -Wextra -Werror`**，并叠加 clang-tidy/cppcheck（本机未安装）与 §9 的自动化夹具。
- 编译器警告在本项目**不能覆盖主要风险**：junction 跟随、路径护栏、错误吞掉、线程生命周期、UI 谎报成功、构建脚本缺陷，全部是编译器静默的。

---

## 6. 关键行为的实测证据

### 6.1 junction 跟随导致删除缓存目录之外的数据（严重，A-01/A-02）

方法：把 `clearC.cpp:146-198` 的 `CalcDirSize`/`DeleteRecursive`/`CleanDirContents` **逐字复制**到 `%TEMP%\clearC_junction_final\final.cpp`（仅加一个测试 `wmain`），用 `cmd /c mklink /J` 创建目录 junction，全部操作限制在 `%TEMP%` 测试目录内。

复现步骤（可重跑，见 §11 附录）：

```bat
mkdir %TEMP%\clearC_junction_final\root1 %TEMP%\clearC_junction_final\victim1
echo important user data > %TEMP%\clearC_junction_final\victim1\IMPORTANT_DATA.txt
mklink /J %TEMP%\clearC_junction_final\root1\link %TEMP%\clearC_junction_final\victim1
:: 编译 exact-copy harness（源码见附录）
g++ -std=c++17 -O1 -municode -o final.exe final.cpp
final.exe %TEMP%\clearC_junction_final
```

实际输出（2026-11-10 本机）：

```text
=== CASE 1: junction inside cache dir (root\link -> victim) ===
  junction target file created: 19 bytes
    FindData: attr=0x00000410 REPARSE=1 tag=0xA0000003
  CalcDirSize(root1) = 19  <-- counts victim1 content outside root1
  after CleanDirContents(root1):
    victim1\IMPORTANT_DATA.txt exists = 0  <-- 0 means DELETED THROUGH JUNCTION
    junction root1\link exists = 0
    victim1 dir exists = 1
=== CASE 2: cache entry path itself is a junction (entry -> victim2) ===
  CalcDirSize(entry2) = 14  (IsDir(entry2)=1)
  after CleanDirContents(entry2): victim2\DATA2.txt exists = 0 ; junction entry2 exists = 1
```

结论：
1. `FindFirstFileW` 的 `dwFileAttributes` 已经给出 `0x410`（`DIRECTORY|REPARSE_POINT`）和 `dwReserved0=0xA0000003`（`IO_REPARSE_TAG_MOUNT_POINT`）——**检测是可行的**，只是代码没查；
2. 缓存目录内的 junction 使统计把**外部** 19 字节计入，`CleanDirContents` 真实删除了 **junction 目标里的用户文件**（CASE 1）；
3. 缓存项自身是 junction 时（如 `%TEMP%` 被迁移），删除的是**目标目录内容**，junction 保留（CASE 2）——跨卷/跨盘数据丢失场景成立；
4. 补充探针：junction 的 `GetFileAttributesW` 返回 `0x410`（REPARSE=1），`CreateFileW(..., FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS)` + `GetFileInformationByHandleEx(FileAttributeTagInfo)` 返回 `attr=0x410, tag=0xA0000003`，`RemoveDirectoryW` 只删除 junction 本身、保留目标目录——可作为修复后的安全删除路径。

> 说明：本机不能创建目录符号链接（`mklink /D` 报"权限不足"），符号链接未实机测试；其检测接口（`FILE_ATTRIBUTE_REPARSE_POINT`/`FILE_ATTRIBUTE_TAG_INFO`）与 junction 相同，修复方案同样适用。

### 6.2 build.bat 行为实测（F-01/F-02/F-03）

方法：把 HEAD 版 `build.bat`（blob `4117848b…`，1257 B）复制到 `%TEMP%\bat_build_probe\` 空目录（不含 `clearC.cpp`/`resource.rc`，用于模拟"从错误 CWD 运行"），`cmd /c build.bat <nul` 运行并捕获 stdout/stderr。

**结果 1（LF-only 原文件 + UTF-8 中文 + `chcp 65001`）：解析错位、控制流混乱**

```text
stdout: 资源编译失败!
        （随后混入 else 分支的手工命令文本）
stderr: '本' is not recognized as an internal or external command,
        'MSYS2' is not recognized as an internal or external command,
        '文件...' is not recognized as an internal or external command,
        '未找到' is not recognized as an internal or external command,
        cc1.exe: fatal error: resource.o: No such file or directory
```

**结果 2（同一文件仅把换行改成 CRLF）：输出干净，但"编译失败仍报成功"**

```text
stdout:
[1/2] 编译资源文件...
[2/2] 编译主程序...
编译成功: clearC.exe
stderr:
cc1.exe: fatal error: resource.rc: No such file or directory
D:\msys64\ucrt64\bin\windres: preprocessing failed.
cc1plus.exe: fatal error: clearC.cpp: No such file or directory
The system cannot find the file clearC.exe.
```

→ `%ERRORLEVEL%` 错误检查（`build.bat:16`、`24`）从未触发：windres、g++ 都失败，脚本仍打印"编译成功"并执行 `start clearC.exe`。这同时验证了 F-01（未 cd、找不到文件）与 F-02（错误检查失效）。

**结果 3（最小对照实验，隔离行尾/编码因素）**

- UTF-8 中文 + `chcp 65001` + **CRLF** → 正常：输出三行中文，exit 0。
- 同一内容 **LF-only** → 解析错位：`'中文行' is not recognized`、`'otepad.exe" (' is not recognized`、`'ho' is not recognized`，且括号块内只执行了部分行。
- 纯 ASCII + LF-only + `goto`/括号块 → **正常**（`goto`/标签/`if errorlevel` 均可工作）。

结论：F-03 的触发条件是"非 ASCII + LF-only + 代码页切换"；新版 build.bat 已改为全英文（规避了当前触发条件），但 `.gitattributes` 仍缺失，属于"未爆炸的雷"，建议修根因而非只改文案。

### 6.3 产物一致性（F-09/F-07）

- 仓库根 `clearC.exe`：SHA-256 `4EE74DE3…AACE8`，962,063 B。
- `%TEMP%\clearC_audit\` 用当前源码 + 本机工具链完整重建：SHA-256 `EFC1D255…C8F6A`，**同样 962,063 B**。
- 逐字节比较：**仅 5 字节不同**，位置 `0x88-0x8A`（PE `TimeDateStamp`，3 字节）与 `0xD8-0xD9`（PE Checksum，2 字节）。
→ 仓库内 exe 与当前 HEAD 源码**一致**（不是过期的可疑二进制）；差异仅来自链接时间戳/校验和。仍建议产物移出源码目录（F-09）。
- `objdump -p` 导入表：`COMCTL32.dll / GDI32.dll / KERNEL32.dll / USER32.dll / api-ms-win-crt-*`；**无 SHLWAPI/OLE32/SHELL32 符号** → F-07 成立（`-lshlwapi -lole32 -luuid` 未被使用）。同时说明 `-static` 只静态了 libgcc/libstdc++，UCRT 仍是 api-set 动态依赖（README"无外部依赖"的说法在 Win7 等旧系统上不准确）。

### 6.4 `-municode` 缺失复现（F-04）

按 `clearC.cpp:4-6` 头注释的命令（去掉 `-municode`，其余相同）链接：

```text
ld.exe: .../libmingw32.a(lib64_libmingw32_a-crtexewin.o): in function `main':
crtexewin.c:62: undefined reference to `WinMain'
collect2.exe: error: ld returned 1 exit status
```

→ 头注释命令确实不可用，三处编译命令应统一（F-04）。

---

## 7. 重构建议清单

> 优先级：**P0 必须修**（发版前必须完成）、**P1 建议**（本次重构尽量做）、**P2 可选**（排期允许再做）。每条给出对应问题编号。

### 7.1 必须修（P0）

| 编号 | 建议 | 对应问题 |
| --- | --- | --- |
| R-01 | 重写删除/统计原语：**白名单根 + 规范化 + 受保护目录拒绝 + 根/子项非重解析 + 不越界**；删除链接只删链接本身或跳过；云占位符跳过；每个失败带原因返回 | A-01、A-02、A-03、A-06、A-08 |
| R-02 | 长路径支持：去掉 `MAX_PATH` 定长缓冲，统一 `std::wstring` + `\\?\` 转换 + manifest `longPathAware`；超长/规范化失败显式报错 | A-04 |
| R-03 | 结果化错误模型：`OpResult`（成功/失败/跳过、字节、文件数、失败列表）；UI 汇总+逐项状态+日志；成功文案只在零失败时出现 | A-05、A-07、C-01、C-05、C-08 |
| R-04 | 任务生命周期：状态机 `Idle/Scanning/Cleaning/Stopping`；RAII 完成守卫（异常/提前退出也发 DONE）；检查 `CreateThread`；`CloseHandle`；关闭窗口取消+等待；增加取消按钮 | B-01、B-02、B-06、B-08、C-06 |
| R-05 | 管理员：manifest 显式 `asInvoker` + `IsUserAnAdmin()` + 条目 `adminRequired` 标记 + "以管理员身份重启"（runas，保留选择状态）+ 普通权限下系统项置灰 | E-01、A-09、F-08 |
| R-06 | 数据驱动条目表（动态 `std::vector`，不用 `MAX_ENTRIES`）：`id/name/category/pathTemplates(支持 `*`)/adminRequired/defaultChecked/risk/skipReparse`；条目数量从表自动统计 | A-09、A-10、F-06、D-06 |
| R-07 | 构建链路修复：`cd /d "%~dp0"` + 编译器探测 + `if errorlevel 1 goto :fail`（或 `||`）+ `setlocal` + 产物到 `build\`；`.gitattributes` 固定 bat CRLF（或全 ASCII）；README/头注释/build 三处命令统一（补 `-municode`，MSVC 补 `/utf-8 /EHsc` 或声明仅 MinGW） | F-01、F-02、F-03、F-04、F-05、F-09、F-10 |
| R-08 | 清理前后一致性：确认文案改"预计释放（估算）"；清理结果回传实际释放字节 + `GetDiskFreeSpaceExW` 前后差；清理后先展示报告再重扫（或增量更新） | C-03、C-04、A-11、D-05 |
| R-09 | 危险系统项策略：`%SystemRoot%` 动态获取；Prefetch 默认不勾/移除；SoftwareDistribution 需服务状态检查或走官方清理接口；每项显示风险与管理员要求 | A-09、E-01 |
| R-10 | 空选择判定修复：按"勾选条目数"而不是大小；允许 0 字节项；三种提示区分（未扫描/未勾选/不可访问） | C-02 |

### 7.2 建议（P1）

| 编号 | 建议 | 对应问题 |
| --- | --- | --- |
| R-11 | 并行扫描：按条目并行（线程池，按卷限流），结果槽 + generation 消息；可取消 | D-01 |
| R-12 | 枚举性能：`FindFirstFileExW(FindExInfoBasic, FIND_FIRST_EX_LARGE_FETCH)`，路径复用缓冲，统计一次拿到文件数/字节/分配大小 | D-02、A-11 |
| R-13 | 组合进度：条目权重 + 当前项文件/字节进度，节流刷新（50–100 ms），当前条目名/耗时/ETA | D-03、D-04、C-06 |
| R-14 | 清理优化：并行/重试队列（sharing violation 退避）、可选 `MOVEFILE_DELAY_UNTIL_REBOOT`、Restart Manager 占用检测；或改用 `IFileOperation` + `FOF_NORECURSEREPARSE` | A-07、D-04 |
| R-15 | DPI 正确实现：`WM_DPICHANGED` + `GetDpiForWindow` + 逻辑单位（DIP）+ `AdjustWindowRectExForDpi` + `WM_GETMINMAXINFO` 最小尺寸，字号缩放后 clamp 布局 | E-02、E-03、E-10 |
| R-16 | UI 增强：分类/分组/排序、默认勾选安全项、真实路径 tooltip、右键（打开目录/仅清理此项）、失败状态列/图标 | E-05、C-05 |
| R-17 | 通配路径展开（含中间段 `*`）：`%LOCALAPPDATA%\JetBrains\*\caches`、Chrome `*\Cache` 等；按目录枚举匹配、去重、标记匹配数量；**仅在白名单根内展开** | A-03、D-06 |
| R-18 | 配置化：条目表/排除规则可由内置默认 + 用户 JSON/INI 覆盖；日志目录/保留策略可配 | R-06、C-08 |
| R-19 | 单实例 + 崩溃恢复 + 退出清理：互斥命名对象、`WM_QUERYENDSESSION`、崩溃后残留状态提示 | A-12、B-02 |
| R-20 | 可测试性：把"路径校验/重解析检测/通配展开/大小格式化/结果汇总"抽成不依赖 HWND 的纯函数，配单元测试；Win32 UI 层用 CLI 模式覆盖 | C-08、§9 |

### 7.3 可选（P2）

| 编号 | 建议 |
| --- | --- |
| R-21 | CLI 自测模式：`--dry-run`（只扫描+校验）、`--scan --json`、`--clean <id>`、`--log`、退出码；CI 用夹具（junction、符号链接、长路径、只读/占用、空/受保护路径）全自动验收 |
| R-22 | 日志/诊断：`%LOCALAPPDATA%\clearC\logs\`，记录扫描摘要、删除失败明细、耗时；可选 `--verbose` |
| R-23 | 暗色模式/高对比度：主题画刷、`WM_THEMECHANGED`、`DwmSetWindowAttribute`（E-04） |
| R-24 | 可访问性/键盘：`IsDialogMessage`、助记符、Esc/Enter/F5、屏幕阅读器名称（E-09） |
| R-25 | 工程化：LICENSE、`.editorconfig`、CI（MinGW + `-Werror` + 单测 + CLI 夹具）、版本资源与 tag 同步、可复现构建（F-10/F-09） |
| R-26 | 发布形态：便携 zip/安装包、签名、自动更新（视需求） |
| R-27 | 回收站/撤销：可选 `IFileOperation` 回收站模式（权衡：占用空间，不算"清理"） |

### 7.4 建议的接口/数据结构骨架（可直接落地）

```cpp
// 条目定义：内置默认表，运行期展开为 dynamic 实例
struct CacheEntryDef {
    const wchar_t* id;            // 稳定标识，用于 CLI/配置
    const wchar_t* name;          // 显示名
    Category       category;      // 包管理器/IDE/构建/系统/应用
    Risk           risk;          // Safe / Caution / Dangerous
    bool           adminRequired;
    bool           defaultChecked;
    bool           skipReparse;   // 根路径是重解析点时默认跳过
    std::vector<std::wstring> pathTemplates;  // 支持中间段 *
};

enum class EntryState { Available, Missing, Inaccessible, NeedsAdmin, Redirected, Protected };

// 统一入口（所有扫描/删除都必须过这关）
struct TargetValidation {
    std::wstring canonical;     // GetFullPathNameW/PathCchCanonicalizeEx 结果
    std::wstring volume;        // GetVolumePathNameW
    EntryState   state;
    DWORD        win32Error;
};
TargetValidation ResolveAndValidateTarget(const std::wstring& raw, const CacheEntryDef& def);

// 结果模型（扫描与清理共用）
struct OpResult {
    uint64_t files = 0, dirs = 0, skippedReparse = 0, failed = 0;
    ULONGLONG logicalBytes = 0, allocatedBytes = 0, removedBytes = 0;
    struct Failure { std::wstring path; DWORD error; const wchar_t* stage; };
    std::vector<Failure> failures;
};

// 任务状态机
enum class TaskState { Idle, Scanning, Cleaning, Stopping };
void SetState(TaskState);   // 集中管理按钮/列表/进度/状态文本

// 进度（组合）
struct Progress { int itemIndex, itemCount; uint64_t filesDone; uint64_t bytesDone; std::wstring currentPath; };
```

要点：
- **安全原语单一入口**：`ResolveAndValidateTarget` 是唯一允许把外部路径变成可删除目标的地方；UI 层的"路径"只是展示。
- **结果结构贯穿**：扫描线程/清理线程只写 `OpResult` 槽，UI 负责展示；不再出现"void 函数 + 谎报成功"。
- **消息带 generation**：`PostMessage(WM_*_DONE, generation, index)`，UI 侧丢弃过期消息。
- **枚举函数共用**：一个 `EnumerateDir(path, flags, callback)` 同时服务扫描/清理/大小统计，天然保证三者对重解析点/长路径的处理一致。

---

## 8. 必须保留 / 必须改掉的设计点

### 8.1 必须保留

1. **原生 Win32 + 单 exe、无第三方运行时依赖（MinGW 静态 libstdc++/libgcc）** 的产品形态：体积小、启动快、无需安装。
2. **UI 线程只做 UI，扫描/清理在后台线程，用消息回报**的总体架构方向（问题在生命周期细节，不在分层方向）。
3. **破坏性操作前二次确认 + 保守默认（默认不勾选）**的安全意识；重构可改默认勾选策略，但"用户明确选择才清理"的原则要保留。
4. **缓存项白名单思路**：只清理明确的、可再生的缓存位置，绝不做"任意目录递归清理"。
5. **资源内嵌 manifest + Common Controls v6 + `InitCommonControlsEx`** 的组合（已验证配对正确）。
6. **Ctrl+滚轮调整字号**这个交互需求（实现需按 DPI 重做，见 R-15）。
7. `-mwindows -municode -static` 的构建形态（入口 `wWinMain` 与命令参数要三处一致）。

### 8.2 必须改掉

1. **"通用递归删除/统计"原语**：改为"白名单根 + 规范化 + 非重解析 + 结果化"的安全原语；不再有可直接作用在任意路径上的 `DeleteRecursive`。
2. **`MAX_PATH` 定长 `wchar_t path[MAX_PATH]` 与静默截断**。
3. **`void` + 吞错误的文件操作**：任何删除/统计都必须能回答"成功/失败/为什么"。
4. **`lvIndex` 兼任"视图索引 + 存在标志"的双重语义、线性反查、`GetSelectedSize`/`CleanThread` 双份选择遍历**：改 `LVITEM.lParam`/显式模型。
5. **工作线程直接读 ListView、无锁读写 `g_entries`**：改"UI 线程取快照 → 线程只写结果槽"。
6. **硬编码 20 条 + `MAX_ENTRIES=32` 静默丢弃**：改动态条目表（含分类/风险/管理员/默认勾选/通配）。
7. **分散的按钮 Enable/Disable 管理**：改 `TaskState` 状态机 + 取消路径。
8. **`build.bat` 的"块内 `%ERRORLEVEL%`"、依赖 LF/UTF-8、硬编码路径假设**。
9. **"声明了 PerMonitorV2 却没有 DPI 处理"**：要么实现（推荐），要么改准确说法。
10. **把普通权限下的静默失败当作"清理完成"**：必须有失败统计、管理员提示、日志。

---

## 9. 重构验收清单（可直接执行）

### 9.1 安全（必须全部通过）

- [ ] 夹具：在 `%TEMP%\cc_fixture\cache` 内创建指向 `%TEMP%\cc_fixture\victim` 的 **junction**；扫描后：`cache` 的统计**不包含** `victim` 内容；清理后 `victim\IMPORTANT.txt` **仍存在**。
- [ ] 夹具：缓存项根路径本身是 junction（模拟 `%TEMP%` 重定向）；默认状态下该项显示"已重定向/跳过"，清理不动目标内容。
- [ ] 夹具：`..\`、`.`、空串、盘根（若有测试盘可用 `subst` 虚拟盘）、`%USERPROFILE%`、`%SystemRoot%`、UNC 路径模板 → 全部被校验器拒绝，且日志写明拒绝原因。
- [ ] 夹具：>260 字符的缓存路径（`\\?\` 长路径）能正确统计/清理；截图或 JSON 断言 `skippedLongPath == 0`（或明确报错）。
- [ ] 夹具：只读文件/目录、隐藏/系统属性、被打开句柄的文件 → 结果中**失败数与原因**正确，UI 不显示"全部完成"。
- [ ] 夹具：云占位符（可用 `FILE_ATTRIBUTE_OFFLINE` 手工设置模拟）默认跳过。
- [ ] 清理过程中关闭窗口：有"正在清理，是否停止"提示；选择停止后进程正常退出，日志记录已删除/未处理项。

### 9.2 线程/状态

- [ ] 扫描中/清理中按钮状态正确；任务结束后按钮恢复；**不存在**永久禁用（人为让线程立即返回、模拟 `CreateThread` 失败路径亦能恢复）。
- [ ] 取消按钮在扫描/清理中可用；取消后 3 秒内回到 Idle，DONE 只处理一次（generation 校验）。
- [ ] UI 操作（勾选/全选）在 busy 时被禁用或明确快照语义。
- [ ] 连续执行 3 次"扫描→清理→扫描"无崩溃、无句柄增长（任务管理器/Process Explorer 观察 handle 数稳定）。

### 9.3 功能/结果

- [ ] 清理失败（锁定文件、无权限系统项）时，UI 显示"释放 X / 失败 N / 跳过 M"与原因分类；"清理完成"仅在零失败时出现。
- [ ] 0 字节项的勾选清理能执行；未扫描/未勾选/不可访问三种提示正确。
- [ ] 普通权限启动：系统项显示需管理员并可一键提权重启；提权后选择状态保留；提权后清理系统项成功/失败均有报告。
- [ ] 清理前后 `GetDiskFreeSpaceExW` 差值展示正确（与实际释放字节对比，允许系统缓存重建造成的偏差并在报告里注明）。

### 9.4 性能/进度

- [ ] 扫描一个 10 万文件的测试目录：进度持续变化（不出现长时间定格）；显示当前条目名/文件数/耗时。
- [ ] 并行扫描开启后，20 项总耗时对比旧版有可量化提升（记录基准）。
- [ ] 清理 10 万文件：进度按字节/文件更新，UI 不冻结，可取消。

### 9.5 构建/工程

- [ ] 从任意 CWD 运行 `build.bat` 成功，产物在 `build\`；故意引入语法错误时脚本**exit /b 非 0**且不打印"成功"、不启动旧 exe。
- [ ] `build.bat` 在 CRLF 与 LF（ASCII）两种签出下都能工作；仓库有 `.gitattributes`。
- [ ] `g++ -std=c++17 -Wall -Wextra -Werror -c` 通过；CI 有此门禁。
- [ ] README 的 MinGW/MSVC 命令与实际构建一致且经过验证（MSVC 至少注明未验证或移除）。
- [ ] 条目数量/分类从代码自动生成到 README（或 README 不写死数量）。
- [ ] 版本资源/文件属性页有版本号；`.gitignore` 覆盖 `build/`。

### 9.6 CLI 自测（建议实现后作为 CI 主干）

```bat
:: 只扫描、机器可读输出、不删除
clearC.exe --scan --json --log %TEMP%\cc_scan.json
:: 只校验目标合法性（不统计、不删除）
clearC.exe --validate --json
:: 清理指定条目（夹具内），失败时退出码非 0
clearC.exe --clean <id> --log %TEMP%\cc_clean.log
:: 全量干跑
clearC.exe --dry-run --json
```

验收断言：junction/符号链接不跟随；受保护路径被拒；失败项计入 JSON；退出码与结果一致；重复执行幂等。

---

## 10. 工作区重构进展快照（截至 2026-11-10 11:14，非正式审计）

> 本节只记录"审计期间 Lead 正在并行重构"的事实与初步观察，**不构成对新代码的审计结论**（文件仍在变化；以 §9 验收清单为准）。

### 10.1 最终状态更新（11:22，覆盖下文 11:14 快照）

- `clearC.cpp` 已从工作区删除（`git status` 显示 staged `D`），legacy 实现退场；本报告 A–F 组的行号请用 `git show HEAD:clearC.cpp` 查看。
- `README.md` 已重写：69 项/7 类、并行扫描、分类排序、安全护栏、管理员分级、失败统计、谨慎项默认不勾选、CLI（`--scan --json`/`--list`/`--self-test`/`--version`）。**实测 `src/catalog.cpp` 条目数 = 68 个 `CACHE_DIR(` + 1 个回收站 = 69，与 README 一致**（但仍是硬编码数字，条目增减后易漂移，建议由 `--list`/构建脚本生成）。
- `.gitignore` 已加 `build/`、`*.pdb`、`*.ilk`、`*.log`；新增 `LICENSE`（MIT）；`manifest.xml` 已加 `asInvoker`/`longPathAware`/版本 `2.0.0.0`；`resource.rc` 已加 `VERSIONINFO`；`build.bat` 已改为全英文 + `setlocal` + `cd /d "%~dp0"` + 编译器探测 + `if errorlevel 1 goto fail` + 产物进 `build\` + `clean/release/run` 参数。
- **仍未覆盖/需复核**：
  - `.gitattributes` 仍不存在；新 `build.bat` 是 **LF-only**（当前全 ASCII，实测 LF+ASCII+`goto`/括号块可正常运行，但加中文或 `core.autocrlf` 签出即复发 F-03）→ 建议 `*.bat text eol=crlf`；
  - README 的 MSVC 命令仍缺 `/utf-8`、`/EHsc`（F-05 对新 `src/` 同样适用：源码含中文宽字符串）；
  - `-lshlwapi -lole32 -luuid` 仍在链接命令中（F-07；新代码用了 Shell API，`shell32` 可能确实需要，但 shlwapi/ole32/uuid 建议按实际导入再确认）；
  - exe 仍输出到仓库根目录（F-09），未加 CI（F-10 部分）；
  - README 新增的"60+ 项 1 秒内完成""并行扫描""IsSafeToClean 拒绝盘根/系统目录""恢复回收站"等是**待验证声明**，建议用 `--self-test` + §9.1/§9.3 夹具实测，并把实测结果写回 README；
  - `--self-test`、`--scan --json` 等 CLI 已经就绪，正适合承载本报告 §9 的自动化验收。

**工作区 11:14 快照（部分已被 10.1 覆盖，保留以说明演进过程）**：`git status` 显示 `M build.bat`、`M manifest.xml`、`M resource.rc`，未跟踪 `build/`、`src/`。`clearC.cpp` 未改（仍是本报告的审计对象）。新增 `src/`：`catalog.*`、`cli.*`、`common.h`、`main.cpp`、`scan.*`、`ui.*`、`util.*`（约 10 万字节）。

**新版已覆盖或接近覆盖的本报告条目**（按代码/符号初查）：

| 条目 | 新实现中的迹象 |
| --- | --- |
| A-01/A-02（重解析点） | `src/util.cpp` 已用 `FindFirstFileExW(FindExInfoBasic, …)`，并在枚举时检查 `FILE_ATTRIBUTE_REPARSE_POINT`（`util.cpp:89-138`、`302`）；`src/cli.cpp` 提供 junction 夹具（`cli.cpp:306-344`） |
| A-04（长路径） | `src/util.cpp:17-18` 有 `\\?\`/`\\?\UNC\` 前缀；`cli.cpp:471-481` 有 >260 字符夹具与断言 |
| A-11（实际释放空间） | `src/util.cpp:496` 已用 `GetDiskFreeSpaceExW` |
| B-02/B-08（任务收尾/等待） | `src/ui.cpp:378` 有 `WaitForSingleObject` |
| E-01（管理员） | `src/ui.cpp:405` 有 `ShellExecuteW(..., L"runas", ...)` 重启提权；新版 manifest 已加 `requestedExecutionLevel asInvoker` |
| E-02/E-03（布局/DPI） | `src/ui.cpp:99-106` 动态 `GetDpiForWindow`；`ui.cpp:659/673` 已处理 `WM_DPICHANGED`、`WM_GETMINMAXINFO` |
| F-01/F-02（构建路径/错误检查） | 新版 `build.bat`：`setlocal`、`cd /d "%~dp0"`、`where g++`/多根探测、`if errorlevel 1 goto fail`、产物进 `build\` |
| F-08（manifest） | 新版 manifest 增加 `asInvoker`、`longPathAware`、版本号；`resource.rc` 增加 `VERSIONINFO` |

**新版仍需关注/复核的点**：

| 条目 | 说明 |
| --- | --- |
| F-03（行尾/编码） | 新 `build.bat` 仍是 **LF-only**（首字节 `@echo off\n`）。当前全英文，实测 LF + ASCII + `goto`/括号块可正常运行；但仓库仍无 `.gitattributes`，一旦加中文、`chcp` 或按 `core.autocrlf` 签出就会复发 → 建议 `.gitattributes: *.bat text eol=crlf` |
| F-05/F-06（README） | 11:14 时 README 尚未更新；11:22 复查发现 README 已重写并修正条目数量（69 项，与 `src/catalog.cpp` 一致，见 §10.1）。仍遗留：MSVC 命令缺 `/utf-8 /EHsc`；条目数仍是硬编码文本，建议由 `--list`/构建脚本生成 |
| F-07（未用库） | 新 build.bat 仍链接 `-lshlwapi -lole32 -luuid`；若新版仍未使用 SHLWAPI/OLE/Shell API，建议移除 |
| F-09/F-10（产物/工程） | 11:22 复查：`.gitignore` 已忽略 `build/`、已新增 LICENSE；仍遗留：exe 仍输出到仓库根、仍无 `.gitattributes`、仍无 CI |
| A-05/C-01（失败上报） | 需在 `src/` 的实现里确认 `OpResult` 式的失败统计与 UI 展示是否完整（本报告 §9.3 可直接验收） |
| B-03/B-04（并发/线程模型） | 需确认新 UI/scan 是否已做到"UI 线程取快照、工作线程不碰 HWND" |
| A-03（危险路径护栏） | 已见长路径/重解析处理，但"受保护目录白名单 + 盘根/用户根拒绝 + 根路径等于父目录拒绝"需确认是否全部落地（§9.1 夹具验收） |

**建议**：以本报告 §7（R-01…）为重构 checklist、§9 为验收矩阵；新版代码完成后，把 §9.1 的 junction/长路径/受保护路径夹具接入 `--scan --json` 自测，即可回归验证 A 组风险。

---

## 11. 附录

### 11.1 junction 复现 harness（完整可编译，仅用于 `%TEMP%` 测试）

```cpp
// final.cpp —— 逐字复制 clearC.cpp:146-198 的三个函数，仅用于验证 junction 行为
#include <windows.h>
#include <string>
#include <cstdio>
static bool IsDir(const wchar_t* p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static ULONGLONG CalcDirSize(const std::wstring& dir) {
    ULONGLONG total = 0; WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) total += CalcDirSize(full);
        else total += ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    } while (FindNextFileW(h, &fd));
    FindClose(h); return total;
}
static void DeleteRecursive(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) { do { if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) DeleteRecursive(path + L"\\" + fd.cFileName); } while (FindNextFileW(h, &fd)); FindClose(h); }
        if (attr & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
        RemoveDirectoryW(path.c_str());
    } else {
        if (attr & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
        DeleteFileW(path.c_str());
    }
}
static void CleanDirContents(const std::wstring& dir) {
    if (!IsDir(dir.c_str())) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do { if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) DeleteRecursive(dir + L"\\" + fd.cFileName); } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static int Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
int wmain(int argc, wchar_t** argv) {
    std::wstring t = argv[1];
    // CASE 1: 缓存目录内 junction -> 外部数据目录；验证统计与删除是否越界
    std::wstring r1 = t + L"\\root1", v1 = t + L"\\victim1", l1 = r1 + L"\\link";
    CreateDirectoryW(r1.c_str(), nullptr); CreateDirectoryW(v1.c_str(), nullptr);
    { FILE* f = _wfopen((v1 + L"\\IMPORTANT_DATA.txt").c_str(), L"wb"); if (f) { fputs("important user data", f); fclose(f); } }
    wprintf(L"[1] CalcDirSize(root1) = %llu (victim1 alone = %llu)\n", CalcDirSize(r1), CalcDirSize(v1));
    CleanDirContents(r1);
    wprintf(L"[2] after clean: victim1\\IMPORTANT_DATA.txt exists=%d (expect 1 if safe), junction exists=%d\n",
            Exists(v1 + L"\\IMPORTANT_DATA.txt"), Exists(l1));
    // CASE 2: 缓存项根路径本身是 junction；验证是否清空链接目标
    std::wstring v2 = t + L"\\victim2", e2 = t + L"\\entry2";
    CreateDirectoryW(v2.c_str(), nullptr);
    { FILE* f = _wfopen((v2 + L"\\DATA2.txt").c_str(), L"wb"); if (f) { fputs("more user data", f); fclose(f); } }
    wprintf(L"[3] CalcDirSize(entry2=%d) = %llu\n", (int)IsDir(e2.c_str()), CalcDirSize(e2));
    CleanDirContents(e2);
    wprintf(L"[4] after clean: victim2\\DATA2.txt exists=%d (expect 1 if safe), junction entry2 exists=%d\n",
            Exists(v2 + L"\\DATA2.txt"), Exists(e2));
    return 0;
}
```

构建与运行（先创建 junction，再跑 harness；全部在 `%TEMP%`）：

```bat
set "PATH=D:\msys64\ucrt64\bin;%PATH%"
mkdir %TEMP%\cc_junction\root1 %TEMP%\cc_junction\victim1 %TEMP%\cc_junction\victim2
echo important user data > %TEMP%\cc_junction\victim1\IMPORTANT_DATA.txt
echo more user data > %TEMP%\cc_junction\victim2\DATA2.txt
mklink /J %TEMP%\cc_junction\root1\link %TEMP%\cc_junction\victim1
mklink /J %TEMP%\cc_junction\entry2     %TEMP%\cc_junction\victim2
g++ -std=c++17 -O1 -municode -o %TEMP%\cc_junction\final.exe %TEMP%\cc_junction\final.cpp
%TEMP%\cc_junction\final.exe %TEMP%\cc_junction
```

修复后的预期：`[2]`/`[4]` 的两处 `exists` 均为 **1**（目标数据未被触碰），并且结果中记录"跳过重解析点"。

### 11.2 build.bat 行为验证方法（不写仓库）

```bat
:: 1) 把 HEAD 版 build.bat 复制到空临时目录（不含源码），从该目录运行
copy "D:\Program Files\clearC\build.bat" %TEMP%\batprobe\
cd /d %TEMP%\batprobe
build.bat <nul        :: 观察：找不到源码仍打印"编译成功"（F-02 证据）

:: 2) 行尾/编码对照：把 build.bat 转 CRLF 后重跑，对比输出
:: 3) 最小验证：UTF-8 中文 + chcp 65001 + LF vs CRLF 的 batch 解析对照
```

注意：**不要**在仓库目录运行旧脚本（会覆盖 `clearC.exe`）。

### 11.3 编译与产物核验命令

```bat
set "PATH=D:\msys64\ucrt64\bin;%PATH%"

:: 任务要求的警告检查（0 条）
g++ -std=c++17 -Wall -Wextra -fsyntax-only "D:\Program Files\clearC\clearC.cpp"

:: 完整编译（1 条 unused variable），产物放 %TEMP%
mkdir %TEMP%\clearC_audit
copy "D:\Program Files\clearC\clearC.cpp" %TEMP%\clearC_audit\
copy "D:\Program Files\clearC\resource.h" %TEMP%\clearC_audit\
copy "D:\Program Files\clearC\resource.rc" %TEMP%\clearC_audit\
copy "D:\Program Files\clearC\manifest.xml" %TEMP%\clearC_audit\
xcopy /E /I "D:\Program Files\clearC\assets" %TEMP%\clearC_audit\assets
cd /d %TEMP%\clearC_audit
windres resource.rc -o resource.o
g++ -std=c++17 -O2 -Wall -Wextra -o clearC_audit.exe clearC.cpp resource.o ^
    -mwindows -municode -lcomctl32 -lshlwapi -lole32 -luuid -static

:: 产物对比（仓库 exe vs 重建 exe：仅 PE 时间戳/校验和 5 字节不同）
certutil -hashfile "D:\Program Files\clearC\clearC.exe" SHA256
certutil -hashfile %TEMP%\clearC_audit\clearC_audit.exe SHA256
```

---

## 12. 问题索引（快速浏览）

| 编号 | 严重度 | 标题 | 位置 |
| --- | --- | --- | --- |
| A-01 | 严重 | 递归统计/删除跟随 junction/符号链接/重解析点 | clearC.cpp:146-198, 313 |
| A-02 | 严重 | 清理项自身为重定向/junction 时清空目标 | clearC.cpp:141-144, 188-198, 408 |
| A-03 | 严重 | 无危险路径护栏（空/相对/盘根/用户根/Windows/UNC/截断） | clearC.cpp:204-249, 427-451 |
| A-04 | 高 | MAX_PATH 静默截断到另一合法路径；长路径静默失败 | clearC.cpp:75, 209-210, 141-198 |
| A-05 | 高 | 文件操作 void 返回，删除失败全吞 | clearC.cpp:164-198, 313 |
| A-06 | 中 | 只读/隐藏/系统/离线/云占位符属性处理不完整 | clearC.cpp:178-184 |
| A-07 | 中 | 被占用文件单次尝试，无重试/延迟/占用进程提示 | clearC.cpp:164-198, 288-319 |
| A-08 | 中 | TOCTOU：扫描只验 IsDir，清理前不复核 | clearC.cpp:408, 313 |
| A-09 | 中 | 硬编码 C:\Windows；Prefetch/SoftwareDistribution 语义/服务未处理 | clearC.cpp:246-247 |
| A-10 | 中 | 递归无深度上限；junction 环路 → 无限递归/栈溢出 | clearC.cpp:146-186 |
| A-11 | 低 | 逻辑大小 ≠ 实际可释放空间（硬链接/稀疏/云） | clearC.cpp:158, 437-438 |
| A-12 | 低 | 清理中断/部分删除无记录，退出不警告 | clearC.cpp:673-687 |
| B-01 | 高 | 提前 return/线程创建失败/异常均不发 DONE，按钮永久禁用 | clearC.cpp:263, 311, 424/450, 662-676 |
| B-02 | 高 | 窗口销毁时线程失控：不置标志/不等待/句柄泄漏/删到一半 | clearC.cpp:424, 450, 683-687, 739-746 |
| B-03 | 中 | g_mtx 覆盖不完整；持锁调用窗口函数 | clearC.cpp:258-273, 313, 409-413, 646-654 |
| B-04 | 中 | 工作线程直接读 ListView | clearC.cpp:291-293 |
| B-05 | 中 | lvIndex 双重语义 + 选择映射重复实现 O(n²) | clearC.cpp:78, 295-300, 371-375 |
| B-06 | 中 | busy 时列表/全选仍可操作，快照与实时状态不一致 | clearC.cpp:399-400, 445-446, 616-621 |
| B-07 | 低 | PostMessage 未检查/无 generation/无节流 | clearC.cpp:275-278, 314-315 |
| B-08 | 低 | 无任务状态机与取消；按钮状态分散 | clearC.cpp:399-400, 445-446, 557, 662-676 |
| C-01 | 高 | 无条件弹"清理完成"，无失败统计 | clearC.cpp:313, 673-681 |
| C-02 | 中 | size==0 当"未选择"：0 字节项无法清理，提示错误 | clearC.cpp:430-435 |
| C-03 | 中 | 确认框"将释放约 X"与实际不一致 | clearC.cpp:437-439 |
| C-04 | 中 | 清理后立即自动重扫，覆盖结果、无前后对比 | clearC.cpp:678-680 |
| C-05 | 中 | 不存在/不可访问/需管理员/已重定向无状态区分 | clearC.cpp:406-422, 271 |
| C-06 | 低 | 状态机缺失；字号提示覆盖状态；无当前项/耗时 | clearC.cpp:599, 277-278 |
| C-07 | 低 | exists 死字段；lvIndex 残留状态 | clearC.cpp:77, 212, 271 |
| C-08 | 中 | 无日志/诊断/退出码/dry-run | 全文件 |
| D-01 | 中 | 单线程串行扫描 | clearC.cpp:255-282 |
| D-02 | 中 | 每目录 FindFirstFile，无 LARGE_FETCH/ExInfoBasic；字符串反复分配 | clearC.cpp:149-195 |
| D-03 | 中 | 进度按项非字节；大项看似卡死 | clearC.cpp:277-278, 314-315 |
| D-04 | 中 | 清理逐文件串行，无并行/重试/细粒度进度 | clearC.cpp:288-319 |
| D-05 | 低 | 每次全量重算，清理后整表重建 | clearC.cpp:404-422 |
| D-06 | 低 | 条目间无重叠/去重检测 | clearC.cpp:216-249 |
| E-01 | 高 | 无管理员检测/提示/提权重启，系统项静默失败 | clearC.cpp:216-249, 427-451; manifest |
| E-02 | 中 | 无最小窗口尺寸 → 重叠/负尺寸/列宽 ≤0 | clearC.cpp:457-485, 579-581 |
| E-03 | 中 | 声明 PMv2 但无 DPI 处理 | clearC.cpp:67, 326, 349-354, 706-734 |
| E-04 | 低 | WM_CTLCOLORSTATIC 硬编码 COLOR_WINDOW，暗色/高对比度不适配 | clearC.cpp:606-610, 717 |
| E-05 | 低 | 默认不勾选；无分类/排序/分组/反选/tooltip/右键 | clearC.cpp:493-537, 625-644 |
| E-06 | 低 | 滚轮子类化固定 16 上限/无 SetWindowSubclass 链 | clearC.cpp:70, 107-109, 328-343 |
| E-07 | 低 | 全选用 BS_AUTOCHECKBOX 却设 BST_INDETERMINATE | clearC.cpp:497-499, 637-640 |
| E-08 | 低 | 无 DOUBLEBUFFER；大小列未右对齐；列宽无记忆 | clearC.cpp:514-537 |
| E-09 | 低 | 无键盘/IsDialogMessage/焦点顺序/可访问性 | clearC.cpp:739-743 |
| E-10 | 低 | 固定窗口尺寸、不记忆、多屏居中到主屏 | clearC.cpp:62-63, 723-734 |
| E-11 | 低 | 24px STATIC 承载 32px 图标；Static 固定宽度被裁剪 | clearC.cpp:467-469, 502-511 |
| F-01 | 高 | build.bat 未 cd + 硬编码 D:\msys64 + 无编译器探测 | build.bat:9-15, 23 |
| F-02 | 高 | 块内 %ERRORLEVEL% 解析期展开，失败仍报成功并 start | build.bat:11-28 |
| F-03 | 高 | LF-only + UTF-8 中文 + chcp 65001 → cmd 解析错位；缺 .gitattributes | build.bat 全文 |
| F-04 | 中 | 头注释编译命令缺 -municode → 链接失败 | clearC.cpp:4-6 |
| F-05 | 中 | README MSVC 命令缺 /utf-8 /EHsc，源文件无 BOM，未验证 | README.md:37-42 |
| F-06 | 低 | README"19 类"vs 实际 20 条；头注释漏 CachedProfileData | README.md:9-13; clearC.cpp:8-11 |
| F-07 | 低 | 未使用的 include/lib（实测无相关导入） | clearC.cpp:23-24; build.bat:23 |
| F-08 | 中 | manifest 缺 asInvoker/longPathAware/supportedOS；DPI 声明与实现不一致 | manifest.xml:1-16 |
| F-09 | 低 | 仓库根未跟踪 clearC.exe；产物与源码目录混放 | 仓库根；build.bat |
| F-10 | 低 | 无 LICENSE/.gitattributes/.editorconfig/CI/版本资源 | 仓库根 |
| F-11 | 低 | 资源 ID 冲突核查：**无冲突**（非问题） | resource.h/rc; clearC.cpp:50-59 |

---

### 附：本报告未覆盖的内容

- Lead 并行新增的 `src/` 重构实现与新 `build.bat`/`manifest.xml`/`resource.rc` 只做了事实性快照（§10），未做逐行审计（审计期间仍在变化）。
- 未做真机 UI 截图/DPI 多屏实测、未测试 MSVC 构建、未创建符号链接（权限不足）；相关结论已在对应条目注明依据。
