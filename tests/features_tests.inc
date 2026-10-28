#include "online.h"
#include "json.h"
#include "apps.h"
#include "settings.h"
static const char release_json[] =
    "{\"tag_name\":\"v2.0.0\",\"name\":\"Test release\",\"body\":\"# Notes\\nChinese: \\u4e2d\\u6587 "
    "\\ud83c\\udf38\",\"published_at\":\"2026-10-25T00:00:00Z\",\"draft\":false,\"prerelease\":false,"
    "\"assets\":[{\"name\":\"NetPilotLite.exe\",\"browser_download_url\":\"https://github.com/AcerGL/"
    "NetPilotLite/releases/download/v2.0.0/"
    "NetPilotLite.exe\",\"size\":12345,\"digest\":null},{\"name\":\"NetPilotLite.exe.update\",\"browser_"
    "download_url\":\"https://github.com/AcerGL/NetPilotLite/releases/download/v2.0.0/"
    "NetPilotLite.exe.update\",\"size\":128}]}";
static int feature_tests(const WCHAR *fixture) {
    DWORD a1[4], b[4];
    CHECK(np_semver(L"v1.10.0", a1) && np_semver(L"1.9.99", b) && np_version_compare(a1, b) > 0);
    const WCHAR *bad[] = {L"",           L"1.2",         L"1.2.3.4", L"01.2.3",    L"1.02.3", L"1.2.03",
                          L"1.2.3-beta", L"1.2.3+build", L"-1.2.3",  L"65536.1.1", L"1.2.3 "};
    for (int i = 0; i < _countof(bad); ++i)
        CHECK(!np_semver(bad[i], a1));
    CHECK(np_repo_valid(L"AcerGL", L"NetPilotLite"));
    CHECK(!np_repo_valid(L"../x", L"a"));
    CHECK(!np_repo_valid(L"user", L".."));
    CHECK(!np_repo_valid(L"u@github.com", L"x"));
    NP_SETTINGS settings;
    np_settings_defaults(&settings);
    NP_ONLINE_RELEASE *r = calloc(1, sizeof(*r));
    NP_ERROR e = {0};
    CHECK(np_release_json(release_json, strlen(release_json), &settings.update, r, &e));
    CHECK(wcsstr(r->body, L"中文") != NULL);
    CHECK(np_asset_url(r->url, &settings.update, r->tag, L"NetPilotLite.exe"));
    const WCHAR *urls[] = {
        L"http://github.com/AcerGL/NetPilotLite/releases/download/v2.0.0/NetPilotLite.exe",
        L"https://github.com.evil/AcerGL/NetPilotLite/releases/download/v2.0.0/NetPilotLite.exe",
        L"https://github.com/AcerGL/NetPilotLite/releases/download/v2.0.0/../NetPilotLite.exe",
        L"https://github.com/AcerGL/NetPilotLite/releases/download/v2.0.0/NetPilotLite.exe?x"};
    for (int i = 0; i < _countof(urls); ++i)
        CHECK(!np_asset_url(urls[i], &settings.update, r->tag, L"NetPilotLite.exe"));
    CHECK(np_redirect_url(L"https://release-assets.githubusercontent.com/signed?token=example"));
    CHECK(!np_redirect_url(L"https://release-assets.githubusercontent.com.evil/a"));
    CHECK(!np_redirect_url(L"http://objects.githubusercontent.com/a"));
    CHECK(!np_redirect_url(L"https://u:p@objects.githubusercontent.com/a"));
    CHECK(!np_redirect_url(L"https://objects.githubusercontent.com:444/a"));
    CHECK(!np_redirect_url(L"https://127.0.0.1/a"));
    NP_JSON j;
    const char *invalid[] = {"{",  "[1,]",        "{\"a\":1,}", "01",
                             "1e", "\"\\uXX12\"", "{}garbage",  "{\"a\":false true}"};
    for (int i = 0; i < _countof(invalid); ++i)
        CHECK(!np_json_parse(&j, invalid[i], strlen(invalid[i])));
    CHECK(np_json_parse(&j, "{\"a\":1,\"\\u0061\":2}", strlen("{\"a\":1,\"\\u0061\":2}")));
    CHECK(np_json_field(&j, 0, "a") == -2);
    np_json_free(&j);
    CHECK(np_json_parse(&j, "\"\\uD800\"", 8));
    WCHAR decoded[50];
    CHECK(!np_json_string(&j, 0, decoded, 50));
    np_json_free(&j);
    char changed[4096];
    strcpy_s(changed, 4096, release_json);
    char *v = strstr(changed, "false");
    memcpy(v, "true ", 5);
    CHECK(!np_release_json(changed, strlen(changed), &settings.update, r, &e));
    strcpy_s(changed, 4096, release_json);
    v = strstr(changed, "12345");
    memcpy(v, "0    ", 5);
    CHECK(!np_release_json(changed, strlen(changed), &settings.update, r, &e));
    strcpy_s(changed, 4096, release_json);
    v = strstr(changed, "v2.0.0");
    memcpy(v, "v2.0.x", 6);
    CHECK(!np_release_json(changed, strlen(changed), &settings.update, r, &e));
    WCHAR path[NP_PATH];
    CHECK(np_display_icon_path(L"\"C:\\Apps Space\\程序.exe\",-2", path, NP_PATH) &&
          !wcscmp(path, L"C:\\Apps Space\\程序.exe"));
    CHECK(np_display_icon_path(L"C:\\Apps\\app.exe,0", path, NP_PATH));
    CHECK(!np_display_icon_path(L"\"C:\\Apps\\app.exe\",evil", path, NP_PATH));
    CHECK(!np_display_icon_path(L"\\\\server\\app.exe", path, NP_PATH));
    CHECK(!np_display_icon_path(L"C:\\a\\..\\app.exe", path, NP_PATH));
    CHECK(!np_display_icon_path(L"\"C:\\a.exe\" /uninstall", path, NP_PATH));
    GetModuleFileNameW(NULL, path, NP_PATH);
    CHECK(np_app_exe(path));
    CHECK(!np_app_exe(L"C:\\missing.exe"));
    WCHAR root[NP_PATH];
    WCHAR run[100];
    swprintf_s(run,100,L"artifacts\\feature-config-%lu-%llu",GetCurrentProcessId(),GetTickCount64());
    GetFullPathNameW(run, NP_PATH, root, NULL);
    CreateDirectoryW(root, NULL);
    NP_SETTINGS loaded;
    settings.tray_density = 1;
    settings.update.startup = TRUE;
    wcscpy_s(settings.update.last_check, 40, L"2026-10-25 12:00:00");
    settings.rule_count = 1;
    wcscpy_s(settings.rules[0].family, 128, L"Test_pkg");
    wcscpy_s(settings.rules[0].aumid, 256, L"Test_pkg!App");
    wcscpy_s(settings.rules[0].label, 256, L"包身份");
    CHECK(np_settings_save(&settings, root, &e));
    CHECK(np_settings_load(&loaded, root, &e));
    CHECK(loaded.tray_density == 1 && loaded.update.startup && !wcscmp(loaded.update.owner, L"AcerGL") &&
          !wcscmp(loaded.rules[0].aumid, L"Test_pkg!App"));
    NP_RULE_RESULT rr = np_rule_evaluate(&loaded.rules[0], NULL, NULL, FALSE);
    CHECK(rr == NP_RULE_OFF);
    loaded.rules[0].enabled = TRUE;
    CHECK(np_rule_evaluate(&loaded.rules[0], NULL, NULL, FALSE) == NP_RULE_UNKNOWN);
    np_http_fixture.active = TRUE;
    np_http_fixture.json = release_json;
    volatile LONG cancel = 0;
    DWORD statuses[] = {404, 403, 429, 407, 500};
    for (int i = 0; i < _countof(statuses); ++i) {
        np_http_fixture.status = statuses[i];
        CHECK(!np_online_check(&settings.update, r, &cancel, &e) && e.win32 == statuses[i]);
    }
    np_http_fixture.status = 200;
    CHECK(np_online_check(&settings.update, r, &cancel, &e));
    cancel = 1;
    CHECK(!np_online_check(&settings.update, r, &cancel, &e) && e.win32 == ERROR_CANCELLED);
    cancel = 0;
    if (fixture) {
        NP_RELEASE signed_release;
        CHECK(np_verify_release(fixture, &signed_release, &e));
        GetFullPathNameW(fixture, NP_PATH, np_http_fixture.exe, NULL);
        wcscpy_s(np_http_fixture.directory, NP_PATH, root);
        r->size = signed_release.size;
        CHECK(np_online_download(r, &settings.update, path, &cancel, NULL, NULL, &e));
        CHECK(path[0] && np_online_verify(path, r, &e));
        wcscpy_s(r->digest, 80, L"sha256:0000000000000000000000000000000000000000000000000000000000000000");
        CHECK(!np_online_verify(path, r, &e) && e.win32 == ERROR_CRC);
        r->digest[0] = 0;
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        DWORD n;
        BYTE byte = 0;
        CHECK(f != INVALID_HANDLE_VALUE && WriteFile(f, &byte, 1, &n, NULL));
        if (f != INVALID_HANDLE_VALUE)
            CloseHandle(f);
        CHECK(!np_online_verify(path, r, &e));
        np_online_cleanup(path);
        CHECK(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES);
        np_http_fixture.interrupt = TRUE;
        CHECK(!np_online_download(r, &settings.update, path, &cancel, NULL, NULL, &e) && !path[0]);
        np_http_fixture.interrupt = FALSE;
        cancel = 1;
        CHECK(!np_online_download(r, &settings.update, path, &cancel, NULL, NULL, &e) && !path[0]);
        cancel = 0;
        WCHAR bad_manifest[NP_PATH], test_image[NP_PATH];
        swprintf_s(test_image, NP_PATH, L"%s\\manifest-test.exe", root);
        swprintf_s(bad_manifest, NP_PATH, L"%s.update", test_image);
        signed_release.signature[0] ^= 1;
        CHECK(np_write_exact(bad_manifest, &signed_release, sizeof(signed_release)));
        wcscpy_s(np_http_fixture.exe, NP_PATH, test_image);
        CHECK(!np_online_download(r, &settings.update, path, &cancel, NULL, NULL, &e) &&
              e.win32 == NP_SIGNATURE_ERROR && !path[0]);
        --r->version[0];
        CHECK(!np_online_download(r, &settings.update, path, &cancel, NULL, NULL, &e));
    } else
        wprintf(L"Signed download fixture not supplied; download success/corruption tests skipped.\n");
    np_http_fixture.active = FALSE;
    free(r);
    wprintf(
        L"Feature tests: %u passed, %u failed; no adapter writes; HTTP failure cases use mocked transport.\n",
        passed, failed);
    return failed ? 1 : 0;
}
static int apps_probe(void) {
    for (int running = 0; running < 2; ++running) {
        NP_APP_LIST *list = np_apps_scan(running);
        if (!list)
            return 1;
        size_t paths = 0, packages = 0;
        for (size_t i = 0; i < list->count; ++i) {
            paths += list->rows[i].path[0] != 0;
            packages += list->rows[i].package != 0;
        }
        wprintf(L"Read-only %s: count=%zu readable_paths=%zu packages=%zu registry_errors=%lu "
                L"process_errors=%lu shell=0x%08lX truncated=%d. No app names or paths logged.\n",
                running ? L"processes" : L"installed", list->count, paths, packages, list->registry_errors,
                list->process_errors, (DWORD)list->shell_error, list->truncated);
        np_apps_free(list);
    }
    return 0;
}
static int online_probe(void) {
    NP_SETTINGS s;
    np_settings_defaults(&s);
    NP_ONLINE_RELEASE *r = calloc(1, sizeof(*r));
    NP_ERROR e = {0};
    volatile LONG cancel = 0;
    BOOL ok = r && np_online_check(&s.update, r, &cancel, &e);
    wprintf(L"GitHub read-only latest check AcerGL/NetPilotLite: success=%d error=%lu %s\n", ok, e.win32,
            e.operation);
    free(r);
    return ok ? 0 : 1;
}
