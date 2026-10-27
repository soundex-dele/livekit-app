import { errorText, statusText } from '../../../../../packages/web/locale';
import { Room, RoomEvent, Track } from 'livekit-client';
import { api, terminal, type SessionInfo } from '../services/api';
import '../style.css';
const element = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const receive = element<HTMLButtonElement>('receive'), stop = element<HTMLButtonElement>('stop'), fullscreen = element<HTMLButtonElement>('fullscreen');
const badge = element('badge'), code = element('code'), expiry = element('expiry'), hint = element('hint'), error = element('error'), placeholder = element('placeholder');
const video = document.querySelector('video')!;
type Run = { session?: SessionInfo; room?: Room; stopped: boolean; connecting: boolean; connected: boolean; frames: boolean; nextHeartbeat: number; frameCallback?: number };
let current: Run | undefined;
function active(run: Run) { return current === run && !run.stopped; }
async function finish(run: Run, message = '', state = '已结束') {
 if (run.stopped) return;
 run.stopped = true;
 if (run.frameCallback !== undefined) video.cancelVideoFrameCallback(run.frameCallback);
 const room = run.room;
 // Disconnect media immediately, before the business notification.
 const disconnected = room?.disconnect();
 video.pause(); video.srcObject = null;
 if (current === run) document.body.dataset.playing = 'false';
 if (current === run) { badge.textContent = state; error.textContent = message; code.textContent = '—'; expiry.textContent = ''; hint.textContent = '再次传屏需要生成新投屏码。'; stop.disabled = true; fullscreen.disabled = true; placeholder.hidden = false; }
 await disconnected;
 if (run.session) await api(`sessions/${run.session.id}/stop`, run.session.credential, {}, true).catch(() => {});
 if (current === run) { current = undefined; receive.disabled = false; }
}
async function connect(run: Run) {
 run.connecting = true; badge.textContent = '正在连接';
 try {
  const conn = await api<{media_url: string; media_token: string}>(`sessions/${run.session!.id}/connect`,run.session!.credential,{});
  if (!active(run)) return;
  const room = run.room = new Room({ adaptiveStream: false, dynacast: false });
  room.on(RoomEvent.Disconnected, () => { if(active(run))void finish(run,'媒体连接已断开'); });
  room.on(RoomEvent.Reconnecting, () => { if(active(run))void finish(run,'媒体连接中断，请重新投屏'); });
  room.on(RoomEvent.ParticipantDisconnected, () => { if(active(run))void finish(run,'发送端已离开'); });
  room.on(RoomEvent.TrackUnsubscribed, () => { if(active(run))void finish(run,'发送端已停止共享'); });
  room.on(RoomEvent.TrackSubscribed, track => {
   if (!active(run) || track.kind !== Track.Kind.Video) return;
   track.attach(video); video.muted = true;
   run.frameCallback = video.requestVideoFrameCallback(() => {
    if (!active(run)) return;
    run.frames = true; document.body.dataset.playing = 'true'; badge.textContent = '正在播放'; placeholder.hidden = true; fullscreen.disabled = false; hint.textContent = '正在接收 电脑共享画面。';
   });
   void video.play().catch(e => finish(run,errorText(e)));
  });
  await room.connect(conn.media_url,conn.media_token,{autoSubscribe:true});
  if (!active(run)) { await room.disconnect(); return; }
  run.connected = true; run.nextHeartbeat = 0;
 } catch (e) { if(active(run))await finish(run,errorText(e)); }
}
receive.addEventListener('click',async()=>{
 if(current)return;
 const run: Run = {stopped:false,connecting:false,connected:false,frames:false,nextHeartbeat:0};current=run;
 receive.disabled=true;stop.disabled=false;error.textContent='';badge.textContent='正在生成投屏码';
 try {
  const session = await api<SessionInfo>('sessions','',{});run.session=session;
  if(!active(run)){await api(`sessions/${session.id}/stop`,session.credential,{},true).catch(()=>{});return;}
  code.textContent=session.code!;badge.textContent='等待配对';hint.textContent='在 电脑发送端输入上方投屏码。';
  while(active(run)){
   const s=await api<SessionInfo>(`sessions/${session.id}`,session.credential);
   if(!active(run))break;
   if(terminal(s)){await finish(run,s.reason ? statusText(s.reason) : '',s.state==='expired'?'投屏码已过期':'已结束');break;}
   if(s.state==='waiting')expiry.textContent=`有效期剩余 ${Math.max(0,Math.ceil((Date.parse(s.expires_at)-Date.now())/1000))} 秒`;
   else {
    code.textContent='已配对';expiry.textContent='';
    if(!run.connecting)void connect(run);
    if(Date.now()>=run.nextHeartbeat){
     const beat=await api<SessionInfo>(`sessions/${session.id}/heartbeat`,session.credential,{connected:run.connected});
     if(terminal(beat)){await finish(run,beat.reason ? statusText(beat.reason) : '');break;}
     run.nextHeartbeat=Date.now()+3000;
    }
   }
   await new Promise(resolve=>setTimeout(resolve,500));
  }
 } catch(e){if(active(run))await finish(run,errorText(e));}
});
stop.addEventListener('click',()=>{if(current)void finish(current);});
fullscreen.addEventListener('click',()=>{void element('viewport').requestFullscreen().catch(e=>error.textContent='无法进入全屏，请重试。');});
window.addEventListener('pagehide',()=>{if(current)void finish(current);});
