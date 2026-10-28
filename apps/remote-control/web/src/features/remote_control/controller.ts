import { errorText, statusText } from '../../../../../../packages/web/locale';
import { Room, RoomEvent, Track } from 'livekit-client';
import { ControlQueue, point, topic, type Grant, type Layout } from './protocol';
import { request } from '../../services/api';
import { TextDelivery } from './text_delivery';
const el=<T extends HTMLElement>(id:string)=>document.getElementById(id) as T;
const form=el<HTMLFormElement>('connect-form'),video=el<HTMLVideoElement>('screen'),viewport=el('viewport');
const button=el<HTMLButtonElement>('connect'),stop=el<HTMLButtonElement>('stop');
const textForm=el<HTMLFormElement>('text-form'),draft=el<HTMLTextAreaElement>('remote-text'),sendText=el<HTMLButtonElement>('send-text'),textStatus=el('text-status');
let textTarget=false;
const delivery=new TextDelivery((outcome,message)=>{
  textStatus.textContent=message;
  if(outcome==='accepted')draft.value='';
  if(outcome==='unknown'){void end(message);return;}
  updateTextControls();
});
let generation=0,credential='',grant:Grant|undefined,room:Room|undefined,queue:ControlQueue|undefined,layout:Layout|undefined;
let connecting=false,enabled=false,stopping=false,heartbeat:ReturnType<typeof setInterval>|undefined,poll:ReturnType<typeof setTimeout>|undefined;
const status=(text:string)=>{el('status').textContent=text;};
const api=(path:string,body?:unknown,token=credential)=>request(path,body,token);

function updateTextControls(){
  const available=!!queue&&layout?.text_input===true&&!stopping;
  draft.disabled=!available||delivery.busy;
  sendText.disabled=!available||!textTarget||delivery.busy||!draft.value.length;
  sendText.textContent=delivery.busy?'等待远端确认…':'发送文字';
  textForm.setAttribute('aria-busy',String(delivery.busy));
}
function forgetTextTarget(){textTarget=false;release();updateTextControls();if(!delivery.busy&&layout?.text_input===true)textStatus.textContent='请先点击远程画面中的目标输入框，再发送文字。';}
draft.addEventListener('input',updateTextControls);
textForm.addEventListener('submit',event=>{
  event.preventDefault();
  if(sendText.disabled||!queue||!layout)return;
  try{
    const request=delivery.begin(draft.value);
    textStatus.textContent='正在发送，等待远端确认…';updateTextControls();
    release();queue?.push({...request,layout_version:layout.layout_version});
  }catch(error){textStatus.textContent=error instanceof Error?error.message:'文字发送失败。';updateTextControls();}
});

function release(){enabled=false;if(layout)queue?.push({type:'release'});el('input-status').textContent='键鼠已暂停，点击远程画面重新启用。';}
async function end(message:string){
  if(stopping)return;stopping=true;generation++;clearInterval(heartbeat);clearTimeout(poll);release();
  if(delivery.cancel())textStatus.textContent='连接已结束，文字发送结果不确定。请检查远端内容，不要直接重发。';
  else textStatus.textContent=message;
  textTarget=false;draft.value='';
  const current=room,previous=grant,secret=credential,finishing=queue?.finish();queue=undefined;room=undefined;grant=undefined;credential='';layout=undefined;connecting=false;
  stop.disabled=true;status(message);el('badge').textContent='已结束';el('placeholder').style.display='grid';video.srcObject=null;
  updateTextControls();
  await Promise.allSettled([Promise.resolve(finishing).then(()=>current?.disconnect()),previous?api('control-grants/'+previous.id+'/revoke',{},secret):Promise.resolve()]);
  button.disabled=false;stopping=false;
}
function send(event:Record<string,unknown>){if(enabled&&layout)queue?.push({...event,layout_version:layout.layout_version});}
async function connectMedia(g:Grant,gen:number){
  connecting=true;const media=await api('control-grants/'+g.id+'/connect',{});if(gen!==generation)return;
  const current=room=new Room({adaptiveStream:false,dynacast:false,reconnectPolicy:{nextRetryDelayInMs:()=>null}});
  const startControlChannel=()=>{
    if(gen!==generation||room!==current||queue)return;
    queue=new ControlQueue(g,bytes=>current.localParticipant.publishData(bytes,{reliable:true,topic,destinationIdentities:[g.host_identity]}),e=>{void end(errorText(e));});
    // Optional data channels are negotiated on the first publication. Start
    // after authenticated signaling; waiting for all media transports first
    // can leave a data-only controller silent until the host lease times out.
    layout=undefined;
    queue.push({type:'heartbeat'});heartbeat=setInterval(()=>queue?.push({type:'heartbeat'}),1000);
  };
  current.on(RoomEvent.SignalConnected,startControlChannel);
  current.on(RoomEvent.Disconnected,()=>{if(room===current)void end('媒体连接已断开，请重新申请。');});
  current.on(RoomEvent.Reconnecting,()=>{if(room===current)void end('连接中断，控制授权已结束。');});
  current.on(RoomEvent.ParticipantDisconnected,p=>{if(p.identity===g.host_identity)void end('被控端已离线。');});
  current.on(RoomEvent.TrackSubscribed,(track,_publication,participant)=>{
    if(participant.identity!==g.host_identity||track.kind!==Track.Kind.Video)return;
    track.attach(video);void video.play().catch(()=>status('点击画面开始播放'));el('placeholder').style.display='none';
  });
  current.on(RoomEvent.DataReceived,(bytes,participant,_kind,messageTopic)=>{
    if(messageTopic!==topic||participant?.identity!==g.host_identity||bytes.length>4096||gen!==generation)return;
    try{const v=JSON.parse(new TextDecoder().decode(bytes)) as Layout;
      if(v.v!==1||v.type!=='layout'||v.grant_id!==g.grant_id||v.epoch!==g.epoch||!Number.isSafeInteger(v.layout_version)||v.layout_version<1||!Number.isInteger(v.width)||!Number.isInteger(v.height)||v.width<=0||v.height<=0||typeof v.active!=='boolean')return;
      if(layout&&v.layout_version<layout.layout_version)return;
      delivery.receive(v.text_result);
      if(!v.active){void end('被控端已停止控制。');return;}
      if(!layout||v.layout_version>layout.layout_version){
        release();textTarget=false;layout=v;queue?.push({type:'layout_ack',layout_version:v.layout_version});status('已连接。点击画面启用键鼠操作。');el('badge').textContent='已连接';
        if(!delivery.busy)textStatus.textContent=v.text_input===true?'请先点击远程画面中的目标输入框，再发送文字。':'当前被控端不支持发送文字，请更新被控端。';
      }
      updateTextControls();
    }catch{/* Ignore malformed remote packets. */}
  });
  await current.connect(media.media_url,media.media_token,{autoSubscribe:true});if(gen!==generation){await current.disconnect();return;}
  startControlChannel();
}
async function watch(gen:number){
  if(gen!==generation||!grant)return;
  try{const value=await api('control-grants/'+grant.id) as Grant;if(gen!==generation)return;grant=value;
    if(value.state==='approved'&&!connecting)await connectMedia(value,gen);
    else if(value.state!=='pending'&&value.state!=='approved'){await end('连接申请已结束：'+statusText(value.state));return;}
    if(gen===generation)poll=setTimeout(()=>void watch(gen),500);
  }catch(e){if(gen===generation)await end('连接失败：'+errorText(e));}
}
form.addEventListener('submit',event=>{event.preventDefault();if(button.disabled)return;button.disabled=true;const gen=++generation;
  void(async()=>{try{const value=await api('pairings/redeem',{code:el<HTMLInputElement>('code').value.trim(),name:el<HTMLInputElement>('name').value.trim()},'');
    if(gen!==generation)return;grant=value;credential=value.credential;stop.disabled=false;status('等待被控端确认。对方同意前不会共享画面。');el('badge').textContent='等待确认';void watch(gen);
  }catch(e){button.disabled=false;status('申请失败：'+errorText(e));}})();
});
stop.onclick=()=>void end('你已停止连接。');
const position=(e:MouseEvent)=>point(video.getBoundingClientRect(),video.videoWidth,video.videoHeight,e.clientX,e.clientY);
viewport.addEventListener('pointerdown',event=>{const p=position(event);if(!layout||!queue||!p||delivery.busy)return;event.preventDefault();viewport.focus();enabled=true;textTarget=true;updateTextControls();el('input-status').textContent='正在控制 · 离开窗口可暂停输入';viewport.setPointerCapture(event.pointerId);send({type:'button',button:event.button,down:true,...p});});
viewport.addEventListener('pointerup',event=>{const p=position(event);if(p)send({type:'button',button:event.button,down:false,...p});else release();if(viewport.hasPointerCapture(event.pointerId))viewport.releasePointerCapture(event.pointerId);});
viewport.addEventListener('pointermove',event=>{const p=position(event);if(p)send({type:'move',...p});});
viewport.addEventListener('pointercancel',release);
viewport.addEventListener('contextmenu',e=>e.preventDefault());
viewport.addEventListener('wheel',event=>{if(!enabled)return;const p=position(event);if(p){event.preventDefault();send({type:'wheel',delta:event.deltaY>0?-120:120,...p});}},{passive:false});
viewport.addEventListener('keydown',event=>{if(!enabled||event.isComposing)return;event.preventDefault();send({type:'key',code:event.code,down:true});});
viewport.addEventListener('keyup',event=>{if(!enabled||event.isComposing)return;event.preventDefault();send({type:'key',code:event.code,down:false});});
viewport.addEventListener('blur',release);window.addEventListener('blur',forgetTextTarget);document.addEventListener('visibilitychange',()=>{if(document.hidden)forgetTextTarget();});
window.addEventListener('pagehide',()=>{delivery.cancel();draft.value='';release();queue?.push({type:'stop'});if(grant)void fetch('/api/v1/remote-control/control-grants/'+grant.id+'/revoke',{method:'POST',headers:{Authorization:'Bearer '+credential,'Content-Type':'application/json'},body:'{}',keepalive:true});});
setInterval(()=>queue?.flushMove(),34);
