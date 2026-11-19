import math, re, html as Hm
def esc(s): return Hm.escape(s, quote=True)
CW=6.6          # px per character at 12px Inter (generous, so estimates run long)
BODYW=374       # text width inside the page margins
BUDGET=300      # px of body under the page title (the area is ~312: a little kept back)
def tlen(s): return len(Hm.unescape(re.sub(r'<[^>]+>','',s)))
def nlines(chars, width, size=12): return max(1, math.ceil(chars*CW*size/12/width))
H2=f'font-family: \'Chakra Petch\', Inter, sans-serif; font-weight: 700; font-size: 22px; letter-spacing: 0.02em; text-transform: uppercase; margin: 0; color: {INK}'
H3=f'font-family: \'Chakra Petch\', Inter, sans-serif; font-weight: 700; font-size: 13.5px; letter-spacing: 0.03em; text-transform: uppercase; margin: 0; color: {GOLD}'
NS='flex: 0 0 auto'          # nothing in a page column may shrink (a squashed box draws over the next)
class Blk:
    def __init__(s, html, h): s.html=html; s.h=h
def para(t, w=BODYW, color=None):
    c=f'; color: {color}' if color else ''
    return Blk(f'<p style="{NS}; font-size: 12px; line-height: 1.42; margin: 0{c}">{t}</p>', nlines(tlen(t), w)*12*1.42)
def head3(t): return Blk(f'<h3 style="{NS}; {H3}">{esc(t)}</h3>', 18)
def shot(k, w, h, alt, cap=None):
    img=f'<img src="{U(k)}" alt="{esc(alt)}" style="width: {w}px; height: {h}px; object-fit: cover; display: block; border: 1px solid {INK}">'
    return img, h+2
def shots(*items):          # side by side: (key, alt) pairs, equal width, 4:3
    n=len(items); gap=14; w=(BODYW-gap*(n-1))//n; h=round(w*0.75*(0.8 if n==1 else 1))
    if n==1: h=min(h,150)
    imgs=''.join(shot(k,w,h,a)[0] for k,a in items)
    return Blk(f'<div style="{NS}; display: flex; gap: {gap}px">{imgs}</div>', h+2)
def table(items, keyw=96, w=BODYW):
    tw=w-keyw-10; out=[]; hh=0
    for k,v in items:
        lines=max(nlines(tlen(v),tw), nlines(tlen(k)*1.08,keyw))
        hh+=lines*12*1.35+7
        out.append(f'<div style="{NS}; display: flex; gap: 10px; align-items: baseline; border-bottom: 1px solid #D8D2C4; padding: 3px 0"><div style="flex: 0 0 {keyw}px; font-weight: 700; font-size: 12px; line-height: 1.35">{k}</div><div style="flex: 1 1 auto; font-size: 12px; line-height: 1.35">{v}</div></div>')
    return Blk(f'<div style="{NS}; width: {w}px; display: flex; flex-direction: column">'+''.join(out)+'</div>', hh)
def cols(*blocks, gap=16):
    n=len(blocks)
    return Blk(f'<div style="{NS}; display: flex; gap: {gap}px; align-items: flex-start">'+''.join(b.html for b in blocks)+'</div>', max(b.h for b in blocks))
heights={}
def inner_page(num, title, blocks, kicker=None):
    total=sum(b.h for b in blocks)+10*(len(blocks)-1)
    heights[num]=total
    assert total<=BUDGET, 'page %d (%s) is %d px tall, %d too many' % (num, title, total, total-BUDGET)
    k=f'<div style="{NS}; font-family: \'Chakra Petch\', sans-serif; font-weight: 600; font-size: 12px; letter-spacing: 0.12em; text-transform: uppercase; color: {GOLD}">{esc(kicker)}</div>' if kicker else ''
    return f'''<div style="position: absolute; left: 0; top: 0; width: 8px; height: {P}px; background: {GOLD}"></div>
<div style="position: absolute; left: 40px; top: 34px; width: {BODYW}px; height: 376px; display: flex; flex-direction: column; gap: 10px">
{k}<h2 style="{NS}; {H2}">{esc(title)}</h2>
{''.join(b.html for b in blocks)}
</div>
<div style="position: absolute; left: 40px; width: {BODYW}px; bottom: 18px; display: flex; justify-content: space-between; align-items: center; font-size: 12px; color: {MUTED}">
<span style="font-family: 'Chakra Petch', sans-serif; font-weight: 600; letter-spacing: 0.1em">CUBE PSX</span><span style="font-family: 'Chakra Petch', sans-serif; font-weight: 700; color: {INK}">{num}</span>
</div>'''
pages=[]
def add(fname, title, inner, bg=PAPER): pages.append((fname, title, inner, bg))
