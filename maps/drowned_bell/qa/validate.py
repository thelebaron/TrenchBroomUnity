"""Non-compiling source checks: convexity, entity clearance, links, materials."""
from pathlib import Path
import json,math,re,struct
root=Path(__file__).resolve().parents[1]
data=json.loads((root/'qa/geometry.json').read_text())
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def sub(a,b):return [x-y for x,y in zip(a,b)]
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
brushes=[];errors=[]
for b in data['brushes']:
 p=b['points']; c=[sum(v[i] for v in p)/len(p) for i in range(3)];planes=[]
 for f in b['faces']:
  a,v,w=[p[i] for i in f[:3]]; n=cross(sub(v,a),sub(w,a)); ln=math.sqrt(dot(n,n))
  if ln<0.01:errors.append(['degenerate',b['index']]);continue
  n=[v/ln for v in n]
  if dot(n,sub(c,a))<0:n=[-v for v in n]
  d=dot(n,a);planes.append((n,d))
  if any(dot(n,v)<d-.01 for v in p):errors.append(['nonconvex',b['index']])
 brushes.append((b,planes))
for e in data['entities']:
 cls=e['classname']
 if not (cls.startswith('monster_') or cls.startswith('item_') or cls.startswith('weapon_') or cls=='info_player_start'):continue
 o=list(map(float,e['origin'].split()))
 if cls.startswith('monster_') or cls=='info_player_start':
  r=32 if cls in ['monster_ogre','monster_shambler','monster_demon1'] else 16
  mn=[-r,-r,-24];mx=[r,r,64 if r==32 else 40]
 elif cls.startswith('item_key') or cls.startswith('item_artifact'):mn=[-16,-16,-24];mx=[16,16,32]
 elif cls in ['item_health','item_shells','item_spikes','item_rockets']:mn=[0,0,0];mx=[32,32,56]
 else:mn=[-16,-16,0];mx=[16,16,56]
 center=[o[i]+(mn[i]+mx[i])/2 for i in range(3)]; half=[(mx[i]-mn[i])/2 for i in range(3)]
 hits=[]
 for b,planes in brushes:
  if all(dot(n,center)+dot([abs(v) for v in n],half)>d+.05 for n,d in planes):hits.append(b['index'])
 if hits:errors.append(['entity_overlap',cls,e['origin'],hits])
text=(root/'drowned_bell.map').read_text()
targetnames=set(re.findall(r'"targetname" "([^"]+)"',text))
for t in re.findall(r'"target" "([^"]+)"',text):
 if t not in targetnames:errors.append(['unresolved_target',t])
materials=set(re.findall(r'\)\s+([^\s]+) 0 0 0 1 1',text))
available=set()
for f in Path(r'C:\Users\thele\Downloads\quake_map_source-master\gfx').glob('*.wad'):
 d=f.read_bytes();_,n,o=struct.unpack_from('<4sii',d)
 available.update(struct.unpack_from('<iiiBBBB16s',d,o+i*32)[-1].split(b'\0')[0].decode() for i in range(n))
for t in materials-available:errors.append(['missing_texture',t])
print(json.dumps({'issues':errors,'structural_brushes_checked':len(brushes),'materials_checked':len(materials)},indent=2))
(root/'qa/source_checks.json').write_text(json.dumps(errors,indent=2))
