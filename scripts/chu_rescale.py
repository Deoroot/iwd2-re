#!/usr/bin/env python3
"""Type-aware CHU (CHUI V1) coordinate rescaler for the 3.6x HD-UI track (Option A:
pre-scaled CHU + engine 1x mode). Multiplies ONLY coordinate fields by `factor`,
leaving file layout / offsets / table / resrefs / frame-indices / counts byte-identical
(same file size). Struct map validated against iwd2-re/BalDataTypes.h; nType@0x0c agrees
with control-table size for every control in the stock set.

Coordinate fields scaled (pack(2), offsets within each struct):
  PANEL  (0x1c): x@+4 y@+6 w@+8 h@+0xa
  CONTROL base : x@+4 y@+6 w@+8 h@+0xa   (ALL types)
  +nType 0 BUTTON : text anchor combinedX=(xHigh<<8|xLow)@0x19/0x1b, Y=@0x1d/0x1f
  +nType 2 SLIDER : knobOffX@0x24 knobOffY@0x26 knobJumpW@0x28 trackMinY@0x2c
                    trackMaxY@0x2e trackMinX@0x30 trackMaxX@0x32  (SHORT; skip jumpCount@0x2a)
  +nType 3 EDIT   : caret x@0x32 y@0x34
  nType 5/6/7 (TEXTDISPLAY/LABEL/SCROLLBAR): base only
"""
import struct, sys, os, argparse

NT_NAME={0:'BUTTON',2:'SLIDER',3:'EDIT',5:'TEXTDISPLAY',6:'LABEL',7:'SCROLLBAR'}
NT_SIZE={0:0x20,2:0x34,3:0x6a,5:0x2e,6:0x24,7:0x28}

def scale_u16(b,o,f):  # unsigned WORD, clamp 0..0xFFFF
    v=struct.unpack_from('<H',b,o)[0]
    nv=int(round(v*f))
    if nv>0xFFFF: nv=0xFFFF
    struct.pack_into('<H',b,o,nv); return v,nv

def scale_s16(b,o,f):  # signed SHORT
    v=struct.unpack_from('<h',b,o)[0]
    nv=int(round(v*f))
    nv=max(-0x8000,min(0x7fff,nv))
    struct.pack_into('<h',b,o,nv); return v,nv

def scale_button_anchor(b,base,lo_off,hi_off,f):
    lo=b[base+lo_off]; hi=b[base+hi_off]
    v=(hi<<8)|lo
    nv=min(0xFFFF,int(round(v*f)))
    b[base+lo_off]=nv & 0xFF; b[base+hi_off]=(nv>>8)&0xFF
    return v,nv

def rescale(data, factor, fname="", verbose=False):
    b=bytearray(data)
    if bytes(b[:8])!=b'CHUIV1  ':
        raise ValueError(f"{fname}: not CHUI V1")
    n=len(b)
    nPanels=struct.unpack_from('<I',b,8)[0]
    ctOff  =struct.unpack_from('<I',b,0xc)[0]
    ptOff  =struct.unpack_from('<I',b,0x10)[0]
    if ptOff+ nPanels*0x1c > n or ctOff> n:
        raise ValueError(f"{fname}: table offsets out of range (nPanels={nPanels} pt=0x{ptOff:x} ct=0x{ctOff:x} size={n})")
    stats={'panels':0,'controls':0,'mismatch':0,'by':{}}
    for pi in range(nPanels):
        p=ptOff+pi*0x1c
        for off in (4,6,8,0xa): scale_u16(b,p+off,factor)
        stats['panels']+=1
        nctrl=struct.unpack_from('<H',b,p+0xe)[0]
        first=struct.unpack_from('<H',b,p+0x18)[0]
        for ci in range(nctrl):
            te=ctOff+(first+ci)*8
            if te+8>n: raise ValueError(f"{fname}: control table entry out of range")
            coff =struct.unpack_from('<I',b,te)[0]
            csize=struct.unpack_from('<I',b,te+4)[0]
            if coff+0xe>n: raise ValueError(f"{fname}: control struct out of range @0x{coff:x}")
            nt=struct.unpack_from('<H',b,coff+0xc)[0]
            # base x/y/w/h
            for off in (4,6,8,0xa): scale_u16(b,coff+off,factor)
            # Type-specific extras dispatch on the control-table SIZE, not raw nType:
            # nType can carry high flag bits (e.g. 0x100 word-wrap on a TextDisplay -> 261),
            # so size (validated 1:1 with type across the whole stock set) is the robust key.
            SZ_NAME={0x20:'BUTTON',0x24:'LABEL',0x28:'SCROLLBAR',0x2e:'TEXTDISPLAY',0x34:'SLIDER',0x6a:'EDIT'}
            if csize==0x20:      # BUTTON anchors
                scale_button_anchor(b,coff,0x19,0x1b,factor)
                scale_button_anchor(b,coff,0x1d,0x1f,factor)
            elif csize==0x34:    # SLIDER
                for off in (0x24,0x26,0x28,0x2c,0x2e,0x30,0x32): scale_s16(b,coff+off,factor)
            elif csize==0x6a:    # EDIT caret
                for off in (0x32,0x34): scale_u16(b,coff+off,factor)
            elif csize not in SZ_NAME:
                stats['mismatch']+=1
                if verbose: print(f"  ! {fname} ctrl@0x{coff:x} UNKNOWN size=0x{csize:x} nType={nt} (base-only scaled)")
            stats['controls']+=1
            key=SZ_NAME.get(csize,f'?0x{csize:x}')
            stats['by'][key]=stats['by'].get(key,0)+1
    return bytes(b),stats

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--factor',type=float,default=3.6)
    ap.add_argument('--out',required=True,help='output dir')
    ap.add_argument('--verbose',action='store_true')
    ap.add_argument('chu',nargs='+')
    a=ap.parse_args()
    os.makedirs(a.out,exist_ok=True)
    tot={'files':0,'controls':0,'mismatch':0}
    for path in a.chu:
        data=open(path,'rb').read()
        try:
            out,st=rescale(data,a.factor,os.path.basename(path),a.verbose)
        except ValueError as e:
            print(f"SKIP {e}"); continue
        op=os.path.join(a.out,os.path.basename(path))
        open(op,'wb').write(out)
        assert len(out)==len(data),"size changed!"
        tot['files']+=1; tot['controls']+=st['controls']; tot['mismatch']+=st['mismatch']
        print(f"{os.path.basename(path):14s} panels={st['panels']:2d} controls={st['controls']:3d} {st['by']} mism={st['mismatch']}")
    print(f"\nDONE factor={a.factor} files={tot['files']} controls={tot['controls']} mismatches={tot['mismatch']} -> {a.out}")

if __name__=='__main__':
    main()
