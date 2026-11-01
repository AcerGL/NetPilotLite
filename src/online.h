#ifndef NP_ONLINE_H
#define NP_ONLINE_H
#include "update.h"
#ifndef NP_GITHUB_OWNER
#define NP_GITHUB_OWNER L"AcerGL"
#endif
#ifndef NP_GITHUB_REPO
#define NP_GITHUB_REPO L"NetPilotLite"
#endif
typedef struct NP_UPDATE_SETTINGS {
    WCHAR owner[100], repo[100], channel[16], last_check[40];
    BOOL startup;
    BOOL installer; /* Runtime selection only; never serialized as user preference. */
} NP_UPDATE_SETTINGS;
typedef struct NP_ONLINE_RELEASE {
    DWORD version[4];
    WCHAR tag[64], name[256], body[16384], published[64];
    WCHAR url[2048], signature_url[2048], digest[80];
    ULONGLONG size;
    BOOL installer;
} NP_ONLINE_RELEASE;
typedef void (*NP_DOWNLOAD_PROGRESS)(void *, ULONGLONG, ULONGLONG);
#define NP_INSTALL_APPID L"{C47B553D-22CE-4CDD-BA10-2930FC8266D4}"
#define NP_INSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" NP_INSTALL_APPID L"_is1"
BOOL np_installed_directory(WCHAR[NP_PATH]);
BOOL np_current_installed(void);
BOOL np_installer_launch(const WCHAR *, HANDLE *, NP_ERROR *);
BOOL np_repo_valid(const WCHAR *, const WCHAR *);
BOOL np_semver(const WCHAR *, DWORD[4]);
int np_version_compare(const DWORD[4], const DWORD[4]);
BOOL np_release_json(const char *, size_t, const NP_UPDATE_SETTINGS *, NP_ONLINE_RELEASE *, NP_ERROR *);
BOOL np_asset_url(const WCHAR *, const NP_UPDATE_SETTINGS *, const WCHAR *, const WCHAR *);
BOOL np_redirect_url(const WCHAR *);
BOOL np_online_check(const NP_UPDATE_SETTINGS *, NP_ONLINE_RELEASE *, volatile LONG *, NP_ERROR *);
BOOL np_online_download(const NP_ONLINE_RELEASE *, const NP_UPDATE_SETTINGS *, WCHAR[NP_PATH],
                        volatile LONG *, NP_DOWNLOAD_PROGRESS, void *, NP_ERROR *);
BOOL np_online_verify(const WCHAR *, const NP_ONLINE_RELEASE *, NP_ERROR *);
void np_online_cleanup(const WCHAR *);
#ifdef NP_READONLY_BUILD
typedef struct NP_HTTP_FIXTURE {
    BOOL active;
    DWORD status;
    const char *json;
    WCHAR exe[NP_PATH], directory[NP_PATH];
    BOOL interrupt;
    DWORD repo_status; /* Optional second request when latest returns 404. */
    const char *repo_json;
} NP_HTTP_FIXTURE;
extern NP_HTTP_FIXTURE np_http_fixture;
BOOL np_online_transport_probe(NP_ERROR *);
#endif
#endif
