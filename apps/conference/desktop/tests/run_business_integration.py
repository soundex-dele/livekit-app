"""Run isolated AV Meeting business + native media services; never touch physical devices."""
import argparse,json,os,pathlib,socket,subprocess,tempfile,time,urllib.request,hashlib
p=argparse.ArgumentParser();p.add_argument('--client',required=True);p.add_argument('--media-server',required=True);p.add_argument('--business-server',required=True);p.add_argument('--output',default='out/conference-integration');a=p.parse_args()
def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def ready(url,proc):
 deadline=time.monotonic()+30
 while time.monotonic()<deadline:
  if proc.poll() is not None:raise RuntimeError('Server exited before ready')
  try:
   with urllib.request.urlopen(url,timeout=.5) as r:
    if r.status==200:return
  except OSError:pass
  time.sleep(.1)
 raise RuntimeError('Server readiness timed out')
root=pathlib.Path(a.output).resolve()/time.strftime('%Y%m%d-%H%M%S');root.mkdir(parents=True)
api,media=port(),port();secret='meeting-integration-admission-secret';key='meetingtest';media_secret='meeting-integration-media-secret-32bytes'
env=dict(os.environ,LIVEKIT_API_KEY=key,LIVEKIT_API_SECRET=media_secret,CONFERENCE_ADMISSION_SECRET=secret,CONFERENCE_ADMISSION_PORT=str(api),CONFERENCE_TEST_URL=f'http://127.0.0.1:{api}')
config=root/'media.yaml';config.write_text(f'port: {media}\nbind_addresses: [127.0.0.1]\nrtc:\n  port_range_start: 51000\n  port_range_end: 51100\nkeys:\n  {key}: {media_secret}\n')
procs=[]
try:
 with open(root/'media.log','w') as ml,open(root/'business.log','w') as bl:
  m=subprocess.Popen([str(pathlib.Path(a.media_server).resolve()),'--config',str(config)],env=env,stdout=ml,stderr=ml);procs.append(m);ready(f'http://127.0.0.1:{media}/healthz',m)
  b=subprocess.Popen([str(pathlib.Path(a.business_server).resolve()),'-conference','-listen',f'127.0.0.1:{api}','-db',str(root/'meeting.sqlite'),'-media-api',f'http://127.0.0.1:{media}','-media-url',f'ws://127.0.0.1:{media}','-remote-control-web=','-screen-cast-web='],env=env,stdout=bl,stderr=bl);procs.append(b);ready(f'http://127.0.0.1:{api}/healthz',b)
  with open(root/'client.log','w') as cl:
   result=subprocess.run([str(pathlib.Path(a.client).resolve())],env=env,stdout=cl,stderr=cl,timeout=180)
  if result.returncode:raise RuntimeError('Integration failed; see '+str(root/'client.log'))
  report={'passed':True,'native_devices':False,'binaries':{str(pathlib.Path(x).resolve()):hashlib.sha256(pathlib.Path(x).read_bytes()).hexdigest() for x in [a.client,a.media_server,a.business_server]}}
  (root/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),root)
finally:
 for proc in reversed(procs):
  proc.terminate()
  try:proc.wait(timeout=10)
  except subprocess.TimeoutExpired:proc.kill();proc.wait()
