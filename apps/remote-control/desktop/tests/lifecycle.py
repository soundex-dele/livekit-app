import http.server,json,subprocess,sys,threading,time
requests=[]
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_POST(self):
  requests.append(self.path);self.rfile.read(int(self.headers.get('Content-Length',0)))
  if self.path.endswith('/pairings') or self.path.endswith('/redeem'):
   time.sleep(.2)
   data={'id':str(len(requests)),'grant_id':'grant','credential':'fixture-credential','pairing_code':'0123456789','state':'waiting','controller_name':'','controller_identity':'controller','host_identity':'host','epoch':1,'lease_remaining_ms':10000,'expires_at':'2099-01-01T00:00:00Z'}
  elif self.path.endswith('/revoke'):data={'state':'revoked'}
  else:self.send_error(500);return
  body=json.dumps(data).encode();self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
try:
 result=subprocess.run([sys.argv[1],f'http://127.0.0.1:{server.server_port}'],timeout=25)
 assert result.returncode==0
 assert len(requests)==8,requests
 assert sum(p.endswith('/revoke') for p in requests)==4,requests
 assert not any(p.endswith('/connect') or p.endswith('/approve') for p in requests),requests
finally:server.shutdown();server.server_close();thread.join()
