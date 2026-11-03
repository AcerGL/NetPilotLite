#include "update.h"
#include "settings.h"
#include <shlobj.h>
#include <objbase.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
static unsigned passed, failed;
static WCHAR root[NP_PATH];
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (x)                                                                                               \
            ++passed;                                                                                        \
        else {                                                                                               \
            ++failed;                                                                                        \
            wprintf(L"FAIL %d %hs win32=%lu\n", __LINE__, #x, GetLastError());                               \
        }                                                                                                    \
    } while (0)
static BOOL exists(const WCHAR *p) {
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}
static void child(WCHAR *out, const WCHAR *dir, const WCHAR *name) {
    swprintf_s(out, NP_PATH, L"%s\\%s", dir, name);
}
static void copy_product(const WCHAR *folder, const WCHAR *variant) {
    WCHAR exe[NP_PATH], source[NP_PATH], dest[NP_PATH];
    child(source, root, variant);
    child(exe, source, L"NetPilotLite.exe");
    child(dest, folder, L"NetPilotLite.exe");
    CHECK(CopyFileW(exe, dest, FALSE));
    wcscat_s(exe, NP_PATH, L".update");
    wcscat_s(dest, NP_PATH, L".update");
    CHECK(CopyFileW(exe, dest, FALSE));
}
static void folder(WCHAR *out, const WCHAR *name) {
    child(out, root, name);
    CHECK(CreateDirectoryW(out, NULL));
}
static BOOL CALLBACK close_test(HWND w, LPARAM lp) {
    DWORD pid;
    GetWindowThreadProcessId(w, &pid);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    WCHAR path[NP_PATH];
    DWORD n = _countof(path);
    if (h) {
        if (QueryFullProcessImageNameW(h, 0, path, &n) && !_wcsicmp(path, (const WCHAR *)lp)) {
            SendMessageW(w, WM_COMMAND, 106, 0);
            WaitForSingleObject(h, 5000);
        }
        CloseHandle(h);
    }
    return TRUE;
}
static void close_app(const WCHAR *path) {
    EnumWindows(close_test, (LPARAM)path);
}
typedef struct WINDOWS {
    const WCHAR *path;
    int count;
    HWND window;
} WINDOWS;
static BOOL CALLBACK find_app(HWND w, LPARAM param) {
    WINDOWS *c = (WINDOWS *)param;
    WCHAR cls[80];
    GetClassNameW(w, cls, _countof(cls));
    if (wcscmp(cls, L"NetPilotLiteTestWindow1"))
        return TRUE;
    DWORD pid, n = NP_PATH;
    WCHAR p[NP_PATH];
    GetWindowThreadProcessId(w, &pid);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) {
        if (QueryFullProcessImageNameW(h, 0, p, &n) && !_wcsicmp(c->path, p)) {
            ++c->count;
            c->window = w;
        }
        CloseHandle(h);
    }
    return TRUE;
}
static WINDOWS find(const WCHAR *path) {
    WINDOWS c = {path, 0, NULL};
    EnumWindows(find_app, (LPARAM)&c);
    return c;
}
static BOOL run_app(const WCHAR *path) {
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    WCHAR cmd[NP_PATH + 2];
    si.cb = sizeof(si);
    swprintf_s(cmd, _countof(cmd), L"\"%s\"", path);
    if (!CreateProcessW(path, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        return FALSE;
    WaitForInputIdle(pi.hProcess, 10000);
    CloseHandle(pi.hThread);
    BOOL alive = WaitForSingleObject(pi.hProcess, 500) == WAIT_TIMEOUT;
    CloseHandle(pi.hProcess);
    return alive;
}
static BOOL shortcut(const WCHAR *link, const WCHAR *target, BOOL create) {
    IShellLinkW *s = NULL;
    IPersistFile *p = NULL;
    BOOL ok = FALSE;
    if (SUCCEEDED(
            CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&s)) &&
        SUCCEEDED(s->lpVtbl->QueryInterface(s, &IID_IPersistFile, (void **)&p))) {
        if (create)
            ok = SUCCEEDED(s->lpVtbl->SetPath(s, target)) && SUCCEEDED(p->lpVtbl->Save(p, link, TRUE));
        else {
            WCHAR found[NP_PATH];
            WIN32_FIND_DATAW data;
            ok = SUCCEEDED(p->lpVtbl->Load(p, link, STGM_READ)) &&
                 SUCCEEDED(s->lpVtbl->GetPath(s, found, _countof(found), &data, SLGP_RAWPATH)) &&
                 !_wcsicmp(found, target);
        }
    }
    if (p)
        p->lpVtbl->Release(p);
    if (s)
        s->lpVtbl->Release(s);
    return ok;
}
static void settings_tests(void) {
    WCHAR dir[NP_PATH], p[NP_PATH], bak[NP_PATH];
    folder(dir, L"config");
    NP_SETTINGS s, t;
    NP_ERROR e;
    NP_MOCK m;
    np_mock_init(&m);
    np_settings_defaults(&s);
    s.has[0] = s.has[1] = TRUE;
    s.binding[0] = m.snapshot.adapters[0].identity;
    s.binding[1] = m.snapshot.adapters[2].identity;
    s.theme = 4;
    s.mask = 62;
    s.window = (RECT){31, 42, 1111, 888};
    s.close_tray = FALSE;
    s.maximized = TRUE;
    s.custom_card = RGB(10, 20, 30);
    s.card_opacity = 79;
    s.font_scale = 115;
    s.density = 1;
    s.animations = FALSE;
    s.tray_style = 3;
    s.tray_opacity = 81;
    s.tray_size = 420;
    s.tray_icon = 1;
    s.tray_round = FALSE;
    s.tray_accent = RGB(100, 170, 220);
    wcscpy_s(s.cover, NP_PATH, L"C:\\图片\\自定义封面.jpg");
    wcscpy_s(s.image, NP_PATH, L"C:\\图片\\自定义背景.jpg");
    s.rule_count = 2;
    wcscpy_s(s.rules[0].path, NP_PATH, L"C:\\Apps\\中文 空格\\Browser.exe");
    s.rules[0].target = 1;
    s.rules[0].enabled = TRUE;
    wcscpy_s(s.rules[1].path, NP_PATH, L"D:\\Tools\\Game.exe");
    s.rules[1].target = 0;
    s.rules[1].enabled = FALSE;
    CHECK(np_settings_save(&s, dir, &e));
    CHECK(np_settings_load(&t, dir, &e));
    CHECK(t.card_opacity == 79 && t.font_scale == 115 && t.density == 1 && !t.animations);
    CHECK(t.tray_style == 3 && t.tray_opacity == 81 && t.tray_size == 420 && t.tray_icon == 1 &&
          !t.tray_round && t.tray_accent == s.tray_accent);
    CHECK(!wcscmp(t.cover, s.cover));
    CHECK(t.rule_count == 2 && !wcscmp(t.rules[0].path, s.rules[0].path) && t.rules[0].target == 1 &&
          t.rules[0].enabled);
    CHECK(!wcscmp(t.rules[1].path, s.rules[1].path) && t.rules[1].target == 0 && !t.rules[1].enabled);
    CHECK(np_identity_equal(&s.binding[0], &t.binding[0]) && np_identity_equal(&s.binding[1], &t.binding[1]));
    CHECK(t.theme == 4 && t.mask == 62 && !t.close_tray && t.maximized && t.custom_card == s.custom_card &&
          !wcscmp(t.image, s.image) && !memcmp(&s.window, &t.window, sizeof(RECT)));
    child(p, dir, L"settings.ini");
    WritePrivateProfileStringW(L"Other", L"FuturePreference", L"preserved", p);
    CHECK(np_settings_save(&t, dir, &e));
    WCHAR text[64];
    GetPrivateProfileStringW(L"Other", L"FuturePreference", L"", text, 64, p);
    CHECK(!wcscmp(text, L"preserved"));
    child(bak, dir, L"settings.ini.bak");
    CHECK(exists(bak));
    WritePrivateProfileStringW(L"Config", L"SchemaVersion", L"1", p);
    CHECK(np_settings_load(&t, dir, &e));
    child(bak, dir, L"settings.ini.migration.bak");
    CHECK(exists(bak));
    CHECK(np_settings_save(&t, dir, &e));
    CHECK(GetPrivateProfileIntW(L"Config", L"SchemaVersion", 0, p) == 2);
    CHECK(np_settings_load(&t, dir, &e) && t.rule_count == 2 && t.rules[0].enabled);
    t.rule_count = 1;
    CHECK(np_settings_save(&t, dir, &e));
    GetPrivateProfileStringW(L"ObservationRule1", L"Path", L"missing", text, 64, p);
    CHECK(!wcscmp(text, L"missing"));
    WritePrivateProfileStringW(L"ObservationRule0", L"Target", L"500", p);
    CHECK(np_settings_load(&t, dir, &e) && t.rules[0].target == 2);
    WritePrivateProfileStringW(L"RoutingObservation", L"Count", L"999", p);
    CHECK(np_settings_load(&t, dir, &e) && t.rule_count == 0);
    CHECK(np_settings_save(&s, dir, &e));
    WritePrivateProfileStringW(L"Config", L"SchemaVersion", L"99", p);
    BYTE before[32], after[32];
    np_hash_file(p, before);
    CHECK(!np_settings_load(&t, dir, &e) && t.binding_invalid);
    CHECK(!np_settings_save(&s, dir, &e));
    np_hash_file(p, after);
    CHECK(!memcmp(before, after, 32));
    wprintf(L"Configuration: binding/theme/image/window/observation-rule/unknown-key retention, invalid rule "
            L"input, migration and future-schema "
            L"protection verified.\n");
}
int wmain(int argc, WCHAR **argv) {
    _setmode(_fileno(stdout), _O_U8TEXT);
    if (argc != 2)
        return 2;
    GetFullPathNameW(argv[1], NP_PATH, root, NULL);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    settings_tests();
    WCHAR src[NP_PATH], newexe[NP_PATH], badexe[NP_PATH], dir[NP_PATH], target[NP_PATH], link[NP_PATH],
        config[NP_PATH], journal[NP_PATH], backup[NP_PATH];
    child(src, root, L"new");
    child(newexe, src, L"NetPilotLite.exe");
    child(src, root, L"fail");
    child(badexe, src, L"NetPilotLite.exe");
    NP_RELEASE release;
    NP_ERROR e;
    CHECK(np_verify_release(newexe, &release, &e));
    CHECK(release.version[0] == 1 && release.version[1] == 5 && release.version[2] == 0);
    folder(dir, L"中文 空格 upgrade");
    copy_product(dir, L"old");
    child(target, dir, L"NetPilotLite.exe");
    child(link, dir, L"桌面快捷方式.lnk");
    CHECK(shortcut(link, target, TRUE));
    child(config, dir, L"user-preferences.bin");
    BYTE content[] = {1, 2, 3, 4, 5};
    CHECK(np_write_exact(config, content, sizeof(content)));
    BYTE confhash[32], afterhash[32];
    np_hash_file(config, confhash);
    CHECK(run_app(target));
    CHECK(np_updater_apply(newexe, target, FALSE) == 0);
    DWORD version[4];
    CHECK(np_file_version(target, version) && version[1] == 5 && version[2] == 0);
    CHECK(np_release_matches(target, &release));
    CHECK(shortcut(link, target, FALSE));
    np_hash_file(config, afterhash);
    CHECK(!memcmp(confhash, afterhash, 32));
    CHECK(!run_app(target));
    CHECK(find(target).count == 1);
    close_app(target);
    child(journal, dir, L".NetPilotLite.update-state");
    child(backup, dir, L".NetPilotLite.previous");
    CHECK(!exists(journal) && !exists(backup));
    wprintf(L"Running legacy mock GUI 1.0.0 -> mock GUI 1.5.0: fixed path, normal exit, startup ACK, "
            L"shortcut and configuration retained.\n");
    /* This is the actual previous 1.0.1 mock GUI retained from the published baseline. */
    folder(dir, L"prior-101-upgrade");
    copy_product(dir, L"prior");
    child(target, dir, L"NetPilotLite.exe");
    CHECK(np_file_version(target, version) && version[1] == 0 && version[2] == 1);
    NP_SETTINGS prior_settings;
    np_settings_defaults(&prior_settings);
    prior_settings.rule_count = 1;
    prior_settings.rules[0].target = 1;
    prior_settings.rules[0].enabled = TRUE;
    wcscpy_s(prior_settings.rules[0].path, NP_PATH, L"C:\\Test Apps\\Browser.exe");
    CHECK(np_settings_save(&prior_settings, dir, &e));
    child(config, dir, L"settings.ini");
    CHECK(np_hash_file(config, confhash));
    CHECK(run_app(target));
    CHECK(np_updater_apply(newexe, target, FALSE) == 0);
    CHECK(np_file_version(target, version) && version[1] == 5 && version[2] == 0);
    CHECK(np_hash_file(config, afterhash) && !memcmp(confhash, afterhash, 32));
    CHECK(np_settings_load(&prior_settings, dir, &e) && prior_settings.rule_count == 1 &&
          prior_settings.rules[0].enabled);
    CHECK(find(target).count == 1);
    close_app(target);
    wprintf(
        L"Running previous 1.0.1 mock GUI -> 1.5.0: same path replacement and single instance verified.\n");
    child(src, root, L"stagea");
    child(target, src, L"NetPilotLite.exe");
    if (exists(target)) {
        folder(dir, L"stage-a-build-upgrade");
        copy_product(dir, L"stagea");
        child(target, dir, L"NetPilotLite.exe");
        CHECK(np_file_version(target, version) && version[1] == 1 && version[2] == 0 && version[3] == 0);
        CHECK(run_app(target));
        CHECK(np_updater_apply(newexe, target, FALSE) == 0);
        CHECK(np_file_version(target, version) && version[1] == 5 && version[2] == 0 && version[3] == 0);
        CHECK(find(target).count == 1);
        close_app(target);
        wprintf(
            L"Retained Stage A 1.1.0.0 mock GUI -> final 1.5.0: same path and single instance verified.\n");
    } else
        wprintf(L"Optional retained Stage A mock fixture not supplied; Stage A upgrade not run.\n");
    folder(dir, L"corrupt");
    copy_product(dir, L"new");
    WCHAR corrupt[NP_PATH];
    child(corrupt, dir, L"NetPilotLite.exe");
    HANDLE f = CreateFileW(corrupt, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    SetFilePointer(f, 512, NULL, FILE_BEGIN);
    DWORD wrote;
    BYTE x = 0xff;
    WriteFile(f, &x, 1, &wrote, NULL);
    CloseHandle(f);
    CHECK(!np_verify_release(corrupt, &release, &e));
    wprintf(L"Corrupt payload blocked before mutation.\n");
    folder(dir, L"rollback");
    copy_product(dir, L"old");
    child(target, dir, L"NetPilotLite.exe");
    NP_RELEASE original;
    CHECK(np_verify_release(target, &original, &e));
    CHECK(np_updater_apply(badexe, target, FALSE) == ERROR_APP_INIT_FAILURE);
    CHECK(np_release_matches(target, &original));
    close_app(target);
    wprintf(L"Signed candidate startup failure: actual old EXE restored and restarted.\n");
    for (int k = 0; k < 2; ++k) {
        folder(dir, k ? L"crash-replaced" : L"crash-prepared");
        copy_product(dir, L"old");
        child(target, dir, L"NetPilotLite.exe");
        CHECK(np_verify_release(target, &original, &e));
        SetEnvironmentVariableW(L"NP_UPDATE_TEST_FAULT", k ? L"replaced" : L"prepared");
        CHECK(np_updater_apply(newexe, target, FALSE) == (k ? 91 : 90));
        SetEnvironmentVariableW(L"NP_UPDATE_TEST_FAULT", NULL);
        child(journal, dir, L".NetPilotLite.update-state");
        CHECK(exists(journal));
        CHECK(np_updater_apply(NULL, target, TRUE) == 0);
        CHECK(np_release_matches(target, &original));
        CHECK(!exists(journal));
        close_app(target);
    }
    wprintf(L"Injected updater interruption before/after replacement: durable journal recovery restored a "
            L"consistent old version.\n");
    folder(dir, L"crash-committed");
    copy_product(dir, L"old");
    child(target, dir, L"NetPilotLite.exe");
    SetEnvironmentVariableW(L"NP_UPDATE_TEST_FAULT", L"committed");
    CHECK(np_updater_apply(newexe, target, FALSE) == 92);
    SetEnvironmentVariableW(L"NP_UPDATE_TEST_FAULT", NULL);
    CHECK(np_updater_apply(NULL, target, TRUE) == 0);
    CHECK(np_verify_release(target, &release, &e) && release.version[1] == 5 && release.version[2] == 0);
    CHECK(find(target).count == 1);
    close_app(target);
    folder(dir, L"signature");
    copy_product(dir, L"new");
    child(corrupt, dir, L"NetPilotLite.exe.update");
    f = CreateFileW(corrupt, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    SetFilePointer(f, 80, NULL, FILE_BEGIN);
    WriteFile(f, &x, 1, &wrote, NULL);
    CloseHandle(f);
    child(corrupt, dir, L"NetPilotLite.exe");
    CHECK(!np_verify_release(corrupt, &release, &e));
    CHECK(np_updater_apply(newexe, L"C:\\WrongName.exe", FALSE) == ERROR_BAD_PATHNAME);
    folder(dir, L"cancel");
    copy_product(dir, L"old");
    child(target, dir, L"NetPilotLite.exe");
    CHECK(np_verify_release(target, &original, &e));
    CHECK(run_app(target));
    SetEnvironmentVariableW(L"NP_UPDATE_TEST_FAULT", L"cancel");
    CHECK(np_updater_apply(newexe, target, FALSE) == ERROR_CANCELLED);
    SetEnvironmentVariableW(L"NP_UPDATE_TEST_FAULT", NULL);
    CHECK(np_release_matches(target, &original) && find(target).count == 1);
    close_app(target);
    folder(dir, L"busy");
    copy_product(dir, L"old");
    child(target, dir, L"NetPilotLite.exe");
    CHECK(np_verify_release(target, &original, &e));
    CHECK(run_app(target));
    WINDOWS win = find(target);
    CHECK(win.window != NULL);
    SendMessageW(win.window, WM_COMMAND, 900, 0);
    CHECK(np_updater_apply(newexe, target, FALSE) == ERROR_BUSY);
    CHECK(np_release_matches(target, &original) && find(target).count == 1);
    SendMessageW(win.window, WM_COMMAND, 901, 0);
    close_app(target);
    folder(dir, L"locked");
    copy_product(dir, L"old");
    child(target, dir, L"NetPilotLite.exe");
    CHECK(np_verify_release(target, &original, &e));
    HANDLE locked = CreateFileW(target, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(locked != INVALID_HANDLE_VALUE);
    CHECK(np_updater_apply(newexe, target, FALSE) != 0);
    if (locked != INVALID_HANDLE_VALUE)
        CloseHandle(locked);
    CHECK(np_release_matches(target, &original));
    wprintf(L"Single instance, simulated cancellation (not a real UAC prompt), busy normal-exit refusal, and "
            L"actual replacement-sharing failure verified.\n");
    wprintf(L"Update and configuration checks: %u passed, %u failed. No real adapter API is present in "
            L"launched product fixtures.\n",
            passed, failed);
    CoUninitialize();
    return failed ? 1 : 0;
}
