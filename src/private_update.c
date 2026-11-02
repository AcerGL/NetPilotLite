#include "private_update.h"
#include "settings.h"
#include "json.h"
#include <objbase.h>
#include <stdio.h>
static const WCHAR *auth_root(void) {
#ifdef NP_READONLY_BUILD
    if (np_http_fixture.active && np_http_fixture.private_api && np_http_fixture.directory[0])
        return np_http_fixture.directory;
#endif
    return NULL;
}
static BOOL field(const NP_JSON *j, int obj, const char *key, WCHAR *s, size_t n) {
    return np_json_string(j, np_json_field(j, obj, key), s, n);
}
static BOOL call(const NP_UPDATE_SETTINGS *s, const WCHAR *token, const WCHAR *suffix, BOOL asset,
                 BYTE **bytes, DWORD *n, HANDLE f, ULONGLONG limit, volatile LONG *cancel,
                 NP_DOWNLOAD_PROGRESS cb, void *ctx, NP_ERROR *e) {
    WCHAR url[600];
    swprintf_s(url, 600, L"https://api.github.com/repos/%s/%s%s", s->owner, s->repo, suffix);
    return np_private_http(s, token, url, asset, bytes, n, f, limit, cancel, cb, ctx, e);
}
BOOL np_auth_validate(const NP_UPDATE_SETTINGS *s, const WCHAR *token, volatile LONG *cancel, NP_ERROR *e) {
    BYTE *b = NULL;
    DWORD n = 0;
    ZeroMemory(e, sizeof(*e));
    if (!call(s, token, L"", FALSE, &b, &n, INVALID_HANDLE_VALUE, 1024 * 1024, cancel, NULL, NULL, e))
        return FALSE;
    NP_JSON j;
    WCHAR actual[220], expected[220];
    swprintf_s(expected, 220, L"%s/%s", s->owner, s->repo);
    BOOL ok = np_json_parse(&j, (char *)b, n);
    if (ok) {
        ok = field(&j, 0, "full_name", actual, 220) && !_wcsicmp(actual, expected);
        np_json_free(&j);
    }
    free(b);
    b = NULL;
    if (!ok) {
        np_error(e, ERROR_INVALID_DATA, 0, L"GitHub 仓库身份不匹配");
        return FALSE;
    }
    if (!call(s, token, L"/releases?per_page=1", FALSE, &b, &n, INVALID_HANDLE_VALUE, 1024 * 1024, cancel,
              NULL, NULL, e))
        return FALSE;
    ok = np_json_parse(&j, (char *)b, n);
    if (ok) {
        ok = j.tokens[0].type == '[';
        np_json_free(&j);
    }
    free(b);
    if (!ok)
        np_error(e, ERROR_INVALID_DATA, 0, L"Release 读取权限验证失败");
    return ok;
}
BOOL np_private_parse(const char *text, size_t n, const NP_UPDATE_SETTINGS *s, NP_PRIVATE_RELEASE *r,
                      NP_ERROR *e) {
    ZeroMemory(r, sizeof(*r));
    ZeroMemory(e, sizeof(*e));
    NP_JSON j;
    if (!np_repo_valid(s->owner, s->repo) || !np_json_parse(&j, text, n))
        goto bad;
    BOOL ok = j.tokens[0].type == '{' && np_json_false(&j, np_json_field(&j, 0, "draft")) &&
              np_json_false(&j, np_json_field(&j, 0, "prerelease")) &&
              field(&j, 0, "tag_name", r->info.tag, 64) && np_semver(r->info.tag, r->info.version) &&
              field(&j, 0, "published_at", r->info.published, 64);
    int title = np_json_field(&j, 0, "name"), body = np_json_field(&j, 0, "body");
    if (title >= 0 && j.tokens[title].type == 'n')
        wcscpy_s(r->info.name, 256, r->info.tag);
    else
        ok = ok && field(&j, 0, "name", r->info.name, 256);
    if (body >= 0 && j.tokens[body].type == '"')
        ok = ok && field(&j, 0, "body", r->info.body, 16384);
    else if (body == -2)
        ok = FALSE;
    int assets = np_json_field(&j, 0, "assets");
    unsigned mask = 0;
    const WCHAR *names[] = {NP_PACKAGE, NP_PACKAGE_JSON, NP_PACKAGE_JSON L".update"};
    if (assets < 0 || j.tokens[assets].type != '[')
        ok = FALSE;
    if (ok)
        for (int i = assets + 1; i < j.tokens[assets].next; i = j.tokens[i].next) {
            WCHAR name[180], url[2048], expected[2048];
            if (!field(&j, i, "name", name, 180))
                continue;
            for (int k = 0; k < 3; k++)
                if (!wcscmp(name, names[k])) {
                    swprintf_s(expected, 2048, L"https://github.com/%s/%s/releases/download/%s/%s", s->owner,
                               s->repo, r->info.tag, name);
                    ULONGLONG id = 0, size = 0;
                    if ((mask & (1u << k)) || !np_json_uint(&j, np_json_field(&j, i, "id"), &id) || !id ||
                        !np_json_uint(&j, np_json_field(&j, i, "size"), &size) || !size ||
                        !field(&j, i, "browser_download_url", url, 2048) || wcscmp(url, expected) ||
                        size > (k == 0 ? 64ULL * 1024 * 1024 : 65536) ||
                        (k == 2 && size != sizeof(NP_RELEASE)))
                        ok = FALSE;
                    r->asset_id[k] = id;
                    r->size[k] = size;
                    mask |= 1u << k;
                    if (k == 0) {
                        wcscpy_s(r->info.url, 2048, url);
                        r->info.size = size;
                        int d = np_json_field(&j, i, "digest");
                        if (d == -2)
                            ok = FALSE;
                        else if (d >= 0 && j.tokens[d].type == '"')
                            ok = ok && field(&j, i, "digest", r->info.digest, 80) &&
                                 wcslen(r->info.digest) == 71 && !wcsncmp(r->info.digest, L"sha256:", 7);
                    }
                }
        }
    np_json_free(&j);
    if (ok && mask == 7)
        return TRUE;
    if (ok) {
        np_error(e, ERROR_NOT_FOUND, 0, L"Release 缺少 Windows x64 更新 ZIP、JSON 或可信签名");
        return FALSE;
    }
bad:
    np_error(e, ERROR_INVALID_DATA, 0, L"私有更新 Release 元数据无效；仅接受正式稳定版及固定更新资产");
    return FALSE;
}
BOOL np_private_check(const NP_UPDATE_SETTINGS *s, NP_PRIVATE_RELEASE *r, volatile LONG *cancel,
                      NP_ERROR *e) {
    NP_AUTH auth;
    BYTE *b = NULL;
    DWORD n = 0;
    ZeroMemory(e, sizeof(*e));
    if (!np_auth_load(auth_root(), s, &auth, e))
        return FALSE;
    BOOL ok = call(s, auth.token, L"/releases/latest", FALSE, &b, &n, INVALID_HANDLE_VALUE, 1024 * 1024,
                   cancel, NULL, NULL, e);
    if (!ok && e->http_status == 404) {
        NP_ERROR probe = {0};
        if (np_auth_validate(s, auth.token, cancel, &probe)) {
            np_error(e, ERROR_NOT_FOUND, 0, L"尚无可用发布版本：仓库授权有效，但没有正式稳定 Release");
            e->http_status = 404;
        } else
            *e = probe;
    }
    SecureZeroMemory(&auth, sizeof(auth));
    if (ok)
        ok = np_private_parse((char *)b, n, s, r, e);
    free(b);
    return ok;
}
static BOOL hash_text(const BYTE hash[32], const WCHAR *text) {
    WCHAR hex[65];
    for (int i = 0; i < 32; i++)
        swprintf_s(hex + i * 2, 65 - i * 2, L"%02x", hash[i]);
    return !wcscmp(hex, text);
}
static WORD u16(const BYTE *p) {
    return (WORD)(p[0] | p[1] << 8);
}
static DWORD u32(const BYTE *p) {
    return (DWORD)p[0] | (DWORD)p[1] << 8 | (DWORD)p[2] << 16 | (DWORD)p[3] << 24;
}
static DWORD crc32(const BYTE *p, DWORD n) {
    DWORD c = ~0u;
    for (DWORD i = 0; i < n; i++) {
        c ^= p[i];
        for (int b = 0; b < 8; b++)
            c = (c >> 1) ^ (0xedb88320u & -(LONG)(c & 1));
    }
    return ~c;
}
BOOL np_package_extract(const WCHAR *zip, const WCHAR *json, const NP_PRIVATE_RELEASE *r, WCHAR out[NP_PATH],
                        NP_ERROR *e) {
    out[0] = 0;
    WCHAR sig[NP_PATH];
    swprintf_s(sig, NP_PATH, L"%s.update", json);
    NP_RELEASE signed_json;
    BYTE hash[32];
    if (!np_read_exact(sig, &signed_json, sizeof(signed_json)) || !np_catalog_signature(&signed_json) ||
        memcmp(signed_json.version, r->info.version, 16) || signed_json.size > 65536 || !signed_json.size ||
        !np_hash_file(json, hash) || memcmp(hash, signed_json.hash, 32)) {
        np_error(e, NP_SIGNATURE_ERROR, 0, L"更新清单可信签名无效；未提取或执行程序");
        return FALSE;
    }
    char *text = malloc((size_t)signed_json.size);
    NP_JSON j;
    BOOL ok = text && np_read_exact(json, text, (DWORD)signed_json.size);
    WCHAR version[64], tag[64], arch[32], name[128], sha[80];
    ULONGLONG size = 0;
    DWORD v[4];
    if (ok)
        ok = np_json_parse(&j, text, (size_t)signed_json.size);
    if (ok) {
        ok = field(&j, 0, "version", version, 64) && np_semver(version, v) &&
             !memcmp(v, r->info.version, 16) && field(&j, 0, "release_tag", tag, 64) &&
             !wcscmp(tag, r->info.tag) && field(&j, 0, "architecture", arch, 32) && !wcscmp(arch, L"x64") &&
             field(&j, 0, "package_name", name, 128) && !wcscmp(name, NP_PACKAGE) &&
             field(&j, 0, "sha256", sha, 80) && wcslen(sha) == 64 &&
             np_json_uint(&j, np_json_field(&j, 0, "package_size"), &size) && size == r->size[0] &&
             size <= 64ULL * 1024 * 1024;
        np_json_free(&j);
    }
    free(text);
    if (!ok) {
        np_error(e, ERROR_INVALID_DATA, 0, L"已签名更新 JSON 的版本、架构或大小不匹配");
        return FALSE;
    }
    BYTE *bytes = malloc((size_t)size);
    ok = bytes && size >= 22 && np_read_exact(zip, bytes, (DWORD)size) && np_hash(bytes, (DWORD)size, hash) &&
         hash_text(hash, sha) && (!r->info.digest[0] || hash_text(hash, r->info.digest + 7));
    if (!ok) {
        free(bytes);
        np_error(e, ERROR_CRC, 0, L"更新 ZIP 大小或 SHA-256 不匹配");
        return FALSE;
    }
    /* Deliberately narrow ZIP profile: two stored regular files, no paths, extras,
       comments, encryption, ZIP64, symlinks or data descriptors. Builder emits this profile. */
    const char *names[] = {"NetPilotLite.exe", "NetPilotLite.exe.update"};
    DWORD offsets[2], lengths[2];
    size_t pos = 0;
    for (int i = 0; i < 2 && ok; i++) {
        size_t name_len = strlen(names[i]);
        offsets[i] = (DWORD)pos;
        if (pos + 30 + name_len > size || u32(bytes + pos) != 0x04034b50 || u16(bytes + pos + 6) ||
            u16(bytes + pos + 8) || u16(bytes + pos + 26) != name_len || u16(bytes + pos + 28) ||
            memcmp(bytes + pos + 30, names[i], name_len)) {
            ok = FALSE;
            break;
        }
        lengths[i] = u32(bytes + pos + 22);
        ok = lengths[i] && u32(bytes + pos + 18) == lengths[i] && pos + 30 + name_len + lengths[i] <= size;
        if (ok)
            ok = crc32(bytes + pos + 30 + name_len, lengths[i]) == u32(bytes + pos + 14);
        pos += 30 + name_len + lengths[i];
    }
    size_t central = pos;
    for (int i = 0; i < 2 && ok; i++) {
        size_t len = strlen(names[i]);
        ok = pos + 46 + len <= size && u32(bytes + pos) == 0x02014b50 && !u16(bytes + pos + 8) &&
             !u16(bytes + pos + 10) && u32(bytes + pos + 20) == lengths[i] &&
             u32(bytes + pos + 24) == lengths[i] && u16(bytes + pos + 28) == len && !u16(bytes + pos + 30) &&
             !u16(bytes + pos + 32) && !u16(bytes + pos + 34) && u32(bytes + pos + 42) == offsets[i] &&
             !memcmp(bytes + pos + 46, names[i], len) && !(u32(bytes + pos + 38) & 0x10);
        pos += 46 + len;
    }
    ok = ok && pos + 22 == size && u32(bytes + pos) == 0x06054b50 && !u16(bytes + pos + 4) &&
         !u16(bytes + pos + 6) && u16(bytes + pos + 8) == 2 && u16(bytes + pos + 10) == 2 &&
         u32(bytes + pos + 12) == pos - central && u32(bytes + pos + 16) == central && !u16(bytes + pos + 20);
    if (ok) {
        WCHAR dir[NP_PATH];
        wcscpy_s(dir, NP_PATH, zip);
        WCHAR *slash = wcsrchr(dir, L'\\');
        if (!slash)
            ok = FALSE;
        else
            *slash = 0;
        if (ok)
            for (int i = 0; i < 2 && ok; i++) {
                WCHAR path[NP_PATH];
                swprintf_s(path, NP_PATH, L"%s\\%hs", dir, names[i]);
                ok = np_write_exact(path, bytes + offsets[i] + 30 + strlen(names[i]), lengths[i]);
                if (!i && ok)
                    wcscpy_s(out, NP_PATH, path);
            }
        NP_RELEASE image;
        if (ok)
            ok = np_verify_release(out, &image, e) && !memcmp(image.version, r->info.version, 16);
    }
    free(bytes);
    if (!ok) {
        if (!e->win32)
            np_error(e, ERROR_INVALID_DATA, 0, L"更新 ZIP 不符合受控结构或程序签名/版本错误");
        out[0] = 0;
    }
    return ok;
}
BOOL np_private_download(const NP_PRIVATE_RELEASE *r, const NP_UPDATE_SETTINGS *s, WCHAR out[NP_PATH],
                         volatile LONG *cancel, NP_DOWNLOAD_PROGRESS cb, void *ctx, NP_ERROR *e) {
    NP_AUTH auth;
    ZeroMemory(e, sizeof(*e));
    out[0] = 0;
    DWORD current[4] = {NP_VERSION_NUM};
    if (np_version_compare(r->info.version, current) <= 0) {
        np_error(e, ERROR_REVISION_MISMATCH, 0, L"不重复安装或降级");
        return FALSE;
    }
    if (!np_auth_load(auth_root(), s, &auth, e))
        return FALSE;
    WCHAR base[NP_PATH], dir[NP_PATH], guid[40], files[3][NP_PATH];
    GUID id;
    BOOL made = FALSE;
    BOOL have_base =
        auth_root() ? wcscpy_s(base, NP_PATH, auth_root()) == 0 : np_data_directory(base, NP_PATH);
    BOOL ok = have_base && SUCCEEDED(CoCreateGuid(&id)) && StringFromGUID2(&id, guid, 40);
    if (ok) {
        swprintf_s(dir, NP_PATH, L"%s\\UpdateCache-%s", base, guid);
        ok = CreateDirectoryW(dir, NULL);
        made = ok;
    }
    const WCHAR *names[] = {NP_PACKAGE, NP_PACKAGE_JSON, NP_PACKAGE_JSON L".update"};
    if (ok)
        for (int i = 0; i < 3; i++)
            swprintf_s(files[i], NP_PATH, L"%s\\%s", dir, names[i]);
    /* JSON/signature first; ZIP remains inert data until ALL checks succeed. */
    int order[] = {1, 2, 0};
    for (int k = 0; k < 3 && ok; k++) {
        int i = order[k];
        WCHAR suffix[128];
        swprintf_s(suffix, 128, L"/releases/assets/%llu", r->asset_id[i]);
        HANDLE f = CreateFileW(files[i], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                               FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        ok = f != INVALID_HANDLE_VALUE &&
             call(s, auth.token, suffix, TRUE, NULL, NULL, f, r->size[i], cancel, i == 0 ? cb : NULL, ctx, e);
        if (ok)
            ok = FlushFileBuffers(f);
        if (f != INVALID_HANDLE_VALUE)
            CloseHandle(f);
    }
    SecureZeroMemory(&auth, sizeof(auth));
    if (ok)
        ok = np_package_extract(files[0], files[1], r, out, e);
    if (made) {
        for (int i = 0; i < 3; i++)
            DeleteFileW(files[i]);
        if (!ok) {
            WCHAR p[NP_PATH];
            swprintf_s(p, NP_PATH, L"%s\\NetPilotLite.exe", dir);
            DeleteFileW(p);
            swprintf_s(p, NP_PATH, L"%s\\NetPilotLite.exe.update", dir);
            DeleteFileW(p);
            RemoveDirectoryW(dir);
        }
    }
    if (!ok && !e->win32)
        np_error(e, GetLastError() ? GetLastError() : ERROR_WRITE_FAULT, 0, L"私有更新下载失败，可重试");
    return ok;
}
