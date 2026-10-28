#ifndef NP_DUAL_H
#define NP_DUAL_H
#ifndef NP_READONLY_BUILD
#error The dual-egress prototype must compile with NP_READONLY_BUILD.
#endif
#include "routing.h"
#include "json.h"
#define DP_NODES 256
#define DP_FLOWS 256
#define DP_APPS 32
typedef struct DP_NODE { WCHAR name[256],type[40],binding[128]; BOOL eligible,udp,chained; } DP_NODE;
typedef struct DP_FLOW { WCHAR app[128],path[NP_PATH],protocol[24],rule[160],node[256],inbound_source[100],expected[100],actual[180],confidence[160]; } DP_FLOW;
typedef struct DP_APP { WCHAR path[NP_PATH]; int target; } DP_APP;
typedef struct DP_STATE {
    NP_SNAPSHOT adapters;
    NP_MONITOR sockets;
    DP_NODE nodes[DP_NODES]; size_t node_count,group_count,provider_count,rule_count;
    DP_FLOW flows[DP_FLOWS]; size_t flow_count;
    WCHAR runtime[NP_PATH],version[80],mode[40],process_mode[40],dns_mode[40],global_interface[128];
    DWORD api_error,scan_error,core_pid;
    BOOL tun,truncated,config_valid,live_config;
    WCHAR summary[4096];
} DP_STATE;
BOOL dp_read_config(const WCHAR *,DP_STATE *);
BOOL dp_get(const WCHAR *, char **,DWORD *,DWORD *,DWORD *);
BOOL dp_http_decode(const char *,size_t,char **,DWORD *);
BOOL dp_audit(DP_STATE *);
BOOL dp_export(const WCHAR *,const DP_STATE *,size_t,const DP_APP *,size_t,NP_ERROR *);
BOOL dp_write_report(const WCHAR *,const DP_STATE *);
int dp_tests(void);
#endif
