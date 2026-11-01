#ifndef NETPILOT_LITE_H
#define NETPILOT_LITE_H
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define NP_MAX_ADAPTERS 64
#define NP_ID_LEN 512
#define NP_NAME_LEN 256
#define NP_BINDING_VERSION 1

typedef enum NP_STATE {
    NP_UNKNOWN,
    NP_DISABLED,
    NP_ENABLED_DISCONNECTED,
    NP_ENABLED_CONNECTED,
    NP_ENABLED_LINK_UNKNOWN,
    NP_DEVICE_ERROR,
    NP_INTERFACE_DISABLED
} NP_STATE;
typedef struct NP_BINDING {
    DWORD version;
    GUID guid;
    WCHAR instance[NP_ID_LEN];
} NP_BINDING;
typedef struct NP_ADAPTER {
    NP_BINDING identity;
    WCHAR name[NP_NAME_LEN];
    WCHAR alias[NP_NAME_LEN];
    BOOL physical_ethernet;
    BOOL intel_i219;
    BOOL physical_wifi, intel_ax203;
    WCHAR ssid[64], ip[80];
    DWORD wifi_detail_error;
    NP_STATE state;
    DWORD device_status, problem;
} NP_ADAPTER;
typedef struct NP_SNAPSHOT {
    NP_ADAPTER adapters[NP_MAX_ADAPTERS];
    size_t count;
    WCHAR wifi[384];
} NP_SNAPSHOT;
typedef struct NP_ERROR {
    DWORD win32;
    DWORD config_ret;
    WCHAR operation[256];
    DWORD http_status; /* Online diagnostics only; never persisted in user settings. */
} NP_ERROR;
typedef struct NP_BACKEND {
    void *context;
    BOOL (*scan)(void *, NP_SNAPSHOT *, NP_ERROR *);
    BOOL (*set_enabled)(void *, const NP_BINDING *, BOOL, NP_ERROR *);
    BOOL (*safe_local_session)(void *);
    ULONGLONG (*now)(void *);
    void (*pause)(void *, DWORD);
} NP_BACKEND;
typedef struct NP_RESULT {
    BOOL success, unchanged, observed;
    NP_STATE state;
    NP_ERROR error;
} NP_RESULT;

BOOL np_identity_equal(const NP_BINDING *, const NP_BINDING *);
const NP_ADAPTER *np_find(const NP_SNAPSHOT *, const NP_BINDING *);
int np_auto_select(const NP_SNAPSHOT *);
int np_auto_wifi(const NP_SNAPSHOT *);
NP_RESULT np_hotspot(NP_BACKEND *, const NP_BINDING *, const NP_BINDING *, BOOL, DWORD);
BOOL np_enabled(NP_STATE);
const WCHAR *np_state_text(NP_STATE);
void np_error(NP_ERROR *, DWORD, DWORD, const WCHAR *);
NP_RESULT np_execute(NP_BACKEND *, const NP_BINDING *, BOOL, DWORD);
NP_BACKEND np_windows_backend(void);
int np_binding_load(NP_BINDING *); /* 0: first run, 1: valid, -1: unreadable; never auto-retarget -1. */
BOOL np_binding_save(const NP_BINDING *, NP_ERROR *);

typedef struct NP_MOCK {
    NP_SNAPSHOT snapshot;
    ULONGLONG clock;
    unsigned writes, reads;
    BOOL remote, fail_write, ignore_write, fail_read;
    NP_BINDING last_target;
} NP_MOCK;
void np_mock_init(NP_MOCK *);
NP_BACKEND np_mock_backend(NP_MOCK *);
int np_app_run(HINSTANCE, int, BOOL, BOOL);
#endif
