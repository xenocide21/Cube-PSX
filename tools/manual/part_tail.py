# ---------- tray inlay (back of the case) 151x118 mm: every block placed and sized, nothing left to flow ----------
IW,IH,SP=571,446,25
def spine(left):
    rot='-90' if left else '90'
    return f'<div style="position: absolute; {"left" if left else "right"}: 0; top: 0; width: {SP}px; height: {IH}px; background: #000; border-{"right" if left else "left"}: 1px dashed #5A5650; box-sizing: border-box"></div><div style="position: absolute; {"left" if left else "right"}: 0; top: 0; width: {SP}px; height: {IH}px; display: flex; align-items: center; justify-content: center"><div style="transform: rotate({rot}deg); white-space: nowrap; font-family: \'Chakra Petch\', sans-serif; font-weight: 700; font-size: 12px; letter-spacing: 0.2em; color: {GOLDL}">CUBE PSX · v0.23</div></div>'
feat=['23 single-player maps, 4 campaigns','Up to 7 bots, 4 match types','Facing Cubes: temples in space','2 players by link cable','Giga Chad or Giga Stacy']
fl=''.join(f'<div style="display: flex; gap: 6px; font-size: 12px; line-height: 16px; white-space: nowrap"><span style="color: {GOLDL}; font-weight: 700">›</span><span>{esc(x)}</span></div>' for x in feat)
L=SP+40; CWID=IW-2*SP-80        # the back panel's text column: 441 px
sw=(CWID-20)//3; sh=round(sw*0.75)
badges=''.join(f'<span style="flex: 0 0 auto; font-size: 12px; line-height: 18px; border: 1px solid {GOLDL}; padding: 0 7px; color: #fff">{b}</span>' for b in ['1–2 PLAYERS','MEMORY CARD 2 BLOCKS','ANALOG','LINK CABLE'])
tray=f'''<div style="position: absolute; left: {SP}px; top: 0; width: {IW-2*SP}px; height: {IH}px; background: {DARK}"></div>
{spine(True)}{spine(False)}
<img src="{U('logo')}" alt="The Cube logo" style="position: absolute; left: {L}px; top: 28px; width: 36px; height: 36px; image-rendering: pixelated">
<div style="position: absolute; left: {L+46}px; top: 30px; width: 200px; font-family: 'Chakra Petch', sans-serif; font-weight: 700; font-size: 26px; line-height: 32px; color: #fff">CUBE PSX</div>
<div style="position: absolute; left: {L+CWID-200}px; top: 40px; width: 200px; text-align: right; font-family: 'Chakra Petch', sans-serif; font-weight: 600; font-size: 12px; line-height: 16px; letter-spacing: 0.12em; color: {GOLDL}">FIRST-PERSON SHOOTER</div>
<img src="{U('cruel01')}" alt="Cruel Part I" style="position: absolute; left: {L}px; top: 80px; width: {sw}px; height: {sh}px; object-fit: cover; border: 1px solid #555; box-sizing: border-box">
<img src="{U('space')}" alt="Facing Cubes in space" style="position: absolute; left: {L+sw+10}px; top: 80px; width: {sw}px; height: {sh}px; object-fit: cover; border: 1px solid #555; box-sizing: border-box">
<img src="{U('ctfbase')}" alt="A team match" style="position: absolute; left: {L+2*sw+20}px; top: 80px; width: {sw}px; height: {sh}px; object-fit: cover; border: 1px solid #555; box-sizing: border-box">
<p style="position: absolute; left: {L}px; top: {80+sh+14}px; width: 180px; margin: 0; font-size: 12.5px; line-height: 18px; color: #E8E3D8">Cube, Wouter van Oortmerssen’s fast, free shooter, comes to the PlayStation. Bring a friend and a link cable.</p>
<div style="position: absolute; left: {L+200}px; top: {80+sh+14}px; width: {CWID-200}px; display: flex; flex-direction: column; gap: 2px; color: #E8E3D8">{fl}</div>
<div style="position: absolute; left: {L}px; top: 318px; width: {CWID}px; display: flex; gap: 8px">{badges}</div>
<p style="position: absolute; left: {L}px; top: 350px; width: {CWID}px; margin: 0; font-size: 12px; line-height: 16px; color: #ABA597">Cube © 2001–2005 Wouter van Oortmerssen · Cube PSX © 2026 Xenocide21 · Media © its authors, see the manual · Free, not for sale · Not licensed by Sony</p>'''
add('TrayInlay.dc.html','Back inlay with spines', tray, '#000')

# ---------- disc label 117 mm, 41 mm hole: text kept inside the circle ----------
D=442; HOLE=155; C=D//2
disc=f'''<div style="position: absolute; left: 0; top: 0; width: {D}px; height: {D}px; border-radius: 50%; overflow: hidden; background: {DARK}">
<img src="{U('space')}" alt="Space" style="position: absolute; left: 0; top: 0; width: {D}px; height: {D}px; object-fit: cover; opacity: 0.45">
<div style="position: absolute; left: 0; top: 0; width: {D}px; height: {D}px; background: radial-gradient(circle at 50% 50%, rgba(18,18,22,0) 30%, rgba(18,18,22,0.85) 75%)"></div>
<div style="position: absolute; left: {C-120}px; top: 46px; width: 240px; text-align: center; font-family: 'Chakra Petch', sans-serif; font-weight: 700; font-size: 38px; line-height: 44px; color: #fff">CUBE PSX</div>
<div style="position: absolute; left: {C-120}px; top: 92px; width: 240px; text-align: center; font-family: 'Chakra Petch', sans-serif; font-weight: 600; font-size: 12px; line-height: 16px; letter-spacing: 0.12em; color: {GOLDL}">THE PLAYSTATION PORT OF CUBE</div>
<div style="position: absolute; left: {C-HOLE//2}px; top: {C-HOLE//2}px; width: {HOLE}px; height: {HOLE}px; border-radius: 50%; background: #fff; border: 1px dashed #888; box-sizing: border-box"></div>
<img src="{U('chad')}" alt="Giga Chad" style="position: absolute; left: 50px; top: 180px; width: 66px; height: 81px; image-rendering: pixelated">
<img src="{U('stacy')}" alt="Giga Stacy" style="position: absolute; left: {D-50-66}px; top: 180px; width: 66px; height: 81px; image-rendering: pixelated">
<div style="position: absolute; left: {C-110}px; top: 318px; width: 220px; text-align: center; font-size: 12px; line-height: 16px; font-weight: 700; color: #fff">v0.23 · DISC 1 OF 1</div>
<div style="position: absolute; left: {C-110}px; top: 338px; width: 220px; text-align: center; font-size: 12px; line-height: 16px; color: #ABA597">Free fan homebrew, not for sale<br>Not licensed by Sony</div>
</div>'''
add('DiscLabel.dc.html','Disc label', disc, 'transparent')

for fname,title,inner,bg in pages:
    w,h=(P,P)
    if fname=='TrayInlay.dc.html': w,h=IW,IH
    if fname=='DiscLabel.dc.html': w,h=D,D
    open(os.path.join(R,fname),'w').write(page_file(fname,title,w,h,inner,bg))

# canvas.json
boards={}; order=[]
for i,(fname,title,_,_) in enumerate(pages[:28]):
    r,c=divmod(i,6)
    boards[fname]={'x':c*(P+80),'y':r*(P+120)+300,'w':P,'h':P,'title':'%d · %s'%(i+1,title),'page':'manual'}
    order.append(fname)
boards['TrayInlay.dc.html']={'x':0,'y':300,'w':IW,'h':IH,'title':'Back inlay (151 x 118 mm, spines 6.5 mm)','page':'case'}
boards['DiscLabel.dc.html']={'x':IW+80,'y':300,'w':D,'h':D,'title':'Disc label (117 mm, 41 mm hole)','page':'case'}
order+=['TrayInlay.dc.html','DiscLabel.dc.html']
now=datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
canvas={'v':3,'createdOnFiles':{'v':1,'at':now},'title':'Cube PSX - CD manual, case inserts and disc label','launch':{'view':'canvas','page':'manual'},
 'pages':[{'id':'manual','name':'Manual booklet (120 mm pages)'},{'id':'case','name':'Back inlay and disc label'}],
 'boards':boards,'order':order,
 'notes':{'mt':{'x':0,'y':0,'text':'Cube PSX manual: 28 pages, 120 x 120 mm. Page 1 is the front cover of the case.','kind':'title1','maxW':3134,'page':'manual'},
          'ct':{'x':0,'y':0,'text':'Back of the case (tray inlay) and the disc','kind':'title1','maxW':1100,'page':'case'}},
 'designSystems':[]}
json.dump(canvas,open(os.path.join(R,'canvas.json'),'w'),indent=1)
print(len(pages),'files')
