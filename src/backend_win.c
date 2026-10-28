#include "lite.h"
#include <setupapi.h>
#include <cfgmgr32.h>
#include <cfg.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <wtsapi32.h>
#include <objbase.h>
#include <string.h>
#include <wchar.h>
#include <wlanapi.h>
#include <ws2tcpip.h>

static const GUID net_class = {0x4d36e972, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const WCHAR *binding_key = L"Software\\NetPilotLite";
static BOOL valid_binding(const NP_BINDING *b) {
    GUID empty = {0};
    return b->version == NP_BINDING_VERSION && b->instance[0] && wmemchr(b->instance, 0, NP_ID_LEN) &&
           memcmp(&b->guid, &empty, sizeof(GUID));
}
int np_binding_load(NP_BINDING *binding) {
    DWORD size = sizeof(*binding);
    memset(binding, 0, sizeof(*binding));
    LSTATUS code =
        RegGetValueW(HKEY_CURRENT_USER, binding_key, L"Binding", RRF_RT_REG_BINARY, NULL, binding, &size);
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
        return 0;
    if (code == ERROR_SUCCESS && size == sizeof(*binding) && valid_binding(binding))
        return 1;
    memset(binding, 0, sizeof(*binding));
    return -1;
}
BOOL np_binding_save(const NP_BINDING *binding, NP_ERROR *error) {
    HKEY key;
    DWORD status;
    if (!valid_binding(binding)) {
        np_error(error, ERROR_INVALID_DATA, 0, L"保存网卡绑定");
        return FALSE;
    }
    status =
        (DWORD)RegCreateKeyExW(HKEY_CURRENT_USER, binding_key, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (status == ERROR_SUCCESS) {
        status =
            (DWORD)RegSetValueExW(key, L"Binding", 0, REG_BINARY, (const BYTE *)binding, sizeof(*binding));
        RegCloseKey(key);
    }
    if (status)
        np_error(error, status, 0, L"保存用户网卡绑定");
    return status == ERROR_SUCCESS;
}
static void device_text(HDEVINFO set, SP_DEVINFO_DATA *data, DWORD property, WCHAR *out, DWORD chars) {
    DWORD type = 0;
    out[0] = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, data, property, &type, (BYTE *)out, chars * sizeof(WCHAR),
                                           NULL) ||
        type != REG_SZ)
        out[0] = 0;
    out[chars - 1] = 0;
}
static BOOL reg_dword(HKEY key, const WCHAR *name, DWORD *value) {
    DWORD size = sizeof(*value);
    return RegGetValueW(key, NULL, name, RRF_RT_REG_DWORD, NULL, value, &size) == ERROR_SUCCESS;
}
static BOOL device_read(HDEVINFO set, SP_DEVINFO_DATA *data, const MIB_IF_TABLE2 *table, NP_ADAPTER *a) {
    WCHAR guid_text[80];
    DWORD chars = sizeof(guid_text), characteristics = 0, if_type = 0;
    ULONG status = 0, problem = 0;
    HKEY driver;
    const MIB_IF_ROW2 *row = NULL;
    memset(a, 0, sizeof(*a));
    a->identity.version = NP_BINDING_VERSION;
    if (!SetupDiGetDeviceInstanceIdW(set, data, a->identity.instance, NP_ID_LEN, NULL))
        return FALSE;
    /* Conservative hardware allow-list, so ROOT/SWD/TUN/VPN devices never become candidates. */
    if (_wcsnicmp(a->identity.instance, L"PCI\\", 4) && _wcsnicmp(a->identity.instance, L"USB\\", 4))
        return FALSE;
    driver = SetupDiOpenDevRegKey(set, data, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE);
    if (driver == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL guid_ok = RegGetValueW(driver, NULL, L"NetCfgInstanceId", RRF_RT_REG_SZ, NULL, guid_text, &chars) ==
                   ERROR_SUCCESS;
    reg_dword(driver, L"Characteristics", &characteristics);
    reg_dword(driver, L"*IfType", &if_type);
    RegCloseKey(driver);
    if (!guid_ok || FAILED(CLSIDFromString(guid_text, &a->identity.guid)))
        return FALSE;
    for (ULONG i = 0; i < table->NumEntries; ++i)
        if (!memcmp(&table->Table[i].InterfaceGuid, &a->identity.guid, sizeof(GUID))) {
            row = &table->Table[i];
            break;
        }
    /* NCF_PHYSICAL=4, NCF_VIRTUAL=1, NCF_SOFTWARE_ENUMERATED=2. */
    if (!(characteristics & 4) || (characteristics & 3))
        return FALSE;
    if (row) {
        if ((row->Type != IF_TYPE_ETHERNET_CSMACD && row->Type != IF_TYPE_IEEE80211) ||
            !row->InterfaceAndOperStatusFlags.HardwareInterface ||
            row->InterfaceAndOperStatusFlags.FilterInterface)
            return FALSE;
    } else if (if_type != IF_TYPE_ETHERNET_CSMACD && if_type != IF_TYPE_IEEE80211)
        return FALSE;
    a->physical_ethernet = (row ? row->Type : if_type) == IF_TYPE_ETHERNET_CSMACD;
    a->physical_wifi = !a->physical_ethernet;
    device_text(set, data, SPDRP_FRIENDLYNAME, a->name, NP_NAME_LEN);
    if (!a->name[0])
        device_text(set, data, SPDRP_DEVICEDESC, a->name, NP_NAME_LEN);
    if (!a->name[0])
        wcscpy_s(a->name, NP_NAME_LEN, L"物理以太网适配器");
    if (row)
        wcsncpy_s(a->alias, NP_NAME_LEN, row->Alias, _TRUNCATE);
    a->intel_i219 =
        !_wcsnicmp(a->identity.instance, L"PCI\\VEN_8086&", 13) && wcsstr(a->name, L"I219-V") != NULL;
    a->intel_ax203 = a->physical_wifi && !_wcsnicmp(a->identity.instance, L"PCI\\VEN_8086&", 13) &&
                     wcsstr(a->name, L"AX203") != NULL;
    CONFIGRET cr = CM_Get_DevNode_Status(&status, &problem, data->DevInst, 0);
    a->device_status = status;
    a->problem = problem;
    if (cr != CR_SUCCESS) {
        a->state = NP_UNKNOWN;
        return TRUE;
    }
    if ((status & DN_HAS_PROBLEM) && problem == CM_PROB_DISABLED)
        a->state = NP_DISABLED;
    else if ((status & DN_HAS_PROBLEM) || !(status & DN_STARTED))
        a->state = NP_DEVICE_ERROR;
    else if (row && row->AdminStatus != NET_IF_ADMIN_STATUS_UP)
        a->state = NP_INTERFACE_DISABLED;
    else if (!row || row->MediaConnectState == MediaConnectStateUnknown)
        a->state = NP_ENABLED_LINK_UNKNOWN;
    else if (row->MediaConnectState == MediaConnectStateConnected)
        a->state = NP_ENABLED_CONNECTED;
    else
        a->state = NP_ENABLED_DISCONNECTED;
    return TRUE;
}
static BOOL win_scan(void *ctx, NP_SNAPSHOT *out, NP_ERROR *error) {
    HDEVINFO set;
    MIB_IF_TABLE2 *table = NULL;
    DWORD code;
    (void)ctx;
    memset(out, 0, sizeof(*out));
    code = GetIfTable2(&table);
    if (code) {
        np_error(error, code, 0, L"GetIfTable2");
        return FALSE;
    }
    unsigned wifi_count = 0, wifi_up = 0, wifi_connected = 0;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2 *row = &table->Table[i];
        if (row->Type == IF_TYPE_IEEE80211 && row->InterfaceAndOperStatusFlags.HardwareInterface) {
            ++wifi_count;
            if (row->AdminStatus == NET_IF_ADMIN_STATUS_UP)
                ++wifi_up;
            if (row->OperStatus == IfOperStatusUp && row->MediaConnectState == MediaConnectStateConnected)
                ++wifi_connected;
        }
    }
    if (wifi_connected)
        wcscpy_s(out->wifi, _countof(out->wifi), L"Wi-Fi：已启用 · 链路已连接（只读）");
    else if (wifi_up)
        wcscpy_s(out->wifi, _countof(out->wifi), L"Wi-Fi：已启用 · 未连接（只读）");
    else if (wifi_count)
        wcscpy_s(out->wifi, _countof(out->wifi), L"Wi-Fi：接口未启用（只读）");
    else
        wcscpy_s(out->wifi, _countof(out->wifi), L"Wi-Fi：未发现活动接口，可能已禁用或不可用（只读）");
    /* DIGCF_PRESENT includes disabled PnP devices; never enumerate just active IP interfaces. */
    set = SetupDiGetClassDevsW(&net_class, NULL, NULL, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        code = GetLastError();
        FreeMibTable(table);
        np_error(error, code, 0, L"SetupDiGetClassDevs");
        return FALSE;
    }
    BOOL ok = TRUE;
    for (DWORD i = 0;; ++i) {
        SP_DEVINFO_DATA data = {0};
        NP_ADAPTER adapter;
        data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInfo(set, i, &data)) {
            code = GetLastError();
            if (code != ERROR_NO_MORE_ITEMS) {
                ok = FALSE;
                np_error(error, code, 0, L"SetupDiEnumDeviceInfo");
            }
            break;
        }
        if (device_read(set, &data, table, &adapter)) {
            if (out->count == NP_MAX_ADAPTERS) {
                ok = FALSE;
                np_error(error, ERROR_MORE_DATA, 0, L"物理网卡数量超出上限");
                break;
            }
            out->adapters[out->count++] = adapter;
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    FreeMibTable(table);
    ULONG bytes = 16384;
    IP_ADAPTER_ADDRESSES *ips = malloc(bytes);
    ULONG ipcode =
        ips ? GetAdaptersAddresses(AF_INET,
                                   GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                   NULL, ips, &bytes)
            : ERROR_NOT_ENOUGH_MEMORY;
    if (ipcode == ERROR_BUFFER_OVERFLOW) {
        free(ips);
        ips = malloc(bytes);
        ipcode = ips ? GetAdaptersAddresses(AF_INET,
                                            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                                GAA_FLAG_SKIP_DNS_SERVER,
                                            NULL, ips, &bytes)
                     : ERROR_NOT_ENOUGH_MEMORY;
    }
    HANDLE wlan = NULL;
    DWORD negotiated = 0;
    DWORD wlan_error = WlanOpenHandle(2, NULL, &negotiated, &wlan);
    for (size_t i = 0; i < out->count; ++i) {
        NP_ADAPTER *a = &out->adapters[i];
        if (ipcode == NO_ERROR)
            for (IP_ADAPTER_ADDRESSES *p = ips; p; p = p->Next) {
                GUID g;
                if (ConvertInterfaceLuidToGuid(&p->Luid, &g) == NO_ERROR &&
                    !memcmp(&g, &a->identity.guid, sizeof(g)) && p->FirstUnicastAddress) {
                    SOCKADDR *sa = p->FirstUnicastAddress->Address.lpSockaddr;
                    if (sa->sa_family == AF_INET)
                        InetNtopW(AF_INET, &((SOCKADDR_IN *)sa)->sin_addr, a->ip, _countof(a->ip));
                    break;
                }
            }
        if (a->physical_wifi) {
            a->wifi_detail_error = wlan_error;
            if (wlan) {
                WLAN_CONNECTION_ATTRIBUTES *attr = NULL;
                DWORD n = 0;
                WLAN_OPCODE_VALUE_TYPE type;
                DWORD err = WlanQueryInterface(wlan, &a->identity.guid, wlan_intf_opcode_current_connection,
                                               NULL, &n, (void **)&attr, &type);
                a->wifi_detail_error = err;
                if (!err && attr && n >= sizeof(*attr)) {
                    int count =
                        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                            (const char *)attr->wlanAssociationAttributes.dot11Ssid.ucSSID,
                                            (int)attr->wlanAssociationAttributes.dot11Ssid.uSSIDLength,
                                            a->ssid, _countof(a->ssid) - 1);
                    if (count > 0)
                        a->ssid[count] = 0;
                    else
                        wcscpy_s(a->ssid, _countof(a->ssid), L"非 UTF-8 SSID");
                }
                if (attr)
                    WlanFreeMemory(attr);
            }
        }
    }
    if (wlan)
        WlanCloseHandle(wlan, NULL);
    free(ips);
    return ok;
}
static BOOL win_local(void *ctx) {
    LPWSTR protocol = NULL;
    DWORD bytes = 0;
    PWTS_SESSION_INFOW sessions = NULL;
    DWORD count = 0;
    (void)ctx;
    if (GetSystemMetrics(SM_REMOTESESSION))
        return FALSE;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSClientProtocolType,
                                     &protocol, &bytes))
        return FALSE;
    BOOL local = bytes >= sizeof(USHORT) && *(USHORT *)protocol == 0;
    WTSFreeMemory(protocol);
    if (!local || !WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count))
        return FALSE;
    for (DWORD i = 0; i < count && local; ++i)
        if (sessions[i].State == WTSActive) {
            protocol = NULL;
            bytes = 0;
            if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessions[i].SessionId,
                                             WTSClientProtocolType, &protocol, &bytes))
                local = FALSE;
            else {
                if (bytes < sizeof(USHORT) || *(USHORT *)protocol != 0)
                    local = FALSE;
                WTSFreeMemory(protocol);
            }
        }
    WTSFreeMemory(sessions);
    return local;
}
static BOOL win_set(void *ctx, const NP_BINDING *binding, BOOL enable, NP_ERROR *error) {
    (void)ctx;
#ifdef NP_READONLY_BUILD
    (void)binding;
    (void)enable;
    np_error(error, ERROR_ACCESS_DENIED, 0, L"测试构建已编译移除真实网卡修改入口");
    return FALSE;
#else
    HDEVINFO set;
    SP_DEVINFO_DATA data = {0};
    MIB_IF_TABLE2 *table = NULL;
    NP_ADAPTER a;
    DWORD code;
    if (!enable && !win_local(NULL)) {
        np_error(error, ERROR_ACCESS_DENIED, 0, L"远程桌面安全检查");
        return FALSE;
    }
    if (!valid_binding(binding)) {
        np_error(error, ERROR_INVALID_DATA, 0, L"绑定身份无效");
        return FALSE;
    }
    set = SetupDiCreateDeviceInfoList(&net_class, NULL);
    if (set == INVALID_HANDLE_VALUE) {
        np_error(error, GetLastError(), 0, L"SetupDiCreateDeviceInfoList");
        return FALSE;
    }
    data.cbSize = sizeof(data);
    BOOL ok = FALSE;
    if (!SetupDiOpenDeviceInfoW(set, binding->instance, NULL, 0, &data))
        np_error(error, GetLastError(), 0, L"按设备实例重新打开目标");
    else if ((code = GetIfTable2(&table)) != NO_ERROR)
        np_error(error, code, 0, L"修改前重新读取接口");
    else if (!device_read(set, &data, table, &a) || !np_identity_equal(&a.identity, binding) ||
             (!a.physical_ethernet && !a.physical_wifi))
        np_error(error, ERROR_NOT_FOUND, 0, L"目标身份或物理以太网类型已改变");
    else {
        CONFIGRET cr = enable ? CM_Enable_DevNode(data.DevInst, 0)
                              : CM_Disable_DevNode(data.DevInst, CM_DISABLE_PERSIST | CM_DISABLE_UI_NOT_OK);
        if (cr == CR_SUCCESS)
            ok = TRUE;
        else
            np_error(error, CM_MapCrToWin32Err(cr, ERROR_GEN_FAILURE), cr,
                     enable ? L"CM_Enable_DevNode" : L"CM_Disable_DevNode");
    }
    if (table)
        FreeMibTable(table);
    SetupDiDestroyDeviceInfoList(set);
    return ok;
#endif
}
static ULONGLONG win_now(void *ctx) {
    (void)ctx;
    return GetTickCount64();
}
static void win_pause(void *ctx, DWORD ms) {
    (void)ctx;
    Sleep(ms);
}
NP_BACKEND np_windows_backend(void) {
    NP_BACKEND b = {NULL, win_scan, win_set, win_local, win_now, win_pause};
    return b;
}
