#!/usr/bin/env python3
"""Read-only CHU (CHUI V1) inspector. Validates struct map from iwd2-re/BalDataTypes.h
before building the rescaler. Dumps panels + controls: offset, table-size, nType@0x0c,
base x/y/w/h. pack(2). Offsets in header/table are file-absolute."""
import struct, sys, glob, os

U16=lambda b,o: struct.unpack_from('<H',b,o)[0]
U32=lambda b,o: struct.unpack_from('<I',b,o)[0]

# size -> guessed type (computed from BalDataTypes.h, pack(2))
SIZE_TYPE={0x20:'BUTTON',0x24:'LABEL',0x28:'SCROLLBAR',0x2e:'TEXTDISPLAY',0x34:'SLIDER',0x6a:'EDIT'}

def inspect(path):
    b=open(path,'rb').read()
    if b[:8]!=b'CHUIV1  ':
        print(f"{path}: not CHUI V1 ({b[:8]!r})"); return
    nPanels=U32(b,0x08); ctOff=U32(b,0x0c); ptOff=U32(b,0x10)
    print(f"\n=== {os.path.basename(path)}  panels={nPanels} ctrlTable@0x{ctOff:x} panelTable@0x{ptOff:x} size={len(b)} ===")
    # control table count: spans from ctOff to ptOff (if ct precedes pt) else infer from panels
    if ptOff>ctOff:
        ctCount=(ptOff-ctOff)//8
    else:
        ctCount=None
    typehist={}
    for pi in range(nPanels):
        p=ptOff+pi*0x1c
        pid=U32(b,p); px,py,pw,ph=U16(b,p+4),U16(b,p+6),U16(b,p+8),U16(b,p+0xa)
        ptype=U16(b,p+0xc); nctrl=U16(b,p+0xe); first=U16(b,p+0x18); flags=U16(b,p+0x1a)
        mos=b[p+0x10:p+0x18].split(b'\0')[0].decode('latin1','replace')
        print(f" panel#{pi} id={pid} xywh=({px},{py},{pw},{ph}) type={ptype} nctrl={nctrl} first={first} mos={mos!r} flags=0x{flags:x}")
        for ci in range(nctrl):
            te=ctOff+(first+ci)*8
            coff=U32(b,te); csize=U32(b,te+4)
            cid=U32(b,coff); cx,cy,cw,ch=U16(b,coff+4),U16(b,coff+6),U16(b,coff+8),U16(b,coff+0xa)
            ctype=U16(b,coff+0xc)
            guess=SIZE_TYPE.get(csize,f'?sz0x{csize:x}')
            typehist[guess]=typehist.get(guess,0)+1
            print(f"    ctrl[{first+ci}] @0x{coff:x} size=0x{csize:x}({guess}) nType={ctype} id={cid} xywh=({cx},{cy},{cw},{ch})")
    print(f"  -- size->type histogram: {typehist}")

if __name__=='__main__':
    files=sys.argv[1:] or sorted(glob.glob('/home/wills/iwd2-re/data/near_infinity_export/CHU/*'))
    for f in files: inspect(f)
