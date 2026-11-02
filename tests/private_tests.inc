#include "private_update.h"
#include <winhttp.h>
#include <objbase.h>
static const WCHAR fake_pat[] = L"github_pat_NONFUNCTIONAL_TEST_ONLY_0000000000000000000000";
static BOOL private_temp(WCHAR dir[NP_PATH]) {
    WCHAR base[NP_PATH], id[40];
    GUID g;
    if (!GetTempPathW(NP_PATH, base) || FAILED(CoCreateGuid(&g)) || !StringFromGUID2(&g, id, 40))
        return FALSE;
    swprintf_s(dir, NP_PATH, L"%sNetPilot-private-test-%s", base, id);
    return CreateDirectoryW(dir, NULL);
}
static int private_tests(void) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    WCHAR dir[NP_PATH], path[NP_PATH], boot[40], again[40];
    NP_ERROR e = {0};
    NP_SETTINGS settings;
    np_settings_defaults(&settings);
    CHECK(private_temp(dir));
    NP_AUTH auth = {0}, loaded = {0};
    auth.magic = 0x4e504154;
    wcscpy_s(auth.owner, 100, settings.update.owner);
    wcscpy_s(auth.repo, 100, settings.update.repo);
    wcscpy_s(auth.token, NP_TOKEN_CHARS, fake_pat);
    CHECK(np_token_valid(fake_pat));
    CHECK(!np_token_valid(L"ghp_legacy"));
    CHECK(!np_token_valid(L"github_pat_bad\r\nAuthorization: value"));
    CHECK(np_auth_store(dir, &auth, &e));
    CHECK(np_auth_load(dir, &settings.update, &loaded, &e));
    CHECK(!wcscmp(loaded.token, fake_pat));
    swprintf_s(path, NP_PATH, L"%s\\github-auth.dpapi", dir);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    BYTE encrypted[8192];
    DWORD got = 0;
    CHECK(f != INVALID_HANDLE_VALUE && ReadFile(f, encrypted, sizeof(encrypted), &got, NULL));
    CloseHandle(f);
    BOOL found = FALSE;
    for (DWORD i = 0; i + sizeof(fake_pat) <= got; i++)
        if (!memcmp(encrypted + i, fake_pat, sizeof(fake_pat)))
            found = TRUE;
    CHECK(!found);
    NP_UPDATE_SETTINGS wrong = settings.update;
    wcscpy_s(wrong.repo, 100, L"DifferentRepo");
    CHECK(!np_auth_load(dir, &wrong, &loaded, &e));
    CHECK(np_auth_remove(dir, &e));
    CHECK(!np_auth_load(dir, &settings.update, &loaded, &e) && e.win32 == ERROR_LOGON_FAILURE);
    CHECK(np_auth_store(dir, &auth, &e));
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    BYTE zero = encrypted[40] ^ 0x55;
    SetFilePointer(f, 40, NULL, FILE_BEGIN);
    WriteFile(f, &zero, 1, &got, NULL);
    CloseHandle(f);
    CHECK(!np_auth_load(dir, &settings.update, &loaded, &e));
    CHECK(np_auth_remove(dir, &e));
    SecureZeroMemory(&auth, sizeof(auth));
    SecureZeroMemory(&loaded, sizeof(loaded));
    CHECK(np_boot_id(boot));
    CHECK(np_boot_id(again) && !wcscmp(boot, again));
    const WCHAR *cycle = L"{00000000-0000-0000-0000-000000000001}",
                *next = L"{00000000-0000-0000-0000-000000000002}";
    unsigned attempt = 0;
    CHECK(np_boot_due(dir, cycle));
    CHECK(np_boot_attempt(dir, cycle, 1000, &attempt) && attempt == 1);
    CHECK(!np_boot_attempt(dir, cycle, 29999, &attempt));
    CHECK(np_boot_attempt(dir, cycle, 31000, &attempt) && attempt == 2);
    CHECK(!np_boot_attempt(dir, cycle, 120999, &attempt));
    CHECK(np_boot_attempt(dir, cycle, 121000, &attempt) && attempt == 3);
    CHECK(!np_boot_attempt(dir, cycle, 999999, &attempt));
    CHECK(np_boot_complete(dir, cycle));
    CHECK(!np_boot_due(dir, cycle));
    CHECK(np_boot_due(dir, next));
    CHECK(np_boot_attempt(dir, next, 10, &attempt) && attempt == 1);
    DWORD ver[4];
    CHECK(np_ignore_version(dir, L"v1.10.0"));
    CHECK(np_semver(L"1.9.0", ver) && np_ignored(dir, ver));
    CHECK(np_semver(L"1.10.1", ver) && !np_ignored(dir, ver));
    CHECK(!np_autorun(TRUE, &e) && e.win32 == ERROR_NOT_SUPPORTED);
    np_http_fixture = (NP_HTTP_FIXTURE){0};
    np_http_fixture.active = TRUE;
    np_http_fixture.private_api = TRUE;
    np_http_fixture.status = 200;
    np_http_fixture.repo_status = 200;
    np_http_fixture.repo_json = "{\"full_name\":\"AcerGL/NetPilotLite\",\"private\":true}";
    volatile LONG cancel = 0;
    CHECK(np_auth_validate(&settings.update, fake_pat, &cancel, &e));
    np_http_fixture.repo_status = 401;
    CHECK(!np_auth_validate(&settings.update, fake_pat, &cancel, &e) && e.win32 == ERROR_LOGON_FAILURE);
    CHECK(!np_auth_validate(&settings.update, fake_pat, &cancel, &e) &&
          e.http_status == 401); /* revoked/expired equivalent response */
    np_http_fixture.repo_status = 403;
    CHECK(!np_auth_validate(&settings.update, fake_pat, &cancel, &e) && e.http_status == 403);
    np_http_fixture.repo_status = ERROR_WINHTTP_CANNOT_CONNECT;
    CHECK(!np_auth_validate(&settings.update, fake_pat, &cancel, &e) &&
          e.win32 == ERROR_WINHTTP_CANNOT_CONNECT);
    np_http_fixture.repo_status = ERROR_WINHTTP_TIMEOUT;
    CHECK(!np_auth_validate(&settings.update, fake_pat, &cancel, &e) && e.win32 == ERROR_WINHTTP_TIMEOUT);
    np_http_fixture.repo_status = 200;
    cancel = 1;
    CHECK(!np_auth_validate(&settings.update, fake_pat, &cancel, &e) && e.win32 == ERROR_CANCELLED);
    cancel = 0;
    BYTE *b = NULL;
    DWORD n = 0;
    CHECK(!np_private_http(&settings.update, fake_pat, L"https://example.com/", FALSE, &b, &n,
                           INVALID_HANDLE_VALUE, 100, &cancel, NULL, NULL, &e));
    np_http_fixture.active = FALSE;
    char json[4096];
    const char *names[] = {"NetPilotLite-Update-x64.zip", "NetPilotLite-Update-x64.json",
                           "NetPilotLite-Update-x64.json.update"};
    strcpy_s(json, sizeof(json),
             "{\"tag_name\":\"v1.10.0\",\"name\":\"Update\",\"body\":\"Notes\",\"published_at\":\"2026-11-"
             "02T00:00:00Z\",\"draft\":false,\"prerelease\":false,\"assets\":[");
    for (int i = 0; i < 3; i++) {
        char item[700];
        sprintf_s(item, sizeof(item),
                  "%s{\"id\":%d,\"size\":%d,\"name\":\"%s\",\"browser_download_url\":\"https://github.com/"
                  "AcerGL/NetPilotLite/releases/download/v1.10.0/%s\"}",
                  i ? "," : "", i + 1, i == 2 ? 128 : 1024, names[i], names[i]);
        strcat_s(json, sizeof(json), item);
    }
    strcat_s(json, sizeof(json), "]}");
    NP_PRIVATE_RELEASE *r = calloc(1, sizeof(*r));
    CHECK(np_private_parse(json, strlen(json), &settings.update, r, &e));
    DWORD current[] = {1, 9, 0, 0};
    CHECK(np_version_compare(r->info.version, current) > 0);
    CHECK(np_version_compare(r->info.version, r->info.version) == 0);
    char *draft = strstr(json, "\"draft\":false");
    memcpy(draft + 8, "true ", 5);
    CHECK(!np_private_parse(json, strlen(json), &settings.update, r, &e));
    memcpy(draft + 8, "false", 5);
    char *pre = strstr(json, "\"prerelease\":false");
    memcpy(pre + 13, "true ", 5);
    CHECK(!np_private_parse(json, strlen(json), &settings.update, r, &e));
    memcpy(pre + 13, "false", 5);
    CHECK(!np_private_parse(release_json, strlen(release_json), &settings.update, r,
                            &e)); /* old EXE/installer format */
    free(r);
    swprintf_s(path, NP_PATH, L"%s\\update-state.ini", dir);
    DeleteFileW(path);
    RemoveDirectoryW(dir);
    wprintf(L"Private update checks: %u passed, %u failed. HTTP uses synthetic fixtures; DPAPI and boot ID "
            L"use real Windows, no real PAT or network writes.\n",
            passed, failed);
    CoUninitialize();
    return failed ? 1 : 0;
}
static int package_test(const WCHAR *source) {
    WCHAR dir[NP_PATH], zip[NP_PATH], json[NP_PATH], sig[NP_PATH], from[NP_PATH], out[NP_PATH];
    NP_ERROR e = {0};
    CHECK(private_temp(dir));
    const WCHAR *names[] = {NP_PACKAGE, NP_PACKAGE_JSON, NP_PACKAGE_JSON L".update"};
    WCHAR *dest[] = {zip, json, sig};
    for (int i = 0; i < 3; i++) {
        swprintf_s(from, NP_PATH, L"%s\\%s", source, names[i]);
        swprintf_s(dest[i], NP_PATH, L"%s\\%s", dir, names[i]);
        CHECK(CopyFileW(from, dest[i], TRUE));
    }
    NP_RELEASE signed_json;
    CHECK(np_read_exact(sig, &signed_json, sizeof(signed_json)));
    NP_PRIVATE_RELEASE *r = calloc(1, sizeof(*r));
    memcpy(r->info.version, signed_json.version, 16);
    swprintf_s(r->info.tag, 64, L"v%lu.%lu.%lu", r->info.version[0], r->info.version[1], r->info.version[2]);
    WIN32_FILE_ATTRIBUTE_DATA data;
    CHECK(GetFileAttributesExW(zip, GetFileExInfoStandard, &data));
    r->size[0] = data.nFileSizeLow;
    CHECK(np_package_extract(zip, json, r, out, &e));
    NP_RELEASE exe;
    CHECK(np_verify_release(out, &exe, &e));
    DeleteFileW(out);
    swprintf_s(from, NP_PATH, L"%s.update", out);
    DeleteFileW(from);
    HANDLE f = CreateFileW(zip, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    BYTE x = 0;
    DWORD written;
    SetFilePointer(f, 0, NULL, FILE_BEGIN);
    WriteFile(f, &x, 1, &written, NULL);
    CloseHandle(f);
    CHECK(!np_package_extract(zip, json, r, out, &e) && e.win32 == ERROR_CRC);
    f = CreateFileW(sig, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    SetFilePointer(f, 64, NULL, FILE_BEGIN);
    x ^= 0x55;
    WriteFile(f, &x, 1, &written, NULL);
    CloseHandle(f);
    CHECK(!np_package_extract(zip, json, r, out, &e) && e.win32 == NP_SIGNATURE_ERROR);
    for (int i = 0; i < 3; i++)
        DeleteFileW(dest[i]);
    RemoveDirectoryW(dir);
    free(r);
    wprintf(L"Signed ZIP extraction, corrupt package and corrupt signature: %u passed, %u failed. No program "
            L"executed.\n",
            passed, failed);
    return failed ? 1 : 0;
}
static int private_download_test(const WCHAR *source) {
    WCHAR dir[NP_PATH], path[NP_PATH], out[NP_PATH];
    NP_SETTINGS settings;
    NP_ERROR e = {0};
    NP_AUTH auth = {0};
    volatile LONG cancel = 0;
    CHECK(private_temp(dir));
    np_settings_defaults(&settings);
    auth.magic = 0x4e504154;
    wcscpy_s(auth.owner, 100, settings.update.owner);
    wcscpy_s(auth.repo, 100, settings.update.repo);
    wcscpy_s(auth.token, NP_TOKEN_CHARS, fake_pat);
    CHECK(np_auth_store(dir, &auth, &e));
    SecureZeroMemory(&auth, sizeof(auth));
    swprintf_s(path, NP_PATH, L"%s\\release.json", source);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    char json[8192];
    DWORD n = 0;
    CHECK(f != INVALID_HANDLE_VALUE && ReadFile(f, json, sizeof(json) - 1, &n, NULL));
    CloseHandle(f);
    json[n] = 0;
    np_http_fixture = (NP_HTTP_FIXTURE){0};
    np_http_fixture.active = TRUE;
    np_http_fixture.private_api = TRUE;
    np_http_fixture.status = 200;
    np_http_fixture.json = json;
    GetFullPathNameW(source, NP_PATH, np_http_fixture.exe, NULL);
    wcscpy_s(np_http_fixture.directory, NP_PATH, dir);
    NP_PRIVATE_RELEASE *r = calloc(1, sizeof(*r));
    CHECK(np_private_check(&settings.update, r, &cancel, &e));
    CHECK(np_private_download(r, &settings.update, out, &cancel, NULL, NULL, &e));
    NP_RELEASE verified;
    CHECK(np_verify_release(out, &verified, &e));
    np_online_cleanup(out);
    np_http_fixture.interrupt = TRUE;
    CHECK(!np_private_download(r, &settings.update, out, &cancel, NULL, NULL, &e) &&
          e.win32 == ERROR_CONNECTION_ABORTED);
    np_http_fixture.interrupt = FALSE;
    CHECK(np_private_download(r, &settings.update, out, &cancel, NULL, NULL, &e));
    np_online_cleanup(out);
    cancel = 1;
    CHECK(!np_private_download(r, &settings.update, out, &cancel, NULL, NULL, &e) &&
          e.win32 == ERROR_CANCELLED);
    cancel = 0;
    np_http_fixture.status = 401;
    CHECK(!np_private_check(&settings.update, r, &cancel, &e) && e.win32 == ERROR_LOGON_FAILURE);
    np_http_fixture.status = 404;
    np_http_fixture.repo_status = 200;
    np_http_fixture.repo_json = "{\"full_name\":\"AcerGL/NetPilotLite\",\"private\":true}";
    CHECK(!np_private_check(&settings.update, r, &cancel, &e) && e.http_status == 404 &&
          e.win32 == ERROR_NOT_FOUND);
    np_http_fixture.active = FALSE;
    np_auth_remove(dir, &e);
    RemoveDirectoryW(dir);
    free(r);
    wprintf(L"Authenticated protocol mock download/retry/cancel/no-release: %u passed, %u failed. Signed "
            L"image verified but never executed.\n",
            passed, failed);
    return failed ? 1 : 0;
}
