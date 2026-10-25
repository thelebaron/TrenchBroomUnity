from pathlib import Path
import struct
from PIL import Image,ImageDraw
p=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Quake\id1\PAK0.PAK').read_bytes()
_,o,n=struct.unpack_from('<4sii',p)
for i in range(n//64):
 name,pos,size=struct.unpack_from('<56sii',p,o+i*64)
 if name.split(b'\0')[0]==b'gfx/palette.lmp': palette=p[pos:pos+size]
names=['city4_2','city4_7','afloor1_4','cop1_1','column1_2','wood1_1','grave03_2','grave03_3','window02_1','window03','metal5_6','adoor03_3','adoor03_5','*04water2','demc4_4','ceiling1_3']
d=Path(r'C:\Users\thele\Downloads\quake_map_source-master\gfx\medieval.wad').read_bytes()
_,n,o=struct.unpack_from('<4sii',d);tx={}
for i in range(n):
 q=struct.unpack_from('<iiiBBBB16s',d,o+i*32);name=q[-1].split(b'\0')[0].decode()
 if name in names:
  _,w,h,a,b,c,e=struct.unpack_from('<16s6I',d,q[0]);im=Image.frombytes('P',(w,h),d[q[0]+a:q[0]+a+w*h]);im.putpalette(palette);tx[name]=im.convert('RGB')
out=Image.new('RGB',(640,4*184),(30,30,30));draw=ImageDraw.Draw(out)
for i,name in enumerate(names):
 x=i%4*160;y=i//4*184
 if name in tx:out.paste(tx[name].resize((152,152)),(x,y))
 draw.text((x,y+155),name,fill='white')
out.save(Path(__file__).with_suffix('.png'))
