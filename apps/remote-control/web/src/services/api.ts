import { control } from '../../../../../packages/web/control';
export async function request(path:string,body?:unknown,token=''){
  return control.request('remote-control/'+path,body,{credential:token});
}
