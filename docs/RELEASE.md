> 1.4.0 当前流程见 [私有更新说明](PRIVATE_UPDATES.md)。以下为 1.3.x 历史记录，不能用于当前发布。

# NetPilot Lite 1.3.1 发布流程

当前为签名构建候选；正式发布须由用户明确授权。源码上传、CI Artifact、Git tag 与正式 Release 是四件不同的事。客户端仅查询稳定 Release。

## 本地构建和验证

要求 Visual Studio 2022 C++、Windows SDK、可信 Inno Setup 7.1.0 x64、已有 DPAPI 签名私钥。不要重新生成公钥，不把私钥上传 GitHub。

```powershell
./scripts/build-release.ps1 -SigningKey '受信任本机路径/private.dpapi' -InnoCompiler '可信工具路径/ISCC.exe' -ExpectedTag v1.3.1
./scripts/publish-release.ps1 -Tag v1.3.1 -Directory dist -VerifierPath bin/Release/NetPilotLite.Tests.exe
./tests/test-release-pipeline.ps1 -Tag v1.3.1
```

第二条默认只进行本地签名、SHA-256、目录清单、版本检查，绝不发布。第三条对 GitHub 调用做模拟，不代表真实发布成功。

输出固定名称的 Web Setup、Offline Setup、Portable x64 ZIP，以及主 EXE、独立用途签名、version.json、release-catalog.json/.update、SHA256SUMS.txt。ECDSA 公钥沿用旧版，用户设置、绑定和网卡当前状态不变。

## Actions 设置（当前尚未核实这些仓库管理设置）

- `build.yml`：main 提交、PR 或手动触发，Windows Server 2022，VS x64 构建和只读测试。只有 contents:read，不发布软件。
- `release.yml`：推送 `v*` 标签，或输入已有 tag 手动执行。手动 `publish=false` 只构建；没有自动签名和发布。tag 在构建之前必须匹配源码及 Windows 版本资源。
- 同一仓库/标签的发布串行；不取消已在执行的更新任务。每个后续 job 检出 build job 固定的 commit，而非重新解析可移动的标签。
- `release-signing` 环境：配置审批人；设置 RELEASE_SIGNING_ENABLED、LICENSE_CONFIRMED、ASSETS_CLEARED、RELEASE_ALLOWED 为 true 后才能进入发布阶段。禁止用这些开关伪造尚未完成的验收。
- 自托管 Windows x64 runner 标签 `netpilot-signing`：需隔离的受信任签名账号、VS 和 Inno；`SIGNING_KEY_PATH` secret 只存本机私钥路径，`INNO_COMPILER` variable 为已验证编译器路径。DPAPI 私钥依赖原账号/机器，不能直接放到临时 GitHub 托管 runner 使用。未配置 runner 时，签名 job 会等待，不代表完成。
- `release-publication` 环境：配置审批人；下载并在隔离 Windows 中验收该次签名 Artifact 后，将其确切 Offline Setup SHA-256 写入环境变量 `ACCEPTED_OFFLINE_SHA256`。再批准发布 job。只该 job 有 contents:write。
- 签名和发布分离，不在 PR 执行带私钥的任务。受保护 tag/环境审批需仓库管理者实际设置，YAML 不能替代访问控制。

## 正式发布（得到授权且完成验收以后）

在已认证的正常 Git checkout 中，先确认提交与 tag；不要对尚未批准的版本执行以下命令：

```powershell
git tag v1.3.1
git push origin v1.3.1
# 已有 tag 可手动构建（不会发布）：
gh workflow run release.yml --repo AcerGL/NetPilotLite -f tag=v1.3.1 -f publish=false
# 已批准、设置完成后才使用 publish=true。
```

发布 job 使用内置 GITHUB_TOKEN，不需要长期 PAT。脚本流程：签名检查 → 检查已有 tag → 建立 draft → 上传固定十项资产 → 重新下载每一项并比较大小和 SHA-256 → 发布稳定版 → 回查 latest。上传或回读失败保留 draft；重跑复用已经上传的资产但验证其内容，不覆盖已发布资产。若已发布资产不完整或不匹配，明确失败，需人工审查，不能用 `--clobber` 掩盖。

同一批文件的发布重试应使用“重新运行失败的 publish job”；重新执行签名构建可能因打包时间改变哈希，必须重新验收新的确切文件。

本地人工发布也可使用脚本（在可信环境自行完成 GitHub CLI 认证，向进程安全提供短期凭据，不写入项目）：

```powershell
./scripts/publish-release.ps1 -Tag v1.3.1 -Directory dist -VerifierPath bin/Release/NetPilotLite.Tests.exe -Repository AcerGL/NetPilotLite -AcceptedOfflineSHA256 '实际验收通过的64位SHA256' -PublishApproved
```

## 私有仓库与公开更新

2026-10-28 已认证查询确认 `AcerGL/NetPilotLite` 私有，Release 列表为空，tag 列表为空；latest 为 404。匿名客户端无法读取私有 Releases，即使给开发工具授权也不会改变最终用户的访问条件。

可选择保留私有源码并使用单独的公开分发仓库，或明确授权修改现有仓库公开性；当前两者均未执行。使用单独公开分发仓库时，需要相应 GitHub App 安装授权和发布 job 的短期跨仓库授权；当前脚本支持指定仓库，但默认 GITHUB_TOKEN 仅适用于本仓库，不能宣称跨仓库部署已完成。未做决定前继续使用签名本地更新。

没有 Authenticode 证书：应用内信任来自固定 ECDSA 公钥，首次手动运行仍可能显示未知发布者/SmartScreen。不能关闭系统防护来绕过。艺术素材、许可与 Inno 商业使用条件保留原发布确认要求。

官方依据：[GitHub Releases API](https://docs.github.com/en/rest/releases/releases)、[Actions concurrency](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency)、[WinHTTP 自动代理](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpopen)。
