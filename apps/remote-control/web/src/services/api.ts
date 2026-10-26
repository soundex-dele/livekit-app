export async function request(path:string,body?:unknown,token=''){
  const response=await fetch('/api/v1/remote-control/'+path,{method:body===undefined?'GET':'POST',headers:{'Content-Type':'application/json',...(token?{Authorization:'Bearer '+token}:{})},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(5000)});
  const value=await response.json();if(!response.ok)throw new Error(value.error?.code||`HTTP ${response.status}`);return value;
}
