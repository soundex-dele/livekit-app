#!/usr/bin/env python3
"""Audit every Mach-O dependency, then smoke-test a relocated bundle offscreen."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('bundle', type=Path)
args = parser.parse_args()
bundle = args.bundle.resolve()
executable = bundle / 'Contents/MacOS/av_remote_host'
magic = {bytes.fromhex(x) for x in ('cffaedfe', 'feedfacf', 'cefaedfe', 'feedface', 'cafebabe', 'bebafeca', 'cafebabf')}

def output(*command):
    return subprocess.check_output(command, text=True)

def expand(value, owner):
    return Path(value.replace('@executable_path', str(executable.parent)).replace('@loader_path', str(owner.parent)))

def rpaths(owner):
    return [expand(p, owner) for p in re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', output('/usr/bin/otool', '-l', str(owner)))]

seen = set()
for path in bundle.rglob('*'):
    if not path.is_file() or path.resolve() in seen:
        continue
    with path.open('rb') as f:
        if f.read(4) not in magic:
            continue
    path = path.resolve()
    seen.add(path)
    for line in output('/usr/bin/otool', '-L', str(path)).splitlines()[1:]:
        dependency = line.strip().split(' (compatibility version')[0]
        if dependency.startswith(('/usr/lib/', '/System/Library/')):
            continue
        candidates = ([root / dependency[len('@rpath/'):] for root in rpaths(path) + rpaths(executable)]
                      if dependency.startswith('@rpath/') else [expand(dependency, path)])
        assert any(p.exists() and p.resolve().is_relative_to(bundle) for p in candidates), (path, dependency)
subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)
with tempfile.TemporaryDirectory(prefix='av-remote-package-') as temp:
    relocated = Path(temp) / bundle.name
    shutil.copytree(bundle, relocated, symlinks=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('QT_', 'DYLD_'))}
    env.update(QT_QPA_PLATFORM='offscreen', QT_PLUGIN_PATH=str(relocated / 'Contents/PlugIns'))
    result = subprocess.run([str(relocated / 'Contents/MacOS/av_remote_host'), '--smoke-test'], env=env, cwd=temp, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
print(json.dumps({'passed': True, 'mach_o_files': len(seen), 'relocated_offscreen_smoke': True, 'native_capture_or_input': False}, indent=2))
