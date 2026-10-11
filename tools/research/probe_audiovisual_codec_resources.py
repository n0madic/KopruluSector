#!/usr/bin/env python3
"""Separate host FFmpeg resource-decoding proof; never original-code evidence."""
import argparse,hashlib,json,platform,struct,subprocess,sys
from pathlib import Path
import inspect_native_audiovisual_117 as source
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for key in ('assets','asset-manifest','ffmpeg','ffprobe','out'):p.add_argument('--'+key,type=Path,required=True)
 p.add_argument('--primary-source-manifest',type=Path,required=True);a=p.parse_args();am=json.loads(a.asset_manifest.read_text());rows=[]
 for path,resource in am['assets'].items():
  if not path.lower().endswith(('.wav','.smk')):continue
  file=a.assets/path.replace('\\','/');raw=file.read_bytes();assert sha(raw)==resource['sha256'];kind='smk' if path.lower().endswith('.smk') else 'wav'
  probe=subprocess.run([str(a.ffprobe),'-v','error','-count_frames','-show_entries','stream=codec_name,width,height,sample_rate,channels,r_frame_rate,nb_read_frames,duration','-of','json',str(file)],check=True,capture_output=True,timeout=30);streams=json.loads(probe.stdout)['streams']
  if kind=='smk':
   header=source.smk(raw);assert len(streams)==1 and streams[0]['codec_name']=='smackvideo';n=int(streams[0]['nb_read_frames']);assert n==header['table_frames'];width,height=header['width'],header['height'];assert (streams[0]['width'],streams[0]['height'])==(width,height)
   cmd=[str(a.ffmpeg),'-nostdin','-v','error','-fflags','+bitexact','-flags','+bitexact','-threads','1','-i',str(file),'-map','0:v:0','-an','-pix_fmt','pal8','-f','rawvideo','pipe:1'];run=subprocess.run(cmd,check=True,capture_output=True,timeout=30);pixels=run.stdout;stride=width*height+1024;assert len(pixels)==n*stride and len(pixels)<16*1024*1024
   checks=dict(frames=n,indexed_bytes=n*width*height,palette_bytes=n*1024,indexed_frames_sha256=sha(b''.join(pixels[i*stride:i*stride+width*height] for i in range(n))),palette_frames_sha256=sha(b''.join(pixels[i*stride+width*height:(i+1)*stride] for i in range(n))),frame_index_sha256=[sha(pixels[i*stride:i*stride+width*height]) for i in range(n)],frame_palette_sha256=[sha(pixels[i*stride+width*height:(i+1)*stride]) for i in range(n)],raw_output_bytes=len(pixels),raw_output_sha256=sha(pixels))
  else:
   header=source.wav(raw);assert header['format'][:2]==[1,1] and header['format'][5]==16;assert len(streams)==1 and streams[0]['codec_name']=='pcm_s16le'
   cmd=[str(a.ffmpeg),'-nostdin','-v','error','-fflags','+bitexact','-flags','+bitexact','-threads','1','-i',str(file),'-map','0:a:0','-vn','-acodec','pcm_s16le','-f','s16le','pipe:1'];run=subprocess.run(cmd,check=True,capture_output=True,timeout=30);decoded=run.stdout;expected=raw[header['data_offset']:header['data_offset']+header['data_bytes']];assert decoded==expected
   checks=dict(decoded_pcm_bytes=len(decoded),decoded_pcm_sha256=sha(decoded),matches_riff_payload=True,sample_count=len(decoded)//2,sample_rate=header['format'][2],host_duration_ms=header['pcm_duration_ms'])
  rows.append(dict(path=path,input_sha256=sha(raw),host_codec=streams,checks=checks,passed=True,original_code_executed=False))
 binaries={}
 for label,binary in (('ffmpeg',a.ffmpeg),('ffprobe',a.ffprobe)):
  resolved=binary.resolve();version=subprocess.run([str(binary),'-version'],check=True,capture_output=True,text=True).stdout;binaries[label]=dict(binary_sha256=sha(resolved.read_bytes()),version=version.splitlines()[0],configuration=next((x for x in version.splitlines() if x.startswith('configuration:')),None))
 result=dict(original_code_executed=False,complete_game_executed=False,evidence_layer='Host third-party decoding of hash-verified original resources',probe_script_sha256=sha(Path(__file__).read_bytes()),inspector_sha256=sha(Path(source.__file__).read_bytes()),asset_manifest_sha256=sha(a.asset_manifest.read_bytes()),runtime_architecture=platform.machine(),python_version=sys.version,binaries=binaries,primary_algorithm_sources=json.loads(a.primary_source_manifest.read_text()),primary_source_manifest_sha256=sha(a.primary_source_manifest.read_bytes()),host_resource_checks=len(rows),resources=rows,limits=['FFmpeg decoding is independent host evidence; original Smacker calls never execute','Pinned n8.0 source explains file structure; installed FFmpeg version is independently recorded and not asserted to equal that source','No actual display, audio device playback, original palette upload, skip/clock/synchronization or complete UI proof','Only selected 20 SMK and 15 WAV resources; no entire archive codec coverage','No retail media payload or decoded pixels/audio are published; hashes only'])
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(host_resource_checks=len(rows),smk=sum(r['path'].endswith('.smk') for r in rows),wav=sum(r['path'].endswith('.WAV') for r in rows),probe_script_sha256=result['probe_script_sha256'])))
if __name__=='__main__':main()
