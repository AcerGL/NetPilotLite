# Development audit only. Emits allowlisted configuration metadata, never credentials/endpoints.
import sys,pathlib,json,collections,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[3]/'work/dual-audit-python'))
import yaml
base=pathlib.Path.home()/'AppData/Roaming/io.github.clash-verge-rev.clash-verge-rev'
report={}
for name in ['config.yaml','clash-verge.yaml','profiles.yaml','dns_config.yaml','verge.yaml']:
    raw=(base/name).read_bytes();d=yaml.safe_load(raw) or {}
    summary={k:d[k] for k in ('mode','find-process-mode','interface-name','ipv6','external-controller','external-controller-pipe') if k in d}
    summary['sha256']=hashlib.sha256(raw).hexdigest()
    if 'tun' in d:summary['tun']={k:v for k,v in d['tun'].items() if k in ('enable','stack','auto-route','auto-detect-interface','strict-route','dns-hijack','device')}
    if 'dns' in d:
        summary['dns']={k:v for k,v in d['dns'].items() if k in ('enable','enhanced-mode','ipv6','respect-rules','use-hosts','use-system-hosts')}
        summary['resolver_counts']={k:len(d['dns'].get(k,[]) or []) for k in ('nameserver','fallback','default-nameserver','proxy-server-nameserver','direct-nameserver')}
    if 'proxies' in d:
        proxies=d.get('proxies') or [];summary['nodes']=len(proxies);summary['protocols']=dict(collections.Counter(x.get('type') for x in proxies));summary['interface_bound']=sum(bool(x.get('interface-name')) for x in proxies);summary['chained']=sum(bool(x.get('dialer-proxy')) for x in proxies)
        summary['node_names']=[x.get('name') for x in proxies][:12]
        summary['groups']=[{'name':x.get('name'),'type':x.get('type'),'members':len(x.get('proxies',[])),'providers':len(x.get('use',[]))} for x in d.get('proxy-groups',[])]
        summary['providers']=[{'name':k,'type':v.get('type'),'interface':v.get('override',{}).get('interface-name')} for k,v in d.get('proxy-providers',{}).items()]
        summary['rule_types']=dict(collections.Counter(str(x).split(',')[0] for x in d.get('rules',[])));summary['rule_count']=len(d.get('rules',[]))
    if 'current' in d:
        item=next(x for x in d['items'] if x['uid']==d['current']);summary['active_profile']={k:item.get(k) for k in ('uid','type','file','option')};summary['subscription_url_present']=bool(item.get('url'))
    report[name]=summary
out=pathlib.Path(__file__).resolve().parents[2]/'artifacts/dual-egress/config-audit-redacted.json'
out.write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8')
print(json.dumps(report,ensure_ascii=False,indent=2))
