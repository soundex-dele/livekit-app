import {remoteText} from './protocol';

export type TextOutcome = 'accepted' | 'rejected' | 'unknown';
const reasons:Record<string,string>={
  unsupported:'被控端不支持发送文字，请更新被控端。',
  invalid_text:'被控端拒绝了文字内容，请检查字符和长度。',
  input_failed:'远端输入失败，可能只输入了部分文字。请检查远端内容，不要直接重发。',
};

// One explicit request at a time. Reliable transport submission alone is not
// an acknowledgement that the host applied the text to its input system.
export class TextDelivery {
  private nextId=0;
  private pending:{id:number;timer:ReturnType<typeof setTimeout>}|undefined;
  constructor(private completed:(outcome:TextOutcome,message:string)=>void,private timeoutMs=5000){}
  get busy(){return this.pending!==undefined;}
  begin(value:string){
    if(this.busy)throw Error('正在等待上一次文字发送结果。');
    const text=remoteText(value);
    if(this.nextId>=Number.MAX_SAFE_INTEGER)throw Error('请重新连接后发送文字。');
    const id=++this.nextId;
    this.pending={id,timer:setTimeout(()=>{
      this.cancel();this.completed('unknown','未收到文字发送确认，连接已停止。文字可能已输入，请检查远端内容，不要直接重发。');
    },this.timeoutMs)};
    return {type:'text',text_id:id,text};
  }
  receive(result:unknown){
    if(!this.pending||typeof result!=='object'||result===null)return;
    const r=result as Record<string,unknown>;
    if(r.id!==this.pending.id||typeof r.accepted!=='boolean'||typeof r.reason!=='string')return;
    this.cancel();
    this.completed(r.accepted?'accepted':'rejected',r.accepted
      ?'文字已送入远端输入系统，请在画面中确认。'
      :(reasons[r.reason]||'被控端未确认文字输入，请检查远端内容。'));
  }
  cancel(){const pending=this.pending;this.pending=undefined;if(pending)clearTimeout(pending.timer);return !!pending;}
}
