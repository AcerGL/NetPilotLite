#include "online.h"
#include "private_update.h"
#include "settings.h"
#include "json.h"
#include <winhttp.h>
#include <stdio.h>
#include <wchar.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlwapi.h>
#define APP_ASSET L"NetPilotLite.exe"
#define SETUP_ASSET L"NetPilotLite-Offline-Setup.exe"
static const WCHAR *asset_name(BOOL setup) {
    return setup ? SETUP_ASSET : APP_ASSET;
}
static const WCHAR *signature_name(BOOL setup) {
    return setup ? SETUP_ASSET L".update" : APP_ASSET L".update";
}
#define PAYLOAD_LIMIT (64ULL * 1024 * 1024)
static void http_error(NP_ERROR *e, DWORD status, const WCHAR *retry, const WCHAR *reset) {
    WCHAR reason[256];
    const WCHAR *message = L"服务器请求失败；可重试";
    switch (status) {
    case 401:
        message = L"认证失败；此客户端不保存 Token，私有仓库请使用已签名本地更新";
        break;
    case 403:
        message = L"访问被拒绝或 API 配额耗尽；不能据此判定没有 Release";
        break;
    case 404:
        message = L"资源不存在、私有不可见或路径错误；不能据此判定已是最新版";
        break;
    case 407:
        message = L"代理要求身份验证；请检查当前 Windows 代理，程序不会修改它";
        break;
    case 429:
        message = L"GitHub 请求受限，请按 Retry-After / Reset 稍后重试";
        break;
    }
    swprintf_s(reason, 256, L"HTTP %lu：%s。Retry-After=%s；Reset=%s", status, message,
               retry && *retry ? retry : L"未提供", reset && *reset ? reset : L"未提供");
    np_error(e, ERROR_WINHTTP_INVALID_SERVER_RESPONSE, 0, reason);
    e->http_status = status;
}
static const WCHAR *network_reason(DWORD code) {
    switch (code) {
    case ERROR_CANCELLED:
        return L"更新已取消，可重新检查或下载";
    case ERROR_TIMEOUT:
    case ERROR_WINHTTP_TIMEOUT:
        return L"HTTPS 请求超时，请检查当前网络或代理后重试";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return L"DNS 无法解析 GitHub 或代理主机；尚未获得 Release 响应";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return L"TLS 证书验证失败；已阻止下载，不会绕过证书验证";
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return L"无法连接或连接中断；使用 Windows 自动代理，请检查网络、TUN 和代理后重试";
    case ERROR_WINHTTP_AUTODETECTION_FAILED:
    case ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT:
    case ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT:
        return L"Windows 自动代理检测或 PAC 脚本失败，请检查代理后重试";
    case ERROR_FILE_TOO_LARGE:
        return L"下载超过允许大小，已阻止更新";
    default:
        return L"HTTPS 或缓存操作失败；请参考 Windows 错误代码后重试";
    }
}
#ifdef NP_READONLY_BUILD
NP_HTTP_FIXTURE np_http_fixture;
static BOOL fixture_request(const WCHAR *url, BOOL asset, BYTE **memory, DWORD *length, HANDLE file,
                            ULONGLONG limit, volatile LONG *cancel, NP_DOWNLOAD_PROGRESS progress, void *ctx,
                            NP_ERROR *e) {
    BOOL repo = !asset && !wcsstr(url, L"/releases/latest") && np_http_fixture.repo_status;
    DWORD status = repo ? np_http_fixture.repo_status : np_http_fixture.status;
    if (status >= 12000) {
        np_error(e, status, 0, network_reason(status));
        return FALSE;
    }
    if (status != 200) {
        http_error(e, status, NULL, NULL);
        return FALSE;
    }
    if (cancel && InterlockedCompareExchange(cancel, 0, 0)) {
        np_error(e, ERROR_CANCELLED, 0, L"模拟取消");
        return FALSE;
    }
    if (!asset) {
        const char *json = repo ? np_http_fixture.repo_json : np_http_fixture.json;
        if (np_http_fixture.private_api && wcsstr(url, L"/releases?"))
            json = "[]";
        size_t n = strlen(json);
        *memory = malloc(n);
        if (!*memory)
            return FALSE;
        memcpy(*memory, json, n);
        *length = (DWORD)n;
        return TRUE;
    }
    WCHAR path[NP_PATH];
    swprintf_s(path, NP_PATH, L"%s%s", np_http_fixture.exe, wcsstr(url, L".exe.update") ? L".update" : L"");
    if (np_http_fixture.private_api) {
        const WCHAR *name = wcsstr(url, L"/assets/1")   ? NP_PACKAGE
                            : wcsstr(url, L"/assets/2") ? NP_PACKAGE_JSON
                                                        : NP_PACKAGE_JSON L".update";
        swprintf_s(path, NP_PATH, L"%s\\%s", np_http_fixture.exe, name);
    }
    if (np_http_fixture.public_assets) {
        const WCHAR *name = wcsrchr(url, L'/');
        if (!name || !name[1] || wcschr(name + 1, L'\\')) return FALSE;
        swprintf_s(path, NP_PATH, L"%s\\%s", np_http_fixture.exe, name + 1);
    }
    HANDLE in = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (in == INVALID_HANDLE_VALUE)
        return FALSE;
    BYTE *all = malloc((size_t)limit);
    DWORD got = 0;
    BOOL ok = all && limit < MAXDWORD && ReadFile(in, all, (DWORD)limit, &got, NULL);
    LARGE_INTEGER size;
    ok = ok && GetFileSizeEx(in, &size) && (ULONGLONG)size.QuadPart <= limit;
    CloseHandle(in);
    if (ok && file != INVALID_HANDLE_VALUE) {
        DWORD n = np_http_fixture.interrupt ? got / 2 : got, written;
        ok = WriteFile(file, all, n, &written, NULL) && written == n;
        if (progress)
            progress(ctx, n, limit);
        if (np_http_fixture.interrupt) {
            np_error(e, ERROR_CONNECTION_ABORTED, 0, L"模拟下载中断");
            ok = FALSE;
        }
    } else if (ok) {
        *memory = all;
        *length = got;
        all = NULL;
    }
    free(all);
    return ok;
}
#endif
static BOOL online_base(WCHAR out[NP_PATH]) {
#ifdef NP_READONLY_BUILD
    if (np_http_fixture.active && np_http_fixture.directory[0]) {
        wcscpy_s(out, NP_PATH, np_http_fixture.directory);
        return TRUE;
    }
#endif
    return np_data_directory(out, NP_PATH);
}
static BOOL component(const WCHAR *s) {
    size_t n = wcslen(s);
    if (!n || n >= 100 || !wcscmp(s, L".") || !wcscmp(s, L".."))
        return FALSE;
    for (size_t i = 0; i < n; ++i)
        if (!((s[i] >= L'a' && s[i] <= L'z') || (s[i] >= L'A' && s[i] <= L'Z') ||
              (s[i] >= L'0' && s[i] <= L'9') || s[i] == L'-' || s[i] == L'_' || s[i] == L'.'))
            return FALSE;
    return TRUE;
}
BOOL np_repo_valid(const WCHAR *owner, const WCHAR *repo) {
    return owner && repo && component(owner) && component(repo);
}
BOOL np_semver(const WCHAR *s, DWORD out[4]) {
    DWORD v[4] = {0};
    if (*s == L'v')
        ++s;
    for (int i = 0; i < 3; ++i) {
        if (*s < L'0' || *s > L'9')
            return FALSE;
        BOOL zero = *s == L'0';
        int count = 0;
        while (*s >= L'0' && *s <= L'9') {
            DWORD d = (DWORD)(*s++ - L'0');
            if (v[i] > (65535 - d) / 10)
                return FALSE;
            v[i] = v[i] * 10 + d;
            ++count;
        }
        if (zero && count > 1)
            return FALSE;
        if (i < 2) {
            if (*s++ != L'.')
                return FALSE;
        } else if (*s)
            return FALSE;
    }
    memcpy(out, v, sizeof(v));
    return TRUE;
}
int np_version_compare(const DWORD a[4], const DWORD b[4]) {
    for (int i = 0; i < 4; ++i)
        if (a[i] != b[i])
            return a[i] > b[i] ? 1 : -1;
    return 0;
}
BOOL np_asset_url(const WCHAR *url, const NP_UPDATE_SETTINGS *s, const WCHAR *tag, const WCHAR *name) {
    WCHAR expected[2048];
    DWORD v[4];
    if (!np_repo_valid(s->owner, s->repo) || !np_semver(tag, v))
        return FALSE;
    if (wcscmp(name, APP_ASSET) && wcscmp(name, APP_ASSET L".update") && wcscmp(name, SETUP_ASSET) &&
        wcscmp(name, SETUP_ASSET L".update"))
        return FALSE;
    return swprintf_s(expected, 2048, L"https://github.com/%s/%s/releases/download/%s/%s", s->owner, s->repo,
                      tag, name) > 0 &&
           !wcscmp(url, expected);
}
BOOL np_redirect_url(const WCHAR *url) {
    WCHAR host[256];
    URL_COMPONENTS u = {sizeof(u)};
    u.lpszHostName = host;
    u.dwHostNameLength = 256;
    u.dwUserNameLength = u.dwPasswordLength = (DWORD)-1;
    if (!url || wcslen(url) >= 4096 || wcschr(url, L'\\') || wcschr(url, L'#') ||
        !WinHttpCrackUrl(url, 0, 0, &u) || u.nScheme != INTERNET_SCHEME_HTTPS || u.nPort != 443 ||
        u.dwUserNameLength || u.dwPasswordLength)
        return FALSE;
    return !_wcsicmp(host, L"release-assets.githubusercontent.com") ||
           !_wcsicmp(host, L"objects.githubusercontent.com");
}
static BOOL strfield(const NP_JSON *j, int obj, const char *key, WCHAR *out, size_t cap) {
    return np_json_string(j, np_json_field(j, obj, key), out, cap);
}
BOOL np_release_json(const char *text, size_t n, const NP_UPDATE_SETTINGS *s, NP_ONLINE_RELEASE *out,
                     NP_ERROR *e) {
    NP_JSON j;
    ZeroMemory(e, sizeof(*e));
    ZeroMemory(out, sizeof(*out));
    out->installer = s->installer;
    if (!np_repo_valid(s->owner, s->repo) || wcscmp(s->channel, L"Stable") || !np_json_parse(&j, text, n))
        goto invalid;
    BOOL ok = j.tokens[0].type == '{' && np_json_false(&j, np_json_field(&j, 0, "draft")) &&
              np_json_false(&j, np_json_field(&j, 0, "prerelease")) &&
              strfield(&j, 0, "tag_name", out->tag, 64) && np_semver(out->tag, out->version) &&
              strfield(&j, 0, "published_at", out->published, 64);
    int title = np_json_field(&j, 0, "name");
    if (title >= 0 && j.tokens[title].type == 'n')
        wcscpy_s(out->name, 256, out->tag);
    else
        ok = ok && strfield(&j, 0, "name", out->name, 256);
    int body = np_json_field(&j, 0, "body");
    if (body >= 0 && j.tokens[body].type == '"')
        ok = ok && np_json_string(&j, body, out->body, 16384);
    else if (body == -2)
        ok = FALSE;
    int assets = np_json_field(&j, 0, "assets"), exe = 0, sig = 0;
    if (assets < 0 || j.tokens[assets].type != '[')
        ok = FALSE;
    if (ok)
        for (int i = assets + 1; i < j.tokens[assets].next; i = j.tokens[i].next) {
            WCHAR name[256], url[2048];
            ULONGLONG size = 0;
            if (!strfield(&j, i, "name", name, 256))
                continue;
            if (wcscmp(name, asset_name(s->installer)) && wcscmp(name, signature_name(s->installer)))
                continue;
            if (!strfield(&j, i, "browser_download_url", url, 2048) ||
                !np_asset_url(url, s, out->tag, name) ||
                !np_json_uint(&j, np_json_field(&j, i, "size"), &size)) {
                ok = FALSE;
                break;
            }
            if (!wcscmp(name, asset_name(s->installer))) {
                ++exe;
                wcscpy_s(out->url, 2048, url);
                out->size = size;
                if (!size || size > PAYLOAD_LIMIT)
                    ok = FALSE;
                int d = np_json_field(&j, i, "digest");
                if (d == -2)
                    ok = FALSE;
                else if (d >= 0 && j.tokens[d].type == '"') {
                    if (!np_json_string(&j, d, out->digest, 80) || wcslen(out->digest) != 71 ||
                        wcsncmp(out->digest, L"sha256:", 7))
                        ok = FALSE;
                    else
                        for (int k = 7; k < 71; ++k)
                            if (!((out->digest[k] >= L'0' && out->digest[k] <= L'9') ||
                                  (out->digest[k] >= L'a' && out->digest[k] <= L'f')))
                                ok = FALSE;
                }
            } else {
                ++sig;
                wcscpy_s(out->signature_url, 2048, url);
                if (size != sizeof(NP_RELEASE))
                    ok = FALSE;
            }
        }
    np_json_free(&j);
    if (ok && exe == 1 && sig == 1)
        return TRUE;
    if (ok && exe <= 1 && sig <= 1) {
        np_error(e, ERROR_NOT_FOUND, 0,
                 L"当前 Release 缺少适用于 Windows x64 的安装包或可信签名清单；无法下载更新");
        return FALSE;
    }
invalid:
    np_error(
        e, ERROR_INVALID_DATA, 0,
        L"Release 元数据无效：需要 Stable 语义版本、唯一 x64 EXE 及签名清单，且下载地址必须属于配置仓库");
    return FALSE;
}
/* Each request is synchronous on a worker. Redirects are explicitly limited to GitHub's asset CDN. */
static BOOL request_impl(const WCHAR *initial, BOOL asset, BYTE **memory, DWORD *length, HANDLE file,
                         ULONGLONG limit, volatile LONG *cancel, NP_DOWNLOAD_PROGRESS progress, void *context,
                         NP_ERROR *e, const WCHAR *token) {
#ifdef NP_READONLY_BUILD
    if (np_http_fixture.active)
        return fixture_request(initial, asset, memory, length, file, limit, cancel, progress, context, e);
#endif
    HINTERNET session = NULL, connection = NULL, req = NULL;
    BOOL ok = FALSE;
    DWORD error = ERROR_GEN_FAILURE;
    WCHAR url[4096];
    wcscpy_s(url, 4096, initial);
    BYTE *bytes = NULL;
    DWORD used = 0, capacity = 0;
    ULONGLONG total = 0;
    ULONGLONG deadline = GetTickCount64() + 240000;
    session = WinHttpOpen(L"NetPilotLite/" NP_VERSION, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        goto done;
    if (!WinHttpSetTimeouts(session, 5000, 10000, 15000, 20000))
        goto done;
    for (int redirects = 0; redirects <= 5; ++redirects) {
        WCHAR host[256], path[4096];
        URL_COMPONENTS u = {sizeof(u)};
        u.lpszHostName = host;
        u.dwHostNameLength = 256;
        u.lpszUrlPath = path;
        u.dwUrlPathLength = 4096;
        u.dwExtraInfoLength = (DWORD)-1;
        if (cancel && InterlockedCompareExchange(cancel, 0, 0)) {
            error = ERROR_CANCELLED;
            goto done;
        }
        if (!WinHttpCrackUrl(url, 0, 0, &u) || u.nScheme != INTERNET_SCHEME_HTTPS || u.nPort != 443) {
            error = ERROR_INVALID_NAME;
            goto done;
        }
        if (u.dwExtraInfoLength && wcscat_s(path, 4096, u.lpszExtraInfo)) {
            error = ERROR_INVALID_NAME;
            goto done;
        }
        connection = WinHttpConnect(session, host, 443, 0);
        if (!connection)
            goto done;
        req = WinHttpOpenRequest(connection, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!req)
            goto done;
        DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES,
              autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        if (!WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
            !WinHttpSetOption(req, WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon)))
            goto done;
        WCHAR headers[1024];
        BOOL api = !_wcsicmp(host, L"api.github.com");
        swprintf_s(headers, _countof(headers), L"Accept: %s\r\nX-GitHub-Api-Version: 2022-11-28\r\n",
                   asset ? L"application/octet-stream" : L"application/vnd.github+json");
        if (api && token && *token) {
            wcscat_s(headers, _countof(headers), L"Authorization: Bearer ");
            wcscat_s(headers, _countof(headers), token);
            wcscat_s(headers, _countof(headers), L"\r\n");
        }
        BOOL sent = WinHttpSendRequest(req, headers, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        DWORD send_error = GetLastError();
        SecureZeroMemory(headers, sizeof(headers));
        if (!sent) {
            SetLastError(send_error);
            goto done;
        }
        if (!WinHttpReceiveResponse(req, NULL))
            goto done;
        DWORD status = 0, n = sizeof(status);
        if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status,
                                 &n, NULL))
            goto done;
        if (status >= 300 && status < 400) {
            WCHAR location[4096];
            n = sizeof(location);
            if (!asset || redirects == 5 ||
                !WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, NULL, location, &n, NULL) ||
                !np_redirect_url(location)) {
                error = ERROR_ACCESS_DENIED;
                np_error(e, error, 0, L"已阻止未经允许的更新重定向");
                goto done;
            }
            wcscpy_s(url, 4096, location);
            WinHttpCloseHandle(req);
            req = NULL;
            WinHttpCloseHandle(connection);
            connection = NULL;
            continue;
        }
        if (status != 200) {
            WCHAR reset[40] = L"", retry[40] = L"";
            n = sizeof(reset);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, L"X-RateLimit-Reset", reset, &n, NULL);
            n = sizeof(retry);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, L"Retry-After", retry, &n, NULL);
            error = ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
            http_error(e, status, retry, reset);
            goto done;
        }
        for (;;) {
            BYTE chunk[32768];
            DWORD got = 0;
            if (GetTickCount64() > deadline) {
                error = ERROR_TIMEOUT;
                goto done;
            }
            if (cancel && InterlockedCompareExchange(cancel, 0, 0)) {
                error = ERROR_CANCELLED;
                goto done;
            }
            if (!WinHttpReadData(req, chunk, sizeof(chunk), &got))
                goto done;
            if (!got)
                break;
            if (total + got > limit) {
                error = ERROR_FILE_TOO_LARGE;
                goto done;
            }
            if (file != INVALID_HANDLE_VALUE) {
                DWORD written;
                if (!WriteFile(file, chunk, got, &written, NULL) || written != got)
                    goto done;
            } else {
                if (used + got > capacity) {
                    DWORD next = used + got + 32768;
                    BYTE *p = realloc(bytes, next);
                    if (!p) {
                        error = ERROR_NOT_ENOUGH_MEMORY;
                        goto done;
                    }
                    bytes = p;
                    capacity = next;
                }
                memcpy(bytes + used, chunk, got);
                used += got;
            }
            total += got;
            if (progress)
                progress(context, total, limit);
        }
        ok = TRUE;
        break;
    }
done:
    if (!ok && !e->win32) {
        DWORD last = GetLastError();
        DWORD code = error != ERROR_GEN_FAILURE ? error : last ? last : error;
        if (cancel && InterlockedCompareExchange(cancel, 0, 0))
            code = ERROR_CANCELLED;
        np_error(e, code, 0, network_reason(code));
    }
    if (req)
        WinHttpCloseHandle(req);
    if (connection)
        WinHttpCloseHandle(connection);
    if (session)
        WinHttpCloseHandle(session);
    if (ok && memory) {
        *memory = bytes;
        *length = used;
    } else
        free(bytes);
    return ok;
}
static BOOL request(const WCHAR *initial, BOOL asset, BYTE **memory, DWORD *length, HANDLE file,
                    ULONGLONG limit, volatile LONG *cancel, NP_DOWNLOAD_PROGRESS progress, void *context,
                    NP_ERROR *e) {
    return request_impl(initial, asset, memory, length, file, limit, cancel, progress, context, e, NULL);
}
BOOL np_public_http(const WCHAR *url, BOOL asset, BYTE **memory, DWORD *length, HANDLE file,
                    ULONGLONG limit, volatile LONG *cancel, NP_DOWNLOAD_PROGRESS progress,void *ctx,NP_ERROR *e) {
    return request(url,asset,memory,length,file,limit,cancel,progress,ctx,e);
}
BOOL np_private_http(const NP_UPDATE_SETTINGS *s, const WCHAR *token, const WCHAR *url, BOOL asset,
                     BYTE **memory, DWORD *length, HANDLE file, ULONGLONG limit, volatile LONG *cancel,
                     NP_DOWNLOAD_PROGRESS progress, void *context, NP_ERROR *e) {
    WCHAR prefix[400];
    if (!np_repo_valid(s->owner, s->repo) || !np_token_valid(token)) {
        np_error(e, ERROR_LOGON_FAILURE, 0, L"请重新授权有效的 Fine-grained Token");
        return FALSE;
    }
    swprintf_s(prefix, 400, L"https://api.github.com/repos/%s/%s", s->owner, s->repo);
    size_t n = wcslen(prefix);
    if (wcsncmp(url, prefix, n) || (url[n] && url[n] != L'/') || wcschr(url, L'\\') || wcschr(url, L'#')) {
        np_error(e, ERROR_ACCESS_DENIED, 0, L"授权仅可发送到已配置仓库的 GitHub API");
        return FALSE;
    }
    BOOL ok = request_impl(url, asset, memory, length, file, limit, cancel, progress, context, e, token);
    if (!ok && e->http_status == 401) {
        np_error(e, ERROR_LOGON_FAILURE, 0, L"GitHub 授权无效、过期或已撤销，请重新授权");
        e->http_status = 401;
    }
    return ok;
}
BOOL np_online_check(const NP_UPDATE_SETTINGS *s, NP_ONLINE_RELEASE *r, volatile LONG *cancel, NP_ERROR *e) {
    ZeroMemory(e, sizeof(*e));
    if (!np_repo_valid(s->owner, s->repo) || wcscmp(s->channel, L"Stable")) {
        np_error(e, ERROR_INVALID_PARAMETER, 0, L"请填写有效 GitHub Owner / Repository；当前仅支持 Stable");
        return FALSE;
    }
    WCHAR url[512];
    swprintf_s(url, 512, L"https://api.github.com/repos/%s/%s/releases/latest", s->owner, s->repo);
    BYTE *bytes = NULL;
    DWORD n = 0;
    ZeroMemory(r, sizeof(*r));
    if (!request(url, FALSE, &bytes, &n, INVALID_HANDLE_VALUE, 1024 * 1024, cancel, NULL, NULL, e)) {
        /* A private repository and an absent release both return 404 anonymously.
           Confirm public visibility before reporting an empty stable channel. */
        if (e->http_status == 404) {
            NP_ERROR probe = {0};
            swprintf_s(url, 512, L"https://api.github.com/repos/%s/%s", s->owner, s->repo);
            if (request(url, FALSE, &bytes, &n, INVALID_HANDLE_VALUE, 1024 * 1024, cancel, NULL, NULL,
                        &probe)) {
                NP_JSON j;
                if (np_json_parse(&j, (char *)bytes, n)) {
                    if (j.tokens[0].type == '{' && np_json_false(&j, np_json_field(&j, 0, "private"))) {
                        np_error(e, ERROR_NOT_FOUND, 0,
                                 L"尚无可用发布版本：公开仓库可访问，但 latest 没有稳定版");
                        e->http_status = 404;
                    }
                    np_json_free(&j);
                }
                free(bytes);
            } else if (probe.win32 == ERROR_CANCELLED)
                *e = probe;
        }
        return FALSE;
    }
    BOOL ok = np_release_json((char *)bytes, n, s, r, e);
    free(bytes);
    return ok;
}
BOOL np_online_verify(const WCHAR *path, const NP_ONLINE_RELEASE *r, NP_ERROR *e) {
    NP_RELEASE signed_release;
    DWORD current[4] = {NP_VERSION_NUM};
    if (!(r->installer ? np_verify_installer(path, &signed_release, e)
                       : np_verify_release(path, &signed_release, e)))
        return FALSE;
    if (memcmp(signed_release.version, r->version, sizeof(r->version)) || signed_release.size != r->size ||
        np_version_compare(signed_release.version, current) < (r->installer ? 0 : 1)) {
        np_error(e, ERROR_REVISION_MISMATCH, 0, L"签名版本与 Release 不一致，或不是更高版本；拒绝升级");
        return FALSE;
    }
    if (r->digest[0]) {
        WCHAR digest[80] = L"sha256:";
        for (int i = 0; i < 32; ++i)
            swprintf_s(digest + 7 + i * 2, 80 - 7 - i * 2, L"%02x", signed_release.hash[i]);
        if (wcscmp(digest, r->digest)) {
            np_error(e, ERROR_CRC, 0, L"GitHub digest 与签名 SHA-256 不一致");
            return FALSE;
        }
    }
    return TRUE;
}
#ifdef NP_READONLY_BUILD
/* Explicit diagnostic only: public GitHub CLI checksum text is never saved or executed.
   Exercises the SAME WinHTTP proxy/TLS/CDN path without requiring a NetPilot release. */
BOOL np_online_transport_probe(NP_ERROR *e) {
    BYTE *bytes = NULL;
    DWORD n = 0;
    volatile LONG cancel = 0;
    ZeroMemory(e, sizeof(*e));
    if (!request(L"https://api.github.com/repos/cli/cli/releases/latest", FALSE, &bytes, &n,
                 INVALID_HANDLE_VALUE, 1024 * 1024, &cancel, NULL, NULL, e))
        return FALSE;
    NP_JSON j;
    WCHAR tag[64], name[128], url[512];
    DWORD version[4];
    BOOL ok = np_json_parse(&j, (char *)bytes, n);
    BOOL found = FALSE;
    if (ok) {
        ok = strfield(&j, 0, "tag_name", tag, 64) && np_semver(tag, version) && tag[0] == L'v';
        if (ok) {
            swprintf_s(name, 128, L"gh_%s_checksums.txt", tag + 1);
            swprintf_s(url, 512, L"https://github.com/cli/cli/releases/download/%s/%s", tag, name);
            int assets = np_json_field(&j, 0, "assets");
            if (assets >= 0 && j.tokens[assets].type == '[')
                for (int i = assets + 1; i < j.tokens[assets].next; i = j.tokens[i].next) {
                    WCHAR item[128], location[512];
                    if (strfield(&j, i, "name", item, 128) && !wcscmp(item, name) &&
                        strfield(&j, i, "browser_download_url", location, 512) && !wcscmp(location, url))
                        found = TRUE;
                }
        }
        np_json_free(&j);
    }
    free(bytes);
    bytes = NULL;
    if (!ok || !found) {
        np_error(e, ERROR_INVALID_DATA, 0, L"Public diagnostic checksum asset missing");
        return FALSE;
    }
    ok = request(url, TRUE, &bytes, &n, INVALID_HANDLE_VALUE, 65536, &cancel, NULL, NULL, e);
    if (ok) {
        ok = n > 0;
        wprintf(L"Real public API + HTTPS asset/CDN download: %s, %lu bytes; text only, no execution.\n",
                name, n);
    }
    free(bytes);
    return ok;
}
#endif
void np_online_cleanup(const WCHAR *path) {
    /* Only the generated cache shape is eligible. Never recursive, never a caller-supplied arbitrary target.
     */
    WCHAR base[NP_PATH], prefix[NP_PATH], side[NP_PATH], dir[NP_PATH];
    if (!online_base(base))
        return;
    swprintf_s(prefix, NP_PATH, L"%s\\UpdateCache-{", base);
    size_t n = wcslen(prefix);
    const WCHAR *filename = wcsrchr(path, L'\\');
    if (!filename || (wcscmp(filename + 1, APP_ASSET) && wcscmp(filename + 1, SETUP_ASSET)) ||
        wcsncmp(path, prefix, n) || wcslen(path) != n + 37 + wcslen(filename) || path[n + 36] != L'}')
        return;
    WCHAR guidtext[40];
    wcsncpy_s(guidtext, 40, path + n - 1, 38);
    GUID id;
    if (FAILED(CLSIDFromString(guidtext, &id)))
        return;
    wcscpy_s(dir, NP_PATH, path);
    WCHAR *slash = wcsrchr(dir, L'\\');
    *slash = 0;
    DWORD attr = GetFileAttributesW(dir);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT))
        return;
    swprintf_s(side, NP_PATH, L"%s.update", path);
    DeleteFileW(side);
    DeleteFileW(path);
    RemoveDirectoryW(dir);
}
BOOL np_online_download(const NP_ONLINE_RELEASE *r, const NP_UPDATE_SETTINGS *s, WCHAR out[NP_PATH],
                        volatile LONG *cancel, NP_DOWNLOAD_PROGRESS progress, void *context, NP_ERROR *e) {
    ZeroMemory(e, sizeof(*e));
    out[0] = 0;
    DWORD current[4] = {NP_VERSION_NUM};
    if (!np_asset_url(r->url, s, r->tag, asset_name(r->installer)) ||
        !np_asset_url(r->signature_url, s, r->tag, signature_name(r->installer)) || !r->size ||
        r->size > PAYLOAD_LIMIT || np_version_compare(r->version, current) < (r->installer ? 0 : 1)) {
        np_error(e, ERROR_INVALID_DATA, 0, L"更新来源、版本或大小不符合要求");
        return FALSE;
    }
    WCHAR base[NP_PATH], folder[NP_PATH], id[40], side[NP_PATH];
    GUID guid;
    if (!online_base(base) || FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(&guid, id, 40))
        goto fail;
    DWORD attr = GetFileAttributesW(base);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
        SetLastError(ERROR_ACCESS_DENIED);
        goto fail;
    }
    if (swprintf_s(folder, NP_PATH, L"%s\\UpdateCache-%s", base, id) < 0 || !CreateDirectoryW(folder, NULL))
        goto fail;
    swprintf_s(out, NP_PATH, L"%s\\%s", folder, asset_name(r->installer));
    swprintf_s(side, NP_PATH, L"%s.update", out);
    BYTE *manifest = NULL;
    DWORD bytes = 0;
    if (!request(r->signature_url, TRUE, &manifest, &bytes, INVALID_HANDLE_VALUE, sizeof(NP_RELEASE), cancel,
                 NULL, NULL, e))
        goto fail;
    BOOL valid = bytes == sizeof(NP_RELEASE) &&
                 (r->installer ? np_installer_signature((NP_RELEASE *)manifest)
                               : np_release_signature((NP_RELEASE *)manifest)) &&
                 !memcmp(((NP_RELEASE *)manifest)->version, r->version, sizeof(r->version)) &&
                 ((NP_RELEASE *)manifest)->size == r->size;
    if (!valid) {
        free(manifest);
        np_error(e, NP_SIGNATURE_ERROR, 0, L"发布清单签名、版本或大小不匹配；已阻止下载执行");
        goto fail;
    }
    HANDLE sf = CreateFileW(side, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                            FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    DWORD written = 0;
    valid = sf != INVALID_HANDLE_VALUE && WriteFile(sf, manifest, bytes, &written, NULL) &&
            written == bytes && FlushFileBuffers(sf);
    free(manifest);
    if (sf != INVALID_HANDLE_VALUE)
        CloseHandle(sf);
    if (!valid)
        goto fail;
    HANDLE file = CreateFileW(out, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                              FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE)
        goto fail;
    BOOL ok = request(r->url, TRUE, NULL, NULL, file, r->size, cancel, progress, context, e);
    if (ok)
        ok = FlushFileBuffers(file);
    CloseHandle(file);
    if (ok)
        ok = np_online_verify(out, r, e);
    if (ok)
        return TRUE;
fail:
    if (!e->win32)
        np_error(e, GetLastError() ? GetLastError() : ERROR_WRITE_FAULT, 0, L"更新下载或缓存写入失败");
    if (out[0])
        np_online_cleanup(out);
    out[0] = 0;
    return FALSE;
}

/* Installation identity is a fixed Inno record plus the canonical running location, not a filename guess. */
BOOL np_installed_directory(WCHAR out[NP_PATH]) {
    HKEY key;
    out[0] = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, NP_INSTALL_KEY, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key))
        return FALSE;
    DWORD n = NP_PATH * sizeof(WCHAR), type = 0;
    LONG result = RegQueryValueExW(key, L"Inno Setup: App Path", NULL, &type, (BYTE *)out, &n);
    RegCloseKey(key);
    if (result || type != REG_SZ || n < 4 || n >= NP_PATH * sizeof(WCHAR) || out[n / 2 - 1]) {
        out[0] = 0;
        return FALSE;
    }
    return TRUE;
}
BOOL np_current_installed(void) {
    WCHAR dir[NP_PATH], expected[NP_PATH], current[NP_PATH];
    return np_installed_directory(dir) && swprintf_s(expected, NP_PATH, L"%s\\NetPilotLite.exe", dir) > 0 &&
           GetModuleFileNameW(NULL, current, NP_PATH) && !_wcsicmp(expected, current);
}
BOOL np_installer_launch(const WCHAR *path, HANDLE *process, NP_ERROR *e) {
    /* Read lock spans verification and process creation, including the UAC prompt. No supplied arguments. */
    NP_RELEASE release;
    HANDLE lock = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    BOOL ok = lock != INVALID_HANDLE_VALUE && np_verify_installer(path, &release, e);
    DWORD current[4] = {NP_VERSION_NUM};
    if (ok && np_version_compare(release.version, current) < 0) {
        np_error(e, ERROR_REVISION_MISMATCH, 0, L"Installer downgrade refused");
        ok = FALSE;
    }
    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.nShow = SW_SHOWNORMAL;
    if (ok) {
        ok = ShellExecuteExW(&sei);
        if (!ok)
            np_error(e, GetLastError(), 0, L"Start trusted Offline Setup / UAC cancelled");
    }
    if (lock != INVALID_HANDLE_VALUE)
        CloseHandle(lock);
    if (ok) {
        if (process)
            *process = sei.hProcess;
        else
            CloseHandle(sei.hProcess);
    } else if (!e->win32)
        np_error(e, GetLastError(), 0, L"Lock installer for verification");
    return ok;
}
