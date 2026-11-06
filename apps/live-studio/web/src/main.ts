import {Room,RoomEvent,Track,createLocalTracks, type LocalTrack} from 'livekit-client';
import {acceptEvents,pkLabel,stateLabel,type User,type LiveRoom,type Snapshot,type LiveEvent} from './state';
import './style.css';
import {api, control, watch, roomView, eventView, resourceCredential, clearVisitors} from './api';
import {mountAccountPanel, type ResourceEvent} from '../../../../packages/web/control';
import {createButton, emptyState, observeStatus} from '../../../../packages/web/ui';


const app=document.querySelector<HTMLDivElement>('#app')!;
app.innerHTML=`<header class="av-topbar"><a class="av-brand" href="#"><span class="av-brand__mark">AV</span>直播空间</a><nav class="av-nav" aria-label="主导航"><a href="#">发现直播</a><button id="admin" class="av-button--ghost" hidden>平台管理</button></nav><button id="account">登录 / 注册</button></header>
<div id="notice" class="av-alert" role="status" aria-live="polite"></div><main>
<section id="lobby"><div class="av-page-header"><div><p class="eyebrow">LIVE TOGETHER</p><h1>好内容，此刻相遇</h1><p>发现实时直播，与主播交流，一起加入现场。</p></div><button id="refresh">刷新直播</button></div><div class="lobby-grid"><section><div class="section-title"><h2>发现直播</h2><span class="hint">与精彩保持连接</span></div><div id="rooms" class="room-grid"></div></section><aside class="av-card ranking"><span class="ranking-label">DAILY RANKING</span><h2>今日人气主播</h2><p>来自今天的实时互动</p><ol id="daily"></ol></aside></div></section>
<section id="theater" hidden><div class="live-heading"><div><a href="#">← 返回直播列表</a><h1 id="title"></h1></div><span id="status" class="av-badge"></span></div><div class="theater-grid"><div class="stage-column"><section class="video-card"><div id="video-stage"><div id="empty-video"><svg viewBox="0 0 48 48" aria-hidden="true"><rect x="7" y="11" width="25" height="26" rx="5"/><path d="m32 19 10-5v20l-10-5"/></svg><span>等待主播画面…</span></div></div><div class="playback-bar"><button id="sound">开启声音</button><button id="share">复制直播链接</button><span id="online"></span></div></section><div id="pk" class="pk av-alert"></div><section class="av-card interaction"><div><h2>送一份心意</h2><p id="wallet">登录后每天获得 100 免费积分</p></div><div class="gifts"><button data-gift="1">掌声 <small>1 分</small></button><button data-gift="10">鲜花 <small>10 分</small></button><button data-gift="50">星光 <small>50 分</small></button></div></section><section id="guest-panel" class="av-card"><div class="section-title"><h2>加入现场</h2><span class="hint">需主播同意</span></div><button id="request-guest">申请连麦</button><button id="leave-guest" class="av-button--danger" hidden>结束连麦</button><span id="guest-status"></span><div id="guest-media"></div></section><section class="av-card contribution"><h2>本场贡献榜</h2><ol id="board"></ol></section></div>
<aside class="av-card chat-panel"><div class="section-title"><h2>直播聊天</h2><span id="chat-state"></span></div><div id="messages" role="log" aria-live="polite"></div><form id="chat-form"><label class="sr-only" for="message">聊天内容</label><input id="message" maxlength="300" placeholder="说点什么…" autocomplete="off"><button class="av-button--primary">发送</button></form><details id="host-controls" hidden open><summary>主播管理</summary><h3>连麦申请</h3><div id="guests"></div><h3>在线成员</h3><div id="members"></div></details></aside></div></section>
<section id="management" hidden><div class="av-page-header"><div><p class="eyebrow">ADMINISTRATION</p><h1>平台管理</h1><p>账号、组织与四业务管理已整合到统一运营后台。</p></div><a href="#">返回直播大厅</a></div><section class="av-card"><p id="management-status" role="status">正在连接统一后台…</p></section></section>
</main><footer class="page-footer">AV Platform · 实时分享，保持连接</footer>`;
const $=<T extends HTMLElement=HTMLElement>(id:string)=>document.getElementById(id) as T;
let user:User|null=null, rooms:LiveRoom[]=[], current:LiveRoom|null=null,snapshot:Snapshot|null=null;
let program:Room|null=null,stage:Room|null=null,localTracks:LocalTrack[]=[],socket:WebSocket|null=null;
let lastEvent=0,generation=0,refreshing=false,stageConnecting=false,reconnectTimer:ReturnType<typeof setTimeout>|undefined;
let joining=false;
const messages=$('messages');
const errors:Record<string,string>={unauthorized:'请先登录，或重新登录后再试。',invalid_account:'用户名或密码不符合要求。',account_unavailable:'用户名已被使用。',insufficient_points:'今天的免费积分不足。',cannot_gift_self:'不能给自己送礼。',muted:'你已被主播禁言。',banned:'本次直播的观看权限已被移除。',broadcast_not_live:'直播尚未开始，请稍后再试。',broadcast_ended:'直播已结束。',rate_limited:'操作太频繁，请稍后再试。',media_unavailable:'媒体服务暂时不可用。',viewer_capacity:'直播间已满，请稍后再试。',guest_capacity:'连麦席位已满。',pk_active:'PK 期间暂不开放嘉宾连麦。'};
function notify(text:string, tone='info'){$('notice').textContent=errors[text]??text;$('notice').dataset.tone=tone;}
async function guarded(action:()=>Promise<void>){try{await action();}catch(e){notify(e instanceof Error?e.message:String(e),'error');}}
function button(label:string,action:()=>Promise<void>,disabled=false){const b=createButton(label);b.disabled=disabled;b.onclick=()=>{b.disabled=true;void guarded(action).finally(()=>{b.disabled=disabled;});};return b;}
function row(text:string){const div=document.createElement('div');div.className='entry';const span=document.createElement('span');span.textContent=text;div.append(span);return div;}
function authenticate(){if(user)return true;accountPanel.open();return false;}
function syncAccount(){$('admin').hidden=!user;}
async function loadRooms(){
  const list=$('rooms');
  list.setAttribute('aria-busy','true');
  list.replaceChildren(emptyState('正在获取直播', '精彩即将呈现，请稍候。'));
  try {
    rooms=await api<LiveRoom[]>('rooms');
    list.replaceChildren();
    for(const room of rooms){
      const card=document.createElement('a');card.className='room-card';card.href='#room/'+room.id;
      const cover=document.createElement('div');cover.className='cover';
      const symbol=document.createElement('span');symbol.className='cover-play';symbol.setAttribute('aria-hidden','true');
      symbol.innerHTML='<svg viewBox="0 0 24 24"><path d="m9 6 9 6-9 6z"/></svg>';
      const label=document.createElement('span');label.className='av-badge';label.textContent=stateLabel[room.state]??room.state;
      label.dataset.tone=room.broadcast?'success':'neutral';cover.append(symbol,label);
      const title=document.createElement('h3');title.textContent=room.title;
      const detail=document.createElement('p');detail.textContent=room.broadcast?'进入直播间 →':'主播暂未开播';
      card.append(cover,title,detail);list.append(card);
    }
    if(!rooms.length)list.append(emptyState('直播即将开始',control.organization?'这里将展示组织内的直播。使用直播助手开始第一场直播。':'请在右上角登录并选择组织，或通过分享链接进入直播间。'));
    const board=await api<{name:string;points:number}[]>('leaderboard');
    $('daily').replaceChildren(...board.map(item=>{const li=document.createElement('li');const name=document.createElement('span');name.textContent=item.name;const score=document.createElement('strong');score.textContent=`${item.points} 分`;li.append(name,score);return li;}));
    if(!board.length){const item=document.createElement('li');item.className='ranking-empty';item.textContent='今天的互动将从这里开始';$('daily').append(item);}
  } catch(error) {
    list.replaceChildren(emptyState('暂时无法加载直播', '请检查网络连接，点击刷新直播重试。'));
    throw error;
  } finally {list.removeAttribute('aria-busy');}
}
async function stopStage(){const old=stage;stage=null;for(const track of localTracks)track.stop();localTracks=[];await old?.disconnect();$('guest-media').replaceChildren();$('leave-guest').hidden=true;$('guest-status').textContent='';for(const element of $('video-stage').querySelectorAll<HTMLMediaElement>('audio,video'))element.muted=false;}
async function leave(){generation++;clearTimeout(reconnectTimer);socket?.close();socket=null;await stopStage();await program?.disconnect();program=null;current=null;snapshot=null;$('video-stage').querySelectorAll('audio,video').forEach(el=>el.remove());$('empty-video').hidden=false;}
function attach(room:Room,target:HTMLElement){room.on(RoomEvent.TrackSubscribed,(track)=>{const element=track.attach();element.dataset.track=track.sid??'';element.autoplay=true;if(element instanceof HTMLVideoElement){element.playsInline=true;if(target.id==='video-stage')$('empty-video').hidden=true;}target.append(element);void element.play().catch(()=>notify('点击“开启声音”播放直播。'));});room.on(RoomEvent.TrackUnsubscribed,track=>track.detach().forEach(el=>el.remove()));}
async function connectProgram(turn:number){if(!current)return;const id=current.broadcast;const c=await api<{media_url:string;media_token:string}>(`broadcasts/${id}/connect`,{});if(turn!==generation)return;const room=new Room({adaptiveStream:true,dynacast:true});program=room;attach(room,$('video-stage'));room.on(RoomEvent.Disconnected,()=>{if(turn!==generation||snapshot?.broadcast.state==='ended')return;notify('连接中断，正在重新连接…');reconnectTimer=setTimeout(()=>void guarded(async()=>{await connectProgram(turn);}),2000);});await room.connect(c.media_url,c.media_token);if(turn!==generation){await room.disconnect();return;}notify('');}
async function eventStream(turn:number) {
  if(!current)return;
  try {
    const next=await control.eventSocket(current.broadcast,resourceCredential(current.broadcast),lastEvent);
    if(turn!==generation){next.close();return;}
    socket=next;
    next.onopen=()=>{$('chat-state').textContent='已连接';};
    next.onmessage=(message)=>{
      if(turn!==generation)return;
      try {
        const value=JSON.parse(message.data);
        const incoming:ResourceEvent[]=Array.isArray(value)?value:value.items??[value];
        const batch=acceptEvents(lastEvent,incoming.filter(event=>typeof event.sequence==='number').map(eventView));
        lastEvent=batch.last;
        for(const event of batch.events){
          const p=event.payload;
          if(event.kind==='chat'||event.kind==='gift'){
            const line=document.createElement('div');line.className=event.kind==='gift'?'gift-message':'chat-message';
            const name=document.createElement('strong');name.textContent=String(p.name??'');
            const text=document.createElement('span');text.textContent=event.kind==='gift'?`送出 ${p.points} 分礼物`:String(p.text??'');
            line.append(name,text);messages.append(line);while(messages.children.length>200)messages.firstElementChild?.remove();
            messages.scrollTop=messages.scrollHeight;
          }
          if(event.kind==='kick'&&p.identity===user?.id){notify('你已被移出直播间');void leave();return;}
        }
        void refreshSnapshot();
      } catch {notify('事件数据无法解析，正在等待重新同步。');}
    };
    next.onclose=()=>{
      if(turn!==generation||snapshot?.broadcast.state==='ended')return;
      $('chat-state').textContent='重连中';setTimeout(()=>{if(turn===generation)void eventStream(turn);},1500);
    };
  } catch(error) {
    if(turn===generation){notify(error instanceof Error?error.message:'事件连接失败');setTimeout(()=>{if(turn===generation)void eventStream(turn);},1500);}
  }
}
async function join(room:LiveRoom){await leave();current=room;const turn=generation;$('title').textContent=room.title;messages.replaceChildren();lastEvent=0;if(!room.broadcast){$('status').textContent='未开播';return;}await watch(room.broadcast);snapshot=await api<Snapshot>(`broadcasts/${room.broadcast}/snapshot`);if(turn!==generation)return;renderSnapshot();void eventStream(turn);await connectProgram(turn);await loadWallet();}
async function loadWallet(){if(!user){$('wallet').textContent='登录后每天获得 100 免费积分';return;}const wallet=await api<{remaining:number}>('wallet');$('wallet').textContent=`今日剩余 ${wallet.remaining} 免费积分 · 每日重置，不累计`;}
async function refreshSnapshot(){if(refreshing||!current?.broadcast)return;refreshing=true;const turn=generation;try{const s=await api<Snapshot>(`broadcasts/${current.broadcast}/heartbeat`,{connected:true});if(turn!==generation)return;snapshot=s;renderSnapshot();if(s.broadcast.state==='ended'){socket?.close();await stopStage();await program?.disconnect();return;}const guest=s.guests.find(g=>g.user_id===user?.id);if(guest?.state==='approved'&&!stage&&!stageConnecting){$('guest-status').textContent='已批准，请点击进入连麦';if(!$('guest-join')){const b=button('进入连麦',joinStage);b.id='guest-join';$('guest-panel').prepend(b);}}if(guest?.state!=='approved'){document.getElementById('guest-join')?.remove();if(stage)await stopStage();}}catch(e){notify(e instanceof Error?e.message:String(e));}finally{refreshing=false;}}
function renderSnapshot(){if(!snapshot||!current)return;const s=snapshot;$('status').textContent=stateLabel[s.broadcast.state]??s.broadcast.state;$('online').textContent=`${s.online} 人在线`;$('pk').textContent=pkLabel(s.pk);$('board').replaceChildren(...s.leaderboard.map(x=>{const li=document.createElement('li');li.textContent=`${x.name} · ${x.points} 分`;return li;}));const own=user?.id===s.broadcast.owner;$('host-controls').hidden=!own&&!user?.admin;$('request-guest').hidden=own;const guest=s.guests.find(g=>g.user_id===user?.id);if(guest?.state==='pending')$('guest-status').textContent='已申请，等待主播批准';if(guest?.state==='rejected')$('guest-status').textContent='主播暂未接受申请';if(own||user?.admin){$('guests').replaceChildren(...s.guests.filter(g=>g.state==='pending'||g.state==='approved').map(g=>{const r=row(g.name);if(g.state==='pending'){r.append(button('同意',async()=>command('guest/approve',{target:g.user_id})),button('拒绝',async()=>command('guest/reject',{target:g.user_id})));}else r.append(button('下麦',async()=>command('guest/end',{target:g.user_id})));return r;}));$('members').replaceChildren(...s.viewers.filter(v=>v.user_id!==s.broadcast.owner).map(v=>{const r=row(v.name);r.append(button(v.muted?'解除禁言':'禁言',async()=>command('mute',{target:v.identity,enabled:!v.muted})),button('移出',async()=>command('kick',{target:v.identity})));return r;}));}}
async function command(action:string,value:unknown={}){if(!current?.broadcast)throw new Error('当前没有直播');await api(`broadcasts/${current.broadcast}/${action}`,value);await refreshSnapshot();}
async function joinStage(){if(!current||!user||stageConnecting)return;stageConnecting=true;const turn=generation;try{const credentials=await api<{media_url:string;media_token:string}>(`broadcasts/${current.broadcast}/connect`,{stage:true});const tracks=await createLocalTracks({audio:{echoCancellation:true,noiseSuppression:true},video:{resolution:{width:640,height:360,frameRate:30}}});if(turn!==generation){tracks.forEach(t=>t.stop());return;}localTracks=tracks;const room=new Room();stage=room;attach(room,$('guest-media'));await room.connect(credentials.media_url,credentials.media_token);for(const track of tracks){await room.localParticipant.publishTrack(track);if(track.kind===Track.Kind.Video){const preview=track.attach();preview.muted=true;$('guest-media').append(preview);}}for(const element of $('video-stage').querySelectorAll<HTMLMediaElement>('audio,video'))element.muted=true;$('leave-guest').hidden=false;$('guest-status').textContent='连麦中';document.getElementById('guest-join')?.remove();}catch(e){await stopStage();throw e;}finally{stageConnecting=false;}}
async function management(){
  const capabilities=await control.request<{admin_url?:string}>('/api/v1/av/capabilities');
  if(!capabilities.admin_url){$('management-status').textContent='当前服务尚未部署统一运营后台，请联系管理员。';return;}
  const destination=new URL(capabilities.admin_url,location.origin);
  if(destination.origin!==location.origin||destination.pathname!=='/apps/admin/')throw new Error('后台地址无效');
  destination.hash='av-live-studio';location.assign(destination.href);
}
async function route(){if(joining)return;joining=true;try{const hash=location.hash;const admin=hash==='#admin';const roomId=hash.startsWith('#room/')?hash.slice(6):'';$('lobby').hidden=!!roomId||admin;$('theater').hidden=!roomId;$('management').hidden=!admin;if(admin){await leave();if(!user)throw new Error('请先登录');await management();}else if(roomId){const room=roomView(await control.request('live-studio/rooms/'+encodeURIComponent(roomId)));await join(room);}else{await leave();await loadRooms();}}finally{joining=false;}}
$('admin').onclick=()=>{location.hash='admin';};$('refresh').onclick=()=>void guarded(loadRooms);
$('sound').onclick=()=>void guarded(async()=>{await program?.startAudio();await stage?.startAudio();for(const element of $('video-stage').querySelectorAll<HTMLMediaElement>('video,audio')){element.muted=!!stage;await element.play();}$('sound').textContent='声音已开启';});
$('share').onclick=()=>void guarded(async()=>{await navigator.clipboard.writeText(location.href);notify('直播间链接已复制。');});
$<HTMLFormElement>('chat-form').onsubmit=e=>{e.preventDefault();if(!authenticate())return;const input=$<HTMLInputElement>('message');const text=input.value.trim();if(text)void guarded(async()=>{await command('chat',{text});input.value='';});};
document.querySelectorAll<HTMLButtonElement>('[data-gift]').forEach(b=>b.onclick=()=>{if(!authenticate())return;b.disabled=true;void guarded(async()=>{await command('gift',{points:Number(b.dataset.gift),request_id:crypto.randomUUID()});await loadWallet();}).finally(()=>{b.disabled=false;});});
$('request-guest').onclick=()=>{if(authenticate())void guarded(async()=>command('guest/request'));};$('leave-guest').onclick=()=>void guarded(async()=>{await command('guest/end',{target:user?.id});await stopStage();});
window.addEventListener('hashchange',()=>void guarded(route));setInterval(()=>{if(current?.broadcast)void refreshSnapshot();if(snapshot?.pk)$('pk').textContent=pkLabel(snapshot.pk);},3000);

const accountPanel=mountAccountPanel(document.querySelector('header')!,()=>{clearVisitors();void guarded(async()=>{user=await api<User>('/api/v1/auth/me').catch(()=>null);syncAccount();await route();});},{registration:true,trigger:$<HTMLButtonElement>('account'),dialogId:'auth-dialog'});
observeStatus($('status'),text=>text==='直播已结束'?'neutral':text.includes('直播')?'success':'info');
