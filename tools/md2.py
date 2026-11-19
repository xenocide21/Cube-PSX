import struct
def load(path):
    d=open(path,'rb').read()
    h=struct.unpack('<17i',d[:68])
    ident,ver,sw,sh,fs,nskin,nxyz,nst,ntri,ngl,nfr,o_skin,o_st,o_tri,o_fr,o_gl,o_end=h
    assert ident==0x32504449
    st=[struct.unpack('<2h',d[o_st+4*i:o_st+4*i+4]) for i in range(nst)]
    tris=[struct.unpack('<6H',d[o_tri+12*i:o_tri+12*i+12]) for i in range(ntri)]
    frames=[]
    for f in range(nfr):
        o=o_fr+f*fs
        sc=struct.unpack('<3f',d[o:o+12]); tr=struct.unpack('<3f',d[o+12:o+24])
        name=d[o+24:o+40].split(b'\0')[0].decode('latin1')
        vs=[]
        for v in range(nxyz):
            x,y,z,n=struct.unpack('<4B',d[o+40+4*v:o+44+4*v])
            vs.append((x*sc[0]+tr[0],y*sc[1]+tr[1],z*sc[2]+tr[2]))
        frames.append((name,vs))
    return dict(sw=sw,sh=sh,st=st,tris=tris,frames=frames,nxyz=nxyz)
