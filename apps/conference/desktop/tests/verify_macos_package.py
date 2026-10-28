"""Verify installed bundle dependencies/signature and offscreen startup (no devices)."""
import argparse
import json
import os
import pathlib
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('app', type=pathlib.Path)
parser.add_argument('--report', type=pathlib.Path, required=True)
args = parser.parse_args()
app = args.app.resolve()
subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
seen = set()
external = []
for path in app.rglob('*'):
    if not path.is_file():
        continue
    path = path.resolve()
    if path in seen:
        continue
    seen.add(path)
    with path.open('rb') as stream:
        magic = stream.read(4)
    if magic not in [b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca']:
        continue
    # otool -L includes LC_ID_DYLIB, which is the library's own identifier,
    # not a load dependency. Exclude only the exact identifier from -D.
    ids = subprocess.check_output(['otool', '-D', str(path)], text=True).splitlines()[1:]
    dependencies = subprocess.check_output(['otool', '-L', str(path)], text=True)
    for line in dependencies.splitlines()[1:]:
        dep = line.strip().split(' (')[0]
        if dep in ids:
            continue
        if dep.startswith('/') and not dep.startswith(('/usr/lib/', '/System/Library/')):
            external.append((str(path), dep))
if external:
    raise RuntimeError('Non-system dependencies outside bundle: ' + repr(external))
with tempfile.TemporaryDirectory() as temp:
    env = {k: v for k, v in os.environ.items() if not k.startswith(('QT_', 'DYLD_'))}
    env.update(QT_QPA_PLATFORM='offscreen', HOME=temp, DYLD_PRINT_LIBRARIES='1')
    log_path = args.report.with_suffix('.startup.log')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open('w') as log:
        process = subprocess.Popen([str(app / 'Contents/MacOS/livekit_conference_demo')], env=env, stdout=log, stderr=log)
        try:
            time.sleep(4)
            if process.poll() is not None:
                raise RuntimeError('Packaged app exited: ' + str(process.returncode))
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
    trace = log_path.read_text()
    if any('/usr/local/' in line or '/opt/homebrew/' in line for line in trace.splitlines() if 'dyld[' in line):
        raise RuntimeError('Loaded development-host library; see startup log')
report = dict(codesign_deep_strict=True, external_non_system_dependencies=external,
              offscreen_startup_seconds=4, homebrew_libraries_loaded=False,
              native_device_acceptance=False)
args.report.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
