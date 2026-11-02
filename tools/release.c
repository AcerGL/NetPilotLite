/* Offline release signer. The DPAPI-protected private key is NEVER a runtime asset. */
#include "update.h"
#include <bcrypt.h>
#include <wincrypt.h>
#include <stddef.h>
#include <stdio.h>
#include "json.h"
static BOOL catalog_version(const WCHAR *path, DWORD v[4]) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD n = f == INVALID_HANDLE_VALUE ? 0 : GetFileSize(f, NULL);
    if (f != INVALID_HANDLE_VALUE)
        CloseHandle(f);
    char *bytes = n && n <= 65536 ? malloc(n) : NULL;
    NP_JSON j;
    WCHAR value[64], roundtrip[64];
    unsigned x, y, z;
    BOOL ok = bytes && np_read_exact(path, bytes, n) && np_json_parse(&j, bytes, n);
    if (ok) {
        ok = np_json_string(&j, np_json_field(&j, 0, "version"), value, 64);
        np_json_free(&j);
    }
    free(bytes);
    if (!ok || swscanf_s(value, L"%u.%u.%u", &x, &y, &z) != 3 || x > 65535 || y > 65535 || z > 65535)
        return FALSE;
    swprintf_s(roundtrip, 64, L"%u.%u.%u", x, y, z);
    if (wcscmp(roundtrip, value))
        return FALSE;
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = 0;
    return TRUE;
}
int wmain(int argc, WCHAR **argv) {
    BCRYPT_ALG_HANDLE a = NULL;
    BCRYPT_KEY_HANDLE k = NULL;
    BYTE private_blob[104], public_blob[72], digest[32];
    DWORD n = 0;
    DATA_BLOB raw = {0}, sealed = {0};
    BOOL ok = FALSE;
    if (argc != 4) {
        fwprintf(stderr,
                 L"release keygen private.dpapi public.bin | release sign private.dpapi NetPilotLite.exe\n");
        return 2;
    }
    if (BCryptOpenAlgorithmProvider(&a, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0) < 0)
        return 1;
    if (!wcscmp(argv[1], L"keygen")) {
        ok = BCryptGenerateKeyPair(a, &k, 256, 0) >= 0 && BCryptFinalizeKeyPair(k, 0) >= 0 &&
             BCryptExportKey(k, NULL, BCRYPT_ECCPRIVATE_BLOB, private_blob, sizeof(private_blob), &n, 0) >=
                 0 &&
             n == 104 &&
             BCryptExportKey(k, NULL, BCRYPT_ECCPUBLIC_BLOB, public_blob, sizeof(public_blob), &n, 0) >= 0 &&
             n == 72;
        raw.pbData = private_blob;
        raw.cbData = 104;
        if (ok)
            ok = CryptProtectData(&raw, L"NetPilot Lite offline release key", NULL, NULL, NULL,
                                  CRYPTPROTECT_UI_FORBIDDEN, &sealed) &&
                 np_write_exact(argv[2], sealed.pbData, sealed.cbData) &&
                 np_write_exact(argv[3], public_blob, 72);
        if (sealed.pbData)
            LocalFree(sealed.pbData);
    } else if (!wcscmp(argv[1], L"sign") || !wcscmp(argv[1], L"sign-installer") ||
               !wcscmp(argv[1], L"sign-catalog")) {
        BOOL installer = !wcscmp(argv[1], L"sign-installer");
        BOOL catalog = !wcscmp(argv[1], L"sign-catalog");
        BOOL environment = !wcscmp(argv[2], L"ENV");
        HANDLE f = environment
                       ? INVALID_HANDLE_VALUE
                       : CreateFileW(argv[2], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        DWORD size = f == INVALID_HANDLE_VALUE ? 0 : GetFileSize(f, NULL), got;
        if (size && size < 65536) {
            sealed.pbData = malloc(size);
            sealed.cbData = size;
            ok = sealed.pbData && ReadFile(f, sealed.pbData, size, &got, NULL) && got == size;
        }
        if (f != INVALID_HANDLE_VALUE)
            CloseHandle(f);
        if (environment) {
            WCHAR encoded[256] = {0};
            DWORD decoded = sizeof(private_blob);
            DWORD count = GetEnvironmentVariableW(L"NETPILOT_SIGNING_KEY_B64", encoded, _countof(encoded));
            ok = count > 0 && count < _countof(encoded) &&
                 CryptStringToBinaryW(encoded, count, CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, private_blob,
                                      &decoded, NULL, NULL) &&
                 decoded == sizeof(private_blob) &&
                 BCryptImportKeyPair(a, NULL, BCRYPT_ECCPRIVATE_BLOB, &k, private_blob, decoded, 0) >= 0;
            SecureZeroMemory(encoded, sizeof(encoded));
            SecureZeroMemory(private_blob, sizeof(private_blob));
            SetEnvironmentVariableW(L"NETPILOT_SIGNING_KEY_B64", NULL);
        } else if (ok)
            ok = CryptUnprotectData(&sealed, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &raw) &&
                 BCryptImportKeyPair(a, NULL, BCRYPT_ECCPRIVATE_BLOB, &k, raw.pbData, raw.cbData, 0) >= 0;
        if (ok) {
            BYTE expected[72];
            ok = np_read_exact(L"assets\\update-key.bin", expected, 72) &&
                 BCryptExportKey(k, NULL, BCRYPT_ECCPUBLIC_BLOB, public_blob, 72, &n, 0) >= 0 && n == 72 &&
                 !memcmp(expected, public_blob, 72);
            if (!ok)
                fwprintf(stderr, L"Private key does not match the pinned release public key.\n");
        }
        NP_RELEASE r = {0};
        memcpy(r.magic, catalog ? "NPLCAT1" : installer ? "NPLOFF1" : "NPLREL1", 8);
        WCHAR manifest[NP_PATH];
        f = CreateFileW(argv[3], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        LARGE_INTEGER length = {0};
        if (ok)
            ok = f != INVALID_HANDLE_VALUE && GetFileSizeEx(f, &length) &&
                 (catalog || (installer ? np_installer_version(argv[3], r.version)
                                        : np_file_version(argv[3], r.version))) &&
                 np_hash_file(argv[3], r.hash);
        if (f != INVALID_HANDLE_VALUE)
            CloseHandle(f);
        r.size = (ULONGLONG)length.QuadPart;
        if (catalog)
            ok = ok && catalog_version(argv[3], r.version);
        if (ok)
            ok = np_hash(&r, (DWORD)offsetof(NP_RELEASE, signature), digest) &&
                 BCryptSignHash(k, NULL, digest, 32, r.signature, 64, &n, 0) >= 0 && n == 64;
        swprintf_s(manifest, _countof(manifest), L"%s.update", argv[3]);
        if (ok) {
            DeleteFileW(manifest);
            ok = np_write_exact(manifest, &r, sizeof(r));
        }
        if (ok && !installer && !catalog) {
            WCHAR dir[NP_PATH], file[NP_PATH];
            wcscpy_s(dir, _countof(dir), argv[3]);
            WCHAR *slash = wcsrchr(dir, L'\\');
            if (slash)
                *slash = 0;
            else
                wcscpy_s(dir, _countof(dir), L".");
            char hex[65];
            for (int i = 0; i < 32; ++i)
                sprintf_s(hex + i * 2, 3, "%02x", r.hash[i]);
            swprintf_s(file, _countof(file), L"%s\\version.json", dir);
            FILE *out = NULL;
            _wfopen_s(&out, file, L"wb");
            if (out) {
                fprintf(out,
                        "{\"product\":\"NetPilot "
                        "Lite\",\"version\":\"%lu.%lu.%lu\",\"fileVersion\":\"%lu.%lu.%lu.%lu\",\"file\":"
                        "\"NetPilotLite.exe\",\"size\":%llu,"
                        "\"sha256\":\"%s\",\"signature\":\"ECDSA-P256-SHA256\",\"manifest\":\"NetPilotLite."
                        "exe.update\"}\n",
                        r.version[0], r.version[1], r.version[2], r.version[0], r.version[1], r.version[2],
                        r.version[3], r.size, hex);
                fclose(out);
            } else
                ok = FALSE;
            swprintf_s(file, _countof(file), L"%s\\SHA256SUMS.txt", dir);
            _wfopen_s(&out, file, L"wb");
            if (out) {
                fprintf(out, "%s  NetPilotLite.exe\n", hex);
                BYTE hash[32];
                if (np_hash_file(manifest, hash)) {
                    for (int i = 0; i < 32; ++i)
                        sprintf_s(hex + i * 2, 3, "%02x", hash[i]);
                    fprintf(out, "%s  NetPilotLite.exe.update\n", hex);
                } else
                    ok = FALSE;
                WCHAR updater[NP_PATH];
                swprintf_s(updater, _countof(updater), L"%s\\NetPilotLite.Updater.exe", dir);
                if (GetFileAttributesW(updater) != INVALID_FILE_ATTRIBUTES) {
                    if (np_hash_file(updater, hash)) {
                        for (int i = 0; i < 32; ++i)
                            sprintf_s(hex + i * 2, 3, "%02x", hash[i]);
                        fprintf(out, "%s  NetPilotLite.Updater.exe\n", hex);
                    } else
                        ok = FALSE;
                }
                fclose(out);
            } else
                ok = FALSE;
        }
        if (raw.pbData) {
            SecureZeroMemory(raw.pbData, raw.cbData);
            LocalFree(raw.pbData);
        }
        free(sealed.pbData);
    }
    SecureZeroMemory(private_blob, sizeof(private_blob));
    if (k)
        BCryptDestroyKey(k);
    if (a)
        BCryptCloseAlgorithmProvider(a, 0);
    wprintf(L"Release operation: %s\n", ok ? L"OK" : L"FAILED");
    return ok ? 0 : 1;
}
