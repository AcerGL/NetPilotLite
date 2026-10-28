#include "update.h"
#include <bcrypt.h>
#include <stdio.h>
#include <wchar.h>
#include <winver.h>
#include <stddef.h>
BOOL np_read_exact(const WCHAR *p, void *data, DWORD n) {
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    BY_HANDLE_FILE_INFORMATION i;
    DWORD got = 0;
    BOOL ok = GetFileInformationByHandle(f, &i) && !(i.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
              i.nNumberOfLinks == 1 && i.nFileSizeHigh == 0 && i.nFileSizeLow == n &&
              ReadFile(f, data, n, &got, NULL) && got == n;
    CloseHandle(f);
    if (!ok)
        SetLastError(ERROR_INVALID_DATA);
    return ok;
}
BOOL np_write_exact(const WCHAR *p, const void *data, DWORD n) {
    HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_FLAG_WRITE_THROUGH, NULL);
    DWORD done = 0;
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL ok = WriteFile(f, data, n, &done, NULL) && done == n && FlushFileBuffers(f);
    DWORD e = GetLastError();
    CloseHandle(f);
    SetLastError(e);
    return ok;
}
BOOL np_hash(const void *data, DWORD bytes, BYTE out[32]) {
    BCRYPT_ALG_HANDLE a = NULL;
    BCRYPT_HASH_HANDLE h = NULL;
    BOOL ok = BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, NULL, 0) >= 0 &&
              BCryptCreateHash(a, &h, NULL, 0, NULL, 0, 0) >= 0 &&
              BCryptHashData(h, (PUCHAR)data, bytes, 0) >= 0 && BCryptFinishHash(h, out, 32, 0) >= 0;
    if (h)
        BCryptDestroyHash(h);
    if (a)
        BCryptCloseAlgorithmProvider(a, 0);
    return ok;
}
BOOL np_hash_file(const WCHAR *path, BYTE out[32]) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    BY_HANDLE_FILE_INFORMATION i;
    BCRYPT_ALG_HANDLE a = NULL;
    BCRYPT_HASH_HANDLE h = NULL;
    BYTE buf[65536];
    DWORD got;
    BOOL ok = GetFileInformationByHandle(f, &i) &&
              !(i.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
              i.nNumberOfLinks == 1 &&
              BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, NULL, 0) >= 0 &&
              BCryptCreateHash(a, &h, NULL, 0, NULL, 0, 0) >= 0;
    while (ok) {
        if (!ReadFile(f, buf, sizeof(buf), &got, NULL)) {
            ok = FALSE;
            break;
        }
        if (!got)
            break;
        ok = BCryptHashData(h, buf, got, 0) >= 0;
    }
    if (ok)
        ok = BCryptFinishHash(h, out, 32, 0) >= 0;
    if (h)
        BCryptDestroyHash(h);
    if (a)
        BCryptCloseAlgorithmProvider(a, 0);
    CloseHandle(f);
    return ok;
}
BOOL np_file_version(const WCHAR *path, DWORD v[4]) {
    DWORD ignored = 0, n = GetFileVersionInfoSizeW(path, &ignored);
    if (!n || n > 65536)
        return FALSE;
    void *data = malloc(n);
    VS_FIXEDFILEINFO *info = NULL;
    UINT size = 0;
    WCHAR *product = NULL;
    UINT chars = 0;
    BOOL ok = data && GetFileVersionInfoW(path, 0, n, data) &&
              VerQueryValueW(data, L"\\", (void **)&info, &size) && size >= sizeof(*info) &&
              info->dwSignature == 0xfeef04bd &&
              VerQueryValueW(data, L"\\StringFileInfo\\040904b0\\ProductName", (void **)&product, &chars) &&
              chars && !wcscmp(product, L"NetPilot Lite");
    if (ok) {
        v[0] = HIWORD(info->dwFileVersionMS);
        v[1] = LOWORD(info->dwFileVersionMS);
        v[2] = HIWORD(info->dwFileVersionLS);
        v[3] = LOWORD(info->dwFileVersionLS);
    }
    free(data);
    return ok;
}
BOOL np_release_signature(const NP_RELEASE *r) {
    BYTE digest[32];
    BCRYPT_ALG_HANDLE a = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    HRSRC res = FindResourceW(NULL, MAKEINTRESOURCEW(201), RT_RCDATA);
    HGLOBAL mem = res ? LoadResource(NULL, res) : NULL;
    BYTE *pub = mem ? LockResource(mem) : NULL;
    DWORD pubsize = res ? SizeofResource(NULL, res) : 0;
    BOOL ok = !memcmp(r->magic, "NPLREL1", 8) && pub && pubsize == 72 &&
              np_hash(r, (DWORD)offsetof(NP_RELEASE, signature), digest) &&
              BCryptOpenAlgorithmProvider(&a, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0) >= 0 &&
              BCryptImportKeyPair(a, NULL, BCRYPT_ECCPUBLIC_BLOB, &key, pub, pubsize, 0) >= 0 &&
              BCryptVerifySignature(key, NULL, digest, 32, (PUCHAR)r->signature, 64, 0) >= 0;
    if (key)
        BCryptDestroyKey(key);
    if (a)
        BCryptCloseAlgorithmProvider(a, 0);
    return ok;
}
BOOL np_release_matches(const WCHAR *path, const NP_RELEASE *r) {
    BYTE filehash[32];
    DWORD v[4];
    LARGE_INTEGER size;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    BOOL ok = f != INVALID_HANDLE_VALUE && GetFileSizeEx(f, &size) && (ULONGLONG)size.QuadPart == r->size;
    if (f != INVALID_HANDLE_VALUE)
        CloseHandle(f);
    if (!ok || !np_hash_file(path, filehash) || memcmp(filehash, r->hash, 32) || !np_file_version(path, v) ||
        memcmp(v, r->version, sizeof(v))) {
        return FALSE;
    }
    /* Refuse a signed non-x64 image as well. */
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    IMAGE_DOS_HEADER dos = {0};
    IMAGE_NT_HEADERS64 pe;
    DWORD got = 0;
    ok = f != INVALID_HANDLE_VALUE && ReadFile(f, &dos, sizeof(dos), &got, NULL) && got == sizeof(dos) &&
         dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 && dos.e_lfanew < 1048576;
    if (ok) {
        SetFilePointer(f, dos.e_lfanew, NULL, FILE_BEGIN);
        ok = ReadFile(f, &pe, sizeof(pe), &got, NULL) && got == sizeof(pe) &&
             pe.Signature == IMAGE_NT_SIGNATURE && pe.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
             pe.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    }
    if (f != INVALID_HANDLE_VALUE)
        CloseHandle(f);
    return ok;
}
BOOL np_verify_release(const WCHAR *path, NP_RELEASE *r, NP_ERROR *e) {
    WCHAR side[NP_PATH];
    swprintf_s(side, _countof(side), L"%s.update", path);
    if (!np_read_exact(side, r, sizeof(*r)) || !np_release_signature(r)) {
        np_error(e, NP_SIGNATURE_ERROR, 0, L"更新清单缺失或发布签名无效");
        return FALSE;
    }
    if (!np_release_matches(path, r)) {
        np_error(e, ERROR_CRC, 0, L"更新文件大小、SHA-256、产品或版本与签名清单不一致");
        return FALSE;
    }
    return TRUE;
}
