"""Small client driver for the documented JSONL interface; no editor internals."""
import subprocess,json,sys,math
from pathlib import Path
ROOT=Path(__file__).resolve().parent
CLIENT=ROOT.parents[1]/'build-vs18/app/Debug/TrenchBroomCommandLineClient.exe'
counter=0
def call(command,**kwargs):
 global counter
 counter+=1
 req={'id':counter,'command':command,**kwargs}
 p=subprocess.run([str(CLIENT),'--socket','drowned-bell'],input=json.dumps(req)+'\n',text=True,capture_output=True)
 if p.returncode:raise RuntimeError(p.stderr+p.stdout)
 r=json.loads(p.stdout)
 if not r['ok']:raise RuntimeError(r)
 return r['result']
if sys.argv[1]=='view':
 x,y,z,yaw,pitch=map(float,sys.argv[2:7])
 c=call('camera_move',translation=[0,0,0])
 d=c['direction'];oldyaw=math.degrees(math.atan2(d[1],d[0]));oldpitch=math.degrees(math.asin(d[2]))
 print(call('camera_move',translation=[x-c['position'][0],y-c['position'][1],z-c['position'][2]],yaw_degrees=yaw-oldyaw,pitch_degrees=pitch-oldpitch))
 if len(sys.argv)>7:print(call('capture_viewport',path=str(ROOT/'qa'/sys.argv[7]),overwrite=True))
elif sys.argv[1]=='reload':
 print(call('load_map',path=str(ROOT/'drowned_bell.map')))
else: print(call(sys.argv[1]))
