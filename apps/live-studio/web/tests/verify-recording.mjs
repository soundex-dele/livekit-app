import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {resolve,join,dirname} from 'node:path';
import {writeFileSync} from 'node:fs';
const filename=resolve(process.argv[2]||'');assert.ok(process.argv[2],'Usage: node tests/verify-recording.mjs file.mp4');
const bin=process.env.STUDIO_FFMPEG_BIN||'';
function run(program,args){const result=spawnSync(bin?join(bin,program+'.exe'):program,args,{windowsHide:true,maxBuffer:256*1024*1024});assert.equal(result.status,0,String(result.stderr||result.error));return result.stdout;}
const probe=JSON.parse(run('ffprobe',['-v','error','-show_streams','-show_format','-of','json',filename]));
const video=probe.streams.find(s=>s.codec_type==='video'),audio=probe.streams.find(s=>s.codec_type==='audio');assert.equal(video?.codec_name,'h264');assert.equal(audio?.codec_name,'aac');
run('ffmpeg',['-v','error','-i',filename,'-f','null','-']);
const images=run('ffmpeg',['-v','error','-i',filename,'-an','-vf','fps=60,scale=16:16','-pix_fmt','rgb24','-f','rawvideo','-']);
const pcm=run('ffmpeg',['-v','error','-i',filename,'-vn','-ac','1','-ar','48000','-f','f32le','-']);
const markerSeconds=Number(process.env.STUDIO_MARKER_SECONDS||8);const v=[],a=[];let previous=false;
for(let i=0;i<Math.min(images.length/768,markerSeconds*60);i++){const offset=i*768,white=images[offset]>180&&images[offset+1]>180;if(white&&!previous)v.push(i/60);previous=white;}
previous=false;for(let offset=0;offset+480*4<=Math.min(pcm.length,markerSeconds*48000*4);offset+=480*4){let sum=0;for(let j=0;j<480;j++){const x=pcm.readFloatLE(offset+j*4);sum+=x*x;}const loud=Math.sqrt(sum/480)>.025;if(loud&&!previous)a.push(offset/4/48000);previous=loud;}
const offsets=v.map(t=>a.reduce((best,x)=>Math.abs(x-t)<Math.abs(best)?x-t:best,Infinity)).filter(x=>Math.abs(x)<.6);
const report={file:filename,decode:'passed',video_codec:video.codec_name,audio_codec:audio.codec_name,width:video.width,height:video.height,video_duration:video.duration,audio_duration:audio.duration,frames:video.nb_frames,average_fps:Number(video.nb_frames)/Number(video.duration),marker_measurement:{method:'pre-interaction local flash/beep onsets, 60 Hz video and 10 ms audio sampling',interval_seconds:[0,markerSeconds],video_edges:v.length,audio_edges:a.length,offsets_ms:offsets.map(x=>x*1000),max_abs_offset_ms:offsets.length?Math.max(...offsets.map(x=>Math.abs(x)*1000)):null}};
writeFileSync(filename+'.validation.json',JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
assert.ok(offsets.length>=3,'at least three matching audiovisual markers');assert.ok(report.marker_measurement.max_abs_offset_ms<=150,'recorded audiovisual offset exceeds 150 ms');
