#include "public_setup.h"
static unsigned public_progress;
static void public_tick(void *ctx, const WCHAR *name, unsigned done, unsigned count, ULONGLONG bytes,
                        ULONGLONG total) {
    (void)ctx;
    (void)name;
    CHECK(done <= count && bytes <= total);
    public_progress++;
}
static int public_package_test(const WCHAR *input, BOOL transport) {
    WCHAR dir[NP_PATH], path[NP_PATH], setup[NP_PATH], stage[NP_PATH];
    GetFullPathNameW(input, NP_PATH, dir, NULL);
    NP_MANIFEST *m = calloc(1, sizeof(*m));
    NP_ERROR e = {0};
    NP_RELEASE sig;
    swprintf_s(setup, NP_PATH, L"%s\\%s", dir, NP_SETUP_NAME);
    CHECK(np_verify_setup(setup, &sig, &e));
    CHECK(np_manifest_load(dir, sig.version, m, &e));
    CHECK(np_manifest_verify(dir, m, &e));
    CHECK(np_install_relative(L"bin/dependencies/example.dll"));
    CHECK(np_install_relative(L"resources/theme.dat"));
    const WCHAR *bad[] = {L"../evil.exe",     L"bin/../../evil",     L"/absolute",
                          L"C:/Windows/evil", L"bin/CON.dll",        L"bin/a.",
                          L"bin//a",          L"bin/a:stream",       L"bin/a\\b",
                          L"settings.ini",    L"resources/COM1.dat", L"bin/.hidden"};
    for (unsigned i = 0; i < _countof(bad); i++)
        CHECK(!np_install_relative(bad[i]));
    CHECK(!np_setup_cleanup(dir));
    if (transport) {
        swprintf_s(path, NP_PATH, L"%s\\release.json", dir);
        FILE *f = NULL;
        _wfopen_s(&f, path, L"rb");
        CHECK(f != NULL);
        if (!f) {
            free(m);
            return 1;
        }
        char *json = calloc(1, 65537);
        size_t n = fread(json, 1, 65536, f);
        fclose(f);
        NP_SETTINGS settings;
        np_settings_defaults(&settings);
        NP_PUBLIC_RELEASE *r = calloc(1, sizeof(*r));
        CHECK(np_public_parse(json, n, &settings.update, r, &e));
        ZeroMemory(&np_http_fixture, sizeof(np_http_fixture));
        np_http_fixture.active = TRUE;
        np_http_fixture.public_assets = TRUE;
        np_http_fixture.status = 200;
        np_http_fixture.json = json;
        wcscpy_s(np_http_fixture.exe, NP_PATH, dir);
        wcscpy_s(np_http_fixture.directory, NP_PATH, dir);
        volatile LONG cancel = 0;
        CHECK(np_public_check(&settings.update, NULL, r, &cancel, &e));
        CHECK(!np_public_check(&settings.update, L"9.9.9", r, &cancel, &e) &&
              e.win32 == ERROR_REVISION_MISMATCH);
        CHECK(np_public_check(&settings.update, NULL, r, &cancel, &e));
        CHECK(np_public_download_setup(r, setup, &cancel, NULL, NULL, &e));
        WCHAR parent[NP_PATH];
        wcscpy_s(parent, NP_PATH, setup);
        WCHAR *slash = wcsrchr(parent, L'\\');
        if (slash)
            *slash = 0;
        CHECK(np_setup_cleanup(parent));
        CHECK(np_setup_stage(r, stage, m, &cancel, public_tick, NULL, &e));
        CHECK(public_progress > 0);
        CHECK(np_manifest_verify(stage, m, &e));
        CHECK(np_setup_cleanup(stage));
        cancel = 1;
        CHECK(!np_setup_stage(r, stage, m, &cancel, NULL, NULL, &e) && e.win32 == ERROR_CANCELLED &&
              !stage[0]);
        cancel = 0;
        np_http_fixture.interrupt = TRUE;
        CHECK(!np_setup_stage(r, stage, m, &cancel, NULL, NULL, &e) && !stage[0]);
        np_http_fixture.interrupt = FALSE;
        CHECK(np_setup_stage(r, stage, m, &cancel, NULL, NULL, &e));
        swprintf_s(path, NP_PATH, L"%s\\NetPilotLite.exe", stage);
        HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        DWORD wrote;
        BYTE b = 0;
        CHECK(h != INVALID_HANDLE_VALUE && WriteFile(h, &b, 1, &wrote, NULL));
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        CHECK(!np_manifest_verify(stage, m, &e) && e.win32 == ERROR_CRC);
        swprintf_s(path, NP_PATH, L"%s\\manifest.json.update", stage);
        h = CreateFileW(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        CHECK(h != INVALID_HANDLE_VALUE && WriteFile(h, &b, 1, &wrote, NULL));
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        CHECK(!np_manifest_load(stage, NULL, m, &e) && e.win32 == NP_SIGNATURE_ERROR);
        CHECK(np_setup_cleanup(stage));
        np_http_fixture.status = 404;
        CHECK(!np_public_check(&settings.update, NULL, r, &cancel, &e) && e.http_status == 404);
        np_http_fixture.status = 12002;
        CHECK(!np_public_check(&settings.update, NULL, r, &cancel, &e) && e.win32 == 12002);
        np_http_fixture.status = 200;
        CHECK(np_public_check(&settings.update, NULL, r, &cancel, &e));
        ZeroMemory(&np_http_fixture, sizeof(np_http_fixture));
        free(r);
        free(json);
    }
    free(m);
    wprintf(L"Public signed manifest/transport: %u passed, %u failed. No network mutation.\n", passed,
            failed);
    return failed ? 1 : 0;
}
static int public_probe(const WCHAR *directory) {
    NP_SETTINGS s;
    np_settings_defaults(&s);
    NP_PUBLIC_RELEASE *r = calloc(1, sizeof(*r));
    NP_MANIFEST *m = calloc(1, sizeof(*m));
    NP_ERROR e = {0};
    volatile LONG cancel = 0;
    WCHAR stage[NP_PATH];
    /* Anonymous real HTTPS: download/verify only; never install or launch. */
    BOOL ok = r && m && np_public_check(&s.update, NULL, r, &cancel, &e) &&
              np_setup_stage(r, stage, m, &cancel, NULL, NULL, &e);
    if (ok) {
        wprintf(L"Anonymous Release %s: %u runtime files verified. Stage=%s\n", r->info.tag, m->count, stage);
        if (directory && *directory) {
            FILE *f = NULL;
            _wfopen_s(&f, directory, L"w, ccs=UTF-8");
            if (f) {
                fwprintf(f, L"%s", stage);
                fclose(f);
            }
        } else
            np_setup_cleanup(stage);
    } else
        wprintf(L"Public HTTPS probe FAILED: Windows=%lu HTTP=%lu %s\n", e.win32, e.http_status, e.operation);
    free(r);
    free(m);
    return ok ? 0 : 1;
}
