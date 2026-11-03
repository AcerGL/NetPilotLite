#ifndef NP_PUBLIC_SETUP_H
#define NP_PUBLIC_SETUP_H
#include "online.h"
#define NP_SETUP_NAME L"NetPilotLite-Setup-x64.exe"
#define NP_MANIFEST_NAME L"manifest.json"
#define NP_FILE_MAX 32
#define NP_ASSET_MAX 64
typedef struct NP_ASSET {
    WCHAR name[160], url[2048];
    ULONGLONG size;
} NP_ASSET;
typedef struct NP_PUBLIC_RELEASE {
    NP_ONLINE_RELEASE info;
    unsigned count;
    NP_ASSET assets[NP_ASSET_MAX];
} NP_PUBLIC_RELEASE;
typedef struct NP_INSTALL_FILE {
    WCHAR asset[160], path[240];
    ULONGLONG size;
    BYTE hash[32];
} NP_INSTALL_FILE;
typedef struct NP_MANIFEST {
    DWORD version[4];
    WCHAR text_version[64];
    unsigned count;
    ULONGLONG total;
    NP_INSTALL_FILE files[NP_FILE_MAX];
} NP_MANIFEST;
typedef void (*NP_SETUP_PROGRESS)(void *, const WCHAR *, unsigned, unsigned, ULONGLONG, ULONGLONG);
BOOL np_public_http(const WCHAR *, BOOL, BYTE **, DWORD *, HANDLE, ULONGLONG, volatile LONG *,
                    NP_DOWNLOAD_PROGRESS, void *, NP_ERROR *);
BOOL np_public_parse(const char *, size_t, const NP_UPDATE_SETTINGS *, NP_PUBLIC_RELEASE *, NP_ERROR *);
BOOL np_public_check(const NP_UPDATE_SETTINGS *, const WCHAR *, NP_PUBLIC_RELEASE *, volatile LONG *,
                     NP_ERROR *);
const NP_ASSET *np_public_asset(const NP_PUBLIC_RELEASE *, const WCHAR *);
BOOL np_public_download_setup(const NP_PUBLIC_RELEASE *, WCHAR[NP_PATH], volatile LONG *,
                              NP_DOWNLOAD_PROGRESS, void *, NP_ERROR *);
BOOL np_public_launch_setup(const WCHAR *, const WCHAR *, const WCHAR *, NP_ERROR *);
BOOL np_setup_version(const WCHAR *, DWORD[4]);
BOOL np_setup_signature(const NP_RELEASE *);
BOOL np_setup_matches(const WCHAR *, const NP_RELEASE *);
BOOL np_verify_setup(const WCHAR *, NP_RELEASE *, NP_ERROR *);
BOOL np_install_relative(const WCHAR *);
BOOL np_install_absolute(const WCHAR *, BOOL);
BOOL np_install_mkdirs(const WCHAR *);
BOOL np_manifest_parse(const char *, size_t, NP_MANIFEST *, NP_ERROR *);
BOOL np_manifest_load(const WCHAR *, const DWORD *, NP_MANIFEST *, NP_ERROR *);
BOOL np_manifest_verify(const WCHAR *, const NP_MANIFEST *, NP_ERROR *);
BOOL np_setup_stage(const NP_PUBLIC_RELEASE *, WCHAR[NP_PATH], NP_MANIFEST *, volatile LONG *,
                    NP_SETUP_PROGRESS, void *, NP_ERROR *);
BOOL np_setup_cleanup(const WCHAR *);
BOOL np_setup_install(const WCHAR *, const WCHAR *, BOOL, BOOL, NP_ERROR *);
BOOL np_setup_recover(const WCHAR *, BOOL, NP_ERROR *);
BOOL np_setup_pending_start(NP_ERROR *);
#endif
