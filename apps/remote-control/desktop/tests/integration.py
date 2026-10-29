#!/usr/bin/env python3
"""Native -> SFU -> synthetic host. Never capture or inject into the user's desktop."""
import argparse, hashlib, json, os, pathlib, socket, subprocess, time, urllib.request
p=argparse.ArgumentParser();p.add_argument('--media',required=True);p.add_argument('--business',required=True);p.add_argument('--host',required=True);p.add_argument('--controller',required=True);p.add_argument('--output',required=True);a=p.parse_args()
out=pathlib.Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
children=[];logs=[];evidence={'automated':True,'synthetic_media':True,'native_input':False,'scenarios':[],'binaries':{}}
for name in ('media','business','host','controller'):
 file=pathlib.Path(getattr(a,name)).resolve();setattr(a,name,str(file));evidence['binaries'][name]={'path':str(file),'sha256':hashlib.sha256(file.read_bytes()).hexdigest()}
def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def start(label,command,env=None):
 f=open(out/(label+'.log'),'w');logs.append(f);child=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT,env=env,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0);children.append(child);return child
def until(check,label,timeout=30):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  if check():return
  time.sleep(.1)
 raise RuntimeError('Timed out: '+label)
def healthy(url):
 try:return urllib.request.urlopen(url,timeout=1).status==200
 except Exception:return False
def events(name):
 f=out/(name+'.inputs.jsonl')
 return [json.loads(x) for x in f.read_text(encoding='utf-8').splitlines() if x] if f.exists() else []
def connect(name,mode='stop'):
 import re
 host=start(name,[a.host,base,str(out/(name+'.inputs.jsonl'))]);log=out/(name+'.log')
 until(lambda:re.search(r'PAIRING (\d{10})',log.read_text(encoding='utf-8')),'pairing '+name)
 code=re.search(r'PAIRING (\d{10})',log.read_text(encoding='utf-8'))[1]
 controller=start(name+'-controller',[a.controller,base,code,str(out/(name+'.json')),mode,name])
 return host,controller
try:
 mp,bp=port(),port();base=f'http://127.0.0.1:{bp}'
 config=out/'media.json';config.write_text(json.dumps({'bind':'127.0.0.1','port':mp,'keys':{'rctest':'remote-test-secret-at-least-32-characters'},'rtc':{'port_range_start':51600,'port_range_end':51699}}));os.chmod(config,0o600)
 media=start('media',[a.media,'--config',str(config)])
 env=dict(os.environ,LIVEKIT_API_KEY='rctest',LIVEKIT_API_SECRET='remote-test-secret-at-least-32-characters')
 business=start('business',[a.business,'-listen',f'127.0.0.1:{bp}','-db',str(out/'state.sqlite'),'-media-api',f'http://127.0.0.1:{mp}','-media-url',f'ws://127.0.0.1:{mp}'],env)
 until(lambda:healthy(base+'/healthz') and healthy(f'http://127.0.0.1:{mp}/healthz'),'services')
 ha,ca=connect('session-a');hb,cb=connect('session-b')
 for name,host,controller in [('session-a',ha,ca),('session-b',hb,cb)]:
  until(lambda:controller.poll() is not None,name+' controller',45);assert controller.returncode==0,(out/(name+'-controller.log')).read_text(encoding='utf-8')
  until(lambda:host.poll() is not None,name+' host stop',10)
  assert host.returncode==0,(out/(name+'.log')).read_text(encoding='utf-8')
  ev=events(name);texts=[e for e in ev if e['type']=='text'];assert len(texts)==1 and texts[0]['text']=='你好，Qt 远程协助 😀\n第二行\n'+name
  assert any(e['code']=='ShiftLeft' and e['down'] for e in ev) and any(e['code']=='ShiftLeft' and not e['down'] for e in ev)
  evidence['scenarios'].append({'name':name,**json.loads((out/(name+'.json')).read_text(encoding='utf-8'))})
 host,controller=connect('controller-exit','wait-failure')
 until(lambda:any(e['code']=='ControlLeft' and e['down'] for e in events('controller-exit')),'held key before process exit')
 controller.terminate();controller.wait(timeout=10)
 until(lambda:any(e['code']=='ControlLeft' and not e['down'] for e in events('controller-exit')),'process exit releases key',15)
 until(lambda:host.poll() is not None,'process exit stops host',15)
 evidence['scenarios'].append({'name':'controller-process-exit','released':True,'host_exited':True})
 host,controller=connect('business-loss','wait-failure')
 until(lambda:'HOLDING' in (out/'business-loss-controller.log').read_text(encoding='utf-8'),'held key before failure')
 until(lambda:any(e['code']=='ControlLeft' and e['down'] for e in events('business-loss')),'key-down recorded')
 business.terminate();business.wait(timeout=10)
 until(lambda:controller.poll() is not None,'controller notices business failure',20);assert controller.returncode==0
 until(lambda:any(e['code']=='ControlLeft' and not e['down'] for e in events('business-loss')),'failure releases key',15)
 evidence['scenarios'].append({'name':'business-loss','released':True});evidence['passed']=True
finally:
 for child in reversed(children):
  if child.poll() is None:
   child.terminate()
   try:child.wait(timeout=10)
   except subprocess.TimeoutExpired:child.kill();child.wait()
 for f in logs:f.close()
 (out/'evidence.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf-8')
