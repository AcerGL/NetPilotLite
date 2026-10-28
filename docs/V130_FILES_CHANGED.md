# 1.3.0 修改文件

相对 `work/v1.2.0-before-installers/NetPilotLite-1.2.0-source.zip`。本表不列临时构建产物。

- 修改 `.github/workflows/release.yml`
- 修改 `CHANGELOG.md`
- 新增 `docs/INSTALLATION.md`
- 新增 `docs/INSTALLER_ACCEPTANCE.md`
- 新增 `docs/MIGRATION.md`
- 修改 `docs/RELEASE.md`
- 新增 `docs/UPDATING.md`
- 新增 `installer/build.cmd`
- 新增 `installer/common/guard.c`
- 新增 `installer/common/guard.rc`
- 新增 `installer/INNO-LICENSE.txt`
- 新增 `installer/offline/NetPilotLite.iss`
- 新增 `installer/offline/small.bmp`
- 新增 `installer/offline/wizard.bmp`
- 新增 `installer/web/cache.c`
- 新增 `installer/web/main.c`
- 新增 `installer/web/web.manifest`
- 新增 `installer/web/web.rc`
- 新增 `NetPilotLite.InstallGuard.vcxproj`
- 修改 `NetPilotLite.sln`
- 新增 `NetPilotLite.WebSetup.vcxproj`
- 修改 `README.md`
- 新增 `scripts/build-release.ps1`
- 新增 `scripts/test-installer.ps1`
- 修改 `src/app.c`
- 修改 `src/app.rc`
- 修改 `src/main.c`
- 修改 `src/online.c`
- 修改 `src/online.h`
- 修改 `src/trust.c`
- 修改 `src/ui_online.inc`
- 修改 `src/update.h`
- 修改 `tests/features_tests.inc`
- 新增 `tests/install_guard_tests.py`
- 新增 `tests/installer_tests.inc`
- 修改 `tests/resources/new.rc`
- 修改 `tests/test_main.c`
- 修改 `tests/update_tests.c`
- 修改 `tools/release.c`

## 保留的功能实现

- `src/backend_win.c`：与基线字节一致
- `src/routing.c`：与基线字节一致
- `src/visual.c`：与基线字节一致
- `src/core.c`：与基线字节一致
- `src/settings.c`：与基线字节一致
- `src/ui_render.inc`：与基线字节一致
- `src/ui_tray.inc`：与基线字节一致
