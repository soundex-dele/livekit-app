"""Exercise meeting chat against an isolated official LiveKit, without devices."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--client', required=True)
parser.add_argument('--server', required=True)
args = parser.parse_args()
output = Path('out/conference-chat') / datetime.now(timezone.utc).strftime('%Y-%m-%dT%H-%M-%S-%fZ')
output.mkdir(parents=True)

def free_port(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]

port, tcp, udp = free_port(), free_port(), free_port(socket.SOCK_DGRAM)
env = dict(os.environ, LIVEKIT_URL=f'http://127.0.0.1:{port}', LIVEKIT_API_KEY='chattest',
           LIVEKIT_API_SECRET='isolated-conference-chat-test-secret')
config = output / 'server.yaml'
config.write_text(f'port: {port}\nbind_addresses: [127.0.0.1]\nrtc:\n  tcp_port: {tcp}\n  udp_port: {udp}\n'
                  '  use_external_ip: false\n  node_ip: 127.0.0.1\n  enable_loopback_candidate: true\n'
                  f'keys:\n  chattest: {env["LIVEKIT_API_SECRET"]}\nlogging:\n  level: warn\n', encoding='utf-8')
flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
evidence = {'automated': True, 'native_devices': False, 'media_backend': 'official', 'binaries': {}}
for binary in (args.client, args.server):
    evidence['binaries'][str(Path(binary).resolve())] = hashlib.sha256(Path(binary).read_bytes()).hexdigest()
with (output / 'server.log').open('w', encoding='utf-8') as log:
    server = subprocess.Popen([args.server, '--config', str(config.resolve())], stdout=log, stderr=log, creationflags=flags)
    try:
        deadline = time.monotonic() + 20
        while True:
            if server.poll() is not None:
                raise RuntimeError('Test server exited')
            try:
                with socket.create_connection(('127.0.0.1', port), timeout=.5):
                    break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.1)
        with (output / 'client.stdout.log').open('w', encoding='utf-8') as stdout, (output / 'client.stderr.log').open('w', encoding='utf-8') as stderr:
            result = subprocess.run([str(Path(args.client).resolve())], env=env, stdout=stdout, stderr=stderr,
                                    timeout=120, creationflags=flags)
        if result.returncode:
            raise RuntimeError(f'Chat client exited with {result.returncode}')
        evidence.update(json.loads((output / 'client.stdout.log').read_text(encoding='utf-8').strip().splitlines()[-1]))
        if evidence.get('passed') is not True:
            raise RuntimeError('Chat assertions did not pass')
    except Exception as error:
        evidence.update(passed=False, error=str(error))
        raise
    finally:
        server.terminate()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
        (output / 'evidence.json').write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
        print('Evidence:', output.resolve())
