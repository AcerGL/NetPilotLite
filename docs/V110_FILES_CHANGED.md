# V1.1.0 修改文件

仅改动 `NetPilotLite` 子项目；旧 NetPilot/.NET/WiX 项目保留。原网卡执行核心、设备控制后端与签名更新事务保持原有机制。

| 文件 | 内容 |
|---|---|
| `src/app.c` | 缓存绘图接入、批量布局、后台状态更新、事件防抖、配置写入合并、新设置/托盘/只读监控消息；退出等待后台读取 |
| `src/ui_render.inc` | 主窗口完整内容层及视口绘图、静态文字/卡片/图片合成 |
| `src/ui_settings.inc` | 八页设置中心、固定导航和裁剪视口、DPI/主题、外观及托盘个性化控件 |
| `src/ui_tray.inc` | 自定义快捷浮窗、文档化 DWM 材质和回退、工作区/DPI 定位、关闭行为 |
| `src/visual.c/.h` | 可复用 DIB/DC、图片透明度识别、不透明图片 HALFTONE 缩放与资源释放 |
| `src/settings.c/.h` | schema 2 增量外观/托盘/封面/观察规则字段及兼容读写 |
| `src/routing.c/.h` | 只读 TCP/UDP 表、进程身份、源地址映射/路由估计、模拟规则判断；无网络配置写入 |
| `src/ui_routing.inc` | 后台连接快照、虚拟列表、观察规则编辑和诊断、不支持能力的明确标识 |
| `src/update.h`、`src/app.rc`、`src/app.manifest` | 1.1.0.1 文件版本和关于页面构建号；保留固定主文件名与权限声明 |
| `Common.props`、GUI/Tests `.vcxproj` | uxtheme 系统库、新模块和 Solution 浏览项；仍为 C17 /MT |
| `tools/release.c` | 版本 JSON 增加完整文件构建号；SHA 清单包含独立 Updater |
| `tools/package.py` | 可重复生成发布/源码 ZIP 和归档哈希，显式选择脱敏证据，排除私钥和测试运行目录 |
| `tests/test_main.c`、`ui_perf.inc`、`ui_acceptance.inc` | 基线/新绘图性能、像素一致性、布局/主题/资源循环及两分钟驻留 |
| `tests/routing_tests.inc`、`ui_routing_tests.inc` | 映射边界、非约束规则、真实回环 TCP/UDP 读取、模拟页面及 DPI 检查 |
| `tests/update_tests.c`、`tests/build_update_tests.cmd`、`tests/resources/new.rc`、`prior.rc` | 运行中的旧版本升级、规则和新偏好保留、可选 Stage A 构建升级；继续失败/中断/签名故障检查 |
| `tests/prior/*` | 从原始 1.0.1 基线保留的只读 Mock 产品源，用于可重复升级测试 |
| `README.md`、`docs/V110_*`、`ROUTING_FEASIBILITY.md` | 构建/使用、分阶段结果、能力边界、源码及产物说明 |

既有图片、ICO 和签名公钥继续复用，没有生成新私钥、替换信任根或修改 Clash/VPN 配置。
