"""Authoring source for The Drowned Bell. Writes standard Quake MAP, never compiles."""
from pathlib import Path
import math, json, struct

ROOT = Path(__file__).resolve().parent
WADS = Path(r'C:\Users\thele\Downloads\quake_map_source-master\gfx')
world, entities, bounds, collision = [], [], [], []
STONE = 'city4_2'
TRIM = 'cop1_1'
FLOOR = 'afloor1_4'
DARK = 'wood1_1'

def fmt(p):
    return '( ' + ' '.join(f'{v:g}' for v in p) + ' )'

def poly(points, faces, tex=STONE, dest=None):
    dest = world if dest is None else dest
    lines = []
    center = [sum(p[a] for p in points)/len(points) for a in range(3)]
    for face in faces:
        a,b,c = [points[i] for i in face[:3]]
        u = [b[i]-a[i] for i in range(3)]; v = [c[i]-a[i] for i in range(3)]
        n = [u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
        # MAP plane points are clockwise viewed from outside.
        if sum(n[i]*(center[i]-a[i]) for i in range(3)) < 0: b,c=c,b
        lines.append(' '.join(map(fmt,(a,b,c)))+f' {tex} 0 0 0 1 1')
    dest.append('{\n'+'\n'.join(lines)+'\n}')
    if dest is world:
        bounds.append([[min(p[i] for p in points) for i in range(3)], [max(p[i] for p in points) for i in range(3)]])
        if not tex.startswith('*'):
            collision.append({'points':points,'faces':faces,'texture':tex,'index':len(world)})

def box(x1,y1,z1,x2,y2,z2,tex=STONE,dest=None):
    assert x2>x1 and y2>y1 and z2>z1, (x1,y1,z1,x2,y2,z2)
    poly([(x1,y1,z1),(x2,y1,z1),(x2,y2,z1),(x1,y2,z1),
          (x1,y1,z2),(x2,y1,z2),(x2,y2,z2),(x1,y2,z2)],
         [(0,1,2),(4,7,6),(0,4,5),(1,5,6),(2,6,7),(3,7,4)],tex,dest)

def prism_xy(pts,z1,z2,tex=STONE,dest=None):
    n=len(pts)
    poly([(x,y,z) for z in (z1,z2) for x,y in pts],
         [(0,1,2),(n,n+2,n+1)]+[(i,(i+1)%n,(i+1)%n+n) for i in range(n)],tex,dest)

def prism_xz(pts,y1,y2,tex=STONE,dest=None):
    n=len(pts)
    poly([(x,y,z) for y in (y1,y2) for x,z in pts],
         [(0,1,2),(n,n+2,n+1)]+[(i,(i+1)%n,(i+1)%n+n) for i in range(n)],tex,dest)

def ent(cls, origin=None, brushes=None, **props):
    d={'classname':cls}
    if origin is not None: d['origin']=' '.join(str(v) for v in origin)
    d.update({k:str(v) for k,v in props.items()})
    entities.append((d,brushes or []))
    return d

def solid_ent(cls,b,tex,**props):
    out=[]; box(*b,tex,out); return ent(cls,brushes=out,**props)

def light(x,y,z,power=240,style=None):
    props={'light':power}
    if style is not None: props['style']=style
    ent('light',(x,y,z),**props)

def torch(x,y,z):
    ent('light_flame_small_yellow',(x,y,z))
    light(x,y,z+16,200)
    box(x-10,y-10,z-40,x+10,y+10,z-24,TRIM)
    box(x-6,y-6,z-24,x+6,y+6,z-4,'metal1_6')

def column(x,y,z0,z1,r=32):
    pts=[(x+round(r*math.cos(math.pi/8+i*math.pi/4)),y+round(r*math.sin(math.pi/8+i*math.pi/4))) for i in range(8)]
    prism_xy(pts,z0,z1,'column1_2')
    box(x-r-8,y-r-8,z0,x+r+8,y+r+8,z0+24,TRIM)
    box(x-r-8,y-r-8,z1-24,x+r+8,y+r+8,z1,TRIM)

def arch_y(cx,y,base,width=192,height=240,thick=32,depth=32):
    """Faceted load-bearing arch across X, oriented along Y."""
    r=width/2; spring=base+height-r
    box(cx-r-thick,y,base,cx-r,y+depth,spring,TRIM)
    box(cx+r,y,base,cx+r+thick,y+depth,spring,TRIM)
    for i in range(8):
        a=i*math.pi/8; b=(i+1)*math.pi/8
        pts=[(round(cx+r*math.cos(a)),round(spring+r*math.sin(a))),
             (round(cx+(r+thick)*math.cos(a)),round(spring+(r+thick)*math.sin(a))),
             (round(cx+(r+thick)*math.cos(b)),round(spring+(r+thick)*math.sin(b))),
             (round(cx+r*math.cos(b)),round(spring+r*math.sin(b)))]
        prism_xz(pts,y,y+depth,TRIM)

def stair_y(x1,x2,y1,n,z0,step=16,run=32,sign=1):
    for i in range(n):
        ya=y1+sign*i*run; yb=ya+sign*run
        box(x1,min(ya,yb),z0-32,x2,max(ya,yb),z0+(i+1)*step,FLOOR)

def stair_x(y1,y2,x1,n,z0,step=16,run=32,sign=1):
    for i in range(n):
        xa=x1+sign*i*run; xb=xa+sign*run
        box(min(xa,xb),y1,z0-32,max(xa,xb),y2,z0+(i+1)*step,FLOOR)

def monster(kind,x,y,floor=0,angle=270,flags=0):
    # Quake monsters use feet at origin-24.
    return ent('monster_'+kind,(x,y,floor+24),angle=angle,spawnflags=flags)

def item(kind,x,y,floor=0,**props):
    offset=32 if kind.startswith('item_key') or kind.startswith('item_artifact') else 8
    ent(kind,(x,y,floor+offset),**props)

# Union of architectural air volumes. The complement produces a closed stepped shell,
# with true shared openings between wings and no redundant giant outside box.
air=[(0,0,-192,1024,1152,768),(-960,0,0,0,1152,576),
     (0,1152,256,1024,2176,768),(1024,256,-128,2048,1408,640),
     (256,-640,0,768,0,288),(-1152,736,256,-960,1024,448)]
xs=sorted(set([a[i] for a in air for i in (0,3)]+[-1216,2112]))
ys=sorted(set([a[i] for a in air for i in (1,4)]+[-704,2240]))
zs=sorted(set([a[i] for a in air for i in (2,5)]+[-256,832]))
solid=set()
for i in range(len(xs)-1):
 for j in range(len(ys)-1):
  for k in range(len(zs)-1):
   x=(xs[i]+xs[i+1])/2;y=(ys[j]+ys[j+1])/2;z=(zs[k]+zs[k+1])/2
   if not any(a<=x<=d and b<=y<=e and c<=z<=f for a,b,c,d,e,f in air): solid.add((i,j,k))
while solid:
 i,j,k=min(solid); i2=i+1
 while (i2,j,k) in solid:i2+=1
 j2=j+1
 while all((a,j2,k) in solid for a in range(i,i2)):j2+=1
 k2=k+1
 while all((a,b,k2) in solid for a in range(i,i2) for b in range(j,j2)):k2+=1
 for a in range(i,i2):
  for b in range(j,j2):
   for c in range(k,k2):solid.remove((a,b,c))
 tex=FLOOR if zs[k2]<=256 else STONE
 box(xs[i],ys[j],zs[k],xs[i2],ys[j2],zs[k2],tex)

# ENTRY: a compressed, torchlit gatehouse gives way to the nave's full height.
box(256,-640,0,768,-608,48,TRIM)
for x in (272,736):
 box(x,-592,0,x+16,-64,32,TRIM)
 box(x,-592,240,x+16,-64,264,TRIM)
arch_y(512,-544,0,256,240,24)
arch_y(512,-64,0,256,240,24)
for x in (336,688):torch(x,-256,136)
ent('info_player_start',(512,-480,24),angle=90)
ent('info_intermission',(512,416,400),mangle='10 90 0')
solid_ent('trigger_once',(384,-112,0,640,-64,96),'trigger',message='THE DROWNED BELL\nThe silver seal watches from above.')
item('item_shells',416,-208)

# NAVE: dry banks around a genuinely deep longitudinal cistern.
box(0,0,-192,384,1152,0,FLOOR)
box(640,0,-192,1024,1152,0,FLOOR)
box(384,0,-192,640,128,0,FLOOR)
box(384,1120,-192,640,1152,0,FLOOR)
box(384,128,-192,640,1120,-48,'*04water2')
stair_y(400,624,416,12,-192,16,24,-1)
# Cross-cistern bridge; the underside is part of the swim secret route.
box(352,480,-32,672,640,0,'woodflr1_2')
for y in (480,624):box(352,y,-56,672,y+16,-32,TRIM)
for x in (352,656):
 box(x,472,0,x+16,496,40,TRIM);box(x,624,0,x+16,648,40,TRIM)
# West and north upper galleries, with an eastern return overlook.
box(0,0,224,192,1152,256,FLOOR)
box(192,960,224,384,1152,256,FLOOR)
box(640,960,224,1024,1152,256,FLOOR)
box(384,1088,224,640,1152,256,FLOOR)
box(832,704,224,1024,960,256,FLOOR)
for y in (128,416,736,1056):
 column(160,y,0,224,28)
 if y<960:column(864,y,0,192,28)
for y in (96,448,800):
 arch_y(512,y,0,576,624,40,32)
 box(0,y,648,1024,y+32,680,DARK)
# Roof clerestory, continuous cornices, and buttress masses.
for x in (0,992):box(x,0,680,x+32,1152,720,TRIM)
for x in (224,768):box(x,0,736,x+32,1152,768,DARK)
for y in (256,640,1024):
 for x in (0,960):box(x,y,288,x+64,y+64,640,'city4_7')
# Broken clerestory skylights between the timber roof bays.
for ya,yb in ((160,384),(832,928)):
 box(320,ya,752,704,yb,768,'sky4')
 box(304,ya-16,736,320,yb+16,768,TRIM)
 box(704,ya-16,736,720,yb+16,768,TRIM)
 for yy in (ya-16,yb):box(320,yy,736,704,yy+16,768,TRIM)
# The abbey's name-giving bronze bell: open mouth, tapered octagonal shell,
# flared lip and hanging clapper, physically suspended over the bridge.
for i in range(8):
 a=math.pi/8+i*math.pi/4;b=a+math.pi/4
 def ring(r,z,t):return (round(512+r*math.cos(t)),round(608+r*math.sin(t)),z)
 pts=[ring(r,z,t) for z,ri,ro in ((464,88,112),(608,24,48)) for r in (ri,ro) for t in (a,b)]
 poly(pts,[(0,2,3),(4,5,7),(0,1,5),(2,6,7),(0,4,6),(1,3,7)],'cop3_4')
 pts2=[(round(512+r*math.cos(t)),round(608+r*math.sin(t))) for r,t in ((88,a),(128,a),(128,b),(88,b))]
 prism_xy(pts2,448,464,TRIM)
column(512,608,400,568,12)
box(504,600,608,520,616,736,'metal1_6')
box(256,584,704,768,632,736,DARK)
light(512,600,424,160)
# Gallery rail broken only where safe drops are intended.
for y1,y2 in ((0,256),(384,672),(800,960)):
 box(184,y1,256,200,y2,280,TRIM)
for x1,x2 in ((192,368),(656,832)):
 box(x1,952,256,x2,968,288,TRIM)
# Eastern wall has a low gold door and an upper return from the sluiceworks.
box(1008,0,0,1040,496,640,STONE)
box(1008,720,0,1040,1152,224,STONE)
box(1008,496,192,1040,720,640,STONE)
box(1008,720,416,1040,1152,640,STONE)
solid_ent('func_door',(1008,720,256,1040,1152,416),'adoor03_5',angle=-1,lip=0,wait=-1,sounds=3,targetname='choir_return',spawnflags=4)
solid_ent('func_door',(1008,496,0,1040,720,192),'adoor03_5',spawnflags=12,angle=-1,lip=8,wait=-1,sounds=3,message='The gold seal opens the sluiceworks.')
for y in (464,720):box(984,y,0,1048,y+32,224,'key02_2')
# Western screen wall, ground arches and an upper opening at the stair landing.
for y1,y2 in ((0,80),(304,720),(944,1152)):
 box(-16,y1,0,16,y2,224,STONE)
box(-16,0,224,16,704,576,STONE)
box(-16,992,256,16,1152,576,STONE)
box(-16,704,480,16,992,576,STONE)
for y in (96,752):
 box(-32,y-16,0,32,y,208,TRIM);box(-32,y+192,0,32,y+208,208,TRIM)
 box(-32,y,192,32,y+192,224,TRIM)
# Visible silver seal before the key; the rest of this wall is solid.
box(0,1136,256,384,1168,768,STONE)
box(640,1136,256,1024,1168,768,STONE)
box(384,1136,480,640,1168,768,STONE)
solid_ent('func_door',(384,1136,256,640,1168,480),'adoor03_3',spawnflags=20,angle=-1,wait=-1,sounds=3)
arch_y(512,1112,256,256,256,24,24)
# Glowing water alcove, visible from the upper rail and found by swimming beneath bridge.
item('item_health',504,1048,-192,spawnflags=2)
solid_ent('trigger_secret',(400,1008,-184,624,1112,-72),'trigger',message='The bell keeps its drowned offerings.')
light(512,1056,-96,200)
for x,y,z in [(288,160,152),(736,176,144),(320,848,152),(704,864,152),(80,544,384),(944,1024,392)]:torch(x,y,z)
for x,y,z,p in [(512,256,560,350),(512,720,560,350),(512,1056,560,280),(512,736,-16,120)]:light(x,y,z,p)
ent('ambient_drip',(512,832,-80))

# CLOISTER: central tomb island, two ground loops, outer ascending stair and upper choir.
box(-624,320,0,-256,640,80,'grave03_2')
box(-592,352,80,-288,608,96,TRIM)
for y in (352,512):
 box(-552,y,96,-352,y+64,144,'grave03_3')
for x in (-784,-160):
 for y in (256,672):column(x,y,0,448,32)
for y in (224,672):
 box(-960,y,448,0,y+32,480,DARK)
 arch_y(-448,y,0,384,448,24)
# 16-unit risers, 32-unit treads. Broad stair to the choir; ground route stays open.
stair_y(-928,-736,192,16,0)
box(-960,704,224,0,992,256,FLOOR)
box(-960,992,0,0,1152,256,STONE)
for x in (-880,-640,-400,-160):
 box(x,736,512,x+32,1152,544,DARK)
for x in (-688,-432,-176):
 column(x,912,256,512,24)
arch_y(-448,944,256,256,248,24)
# Altar and silver key, warm beacon at top of stairs.
box(-576,1024,256,-320,1120,288,TRIM)
box(-592,1008,256,-304,1024,272,FLOOR)
item('item_key1',-448,1064,288)
light(-448,1056,408,300)
box(-568,1144,320,-328,1152,480,'window02_1')
# Secret 2: a visibly cracked demon panel at the stair landing, shoot to open.
solid_ent('func_door',(-960,768,256,-928,960,432),'demc4_4',angle=-1,health=1,wait=-1,sounds=1,spawnflags=4,lip=0)
solid_ent('trigger_secret',(-1136,768,264,-992,992,368),'trigger',message='A hollow voice behind the choir.')
item('item_artifact_super_damage',-1088,880,256)
light(-1072,880,328,200)
# Secret 3: red armor visible on a tomb canopy; drop from upper choir onto stone ribs.
box(-640,352,224,-576,688,256,TRIM)
box(-640,352,224,-256,416,256,TRIM)
box(-608,368,256,-544,432,272,'metal1_6')
item('item_armorInv',-552,376,272)
solid_ent('trigger_secret',(-576,344,264,-480,432,344),'trigger',message='The forgotten guard of the abbey.')
for x,y,z in [(-144,176,144),(-672,688,152),(-848,816,400),(-144,832,400)]:torch(x,y,z)
light(-448,448,480,350)
item('weapon_nailgun',-672,192)
item('item_armor1',-832,96)
item('item_spikes',-640,144)
item('item_health',-128,576)
item('item_shells',-176,736)
item('item_health',-640,896,256)
item('item_spikes',-272,1024,256)

# SANCTUARY: a raised cruciform choir, screened approaches, and an altar above a drop.
box(0,1152,256,192,1408,416,STONE)
box(832,1152,256,1024,1408,416,STONE)
for x in (224,768):
 for y in (1456,1840):column(x,y,256,656,36)
for y in (1408,1792,2112):arch_y(512,y,256,512,416,32)
box(0,1664,256,320,1696,368,STONE)
box(704,1664,256,1024,1696,368,STONE)
# Altar stair and pedestal.
stair_y(352,672,1856,4,256)
box(288,1984,256,736,2176,320,FLOOR)
box(448,2048,320,576,2144,344,'metal5_6')
box(432,2032,320,592,2048,332,TRIM)
item('item_key2',512,2080,344)
box(384,2160,400,640,2176,592,'window03')
world[-1]=world[-1].replace('window03 0 0 0 1 1','window03 -384 592 0 1 1')
for x in (368,640):box(x,2144,384,x+16,2176,608,TRIM)
for z in (384,592):box(384,2144,z,640,2176,z+16,TRIM)
light(512,2048,480,300)
# Nearby control opens the conspicuous stone sluice below the nave's north gallery.
solid_ent('func_button',(696,2056,352,720,2120,416),'+0button',angle=0,target='bell_return',wait=-1,sounds=1,message='The bell platform descends. Return beneath the silver seal.')
for x,y,z in [(112,1536,408),(912,1536,408),(112,2000,408),(912,2000,408)]:torch(x,y,z)
light(512,1600,656,360)
item('weapon_grenadelauncher',512,1328,256)
item('item_rockets',624,1408,256)
item('item_shells',112,1760,256)
item('item_health',864,1776,256)
item('item_health',160,1984,256)
item('item_armor2',848,2080,256)
# Secret 4: inspect the recess behind the altar's asymmetric side buttress.
box(736,1936,320,768,2128,448,STONE)
solid_ent('trigger_secret',(800,2120,264,1008,2160,352),'trigger',message='Behind the last prayer.')
item('weapon_rocketlauncher',912,2136,256)
item('item_rockets',816,2136,256)

# Transformation: a stone lift in the nave, starts at top and becomes a permanent
# stair-free link after the sanctuary button. It lowers through the gallery gap.
# A func_door mover is deterministic, has no cycling platform soft-lock.
solid_ent('func_door',(384,960,224,640,1088,256),'city4_7',angle=-2,lip=-224,speed=48,wait=-1,targetname='bell_return',spawnflags=4,sounds=3)
# It lowers 256 units into a second cross-cistern bridge, exposing the choir opening.
solid_ent('trigger_once',(416,960,264,608,1088,352),'trigger',message='The flooded stair returns to the nave below.')

# SLUICEWORKS: asymmetric lower trench, island cover, a dog-leg ascending route,
# and a final upper balcony that looks back into the nave.
box(1040,256,-128,1376,1408,0,'metal1_6')
box(1696,256,-128,2048,1408,0,'metal1_6')
box(1376,256,-128,1696,416,0,FLOOR)
box(1376,416,-128,1696,1216,-48,'*04water2')
box(1376,1216,-128,1696,1408,0,FLOOR)
box(1344,560,-32,1728,720,0,'woodflr1_2')
stair_y(1408,1664,1056,8,-128,16,24)
# Tall alternating machinery cover without sealing the flanks.
for x,y in ((1280,400),(1824,480),(1264,864),(1824,992)):
 box(x-48,y-48,0,x+48,y+48,224,'metal5_6')
 box(x-64,y-64,0,x+64,y+64,24,TRIM)
 box(x-64,y-64,224,x+64,y+64,248,TRIM)
for y in (320,768,1312):
 box(1040,y,544,2048,y+32,576,DARK)
 arch_y(1536,y,0,576,512,32)
# North-east stair goes up alongside combat floor to the return balcony.
stair_y(1856,2048,704,16,0)
box(1696,1216,224,2048,1408,256,FLOOR)
solid_ent('trigger_once',(1856,1232,264,2040,1304,352),'trigger',target='choir_return',message='The upper grate opens onto the nave.')
box(1040,1216,224,1696,1408,256,FLOOR)
box(1040,960,224,1216,1216,256,FLOOR)
box(1200,992,256,1216,1216,288,TRIM)
for x in (1264,1600):column(x,1296,256,544,24)
# The exit shrine is on the upper western lip, overlooking the first gold door.
box(1040,784,256,1216,960,272,TRIM)
box(1040,768,256,1072,960,464,'metal5_6')
box(1184,768,256,1216,960,464,'metal5_6')
box(1072,768,432,1184,960,464,'metal5_6')
box(1072,776,272,1184,792,432,'*teleport')
solid_ent('trigger_changelevel',(1072,800,272,1184,848,416),'trigger',map='start')
box(1072,800,464,1184,832,496,'exit02_3')
light(1128,896,360,240)
solid_ent('trigger_once',(1048,1040,264,1200,1136,368),'trigger',message='The bell is silent. The way home is open.')
for x,y,z in [(1120,352,160),(1952,352,160),(1136,832,168),(1760,1152,160),(1536,1344,400)]:torch(x,y,z)
for x,y,z in ((1536,560,432),(1568,1056,448)):light(x,y,z,360)
ent('ambient_drone',(1536,864,320))
item('weapon_supernailgun',1200,672)
item('item_spikes',1144,752)
item('item_spikes',1760,672)
item('item_shells',1952,640)
item('item_health',1760,880)
item('item_health',1184,1120)
item('item_rockets',1952,1328,256)
item('item_health',1520,1344,256)
item('item_shells',1328,1328,256)

# Carefully staged inhabitants. Ambush flag keeps gallery sentries local until seen.
monster('knight',288,320,angle=270)
monster('knight',736,432,angle=180)
monster('ogre',96,800,256,270,1)
monster('wizard',688,864,360,270,1|256)
monster('knight',736,816,angle=270,flags=256)
item('weapon_supershotgun',256,704)
item('item_shells',256,768)
item('item_health',736,224)
item('item_health',768,928)
monster('knight',-256,176,angle=0)
monster('knight',-576,160,angle=0)
monster('ogre',-480,688,0,270,1)
monster('knight',-192,496,angle=270,flags=256)
monster('demon1',-680,576,0,270,1)
monster('wizard',-448,480,280,270,1|256)
monster('knight',-800,864,256,270,1)
monster('hell_knight',-352,912,256,180,1)
monster('ogre',-112,1040,256,180,1|256)
monster('knight',-512,944,256,270,1|512|256)
monster('ogre',784,1056,256,180,1)
monster('knight',368,1456,256,270,1)
monster('knight',672,1488,256,270,1)
monster('ogre',112,1632,256,270,1)
monster('ogre',880,1888,256,270,1|256)
monster('wizard',512,1776,480,270,1)
monster('wizard',768,1968,496,270,1|256)
monster('hell_knight',512,1808,256,270,1)
monster('demon1',896,1552,256,180,1|256)
monster('shambler',512,1984,320,270,1|256)
monster('hell_knight',512,1984,320,270,1|512|1024)
monster('knight',1280,576,0,180,1)
monster('hell_knight',1728,624,0,180,1)
monster('ogre',1440,1280,256,270,1)
monster('ogre',1968,1296,256,270,1|256)
monster('demon1',1792,816,0,270,1)
monster('wizard',1552,864,320,180,1)
monster('wizard',1632,1056,384,180,1|256)
monster('shambler',1760,1104,0,270,1|256)
monster('hell_knight',1760,1104,0,270,1|512|1024)
monster('knight',1216,1032,0,270,1)
monster('ogre',1376,1344,256,0,1)
monster('demon1',1536,1280,256,0,1|512|256)

def write_entity(props, brushes):
    return '{\n'+'\n'.join('"'+k+'" "'+v.replace('"',"'")+'"' for k,v in props.items())+'\n'+'\n'.join(brushes)+'\n}'

props={'classname':'worldspawn','message':'The Drowned Bell','worldtype':'0','sounds':'5',
       'wad':';'.join(str(WADS/n).replace('\\','/') for n in ('medieval.wad','wizard.wad','metal.wad')),
       '_tb_textures':';'.join(str(WADS/n).replace('\\','/') for n in ('medieval.wad','wizard.wad','metal.wad')),
       '_tb_def':'builtin:Quake.fgd','light':'20','_sunlight':'120','_sun_mangle':'110 -65 0'}
text='// Game: Quake\n// Format: Standard\n// The Drowned Bell -- original single-player level\n'
text+=write_entity(props,world)+'\n'+'\n'.join(write_entity(p,b) for p,b in entities)+'\n'
(ROOT/'drowned_bell.map').write_text(text,encoding='ascii')
stats={'world_brushes':len(world),'brush_entities':sum(bool(b) for p,b in entities),'point_entities':sum(not b for p,b in entities),
       'secrets':sum(p['classname']=='trigger_secret' for p,b in entities)}
for skill,mask in [('easy',256),('normal',512),('hard',1024)]:
 stats[skill+'_monsters']=sum(p['classname'].startswith('monster_') and not(int(p.get('spawnflags',0))&mask) for p,b in entities)
(ROOT/'qa'/'design_stats.json').write_text(json.dumps(stats,indent=2))
(ROOT/'qa'/'geometry.json').write_text(json.dumps({'brushes':collision,'entities':[p for p,b in entities if not b]}))
print(json.dumps(stats,indent=2))
