# 旧版本识别与迁移

现有代码审查结论：Lite 1.x 正式配置位于 LocalAppData，旧 1.0 还可能读取 HKCU 网卡绑定。旧 NetPilot .NET/MSI 是独立产品，Program Files\NetPilot，历史 UpgradeCode 为 `{C6563F64-7D7B-42FD-890A-5EE1DC5E5C82}`；不能当作新的 Lite AppId。

1. 正式 Inno：以固定 AppId 的 64-bit HKLM 卸载项及原目录为准，同目录升级。已登记目录与用户选择不一致时拒绝。
2. 便携版：不自动认作安装版。用户选择源目录并勾选迁移，只有有效发布签名的主程序才可原地迁移为安装版。1.2.0 已签名发布副本可通过身份验证。
3. 开发版：存在 src/obj/solution 等源工程特征时拒绝；无签名 EXE 拒绝。仅有同名文件不构成可信身份。未知非空目录拒绝，空目录可首次安装。
4. 旧 MSI：检查实际注册的 NetPilot/WindowsInstaller 记录并给出明确提示，不调用 msiexec、不删 Installer 缓存或注册表，也不移除旧任务/服务。用户应先审核旧恢复事务再手动处理旧产品，Lite 可在独立目录安装。
5. 若出现未知旧 sidecar `settings.ini`，阻止迁移并要求先备份核实。审查未发现已正式交付 Lite 将生产配置写在 EXE 同目录的版本，**未提供任意旧 sidecar 格式的自动迁移器**；不能盲目覆盖 LocalAppData 的现有数据。

本版本应用分流仍是 Monitor Only，没有持久 WFP 过滤器、驱动、SYSTEM 服务或任务需要迁移。关闭 GUI 不会移除/改变网络策略。配置保存继续使用已有的 schema、备份和未知键保留机制。
