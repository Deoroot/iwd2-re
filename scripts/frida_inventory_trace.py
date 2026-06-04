#!/usr/bin/env python3
"""Differential trace of the ORIGINAL IWD2.exe SP inventory pipeline via Frida.

IWD2.exe has no ASLR (ImageBase 0x400000), so Ghidra addresses are absolute.
Hook the inventory drag/swap + ground-pile entry points and dump their
args/returns as JSON lines. Run our own iwd2-re.exe build (with its
Iwd2DebugLog output) under the SAME in-game action, then diff the two logs.

Covers: inventory drag to equipment/quick-weapon/ground slots (the crash + the
ground-drop bugs) and ground-pile detection (the loot-window bug).

Hooks (IWD2.exe absolute == Ghidra), all __thiscall (ecx = this):
  0x62F360  CScreenInventory::SwapWithSlot(INT btn, BOOL showErr, WORD cnt, BOOL stack)
  0x5B81C0  CInfGame::SwapItemPersonal(SHORT portrait, SHORT slot, CItem*& pItem,
                                       STRREF& err, WORD cnt, BOOLEAN fromServer)
  0x5BAD70  CInfGame::SwapItemPersonalInventory(SHORT portrait, CItem*& pItem,
                                       SHORT slot, STRREF& err, WORD cnt, ...)
  0x5B7850  CInfGame::SwapItemGround(LONG container, SHORT slot, CItem*& pItem,
                                     STRREF& err, WORD cnt, BOOLEAN fromServer)
  0x717850  CGameSprite::GetItemUsages(SHORT slot, WORD type, SHORT ability) -> CGameButtonList*
  0x726810  CGameSprite::SetWeaponSet(BYTE weaponSet)
  0x626940  CScreenInventory::FetchGroundPile(SHORT portrait, BOOL evenIfDead) -> LONG
  0x5B75C0  CInfGame::GetGroundPile(LONG sprite) -> LONG

Usage:
  python scripts/frida_inventory_trace.py           # spawn IWD2.exe, hook, log
  python scripts/frida_inventory_trace.py --attach   # attach to a running IWD2.exe

Then in-game (same save + same actions as the RE build): open inventory, drag an
item onto a quick-weapon slot, onto a ground slot; click a ground pile.
Log: tmp_frida_inventory.log (repo root). Ctrl-C / kill to stop.
"""
import frida
import sys
import os
import json

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, "IWD2.exe")
LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_inventory.log")

JS = r"""
'use strict';

// IWD2.exe, no ASLR -> Ghidra addresses are absolute.
const SwapWithSlot   = ptr(0x62F360);
const SwapItemPers   = ptr(0x5B81C0);
const SwapItemPersInv= ptr(0x5BAD70);
const SwapItemGround = ptr(0x5B7850);
const GetItemUsages  = ptr(0x717850);
const SetWeaponSet   = ptr(0x726810);
const FetchGroundPile= ptr(0x626940);
const GetGroundPile  = ptr(0x5B75C0);
const AreaGetGround  = ptr(0x46AF40);   // CGameArea::GetGroundPile(const CPoint&)

const CPTRLIST_COUNT = 0xc;   // CPtrList::m_nCount

function s16(v) { return (v.toInt32() << 16) >> 16; }
function deref(p) { try { return p.readPointer(); } catch (e) { return ptr(0); } }
function derefU32(p) { try { return p.readU32(); } catch (e) { return -1; } }

// Noise control: the inventory screen polls GetItemUsages/SetWeaponSet every
// frame for all party members. Only log them while a SwapWithSlot is running,
// and dedup the per-frame ground-pile polls.
let inSwap = 0;
let lastFetch = 0x7fffffff;
let lastGGP = 0x7fffffff;
let lastArea = 0x7fffffff;

// CScreenInventory::SwapWithSlot(INT btn, BOOL showErr, WORD cnt, BOOL stack)
Interceptor.attach(SwapWithSlot, {
  onEnter(args) {
    this.btn = args[0].toInt32();
    this.cnt = args[2].toInt32() & 0xffff;
    inSwap++;
    send({ tag: 'SwapWithSlot.in', btn: this.btn, showErr: args[1].toInt32(),
           cnt: this.cnt, stack: args[3].toInt32() });
  },
  onLeave(retval) {
    if (inSwap > 0) inSwap--;
    send({ tag: 'SwapWithSlot.out', btn: this.btn, ret: retval.toInt32() });
  }
});

// CGameArea::GetGroundPile(const CPoint& ptPos) -> LONG (the existing pile obj id)
// __thiscall: ecx = this(area); args[0] = &CPoint {x@0, y@4}.
Interceptor.attach(AreaGetGround, {
  onEnter(args) {
    try { this.x = args[0].readS32(); this.y = args[0].add(4).readS32(); }
    catch (e) { this.x = 0; this.y = 0; }
  },
  onLeave(retval) {
    const r = retval.toInt32();
    if (r === lastArea) return;
    lastArea = r;
    send({ tag: 'AreaGetGroundPile', pos: [this.x, this.y], pile: r });
  }
});

// CInfGame::SwapItemPersonal(SHORT portrait, SHORT slot, CItem*& pItem, STRREF& err, WORD cnt, BOOLEAN srv)
Interceptor.attach(SwapItemPers, {
  onEnter(args) {
    this.portrait = s16(args[0]);
    this.slot = s16(args[1]);
    this.pErr = args[3];
    send({ tag: 'SwapItemPersonal.in', portrait: this.portrait, slot: this.slot,
           item: deref(args[2]).toString(), cnt: args[4].toInt32() & 0xffff,
           fromServer: args[5].toInt32() & 0xff });
  },
  onLeave(retval) {
    send({ tag: 'SwapItemPersonal.out', portrait: this.portrait, slot: this.slot,
           ret: retval.toInt32() & 0xff, err: derefU32(this.pErr) });
  }
});

// CInfGame::SwapItemPersonalInventory(SHORT portrait, CItem*& pItem, SHORT slot, STRREF& err, WORD cnt, ...)
Interceptor.attach(SwapItemPersInv, {
  onEnter(args) {
    this.portrait = s16(args[0]);
    this.slot = s16(args[2]);
    this.pErr = args[3];
    send({ tag: 'SwapItemPersonalInventory.in', portrait: this.portrait, slot: this.slot,
           item: deref(args[1]).toString(), cnt: args[4].toInt32() & 0xffff });
  },
  onLeave(retval) {
    send({ tag: 'SwapItemPersonalInventory.out', portrait: this.portrait, slot: this.slot,
           ret: retval.toInt32() & 0xff, err: derefU32(this.pErr) });
  }
});

// CInfGame::SwapItemGround(LONG container, SHORT slot, CItem*& pItem, STRREF& err, WORD cnt, BOOLEAN srv)
Interceptor.attach(SwapItemGround, {
  onEnter(args) {
    this.container = args[0].toInt32();
    this.slot = s16(args[1]);
    this.pErr = args[3];
    send({ tag: 'SwapItemGround.in', container: this.container, slot: this.slot,
           item: deref(args[2]).toString(), cnt: args[4].toInt32() & 0xffff });
  },
  onLeave(retval) {
    send({ tag: 'SwapItemGround.out', container: this.container, slot: this.slot,
           ret: retval.toInt32() & 0xff, err: derefU32(this.pErr) });
  }
});

// CGameSprite::GetItemUsages(SHORT slot, WORD type, SHORT ability) -> CGameButtonList*
// Only while a SwapWithSlot is running (idle render polling is dropped).
Interceptor.attach(GetItemUsages, {
  onEnter(args) {
    this.skip = !inSwap;
    if (this.skip) return;
    this.slot = s16(args[0]);
    this.type = args[1].toInt32() & 0xffff;
  },
  onLeave(retval) {
    if (this.skip) return;
    let count = -1;
    try { count = retval.add(CPTRLIST_COUNT).readU32(); } catch (e) {}
    send({ tag: 'GetItemUsages', slot: this.slot, type: this.type,
           list: retval.toString(), count: count });
  }
});

// CGameSprite::SetWeaponSet(BYTE weaponSet) __thiscall. Only during a swap.
Interceptor.attach(SetWeaponSet, {
  onEnter(args) {
    if (!inSwap) return;
    send({ tag: 'SetWeaponSet', sprite: this.context.ecx.toString(),
           weaponSet: args[0].toInt32() & 0xff });
  }
});

// CScreenInventory::FetchGroundPile(SHORT portrait, BOOL evenIfDead) -> LONG. Deduped.
Interceptor.attach(FetchGroundPile, {
  onEnter(args) {
    this.portrait = s16(args[0]);
    this.evenIfDead = args[1].toInt32();
  },
  onLeave(retval) {
    const r = retval.toInt32();
    if (r === lastFetch) return;
    lastFetch = r;
    send({ tag: 'FetchGroundPile', portrait: this.portrait,
           evenIfDead: this.evenIfDead, pile: r });
  }
});

// CInfGame::GetGroundPile(LONG sprite) -> LONG. Deduped.
Interceptor.attach(GetGroundPile, {
  onEnter(args) { this.sprite = args[0].toInt32(); },
  onLeave(retval) {
    const r = retval.toInt32();
    if (r === lastGGP) return;
    lastGGP = r;
    send({ tag: 'GetGroundPile', sprite: this.sprite, pile: r });
  }
});

send({ tag: 'ready' });
"""


def main():
    attach = "--attach" in sys.argv
    open(LOG, "w").close()

    # Everything goes to the file; the terminal only echoes the swap-flow tags so
    # it stays readable.
    TERMINAL_TAGS = ("SwapWithSlot", "SwapItemPersonal", "SwapItemPersonalInventory",
                     "SwapItemGround", "AreaGetGroundPile", "GetGroundPile",
                     "FetchGroundPile", "ready")

    def on_message(message, data):
        if message["type"] == "send":
            payload = message["payload"]
            line = json.dumps(payload)
            if str(payload.get("tag", "")).startswith(TERMINAL_TAGS):
                print(line, flush=True)
        else:
            line = "ERROR " + json.dumps(message)
            print(line, flush=True)
        with open(LOG, "a") as f:
            f.write(line + "\n")

    if attach:
        session = frida.attach("IWD2.exe")
        pid = None
        print("[*] attached to running IWD2.exe", flush=True)
    else:
        pid = frida.spawn(EXE, cwd=GAME_DIR)
        session = frida.attach(pid)
        print(f"[*] spawned IWD2.exe pid={pid}", flush=True)

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    if pid is not None:
        frida.resume(pid)

    print("[*] hooks live. In-game: load the save, drag an item onto a quick-weapon",
          flush=True)
    print("[*] slot and a ground slot, and click a ground pile.", flush=True)
    print(f"[*] logging to {LOG}", flush=True)
    sys.stdout.flush()
    import threading
    threading.Event().wait()


if __name__ == "__main__":
    main()
