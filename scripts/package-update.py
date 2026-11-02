"""Build the narrow stored-ZIP update format. Never includes user data or installers."""
import argparse, hashlib, json, pathlib, re, shutil, subprocess, zipfile
p = argparse.ArgumentParser()
p.add_argument('--key', required=True, help='DPAPI private-key file, or ENV for GitHub signing secret')
p.add_argument('--output', default='dist/update')
p.add_argument('--tag', required=True)
p.add_argument('--notes', default='Private GitHub updates, per-boot checks and in-place recovery.')
a = p.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
version = re.search(r'#define NP_VERSION L"([0-9.]+)"', (root/'src/update.h').read_text()).group(1)
if not re.fullmatch(r'v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', a.tag) or a.tag != 'v'+version:
    raise SystemExit('Stable tag must exactly match source version')
out = pathlib.Path(a.output).resolve(); out.mkdir(parents=True,exist_ok=True)
tool = root/'bin/Release/ReleaseTool.exe'
key = a.key if a.key == 'ENV' else str(pathlib.Path(a.key).resolve())
exe = root/'bin/Release/NetPilotLite.exe'
subprocess.run([str(tool),'sign',key,str(exe)],cwd=root,check=True)
archive = out/'NetPilotLite-Update-x64.zip'
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_STORED,allowZip64=False) as z:
    for name in ['NetPilotLite.exe','NetPilotLite.exe.update']:
        # Explicit normal-file metadata, no extras, paths, descriptors or Unicode flags.
        info=zipfile.ZipInfo(name,(2026,1,1,0,0,0));info.create_system=0;info.external_attr=0x20
        z.writestr(info,(exe.parent/name).read_bytes())
digest=hashlib.sha256(archive.read_bytes()).hexdigest()
manifest=out/'NetPilotLite-Update-x64.json'
manifest.write_text(json.dumps(dict(version=version,architecture='x64',package_name=archive.name,
    package_size=archive.stat().st_size,sha256=digest,release_tag=a.tag,release_notes=a.notes),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
subprocess.run([str(tool),'sign-catalog',key,str(manifest)],cwd=root,check=True)
(out/(archive.name+'.sha256')).write_text(digest+'  '+archive.name+'\n',encoding='ascii')
assets=[archive,manifest,pathlib.Path(str(manifest)+'.update'),out/(archive.name+'.sha256')]
(out/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(x.read_bytes()).hexdigest()+'  '+x.name+'\n' for x in assets),encoding='ascii')
print('Signed update package created:',out)
