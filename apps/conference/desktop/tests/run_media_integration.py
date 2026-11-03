"""Run the conference media regression with an isolated SFU and no devices."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--client', required=True, type=Path)
parser.add_argument('--server', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)

def free_port(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]

port, tcp, udp = free_port(), free_port(), free_port(socket.SOCK_DGRAM)
env = dict(os.environ, LIVEKIT_URL=f'http://127.0.0.1:{port}',
           LIVEKIT_API_KEY='mediatest', LIVEKIT_API_SECRET='isolated-media-engine-test-secret',
           LIVEKIT_DEMO_TEST_AUDIO='0')
config = root / 'server.yaml'
config.write_text(f'port: {port}\nbind_addresses: [127.0.0.1]\nrtc:\n'
                  f'  tcp_port: {tcp}\n  udp_port: {udp}\n  use_external_ip: false\n'
                  '  node_ip: 127.0.0.1\n  enable_loopback_candidate: true\n'
                  f'keys:\n  mediatest: {env["LIVEKIT_API_SECRET"]}\n'
                  'room:\n  enable_remote_unmute: true\nlogging:\n  level: warn\n')
flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
with (root / 'server.log').open('w') as server_log:
    server = subprocess.Popen([str(args.server.resolve()), '--config', str(config)],
                              stdout=server_log, stderr=server_log, creationflags=flags)
    try:
        deadline = time.monotonic() + 20
        while True:
            if server.poll() is not None:
                raise RuntimeError('SFU exited')
            try:
                with socket.create_connection(('127.0.0.1', port), timeout=.5):
                    break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.1)
        with (root / 'client.log').open('w', encoding='utf-8') as log:
            result = subprocess.run([str(args.client.resolve()), str(root / 'recordings')],
                                    env=env, stdout=log, stderr=log, timeout=180,
                                    creationflags=flags)
        if result.returncode:
            raise RuntimeError(f'Media regression exited {result.returncode}; see {root / "client.log"}')
        report = {'passed': True, 'native_devices': False,
                  'client': str(args.client.resolve())}
        (root / 'result.json').write_text(json.dumps(report, indent=2))
        print(json.dumps(report))
    finally:
        server.terminate()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
        config.unlink(missing_ok=True)
