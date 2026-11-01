# NetPilot Lite 1.3.1 发布与更新验收报告

2026-11-01 复核；本地测试执行于 2026-10-28。当前为签名候选，尚未正式发布，不能宣称真实在线升级已完成。没有运行真实安装器，没有修改网卡、Metric、DNS、代理、Clash 配置或路由。

## 1. 仓库地址与本地 Git

用户指定且已认证 API 确认的仓库是 https://github.com/AcerGL/NetPilotLite ，clone URL 为 https://github.com/AcerGL/NetPilotLite.git 。本地项目没有自己的 `.git/origin`，此前经连接器 Git Data API 上传，不能假称地址读取自本地 Git Remote。诊断脚本在正常 checkout 优先读取 origin，否则注明配置来源。

## 2. 仓库公开性

11 月 1 日 `get_repo` 再次返回 visibility=private。10 月 28 日匿名 GET 仓库、tags、releases、latest、workflows 均为 HTTP 404 / Not Found。开发账号的授权不会传给最终用户；匿名更新不能直接使用私有 Releases，签名本地更新仍可用。

## 3. 当前权限和限制

连接账号 AcerGL，仓库权限返回 admin/maintain/pull/push/triage=true。仓库、main、Releases 和 Actions runs 可读。连接器没有创建 Release/上传二进制资产的工具，采用 Actions 发布架构，不能推断 GitHub 本身拒绝创建 Release。

本机 gh 不在 PATH，未发现安装，未实际执行 gh auth status；没有创建 PAT 或输出凭据。此前 Unknown tool 属工具分发问题，连接恢复后读取已成功。

部分 `/actions/workflows`、`/actions/permissions`、`/actions/permissions/workflow`、`/actions/runners` 查询被连接器允许范围拒绝（工具层 400），不等于 GitHub 策略拒绝。匿名权限/runner 查询返回 401 Requires authentication。Actions 管理策略、可信 runner 和环境审批仍需管理员界面或正常 gh 认证核实。

## 4. Release 和 tag 列表

11 月 1 日已认证 GET `/releases` 成功返回 `[]`，没有正式版、draft、prerelease 或安装资产。10 月 28 日已认证 `/git/matching-refs/tags/` 返回 `[]`。本次未创建 tag 或 Release。

## 5. Actions 状态

11 月 1 日源码同步前，已认证 `/actions/runs` 为 `total_count:0`。远端旧 release.yml 只有 tag/dispatch 触发，普通源码上传不会发布；还依赖签名 runner 和多个开关。没有证据表明曾发生 Release 权限失败。

本次 build.yml 增加 main 普通构建；release.yml 增加版本前置检查、固定 commit、同标签串行、签名与发布分离、仅 publish job 获得 contents:write、draft 上传、逐文件回读校验、最后发布和 latest 回查。已有 published 资产不会自动覆盖。

源码提交 `fd9bfa454b49a1ae2310589cbdb47306906d298a` 已同步 main。[真实 Actions run 41320690329](https://github.com/AcerGL/NetPilotLite/actions/runs/41320690329) 的 Windows build job 成功：版本/脚本检查、C17 Release 编译、核心模拟 44 项、无签名下载 fixture 的协议测试 75 项、分流诊断 60 项均通过。已上传 `NetPilotLite-unsigned-win-x64` Actions Artifact（ID 13088618277，1,247,994 字节）；它只是无签名 CI 预览，不是 Release，不可用于自动更新。CI 未运行私钥签名、Offline 安装或正式发布 job。编译日志保留已有第三方 libyaml 类型转换等警告，未宣称零警告。

## 6. latest 结果及根因

11 月 1 日已认证 latest 仍为 HTTP 404 / Not Found，同时同一连接能读取空 Release 列表，确认尚未发布。10 月 28 日真实匿名 WinHTTP latest 为 Windows 12152 / HTTP 404；仓库本身匿名也不可见，客户端应报告不存在/私有不可见/路径错误，不能显示已是最新版。

已证实的两项原因：没有 Release/资产；私有仓库不能供匿名更新。旧代码另有缺口：缺少上传后回读和同标签串行；版本检查过晚；手动非 tag 执行不能签名；80 字符诊断可能截断详细原因。未发现实际 Release 写权限被拒绝的证据。

## 7. 发布脚本修改

- `.github/workflows/build.yml`、`release.yml`：普通构建与受控发布。
- `scripts/validate-version.ps1`：tag、源码和 Windows 版本资源一致。
- `scripts/build-release.ps1`：构建前 ExpectedTag 验证。
- `scripts/publish-release.ps1`：默认仅本地验证；显式授权后 draft/上传/完整回读/发布；保留确切安装包的隔离验收门槛。
- `scripts/diagnose-github.ps1`：只读状态分类，不输出凭据。
- `tests/test-release-pipeline.ps1`：真实本地验签，模拟 GitHub 失败、重试和幂等。

## 8. 客户端及版本修改

- `src/online.c`、`online.h`：真实/模拟统一 HTTP 分类，DNS/超时/TLS/代理诊断，公开仓库探测，null 标题兼容，缺失 x64 资产提示，只读测试专用传输探针。
- `src/lite.h`：诊断从 80 扩至 256 字符，独立 HTTP 状态；不改变配置格式。
- `src/ui_online.inc`、`ui_settings.inc`：明确结果、最新版本、重试、Release 链接、验签后安装确认，内存/线程失败不再静默。
- `src/update.h`、`src/app.rc`、`installer/web/web.rc`、`installer/offline/NetPilotLite.iss`：版本统一为 1.3.1。
- `tests/features_tests.inc`、`test_main.c`、`ui_features.inc`、`update_tests.c`、`resources/new.rc`：协议、界面和模拟进程验收。
- README、CHANGELOG、RELEASE、UPDATING、此报告和 .gitignore：操作说明与证据隔离。

公钥与旧版一致：SHA-256 `635fb466ec7cd573b10cea71adc0ea5825ca62d92e554636b0e7d3334685d820`。TLS 验证保持开启，仅接受指定 GitHub 资产及官方 CDN。不会拿源码 ZIP 或 Actions Artifact 替换程序。

## 9. 三种构建产物

现有 C17/VS Release x64 工程、静态 CRT：`dist/NetPilotLite-Web-Setup.exe`、`dist/NetPilotLite-Offline-Setup.exe`、`dist/NetPilotLite-Portable-x64.zip`。另有固定名称主 EXE、独立用途 `.update`、version.json、签名目录和 SHA256SUMS.txt。

主 EXE、Offline 和目录清单均使用原有 ECDSA 私钥签署。目录覆盖 Web/Offline/ZIP 和兼容资产；不是 Authenticode，首次手动启动仍可能显示未知发布者。安装包没有在本机执行。

## 10. 检查、下载与发布测试

| 场景 | 结果和证据类型 |
|---|---|
| A 无 Release | 模拟公开仓库+latest 404，显示无稳定版；真实认证空列表已核实；匿名私有保留不可见提示 |
| B 缺少安装资产 | 模拟空 assets/不匹配类型/缺签名，明确拒绝 |
| C 正确稳定 Release | 模拟解析 tag、版本、URL、大小、digest、null 标题；真实 NetPilot Release 不存在 |
| D 旧版本 | 模拟 UI 显示可用更新，未验签时不能安装 |
| E 相同版本 | 模拟 UI 显示已经是最新版本，不安装 |
| F 下载失败 | 模拟 HTTP/DNS/超时/TLS/中断/取消；可重试，重新创建缓存 |
| G 签名失败 | 真实密码学校验拒绝坏 payload/manifest/digest/版本/用途，不执行文件 |
| H 原地升级 | Windows 真实文件替换和进程启动，使用关闭真实网卡写能力的 mock GUI；不等于真实安装器验收 |
| I Clash/TUN | 当时 Clash/Mihomo 运行、Meta Tunnel Up；同一 WinHTTP 实现成功读公开 API 并从 CDN 下载 1971 字节 GitHub CLI 校验文本，未执行；未证明具体物理出口或 NetPilot 在线安装 |
| J Actions 发布 | 发布逻辑使用模拟 API 验收；真实发布/下载尚未执行 |

已确认日志：核心网卡模拟 44 项、更新协议/设置 87 项、GUI 65 项、安装器协议 32 项、发布模拟 17 项均通过。只读分流诊断 60 项通过，包含真实 IPv4/IPv6 本机回环连接，不修改网卡或创建 WFP 规则。

早期受限执行环境出现 DNS 12007 和回环连接失败，允许只读网络后 API/CDN 及回环成功；不能据此说产品网卡故障。正式安装和实际网络修改均未执行。

## 11. 签名、升级和恢复

主程序、安装器、目录签名验证通过。mock GUI 1.0.0、1.0.1、保留的 1.1.0 → 1.3.1：124 项配置/进程/更新检查通过，包括正常退出、原路径替换、启动 ACK、单实例、配置 hash 和快捷方式保留、损坏文件阻止、新版启动失败恢复及替换前后故障注入恢复。

UAC 取消仅模拟，没有实际操作；中断是进程故障注入，不是真实断电。Program Files、安装登记、新装/升级/卸载、安装器异常恢复尚无隔离 Windows 验收，不能用 mock 通过替代。详细日志位于 `artifacts/release-closure/`；环境诊断不上传私人网卡记录或签名私钥。

## 12. 仍需授权和配置

1. 明确授权正式发布，本次只交付候选。
2. 决定匿名更新渠道：授权现有仓库公开，或保留私有源码另建公开分发仓库；当前没有修改公开性。跨仓库发布还需要限定的短期 App 授权，现有 GITHUB_TOKEN 默认只访问本仓库。
3. 配置可信 Windows 签名 runner、两个环境审批与发行许可确认；DPAPI 私钥依赖原签名账号，不能直接用于临时托管 runner。
4. 在隔离 Windows 验收确切 Offline Setup，记录其 SHA-256，再批准发布 job。用户此前无隔离环境，本轮没有冒用本机作安装验收。
5. 发布后验收真实 latest → 下载 → 验签 → UAC → 原目录安装 → 启动及恢复。

状态：已编码、已编译、已模拟测试、部分真实只读网络及文件/进程验证；未正式发布，未完成真实在线升级。复现步骤见 RELEASE.md。
