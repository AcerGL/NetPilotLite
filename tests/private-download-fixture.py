"""Create a higher-version, non-network mock image package for update protocol tests."""
import pathlib,subprocess,struct,json,hashlib,zipfile,sys
root=pathlib.Path(__file__).resolve().parents[1]
key=sys.argv[1] if sys.argv[1]=='ENV' else str(pathlib.Path(sys.argv[1]).resolve())
src=root/'artifacts/update-fixtures/fail'
out=root/'artifacts/private-download-fixture';out.mkdir(exist_ok=True)
signature=(src/'NetPilotLite.exe.update').read_bytes()
v=struct.unpack_from('<4I',signature,8);version='.'.join(map(str,v[:3]));tag='v'+version
archive=out/'NetPilotLite-Update-x64.zip'
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_STORED) as z:
 for name in ('NetPilotLite.exe','NetPilotLite.exe.update'):
  i=zipfile.ZipInfo(name,(2026,1,1,0,0,0));i.create_system=0;i.external_attr=0x20;z.writestr(i,(src/name).read_bytes())
manifest=out/'NetPilotLite-Update-x64.json'
manifest.write_text(json.dumps(dict(version=version,architecture='x64',package_name=archive.name,package_size=archive.stat().st_size,
 sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),release_tag=tag,release_notes='Mock image, never production.'))+'\n',encoding='utf-8')
subprocess.run([str(root/'bin/Release/ReleaseTool.exe'),'sign-catalog',key,str(manifest)],cwd=root,check=True)
names=[archive.name,manifest.name,manifest.name+'.update']
release=dict(tag_name=tag,name='Mock release',body='Mocked authenticated download',published_at='2026-11-02T00:00:00Z',draft=False,prerelease=False,
 assets=[dict(id=i+1,name=n,size=(out/n).stat().st_size,browser_download_url=f'https://github.com/AcerGL/NetPilotLite/releases/download/{tag}/{n}') for i,n in enumerate(names)])
(out/'release.json').write_text(json.dumps(release),encoding='utf-8')
print('Created signed inert mock download package:',out)
