import http.server,json,subprocess,sys,threading,time
requests=[]
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  requests.append(self.path)
  if self.path == '/api/v1/organizations':
   self.reply({'items':[{'id':'fixture-org','data':{'name':'Fixture'}}]})
  else:self.send_error(500)
 def reply(self,data):
  body=json.dumps(data).encode();self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)))
  if self.path == '/api/v1/auth/login':self.send_header('Set-Cookie','pf_session=fixture-account; HttpOnly')
  self.end_headers();self.wfile.write(body)
 def do_POST(self):
  requests.append(self.path);self.rfile.read(int(self.headers.get('Content-Length',0)))
  if self.path == '/api/v1/auth/login':data={'user':{'id':'host','name':'fixture'}}
  elif self.path.endswith('/sessions') or self.path.endswith('/redeem'):
   time.sleep(.2)
   data={'id':str(len(requests)),'resource_id':str(len(requests)),'credential':'fixture-credential','pairing_code':'0123456789','state':'waiting','expires_at':'2099-01-01T00:00:00Z'}
  elif self.path.endswith('/request'):data={'id':'grant','resource_id':'fixture-resource','state':'pending','epoch':1}
  elif self.path.endswith('/stop'):data={'state':'ended'}
  else:self.send_error(500);return
  self.reply(data)
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
try:
 result=subprocess.run([sys.argv[1],f'http://127.0.0.1:{server.server_port}'],timeout=25)
 assert result.returncode==0
 assert sum(p.endswith('/stop') for p in requests)==4,requests
 assert not any(p.endswith('/connect') or p.endswith('/approve') for p in requests),requests
finally:server.shutdown();server.server_close();thread.join()
