#ifndef NP_ROUTING_H
#define NP_ROUTING_H
#include "lite.h"
#include "update.h"
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#define NP_MAX_RULES 32
#define NP_MAX_CONNECTIONS 1024
typedef struct NP_RULE {
    WCHAR path[NP_PATH];
    int target;
    BOOL enabled;
    WCHAR label[256],family[128],aumid[256];
} NP_RULE;
typedef struct NP_SOURCE {
    SOCKADDR_INET address;
    GUID guid;
    NET_LUID luid;
    WCHAR name[128];
    BOOL physical;
} NP_SOURCE;
typedef struct NP_CONNECTION {
    DWORD pid, state, process_error, route_error;
    BOOL udp, source_known, physical, local_only, route_virtual;
    SOCKADDR_INET local, remote;
    GUID source_guid;
    WCHAR exe[NP_PATH], app[128], protocol[32], local_text[100], remote_text[100];
    WCHAR source[160], route[160], note[200];
} NP_CONNECTION;
typedef struct NP_MONITOR {
    size_t count;
    BOOL truncated;
    DWORD errors[4], source_error;
    SYSTEMTIME time;
    NP_CONNECTION rows[NP_MAX_CONNECTIONS];
} NP_MONITOR;
typedef enum NP_RULE_RESULT {
    NP_RULE_OFF,
    NP_RULE_DEFAULT,
    NP_RULE_UNKNOWN,
    NP_RULE_UNAVAILABLE,
    NP_RULE_SOURCE_MATCH,
    NP_RULE_SOURCE_MISMATCH
} NP_RULE_RESULT;
BOOL np_monitor_scan(NP_MONITOR *, const NP_SNAPSHOT *);
void np_monitor_mock(NP_MONITOR *, const NP_SNAPSHOT *);
void np_map_source(NP_CONNECTION *, const NP_SOURCE *, size_t);
NP_RULE_RESULT np_rule_evaluate(const NP_RULE *, const NP_CONNECTION *, const GUID *, BOOL);
const WCHAR *np_rule_result_text(NP_RULE_RESULT);
#endif
