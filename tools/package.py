"""Package saved build outputs; no system installation, network mutation or signing-key access."""
from pathlib import Path
import hashlib,json,sys,zipfile
root=Path(__file__).resolve().parents[1]
out=root.parent/'outputs'
label=sys.argv[1] if len(sys.argv)>1 else '1.1.0'
assert all(c.isalnum() or c in '.-_' for c in label)
out.mkdir(exist_ok=True)
version=json.loads((root/'dist'/'version.json').read_text(encoding='utf-8'))
assert version['sha256']==hashlib.sha256((root/'dist'/'NetPilotLite.exe').read_bytes()).hexdigest()
assert version['size']==(root/'dist'/'NetPilotLite.exe').stat().st_size
for line in (root/'dist'/'SHA256SUMS.txt').read_text(encoding='utf-8').splitlines():
    digest,name=line.split('  ',1)
    assert '/' not in name and '\\' not in name
    assert hashlib.sha256((root/'dist'/name).read_bytes()).hexdigest()==digest
def package(path, files):
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
        for p,name in files:
            assert p.suffix.lower() not in ('.dpapi','.pfx','.key')
            z.write(p,name)
docs=[(p,'docs/'+p.name) for p in (root/'docs').glob('*.md')]
evidence=[]
for name in ['v110-build.txt','v110-perf-before.txt','v110-perf-after.txt','v110-gui-smoke.txt',
             'v110-ui-acceptance.txt','v110-soak.txt','v110-update-tests.txt','v110-import-audit.txt',
             'v110-routing-tests.txt','v110-routing-probe-summary.txt',
             'v110-publish-final.txt','v110-perf-final.txt','v110-mock-final.txt',
             'v110-ui-acceptance-final.txt','v110-soak-final.txt','v110-gui-smoke-final.txt',
             'v110-routing-tests-final.txt','v110-routing-ui-final.txt','v110-update-tests-final.txt',
             'v110-release-verification.txt',
             'v110-settings.png','v110-settings-scrolled.png','v110-tray.png',
             'v110-routing-rules.png','v110-routing-monitor.png']:
    p=root/'artifacts'/name
    if p.exists(): evidence.append((p,'test-evidence/'+name))
evidence += [(p,'test-evidence/'+p.name) for p in (root/'artifacts').glob('v120-*') if p.suffix in ('.txt','.png') and 'network-' not in p.name]
release=[(root/'dist'/n,n) for n in ['NetPilotLite.exe','NetPilotLite.exe.update','NetPilotLite.Updater.exe','version.json','SHA256SUMS.txt']]
release += [(root/'README.md','README.md'),(root/'LICENSE','LICENSE'),(root/'assets'/'NOTICE.md','ASSET_NOTICE.md')]+docs+evidence
rel=out/f'NetPilotLite-{label}-win-x64.zip'; package(rel,release)
sources=[]
for p in root.iterdir():
    if p.is_file() and (p.suffix.lower() in ('.sln','.vcxproj','.props','.cmd','.md') or p.name in ('LICENSE','.gitignore','.clang-format')):
        sources.append((p,'NetPilotLite/'+p.name))
for name in ['src','assets','tools','tests','docs','.github']:
    for p in (root/name).rglob('*'):
        if p.is_file() and p.suffix.lower() not in ('.exe','.obj','.pdb','.dpapi','.pyc'):
            sources.append((p,'NetPilotLite/'+p.relative_to(root).as_posix()))
sources += [(p,'NetPilotLite/'+n) for p,n in evidence]
src=out/f'NetPilotLite-{label}-source.zip';package(src,sources)
checks=''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in [rel,src])
(out/f'NetPilotLite-{label}-archives-SHA256.txt').write_text(checks,encoding='utf-8')
print(checks)
