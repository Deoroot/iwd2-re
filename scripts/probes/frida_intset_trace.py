#!/usr/bin/env python3
"""Trace the ORIGINAL IWD2.exe custom int-tree insert to learn what the two
shared CDerivedStats trees (at +0x470 and +0x480) actually hold.

Both are a custom 16-byte red-black-tree class (int keys, _Nil = 0x8D13F0,
node = {left@0, parent@4, right@8, key@0xC, color@0x10}), distinct from the
plain std::set<int> at +0x490 (_Nil 0x8D13F4). Their element semantics are not
inferable statically -- callers are as diverse as GetSkillCost,
GetMulticlassingPenalty, EquipShield, StoneSkins, CheckExpiration. This probe
logs every insert's caller + key so the values can be mapped to in-game actions.

IWD2.exe has no ASLR (ImageBase 0x400000) -> Ghidra addresses are absolute.

Hook:
  0x44AB10  <int-tree>::Insert(this, ..., pos, int* key)  __thiscall
            this = the tree base; *key = the inserted int (node+0xC after insert)

Run (VM session 1, via vm.sh frida so it renders + takes input):
  scripts/vm.sh frida scripts/frida_intset_trace.py

Then in-game (ORIGINAL IWD2.exe, a save with a caster + items):
  - cast Stoneskin, then Iron Skins        -> StoneSkins inserts (expect 218)
  - equip / unequip a shield and a weapon  -> the equip callers
  - level up a multiclass char / open the record screen (skill cost, multiclass)
  - let a timed effect expire               -> CheckExpiration
Each action's key + caller is one JSON line; correlate offline.

Output (read THIS file, not vm_s1_out.txt): tmp_frida_intset.log
"""
import os
import sys
import time

import frida

LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_intset.log")

JS = r"""
'use strict';

// IWD2.exe, no ASLR (ImageBase 0x400000) -> return addresses ARE Ghidra addrs.
const Insert = ptr(0x44ab10);

function rva(p) {
    try { return '0x' + p.toUInt32().toString(16); }
    catch (e) { return p.toString(); }
}

// <int-tree>::Insert(this=ecx, arg0, arg1, arg2=pos, arg3=int* key).
// The key pointer is the 4th stack arg; *key is the inserted int.
Interceptor.attach(Insert, {
    onEnter: function (args) {
        const self = this.context.ecx;           // the tree base
        let key = null;
        // args[3] should be the key pointer; deref defensively and also keep
        // the raw stack dwords so a wrong index is still recoverable offline.
        const raw = [];
        for (let i = 0; i < 5; i++) {
            try { raw.push('0x' + args[i].toUInt32().toString(16)); }
            catch (e) { raw.push('?'); }
        }
        try { key = args[3].readS32(); } catch (e) { key = null; }

        const caller = rva(this.returnAddress);
        send({
            caller: caller,
            self: '0x' + self.toUInt32().toString(16),
            key: key,
            raw: raw
        });
    }
});

send({ status: 'armed', insert: Insert.toString() });
"""


def main():
    attach = "--attach" in sys.argv
    game_dir = r"C:\GOG Games\Icewind Dale 2"
    exe = os.path.join(game_dir, "IWD2.exe")

    log = open(LOG, "w", buffering=1)

    def on_message(message, data):
        import json
        if message.get("type") == "send":
            log.write(json.dumps(message["payload"]) + "\n")
            log.flush()
            try:
                os.fsync(log.fileno())
            except OSError:
                pass
        else:
            log.write(repr(message) + "\n")
            log.flush()

    if attach:
        session = frida.attach("IWD2.exe")
    else:
        pid = frida.spawn([exe], cwd=game_dir)
        session = frida.attach(pid)

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    if not attach:
        frida.resume(session._impl.pid if hasattr(session, "_impl") else pid)

    # vm.sh frida payload: no stdin -> keep the process alive explicitly.
    while True:
        time.sleep(0.5)


if __name__ == "__main__":
    main()
