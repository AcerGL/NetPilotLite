# 1.0.1 修改文件

历史清单。1.1.0 改动见 [V110_FILES_CHANGED.md](V110_FILES_CHANGED.md)。

旧的 .NET / WiX / MSI 项目保留，仅修改原生 `NetPilotLite` 子项目。

| 文件 | 本次改动 |
|---|---|
| `src/core.c`、`src/lite.h` | 保留 GUID + PnP 精确绑定、远程桌面保护及真实状态重查；扩展物理 Wi-Fi 和带前置连接检查的热点模式 |
| `src/backend_win.c` | 同一 SetupAPI / Configuration Manager 控制路径增加物理 Wi-Fi；只读 WLAN SSID 和 IPv4；禁用状态仍可枚举；保留测试编译禁用真实修改入口 |
| `src/app.c` | 可调整窗口、响应式布局、滚动、DPI、双卡片、模式、主题/背景/关于/更新、托盘菜单、工作线程扫描和异步图片解码、启动确认 |
| `src/main.c` | 保留单实例；严格解析健康确认 GUID；启动检测未完成更新并恢复；测试编译使用独立实例标识 |
| `src/settings.c/.h` | LocalAppData INI、SchemaVersion、注册表旧绑定读取迁移、原子保存、备份/恢复、未来 schema 保护、统一语义主题色 |
| `src/visual.c/.h` | WIC 解码、缓存、像素上限、Fit/Fill/Center、渐变、Alpha 遮罩及 GDI 绘制 |
| `src/trust.c`、`src/update.h` | SHA-256、ECDSA P-256 固定公钥验证、文件/签名记录、产品与版本/x64 PE 校验 |
| `src/updater.c` | 一次性固定用途更新器、正常退出及等待、同卷暂存/替换/备份、持久化事务日志、启动确认、失败回退、三阶段中断恢复、标准 UAC；不含网卡控制接口 |
| `src/update_launch.c` | 提取并验证内置更新器、固定参数启动、健康确认事件、恢复入口 |
| `src/app.rc`、`src/app.manifest`、`src/updater.rc` | 1.0.1 版本、图标/封面/公钥/更新器资源，GUI requireAdministrator、Updater asInvoker 按需 runas |
| `assets/*` | 原封面、独立图标源及多尺寸 ICO、公钥、素材说明；没有私钥 |
| `tools/icon.c` | 原生 ICO 转换器，保留 Alpha，16/24 像素简化网口标识 |
| `tools/release.c` | 原生离线发布工具、DPAPI 保护发布私钥、签名清单、版本 JSON、SHA-256 清单 |
| `Common.props`、三个 `.vcxproj`、`.sln` | 增加 WIC/WLAN/BCrypt 等系统库及 Updater 构建依赖，保持 C17、Release x64、静态 CRT |
| `build.cmd`、`publish.cmd` | 构建/模拟检查及可重复签名发布；固定 dist 路径与主文件名；复制失败不伪报成功 |
| `tests/test_main.c`、`tests/tests.rc` | 扩展双网卡/热点模拟、GUI 和只读探测资源 |
| `tests/update_tests.c`、`tests/fixture_fail.c`、`tests/build_update_tests.cmd`、`tests/resources/*` | 实际文件与进程更新、配置迁移、故障/中断/取消/占用/忙碌测试 |
| `tests/legacy/*` | 1.0.0 参考源的只读 Mock fixture；独立窗口类/互斥体及忙碌测试钩子，不是正式交付程序 |
| `.clang-format`、`.gitignore` | 统一 C 源格式，排除二进制、测试运行目录和私钥 |
| `README.md`、`docs/*`、`artifacts/*` | 构建、使用、升级、手动验收、结果及证据 |

正式交付中没有 MSI、PowerShell 运行脚本、后台服务、计划任务，也没有单独安装的常驻 Helper。临时 Updater 只处理本产品文件事务。
