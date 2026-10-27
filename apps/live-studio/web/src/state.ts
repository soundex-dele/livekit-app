export interface User {id:string;name:string;admin:boolean;disabled:boolean}
export interface LiveRoom {id:string;owner:string;title:string;broadcast:string;state:string}
export interface Broadcast {id:string;room:string;owner:string;state:string;heartbeat:number}
export interface Guest {user_id:string;name:string;state:string}
export interface PK {id:string;a:string;b:string;state:string;deadline:number;score_a:number;score_b:number;winner:string}
export interface Snapshot {broadcast:Broadcast;online:number;viewers:{identity:string;user_id:string;name:string;muted:boolean}[];guests:Guest[];pk:PK|null;leaderboard:{name:string;points:number}[];seq:number}
export interface LiveEvent {seq:number;kind:string;payload:Record<string,unknown>;created:number}
export function acceptEvents(last:number,events:LiveEvent[]):{last:number;events:LiveEvent[]} {
 const ordered=[...events].sort((a,b)=>a.seq-b.seq);const result:LiveEvent[]=[];
 for(const event of ordered)if(event.seq>last){result.push(event);last=event.seq;}
 return {last,events:result};
}
export function pkLabel(pk:PK|null,now=Date.now()):string {
 if(!pk)return '暂无 PK';if(pk.state==='invited')return '等待另一位主播接受 PK';
 if(pk.state==='active'){const remaining=Math.max(0,pk.deadline-Math.floor(now/1000));return `PK ${Math.floor(remaining/60)}:${String(remaining%60).padStart(2,'0')} · ${pk.score_a} : ${pk.score_b}`;}
 return pk.state==='cancelled'?'PK 已取消':`PK 已结束 · ${pk.score_a} : ${pk.score_b}${pk.winner?'':' · 平局'}`;
}
export const stateLabel:Record<string,string>={starting:'准备开播',live:'直播中',reconnecting:'正在重连',ended:'直播已结束',offline:'未开播'};
