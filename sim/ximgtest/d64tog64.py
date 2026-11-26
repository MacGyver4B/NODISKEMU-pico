#!/usr/bin/env python3
# d64tog64.py in.d64 out.g64 [t/s:what ...]   what: d (Datensync weg), h (Headersync weg),
#   hd (beide), T (ganze Spur ohne Sync, Sektor egal), c (Datenpruefsumme falsch)
import sys, struct
G=[0x0a,0x0b,0x12,0x13,0x0e,0x0f,0x16,0x17,0x09,0x19,0x1a,0x1b,0x0d,0x1d,0x1e,0x15]
def gcr(b):
    out=bytearray()
    for i in range(0,len(b),4):
        v=0
        for x in b[i:i+4]: v=(v<<10)|(G[x>>4]<<5)|G[x&15]
        out+=v.to_bytes(5,'big')
    return out
def ns(t): return 21 if t<18 else 19 if t<25 else 18 if t<31 else 17
def zone(t): return 3 if t<18 else 2 if t<25 else 1 if t<31 else 0
SIZE=[6250,6666,7142,7692]
d=open(sys.argv[1],'rb').read()
faults={}
for a in sys.argv[3:]:
    ts,w=a.split(':'); t,s=map(int,ts.split('/')); faults[(t,s if w!='T' else -1)]=w
id1,id2=d[(357)*256+0xa2],d[357*256+0xa3]   # 18/0 ID
off=0; tracks=[]
for t in range(1,36):
    tr=bytearray()
    for s in range(ns(t)):
        f=faults.get((t,s),'') + faults.get((t,-1),'')
        blk=d[(off+s)*256:(off+s+1)*256]
        hs=b'\x55'*5 if ('h' in f or 'T' in f) else b'\xff'*5
        hdr=bytes([8,s^t^id2^id1,s,t,id2,id1,15,15])
        ds=b'\x55'*5 if ('d' in f or 'T' in f) else b'\xff'*5
        ck=0
        for x in blk: ck^=x
        if 'c' in f: ck^=0xff
        db=bytes([7])+blk+bytes([ck,0,0])
        tr+=hs+gcr(hdr)+b'\x55'*9+ds+gcr(db)+b'\x55'*8
    size=SIZE[zone(t)]
    tr+=b'\x55'*(size-len(tr)); assert len(tr)==size,(t,len(tr))
    tracks.append((tr,zone(t))); off+=ns(t)
MAX=7928
hdr=b'GCR-1541'+bytes([0,84])+struct.pack('<H',MAX)
offs=[0]*84; spd=[0]*84; data=bytearray()
base=12+84*8
for i,(tr,z) in enumerate(tracks):
    offs[2*i]=base+len(data); spd[2*i]=z
    data+=struct.pack('<H',len(tr))+tr+b'\x00'*(MAX-len(tr))
open(sys.argv[2],'wb').write(hdr+b''.join(struct.pack('<I',x) for x in offs)+b''.join(struct.pack('<I',x) for x in spd)+data)
