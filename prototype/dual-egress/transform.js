/* NetPilot Lite dual-egress prototype. Runs in Clash Verge's Script engine, never edits subscription files.
 * Export contains the policy only, no credentials. This is a PREVIEW until matching-core and egress tests pass.
 * Call nplTransform(config, policy); append to an existing script only after reviewing its order.
 * Verge 2.5.2 restores its GUI mode AFTER scripts. Rule mode must be selected there and verified
 * through GET /configs. Script exceptions can be skipped by Verge: this script is NOT a kill switch.
 */
function nplTransform(input, policy) {
  const p = policy;
  const prefix = 'NPL-Dual-62d910 / ';
  const campus = prefix + 'Campus Direct', hotspot = prefix + 'Hotspot Proxy';
  const allowed = ['vmess','vless','ss','ssr','trojan','socks5','http','hysteria','hysteria2','tuic'];
  function text(v) { return typeof v === 'string' && v.length > 0 && v.length < 1024 && !/[\r\n\0,]/.test(v); }
  if (!p || p.schema !== 1 || !text(p.ethernet) || !text(p.wifi) || p.ethernet === p.wifi ||
      !Array.isArray(p.nodes) || !Array.isArray(p.apps) || p.nodes.length > 64 || p.apps.length > 32)
    throw new Error('NetPilot: invalid policy');
  if (!p.ethernetGuid || !p.wifiGuid || p.ethernetGuid === p.wifiGuid)
    throw new Error('NetPilot: stable adapter identities missing');
  // Full clone: subscription credentials and protocol fields stay byte-for-byte equal as values.
  const c = JSON.parse(JSON.stringify(input));
  if (!Array.isArray(c.proxies) || !Array.isArray(c.rules)) throw new Error('NetPilot: unsupported profile shape');
  if (c['interface-name']) throw new Error('NetPilot: review existing global interface binding first');
  const proxies = c.proxies, groups = c['proxy-groups'] || [];
  if (!Array.isArray(groups)) throw new Error('NetPilot: malformed groups');
  if (proxies.concat(groups).some(x => typeof x.name === 'string' && x.name.indexOf(prefix) === 0) ||
      c.rules.some(r => typeof r !== 'string' || r.indexOf(prefix) !== -1))
    throw new Error('NetPilot: reserved namespace already present; regenerate from the subscription source');
  const lookup = Object.create(null);
  proxies.forEach(x => { if (!text(x.name) || lookup[x.name]) throw new Error('NetPilot: duplicate or invalid node'); lookup[x.name] = x; });
  const clones=[], members=[], names=Object.create(null);
  p.nodes.forEach(name => {
    if (!text(name) || names[name]) throw new Error('NetPilot: invalid/duplicate node selection');
    names[name]=true;
    const node=lookup[name];
    // If a selected node disappears on subscription refresh, keep the protected group REJECT-only.
    // Unknown chained/protocol/MPTCP nodes are omitted, never silently replaced by an unbound original.
    if (!node || allowed.indexOf(node.type) < 0 || node['dialer-proxy'] || node.mptcp === true) return;
    const copy=JSON.parse(JSON.stringify(node));
    copy.name=prefix+'Wi-Fi / '+name;
    copy['interface-name']=p.wifi;
    clones.push(copy); members.push(copy.name);
  });
  const rules=[], seen=Object.create(null);
  p.apps.forEach(a => {
    if (!a || ['PROCESS-PATH','PROCESS-NAME'].indexOf(a.match) < 0 || !text(a.value) ||
        ['ethernet','wifi','system'].indexOf(a.target)<0) throw new Error('NetPilot: invalid process rule');
    if (a.match === 'PROCESS-PATH' && (!/^[A-Za-z]:\\/.test(a.value) || a.value.indexOf('\\..\\')>=0))
      throw new Error('NetPilot: PROCESS-PATH must be a canonical local path');
    if (a.match === 'PROCESS-NAME' && /[\\/]/.test(a.value)) throw new Error('NetPilot: PROCESS-NAME is a filename only');
    const key=a.match+':'+a.value.toLowerCase();
    if (seen[key]) throw new Error('NetPilot: conflicting duplicate process rule');
    seen[key]=true;
    if (a.target!=='system') rules.push(a.match+','+a.value+','+(a.target==='wifi'?hotspot:campus));
  });
  if (!rules.length) throw new Error('NetPilot: choose at least one explicit process rule');
  c.mode='rule'; c['find-process-mode']='always';
  c.proxies=[{name:campus,type:'direct',udp:true,'ip-version':'dual','interface-name':p.ethernet}].concat(clones,proxies);
  c['proxy-groups']=[{name:hotspot,type:'select',proxies:members.concat(['REJECT'])}].concat(groups);
  c.rules=rules.concat(c.rules);
  // DNS/TUN/provider declarations and original MATCH remain unchanged; coverage limitations must be shown.
  return c;
}
if (typeof module !== 'undefined') module.exports={nplTransform};
