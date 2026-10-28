#include "dual.h"
#include <stdio.h>
#include <stdarg.h>
#include <objbase.h>
static BOOL quote(char *buf,size_t cap,size_t *used,const WCHAR *text){
    char utf8[NP_PATH*4];int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,utf8,sizeof(utf8),NULL,NULL);
    if(!n || *used+2>=cap)return FALSE;buf[(*used)++]='"';
    for(int i=0;i<n-1;++i){unsigned char c=(unsigned char)utf8[i];
        if(*used+7>=cap)return FALSE;
        if(c=='"'||c=='\\'){buf[(*used)++]='\\';buf[(*used)++]=c;}
        else if(c<32){*used+=(size_t)sprintf_s(buf+*used,cap-*used,"\\u%04x",c);}
        else buf[(*used)++]=c;
    }
    buf[(*used)++]='"';buf[*used]=0;return TRUE;
}
static BOOL append(char *buf,size_t cap,size_t *n,const char *s){size_t m=strlen(s);if(*n+m>=cap)return FALSE;memcpy(buf+*n,s,m+1);*n+=m;return TRUE;}
BOOL dp_export(const WCHAR *path,const DP_STATE *s,size_t selected,const DP_APP *apps,size_t count,NP_ERROR *e){
    int ei=np_auto_select(&s->adapters),wi=np_auto_wifi(&s->adapters);BOOL active=FALSE;
    if(!s->config_valid || ei<0 || wi<0 || selected>=s->node_count || !s->nodes[selected].eligible || s->global_interface[0] || !count || count>DP_APPS){np_error(e,ERROR_INVALID_DATA,0,L"需要有效配置、唯一物理网卡、受支持节点和明确应用");return FALSE;}
    const NP_ADAPTER *eth=&s->adapters.adapters[ei],*wifi=&s->adapters.adapters[wi];
    if(!eth->alias[0] || !wifi->alias[0]){np_error(e,ERROR_NOT_FOUND,0,L"缺少网卡真实别名；不能以硬件描述猜测 interface-name");return FALSE;}
    char *buf=calloc(1,128*1024);size_t n=0;BOOL ok=buf!=NULL;WCHAR eg[40],wg[40];
    StringFromGUID2(&eth->identity.guid,eg,40);StringFromGUID2(&wifi->identity.guid,wg,40);
#define ADD(t) (ok=ok && append(buf,128*1024,&n,t))
#define Q(t) (ok=ok && quote(buf,128*1024,&n,t))
    ADD("// PREVIEW ONLY. No runtime configuration has been changed. See DUAL_EGRESS.md before manual use.\nconst NPL_POLICY = {\"schema\":1,\"ethernet\":");Q(eth->alias);
    ADD(",\"wifi\":");Q(wifi->alias);ADD(",\"ethernetGuid\":");Q(eg);ADD(",\"wifiGuid\":");Q(wg);ADD(",\"nodes\":[");Q(s->nodes[selected].name);ADD("],\"apps\":[");
    for(size_t i=0;i<count;++i){
        if(!apps[i].path[0] || wcschr(apps[i].path,L',') || wcschr(apps[i].path,L'\r') || wcschr(apps[i].path,L'\n') || apps[i].target<0 || apps[i].target>2){ok=FALSE;break;}
        if(i)ADD(",");ADD("{\"match\":\"PROCESS-PATH\",\"value\":");Q(apps[i].path);ADD(",\"target\":");Q(apps[i].target==0?L"ethernet":apps[i].target==1?L"wifi":L"system");ADD("}");
        active|=apps[i].target!=2;
    }
    ADD("]};\n");
    HRSRC resource=FindResourceW(NULL,MAKEINTRESOURCEW(301),RT_RCDATA);HGLOBAL memory=resource?LoadResource(NULL,resource):NULL;
    DWORD bytes=resource?SizeofResource(NULL,resource):0;const char *js=memory?LockResource(memory):NULL;
    if(!js || !bytes || n+bytes+150>=128*1024)ok=FALSE;
    if(ok){memcpy(buf+n,js,bytes);n+=bytes;buf[n]=0;ADD("\nfunction main(config, profileName) { return nplTransform(config, NPL_POLICY); }\n");}
    ok=ok && active && np_write_exact(path,buf,(DWORD)n);
    if(!ok)np_error(e,GetLastError()?GetLastError():ERROR_INVALID_DATA,0,L"导出失败；目标必须是新文件且至少有一条非默认规则");
    free(buf);return ok;
#undef ADD
#undef Q
}
