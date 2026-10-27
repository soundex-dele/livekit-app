#!/usr/bin/env python3
"""Audit a Qt/FFmpeg app; --fix normalizes already-bundled dependencies and ad-hoc signs.

Never copies an undiscovered dependency or changes the developer's installed libraries.
"""
import argparse
import os
import pathlib
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('bundle', type=pathlib.Path)
parser.add_argument('--fix', action='store_true')
args = parser.parse_args()
bundle = args.bundle.resolve()
frameworks = bundle / 'Contents/Frameworks'
executable_dir = bundle / 'Contents/MacOS'
if not bundle.is_dir():
    raise SystemExit(f'Missing app bundle: {bundle}')


def run(*command):
    return subprocess.run(command, capture_output=True, text=True, check=True).stdout


def system(path):
    return path.startswith(('/System/Library/', '/usr/lib/'))


def metadata(file):
    deps = [line.strip().split(' (', 1)[0] for line in run('otool', '-L', str(file)).splitlines()[1:]]
    loads = run('otool', '-l', str(file)).splitlines()
    identity, rpaths = None, []
    for i, line in enumerate(loads):
        if line.strip() == 'cmd LC_RPATH':
            rpaths.append(loads[i + 2].strip().removeprefix('path ').split(' (', 1)[0])
        elif line.strip() == 'cmd LC_ID_DYLIB':
            identity = loads[i + 2].strip().removeprefix('name ').split(' (', 1)[0]
    # LC_ID_DYLIB identifies this library; it is not a load dependency.
    return [dep for dep in deps if dep != identity], identity, rpaths


files = []
for file in bundle.rglob('*'):
    if file.is_symlink() and (not file.exists() or not file.resolve().is_relative_to(bundle)):
        raise SystemExit(f'Broken or external bundle symlink: {file.relative_to(bundle)}')
    if file.is_file() and not file.is_symlink() and 'Mach-O' in run('file', '-b', str(file)):
        files.append(file)
if not files:
    raise SystemExit('No Mach-O binaries found')


def bundled_target(dependency):
    if '.framework/' in dependency:
        prefix, suffix = dependency.split('.framework/', 1)
        tail = prefix.rsplit('/', 1)[-1] + '.framework/' + suffix
        candidates = [file for file in files if str(file).endswith('/' + tail)]
    else:
        candidates = [file for file in files if file.name == pathlib.PurePosixPath(dependency).name]
    if len(candidates) != 1 or not candidates[0].is_relative_to(frameworks):
        raise SystemExit(f'Dependency not uniquely bundled: {dependency}')
    return '@rpath/' + candidates[0].relative_to(frameworks).as_posix()


if args.fix:
    for file in files:
        deps, identity, rpaths = metadata(file)
        changes = []
        if identity and file.is_relative_to(frameworks):
            desired_id = '@rpath/' + file.relative_to(frameworks).as_posix()
            if identity != desired_id:
                changes += ['-id', desired_id]
        for dep in deps:
            if dep.startswith('/') and not system(dep):
                changes += ['-change', dep, bundled_target(dep)]
        for rpath in set(rpaths):
            if rpath.startswith('/') and not system(rpath):
                changes += ['-delete_rpath', rpath]
        local_rpath = '@loader_path/' + os.path.relpath(frameworks, file.parent)
        if any(dep.startswith('@rpath/') or (dep.startswith('/') and not system(dep)) for dep in deps):
            if local_rpath not in rpaths:
                changes += ['-add_rpath', local_rpath]
        if changes:
            run('install_name_tool', *changes, str(file))
    # This is a local ad-hoc signature, not a release identity or notarization.
    run('codesign', '--force', '--deep', '--sign', '-', str(bundle))

failures = []
for file in files:
    deps, identity, rpaths = metadata(file)
    for path in rpaths:
        if path.startswith('/') and not system(path):
            failures.append(f'{file.relative_to(bundle)}: external rpath {path}')
    if identity and identity.startswith('/') and not system(identity):
        failures.append(f'{file.relative_to(bundle)}: external library ID {identity}')
    for dep in deps:
        if system(dep):
            continue
        if dep.startswith('/'):
            failures.append(f'{file.relative_to(bundle)}: external dependency {dep}')
            continue
        def expand(path):
            return path.replace('@loader_path', str(file.parent)).replace('@executable_path', str(executable_dir))
        if dep.startswith('@rpath/'):
            candidates = [pathlib.Path(expand(path)) / dep[len('@rpath/'):] for path in rpaths]
        else:
            candidates = [pathlib.Path(expand(dep))]
        if not any(path.exists() and path.resolve().is_relative_to(bundle) for path in candidates):
            failures.append(f'{file.relative_to(bundle)}: unresolved bundled dependency {dep}')
if failures:
    raise SystemExit('Deployment audit failed:\n' + '\n'.join(failures))
print(f'PASS: {len(files)} bundled Mach-O files have portable, resolved dependencies')
