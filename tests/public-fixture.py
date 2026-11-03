"""Signed file-by-file fixtures; no archive and no real adapter operations."""
import pathlib,json,subprocess,shutil,hashlib,sys,uuid
root=pathlib.Path(__file__).resolve().parents[1];key=sys.argv[1];key=key if key=='ENV' else str(pathlib.Path(key).resolve())
out=root/'artifacts'/('public-fixture-'+str(uuid.uuid4()));out.mkdir()
tool=root/'bin/Release/ReleaseTool.exe'
def sign(action,p): subprocess.run([str(tool),action,key,str(p)],check=True,cwd=root)
for kind in ['stage','bad']:
 d=out/kind;d.mkdir();src=root/'artifacts/update-fixtures'/('new' if kind=='stage' else 'setup-fail')
 for name in ['NetPilotLite.exe','NetPilotLite.exe.update']:shutil.copy2(src/name,d/name)
 (d/'resources/nested').mkdir(parents=True);(d/'resources/nested/fixture.dat').write_bytes(b'signed nested dependency fixture')
 files=[]
 for name,path in [('NetPilotLite.exe','NetPilotLite.exe'),('NetPilotLite.exe.update','NetPilotLite.exe.update'),('nested.dat','resources/nested/fixture.dat')]:
  f=d/path;files.append(dict(asset_name=name,install_path=path,size=f.stat().st_size,sha256=hashlib.sha256(f.read_bytes()).hexdigest()))
 (d/'manifest.json').write_text(json.dumps(dict(version='1.5.0',architecture='x64',files=files)),encoding='utf-8');sign('sign-catalog',d/'manifest.json')
 for suffix in ['', '.update']:shutil.copy2(root/('bin/Release/NetPilotLite-Setup-x64.exe'+suffix),d/('RecoverySetup.exe'+suffix))
shutil.copytree(root/'artifacts/update-fixtures/prior',out/'prior')
prior=out/'prior';meta=prior/'.NetPilot-install';meta.mkdir()
files=[dict(asset_name=n,install_path=n,size=(prior/n).stat().st_size,sha256=hashlib.sha256((prior/n).read_bytes()).hexdigest()) for n in ['NetPilotLite.exe','NetPilotLite.exe.update']]
(meta/'manifest.json').write_text(json.dumps(dict(version='1.0.1',architecture='x64',files=files)),encoding='utf-8');sign('sign-catalog',meta/'manifest.json')
assets=out/'assets';shutil.copytree(root/'dist/public',assets)
names=['NetPilotLite.exe','NetPilotLite.exe.update','NetPilotLite-Setup-x64.exe','NetPilotLite-Setup-x64.exe.update','manifest.json','manifest.json.update']
release=dict(tag_name='v1.5.0',name='Public test release',body='Signed file by file',published_at='2026-11-02T00:00:00Z',draft=False,prerelease=False,assets=[dict(name=n,size=(assets/n).stat().st_size,browser_download_url='https://github.com/AcerGL/NetPilotLite/releases/download/v1.5.0/'+n) for n in names])
(assets/'release.json').write_text(json.dumps(release),encoding='utf-8')
(root/'artifacts/public-setup/fixture-path.txt').write_text(str(out),encoding='utf-8');print(out)
