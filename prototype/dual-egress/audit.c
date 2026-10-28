#include "dual.h"
#include <shlobj.h>
#include <stdio.h>
static void jstr(NP_JSON *j,int obj,const char *key,WCHAR *out,size_t cap){if(!np_json_string(j,np_json_field(j,obj,key),out,cap))out[0]=0;}
BOOL dp_audit(DP_STATE *s){
    ZeroMemory(s,sizeof(*s));PWSTR appdata=NULL;
    if(FAILED(SHGetKnownFolderPath(&FOLDERID_RoamingAppData,0,NULL,&appdata)))return FALSE;
    swprintf_s(s->runtime,NP_PATH,L"%s\\io.github.clash-verge-rev.clash-verge-rev\\clash-verge.yaml",appdata);CoTaskMemFree(appdata);
    BOOL config=dp_read_config(s->runtime,s);
    NP_BACKEND backend=np_windows_backend();NP_ERROR error={0};
    if(!backend.scan(NULL,&s->adapters,&error))s->scan_error=error.win32?error.win32:ERROR_GEN_FAILURE;
    np_monitor_scan(&s->sockets,&s->adapters);
    const WCHAR *paths[]={L"/version",L"/configs",L"/connections"};
    for(int k=0;k<3;++k){
        char *text=NULL;DWORD bytes=0,err=0;NP_JSON j;
        if(!dp_get(paths[k],&text,&bytes,&s->core_pid,&err)){s->api_error=err;continue;}
        if(!np_json_parse(&j,text,bytes)){free(text);s->api_error=ERROR_INVALID_DATA;continue;}
        if(k==0)jstr(&j,0,"version",s->version,80);
        if(k==1){
            s->live_config=TRUE;
            jstr(&j,0,"mode",s->mode,40);jstr(&j,0,"find-process-mode",s->process_mode,40);
            int t=np_json_field(&j,0,"tun"),enable=t>=0?np_json_field(&j,t,"enable"):-1;
            if(enable>=0)s->tun=j.tokens[enable].type=='t';
        }
        if(k==2){
            int list=np_json_field(&j,0,"connections");
            if(list>=0 && j.tokens[list].type=='[')for(int i=list+1;i<j.tokens[list].next;i=j.tokens[i].next){
                if(s->flow_count>=DP_FLOWS){s->truncated=TRUE;break;}
                DP_FLOW *f=&s->flows[s->flow_count++];int m=np_json_field(&j,i,"metadata");
                if(m>=0){jstr(&j,m,"process",f->app,128);jstr(&j,m,"processPath",f->path,NP_PATH);jstr(&j,m,"network",f->protocol,24);jstr(&j,m,"sourceIP",f->inbound_source,100);}
                jstr(&j,i,"rule",f->rule,160);
                int chains=np_json_field(&j,i,"chains");
                if(chains>=0 && j.tokens[chains].type=='[' && j.tokens[chains].count)np_json_string(&j,chains+1,f->node,256);
                wcscpy_s(f->expected,100,L"未启用 NetPilot 策略");
                wcscpy_s(f->actual,180,L"未验证");
                wcscpy_s(f->confidence,160,L"Mihomo 入站元数据，不是物理出口证据");
            }
        }
        np_json_free(&j);SecureZeroMemory(text,bytes);free(text);
    }
    int eth=np_auto_select(&s->adapters),wifi=np_auto_wifi(&s->adapters);
    unsigned core_sockets=0,v4=0,v6=0,udp=0,physical=0;
    for(size_t i=0;i<s->sockets.count;++i){NP_CONNECTION *c=&s->sockets.rows[i];if(!s->core_pid||c->pid!=s->core_pid)continue;++core_sockets;v4+=c->local.si_family==AF_INET;v6+=c->local.si_family==AF_INET6;udp+=c->udp;physical+=c->source_known && c->physical && !c->local_only;}
    swprintf_s(s->summary,4096,
        L"双网智能分流 · 只读可行性原型（未应用，热点故障保护未验证）\r\n"
        L"Ethernet：%s · %s\r\nWi-Fi：%s · %s\r\nMihomo：%s · mode=%s · TUN=%s · process=%s\r\n"
        L"运行时配置：%s\r\n节点=%zu，策略组=%zu，Provider=%zu，规则=%zu；DNS=%s\r\n"
        L"连接=%zu；核心 sockets=%u（IPv4 %u / IPv6 %u / UDP %u），物理源地址归属=%u\r\n"
        L"API错误=%lu，网卡扫描错误=%lu；截断=%s\r\n"
        L"物理源地址匹配仅是线索，不能证明单个代理请求实际出站。UDP远端、DNS、复用/隧道及热点中断均未验证。\r\n"
        L"Clash Verge 界面必须为 Rule 模式；脚本中的 mode 可能被界面覆盖。当前未进行更改。\r\n"
        L"仅为明确选择的应用生成规则；未选择的游戏/下载仍遵循原规则，不保证热点零流量。",
        eth>=0?s->adapters.adapters[eth].alias:L"未唯一识别",eth>=0?np_state_text(s->adapters.adapters[eth].state):L"未知",
        wifi>=0?s->adapters.adapters[wifi].alias:L"未唯一识别",wifi>=0?np_state_text(s->adapters.adapters[wifi].state):L"未知",
        s->version[0]?s->version:L"未读取",s->live_config?s->mode:L"实时未知（仅已读文件）",!s->live_config?L"实时未知":s->tun?L"已启用":L"未启用",s->live_config&&s->process_mode[0]?s->process_mode:L"实时未报告",
        config?L"已解析最终 clash-verge.yaml":L"读取失败",s->node_count,s->group_count,s->provider_count,s->rule_count,s->dns_mode,
        s->flow_count,core_sockets,v4,v6,udp,physical,s->api_error,s->scan_error,s->truncated?L"是":L"否");
    return config;
}
static BOOL utf8file(const WCHAR *path,const WCHAR *text){
    int n=WideCharToMultiByte(CP_UTF8,0,text,-1,NULL,0,NULL,NULL);char *s=malloc(n);
    if(!s)return FALSE;WideCharToMultiByte(CP_UTF8,0,text,-1,s,n,NULL,NULL);
    BOOL ok=np_write_exact(path,s,(DWORD)n-1);free(s);return ok;
}
BOOL dp_write_report(const WCHAR *path,const DP_STATE *s){return utf8file(path,s->summary);}
