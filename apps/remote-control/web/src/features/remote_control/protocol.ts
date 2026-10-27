export const topic = 'av.remote-control.v1';
export interface Grant { id:string; grant_id:string; epoch:number; state:string; host_identity:string; controller_identity:string; credential?:string; lease_remaining_ms:number }
export interface Layout { v:1; type:'layout'; grant_id:string; epoch:number; layout_version:number; width:number; height:number; active:boolean; text_input?:boolean; text_result?:{id:number;accepted:boolean;reason:string} }
export function remoteText(value:string):string {
  const text=value.replace(/\r\n?/g,'\n');
  const scalars=Array.from(text);
  if(!scalars.length)throw Error('请先输入要发送的文字。');
  if(scalars.length>256)throw Error('每次最多发送 256 个字符，请分段发送。');
  if(scalars.some(c=>{const n=c.codePointAt(0)!;return (n>=0xd800&&n<=0xdfff)||(n<32&&n!==9&&n!==10)||(n>=127&&n<=159);}))
    throw Error('文字中含有不支持的控制字符，请检查后重试。');
  return text;
}
export function point(rect:{left:number;top:number;width:number;height:number},width:number,height:number,x:number,y:number){
  if(width<=0||height<=0||rect.width<=0||rect.height<=0)return null;
  const scale=Math.min(rect.width/width,rect.height/height),w=width*scale,h=height*scale;
  const left=rect.left+(rect.width-w)/2,top=rect.top+(rect.height-h)/2;
  if(x<left||x>left+w||y<top||y>top+h)return null;
  return {x:Math.max(0,Math.min(1,(x-left)/w)),y:Math.max(0,Math.min(1,(y-top)/h))};
}
export class ControlQueue {
  private items:Record<string,unknown>[]=[];
  private move:Record<string,unknown>|null=null;
  private running=false;
  private closed=false;
  private sequence=0;
  constructor(private grant:Grant,private send:(bytes:Uint8Array)=>Promise<unknown>,private failed:(error:unknown)=>void){}
  push(event:Record<string,unknown>){
    if(this.closed)return;
    if(event.type==='move'){this.move=event;return;}
    this.flushMove();this.items.push(event);this.pump();
  }
  flushMove(){if(this.closed||!this.move)return;this.items.push(this.move);this.move=null;this.pump();}
  close(){this.closed=true;this.items=[];this.move=null;}
  async finish(){
    if(this.closed)return;
    // Discard unsent input and put stop immediately after any in-flight packet.
    // Keep the channel alive until stop has actually been submitted.
    this.move=null;this.items=[{type:'stop'}];this.pump();
    while(!this.closed&&(this.running||this.items.length))await new Promise(r=>setTimeout(r,5));
    this.close();
  }
  private pump(){
    if(this.items.length>256){this.close();this.failed(new Error('控制消息积压，连接已停止'));return;}
    if(this.running||this.closed)return;this.running=true;
    void(async()=>{try{while(this.items.length&&!this.closed){
      const item=this.items.shift()!;
      const bytes=new TextEncoder().encode(JSON.stringify({...item,v:1,grant_id:this.grant.grant_id,epoch:this.grant.epoch,seq:++this.sequence}));
      let timer:ReturnType<typeof setTimeout>|undefined;
      try{await Promise.race([this.send(bytes),new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error('控制通道发送超时')),2000);})]);}finally{clearTimeout(timer);}
    }}catch(e){this.close();this.failed(e);}finally{this.running=false;}})();
  }
}
