#include "dual.h"
#include "yaml.h"
#include <stdio.h>
#include <shlobj.h>
static BOOL unique_map(yaml_document_t *d,yaml_node_t *map){
    if(!map||map->type!=YAML_MAPPING_NODE||map->data.mapping.pairs.top-map->data.mapping.pairs.start>512)return FALSE;
    for(yaml_node_pair_t *p=map->data.mapping.pairs.start;p<map->data.mapping.pairs.top;++p){
        yaml_node_t *k=yaml_document_get_node(d,p->key);if(!k||k->type!=YAML_SCALAR_NODE)return FALSE;
        for(yaml_node_pair_t *q=map->data.mapping.pairs.start;q<p;++q){yaml_node_t *other=yaml_document_get_node(d,q->key);if(!strcmp((char*)k->data.scalar.value,(char*)other->data.scalar.value))return FALSE;}
    }return TRUE;
}
static yaml_node_t *field(yaml_document_t *d,yaml_node_t *map,const char *key) {
    if(!map || map->type!=YAML_MAPPING_NODE)return NULL;
    yaml_node_t *found=NULL;
    for(yaml_node_pair_t *p=map->data.mapping.pairs.start;p<map->data.mapping.pairs.top;++p){
        yaml_node_t *k=yaml_document_get_node(d,p->key);
        if(k && k->type==YAML_SCALAR_NODE && !strcmp((char*)k->data.scalar.value,key)){
            if(found)return NULL; /* duplicate control key is not accepted */
            found=yaml_document_get_node(d,p->value);
        }
    }
    return found;
}
static const char *scalar(yaml_node_t *n){return n && n->type==YAML_SCALAR_NODE?(char*)n->data.scalar.value:"";}
static void wide(yaml_node_t *n,WCHAR *out,int cap){if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,scalar(n),-1,out,cap))out[0]=0;}
static size_t count(yaml_node_t *n){return !n?0:n->type==YAML_SEQUENCE_NODE?(size_t)(n->data.sequence.items.top-n->data.sequence.items.start):n->type==YAML_MAPPING_NODE?(size_t)(n->data.mapping.pairs.top-n->data.mapping.pairs.start):0;}
BOOL dp_read_config(const WCHAR *path,DP_STATE *s){
    s->config_valid=FALSE;s->node_count=0;
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(f==INVALID_HANDLE_VALUE)return FALSE;
    LARGE_INTEGER length;DWORD got=0;
    BOOL ok=GetFileSizeEx(f,&length) && length.QuadPart>0 && length.QuadPart<4*1024*1024;
    BYTE *raw=ok?malloc((size_t)length.QuadPart):NULL;
    ok=ok && raw && ReadFile(f,raw,(DWORD)length.QuadPart,&got,NULL) && got==length.QuadPart;CloseHandle(f);
    if(!ok){free(raw);return FALSE;}
    yaml_parser_t parser;yaml_document_t doc;
    if(!yaml_parser_initialize(&parser)){SecureZeroMemory(raw,got);free(raw);return FALSE;}
    yaml_parser_set_input_string(&parser,raw,got);
    ok=yaml_parser_load(&parser,&doc)!=0;
    yaml_parser_delete(&parser);SecureZeroMemory(raw,got);free(raw);
    if(!ok)return FALSE;
    yaml_node_t *root=yaml_document_get_root_node(&doc);
    ok=unique_map(&doc,root) && doc.nodes.top-doc.nodes.start<100000;
    if(ok){
        wide(field(&doc,root,"mode"),s->mode,40);
        wide(field(&doc,root,"find-process-mode"),s->process_mode,40);
        wide(field(&doc,root,"interface-name"),s->global_interface,128);
        s->tun=!strcmp(scalar(field(&doc,field(&doc,root,"tun"),"enable")),"true");
        wide(field(&doc,field(&doc,root,"dns"),"enhanced-mode"),s->dns_mode,40);
        s->group_count=count(field(&doc,root,"proxy-groups"));s->provider_count=count(field(&doc,root,"proxy-providers"));s->rule_count=count(field(&doc,root,"rules"));
        yaml_node_t *nodes=field(&doc,root,"proxies");
        if(nodes && nodes->type==YAML_SEQUENCE_NODE)for(int *i=nodes->data.sequence.items.start;i<nodes->data.sequence.items.top;++i){
            if(s->node_count>=DP_NODES){s->truncated=TRUE;break;}
            yaml_node_t *node=yaml_document_get_node(&doc,*i);DP_NODE *r=&s->nodes[s->node_count++];
            if(!unique_map(&doc,node)){ok=FALSE;break;}
            wide(field(&doc,node,"name"),r->name,256);wide(field(&doc,node,"type"),r->type,40);wide(field(&doc,node,"interface-name"),r->binding,128);
            r->chained=scalar(field(&doc,node,"dialer-proxy"))[0]!=0;
            r->udp=!strcmp(scalar(field(&doc,node,"udp")),"true");
            const WCHAR *types[]={L"vmess",L"vless",L"ss",L"ssr",L"trojan",L"socks5",L"http",L"hysteria",L"hysteria2",L"tuic"};
            for(int k=0;k<_countof(types);++k)if(!wcscmp(r->type,types[k]))r->eligible=TRUE;
            if(r->chained || !strcmp(scalar(field(&doc,node,"mptcp")),"true"))r->eligible=FALSE;
        }
    }
    /* Credentials are never copied into the audit model or log. Wipe scalar buffers before disposal. */
    for(yaml_node_t *n=doc.nodes.start;n<doc.nodes.top;++n)if(n->type==YAML_SCALAR_NODE)SecureZeroMemory(n->data.scalar.value,n->data.scalar.length);
    yaml_document_delete(&doc);s->config_valid=ok;return ok;
}
