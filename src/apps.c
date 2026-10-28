#include "apps.h"
#include <tlhelp32.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <propsys.h>
#include <propkey.h>
#include <appmodel.h>
#include <stdio.h>
#include <wctype.h>
#include <wchar.h>
static BOOL local_path(const WCHAR *p) {
    return p && wcslen(p) > 3 && iswalpha(p[0]) && p[1] == L':' && p[2] == L'\\' && !wcsstr(p, L"\\..\\");
}
BOOL np_app_exe(const WCHAR *p) {
    if (!local_path(p) || _wcsicmp(PathFindExtensionW(p), L".exe"))
        return FALSE;
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    BY_HANDLE_FILE_INFORMATION info;
    IMAGE_DOS_HEADER dos = {0};
    DWORD n, signature;
    IMAGE_FILE_HEADER header;
    BOOL ok = GetFileInformationByHandle(f, &info) &&
              !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
              ReadFile(f, &dos, sizeof(dos), &n, NULL) && n == sizeof(dos) &&
              dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= sizeof(dos) && dos.e_lfanew < 1024 * 1024;
    if (ok) {
        SetFilePointer(f, dos.e_lfanew, NULL, FILE_BEGIN);
        ok = ReadFile(f, &signature, 4, &n, NULL) && n == 4 && signature == IMAGE_NT_SIGNATURE &&
             ReadFile(f, &header, sizeof(header), &n, NULL) && n == sizeof(header) &&
             (header.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) &&
             !(header.Characteristics & IMAGE_FILE_DLL);
    }
    CloseHandle(f);
    return ok;
}
BOOL np_display_icon_path(const WCHAR *raw, WCHAR *out, size_t chars) {
    WCHAR expanded[NP_PATH];
    DWORD n = ExpandEnvironmentStringsW(raw, expanded, NP_PATH);
    if (!n || n > NP_PATH)
        return FALSE;
    WCHAR *p = expanded;
    while (*p == L' ')
        ++p;
    if (*p == L'"') {
        WCHAR *end = wcschr(++p, L'"');
        if (!end)
            return FALSE;
        *end = 0;
        WCHAR *tail = end + 1;
        while (*tail == L' ')
            ++tail;
        if (*tail) {
            if (*tail != L',')
                return FALSE;
            WCHAR *stop;
            wcstol(tail + 1, &stop, 10);
            if (stop == tail + 1 || *stop)
                return FALSE;
        }
    } else {
        WCHAR *comma = wcsrchr(p, L',');
        if (comma) {
            WCHAR *end = NULL;
            wcstol(comma + 1, &end, 10);
            if (end == comma + 1 || *end)
                return FALSE;
            *comma = 0;
        }
    }
    size_t len = wcslen(p);
    while (len && p[len - 1] == L' ')
        p[--len] = 0;
    if (!local_path(p) || len >= chars || wcschr(p, L'"'))
        return FALSE;
    wcscpy_s(out, chars, p);
    return TRUE;
}
static BOOL installer_name(const WCHAR *p) {
    const WCHAR *n = PathFindFileNameW(p);
    return StrStrIW(n, L"unins") || StrStrIW(n, L"setup") || StrStrIW(n, L"updat") ||
           !_wcsicmp(n, L"msiexec.exe");
}
static void regtext(HKEY k, const WCHAR *name, WCHAR *out, DWORD chars) {
    DWORD bytes = chars * sizeof(WCHAR);
    out[0] = 0;
    if (RegGetValueW(k, NULL, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, out, &bytes) != ERROR_SUCCESS)
        out[0] = 0;
    out[chars - 1] = 0;
}
static void file_metadata(NP_APP_ENTRY *r) {
    DWORD ignored, n = GetFileVersionInfoSizeW(r->path, &ignored);
    if (!n || n > 1024 * 1024)
        return;
    void *data = malloc(n);
    if (!data)
        return;
    if (GetFileVersionInfoW(r->path, 0, n, data)) {
        struct LANG {
            WORD language, code;
        } *langs;
        UINT size;
        if (VerQueryValueW(data, L"\\VarFileInfo\\Translation", (void **)&langs, &size) && size >= 4) {
            const WCHAR *keys[] = {L"CompanyName", L"FileVersion"};
            WCHAR *outs[] = {r->publisher, r->version};
            size_t caps[] = {256, 80};
            for (int i = 0; i < 2; ++i)
                if (!outs[i][0]) {
                    WCHAR query[100], *v = NULL;
                    swprintf_s(query, 100, L"\\StringFileInfo\\%04x%04x\\%s", langs[0].language,
                               langs[0].code, keys[i]);
                    if (VerQueryValueW(data, query, (void **)&v, &size) && v)
                        wcsncpy_s(outs[i], caps[i], v, _TRUNCATE);
                }
        }
    }
    free(data);
}
static void candidate(NP_APP_ENTRY *r, const WCHAR *display) {
    WCHAR path[NP_PATH];
    if (np_display_icon_path(display, path, NP_PATH) && np_app_exe(path) && !installer_name(path))
        wcscpy_s(r->path, NP_PATH, path);
    if (!r->path[0] && local_path(r->location)) {
        WCHAR glob[NP_PATH], found[NP_PATH] = L"";
        if (swprintf_s(glob, NP_PATH, L"%s\\*.exe", r->location) <= 0)
            return;
        WIN32_FIND_DATAW data;
        HANDLE h = FindFirstFileW(glob, &data);
        int count = 0;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (++count > 128)
                    break;
                if (!(data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
                    !installer_name(data.cFileName)) {
                    if (swprintf_s(path, NP_PATH, L"%s\\%s", r->location, data.cFileName) > 0 &&
                        np_app_exe(path)) {
                        if (found[0]) {
                            found[0] = 0;
                            break;
                        }
                        wcscpy_s(found, NP_PATH, path);
                    }
                }
            } while (FindNextFileW(h, &data));
            FindClose(h);
        }
        if (count <= 128 && found[0])
            wcscpy_s(r->path, NP_PATH, found);
    }
    r->candidate = r->path[0] != 0;
    wcscpy_s(r->note, 180,
             r->candidate ? L"候选 EXE · 添加前需确认（可能为启动器）"
                          : L"无法确定 EXE · 请从运行进程或手动选择");
}
static void append(NP_APP_LIST *list, NP_APP_ENTRY *r) {
    for (size_t i = 0; i < list->count; ++i) {
        NP_APP_ENTRY *p = &list->rows[i];
        if (r->package && p->package && !r->pid && !p->pid && !_wcsicmp(r->family, p->family) &&
            !_wcsicmp(r->aumid, p->aumid)) {
            if (r->icon)
                DestroyIcon(r->icon);
            return;
        }
        if (!r->package && !p->package && !r->pid && !p->pid && !_wcsicmp(r->name, p->name) &&
            !_wcsicmp(r->path, p->path) && !_wcsicmp(r->version, p->version)) {
            if (r->icon)
                DestroyIcon(r->icon);
            return;
        }
    }
    if (list->count >= NP_MAX_APPS) {
        list->truncated = TRUE;
        if (r->icon)
            DestroyIcon(r->icon);
        return;
    }
    if (!r->icon && r->path[0])
        ExtractIconExW(r->path, 0, NULL, &r->icon, 1);
    if (!r->icon)
        r->icon = CopyIcon(LoadIconW(NULL, IDI_APPLICATION));
    list->rows[list->count++] = *r;
}
static void uninstall_view(NP_APP_LIST *list, HKEY root, REGSAM view) {
    HKEY parent;
    LONG err = RegOpenKeyExW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0,
                             KEY_READ | view, &parent);
    if (err != ERROR_SUCCESS) {
        if (err != ERROR_FILE_NOT_FOUND)
            ++list->registry_errors;
        return;
    }
    for (DWORD index = 0; index < 8192; ++index) {
        WCHAR key[256];
        DWORD chars = 256;
        err = RegEnumKeyExW(parent, index, key, &chars, NULL, NULL, NULL, NULL);
        if (err == ERROR_NO_MORE_ITEMS)
            break;
        if (err) {
            ++list->registry_errors;
            continue;
        }
        HKEY k;
        if (RegOpenKeyExW(parent, key, 0, KEY_READ | view, &k))
            continue;
        NP_APP_ENTRY r = {0};
        WCHAR icon[NP_PATH];
        regtext(k, L"DisplayName", r.name, 256);
        regtext(k, L"Publisher", r.publisher, 256);
        regtext(k, L"DisplayVersion", r.version, 80);
        regtext(k, L"InstallLocation", r.location, NP_PATH);
        regtext(k, L"DisplayIcon", icon, NP_PATH);
        regtext(k, L"UninstallString", r.uninstall, NP_PATH);
        RegCloseKey(k);
        if (r.name[0]) {
            candidate(&r, icon);
            append(list, &r);
        }
        if (list->truncated)
            break;
    }
    RegCloseKey(parent);
}
static void shell_string(IShellItem2 *item, const WCHAR *canonical, WCHAR *out, size_t chars) {
    PROPERTYKEY key;
    PWSTR text = NULL;
    if (SUCCEEDED(PSGetPropertyKeyFromName(canonical, &key)) &&
        SUCCEEDED(item->lpVtbl->GetString(item, &key, &text))) {
        wcsncpy_s(out, chars, text, _TRUNCATE);
        CoTaskMemFree(text);
    }
}
static void packages(NP_APP_LIST *list) {
    IShellItem *folder = NULL;
    IEnumShellItems *items = NULL;
    HRESULT hr = SHGetKnownFolderItem(&FOLDERID_AppsFolder, 0, NULL, &IID_IShellItem, (void **)&folder);
    if (SUCCEEDED(hr))
        hr = folder->lpVtbl->BindToHandler(folder, NULL, &BHID_EnumItems, &IID_IEnumShellItems,
                                           (void **)&items);
    if (SUCCEEDED(hr)) {
        IShellItem *item;
        ULONG got;
        while (items->lpVtbl->Next(items, 1, &item, &got) == S_OK) {
            IShellItem2 *props = NULL;
            NP_APP_ENTRY r = {0};
            PWSTR name = NULL;
            if (SUCCEEDED(item->lpVtbl->QueryInterface(item, &IID_IShellItem2, (void **)&props))) {
                shell_string(props, L"System.AppUserModel.ID", r.aumid, 256);
                UINT32 family = 128, praid = 256;
                WCHAR relative[256];
                if (ParseApplicationUserModelId(r.aumid, &family, r.family, &praid, relative) ==
                    ERROR_SUCCESS) {
                    r.package = TRUE;
                    if (SUCCEEDED(item->lpVtbl->GetDisplayName(item, SIGDN_NORMALDISPLAY, &name))) {
                        wcsncpy_s(r.name, 256, name, _TRUNCATE);
                        CoTaskMemFree(name);
                    }
                    shell_string(props, L"System.AppUserModel.PackagePublisher", r.publisher, 256);
                    wcscpy_s(r.note, 180, L"MSIX/Store 包身份 · 不使用受保护包路径绑定 EXE");
                    IShellItemImageFactory *images = NULL;
                    if (SUCCEEDED(item->lpVtbl->QueryInterface(item, &IID_IShellItemImageFactory,
                                                               (void **)&images))) {
                        HBITMAP bmp = NULL;
                        if (SUCCEEDED(
                                images->lpVtbl->GetImage(images, (SIZE){24, 24}, SIIGBF_ICONONLY, &bmp))) {
                            BYTE bits[96] = {0};
                            HBITMAP mask = CreateBitmap(24, 24, 1, 1, bits);
                            ICONINFO info = {TRUE, 0, 0, mask, bmp};
                            r.icon = CreateIconIndirect(&info);
                            DeleteObject(mask);
                            DeleteObject(bmp);
                        }
                        images->lpVtbl->Release(images);
                    }
                    append(list, &r);
                }
                props->lpVtbl->Release(props);
            }
            item->lpVtbl->Release(item);
            if (list->truncated)
                break;
        }
    }
    list->shell_error = hr;
    if (items)
        items->lpVtbl->Release(items);
    if (folder)
        folder->lpVtbl->Release(folder);
}
static void processes(NP_APP_LIST *list) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        ++list->process_errors;
        return;
    }
    PROCESSENTRY32W pe = {sizeof(pe)};
    if (Process32FirstW(snap, &pe))
        do {
            NP_APP_ENTRY r = {0};
            r.pid = pe.th32ProcessID;
            wcsncpy_s(r.name, 256, pe.szExeFile, _TRUNCATE);
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, r.pid);
            DWORD n = NP_PATH;
            if (!h || !QueryFullProcessImageNameW(h, 0, r.path, &n)) {
                r.path[0] = 0;
                r.error = GetLastError();
                ++list->process_errors;
                swprintf_s(r.note, 180, L"路径不可读/进程已退出 · Windows %lu", r.error);
            } else {
                UINT32 chars = 128;
                if (GetPackageFamilyName(h, &chars, r.family) == ERROR_SUCCESS) {
                    r.package = TRUE;
                    chars = 256;
                    GetApplicationUserModelId(h, &chars, r.aumid);
                }
                wcscpy_s(r.note, 180,
                         r.package ? L"打包进程 · 仅保留包身份，出口待验证"
                                   : L"运行中进程 · 路径可读，出口待验证");
                file_metadata(&r);
            }
            if (h)
                CloseHandle(h);
            append(list, &r);
            if (list->truncated)
                break;
        } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
}
NP_APP_LIST *np_apps_scan(BOOL running) {
    NP_APP_LIST *list = calloc(1, sizeof(*list));
    if (!list)
        return NULL;
    HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (running)
        processes(list);
    else {
        uninstall_view(list, HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
        uninstall_view(list, HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY);
        uninstall_view(list, HKEY_CURRENT_USER, KEY_WOW64_64KEY);
        uninstall_view(list, HKEY_CURRENT_USER, KEY_WOW64_32KEY);
        packages(list);
    }
    if (SUCCEEDED(co))
        CoUninitialize();
    return list;
}
void np_apps_free(NP_APP_LIST *list) {
    if (list) {
        for (size_t i = 0; i < list->count; ++i)
            if (list->rows[i].icon)
                DestroyIcon(list->rows[i].icon);
        free(list);
    }
}
