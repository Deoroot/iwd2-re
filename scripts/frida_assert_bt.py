"""Capture native backtrace at a debug-CRT assertion (_CrtDbgReport[W]).

The _CrtIsValidHeapPointer(block) assert fires from _free_dbg -> _CrtDbgReportW.
Hooking the reporter onEnter and dumping Thread.backtrace gives the call stack
of the bad free (our caller). Base 0x400000, no ASLR -> raw addrs map directly;
addr2fn them afterwards.

Run as session-1 payload:  scripts/vm.sh frida scripts/frida_assert_bt.py
Reproduce the crash; backtrace lands in iwd2-re-assert-bt.log (CWD).
"""
import frida, sys, time, os

OUT = r"C:\GOG Games\Icewind Dale 2\iwd2-re-assert-bt.log"

JS = r"""
var OUT = '%s';
function log(s) { send({f: s}); }
Process.enumerateModules().forEach(function (m) {
    var n = m.name.toLowerCase();
    if (n.indexOf('ucrt') >= 0 || n.indexOf('vcruntime') >= 0 || n.indexOf('msvcr') >= 0) {
        log('crt module: ' + m.name + ' @ ' + m.base);
    }
});
['_CrtDbgReportW', '_CrtDbgReport', '_wassert', '_assert'].forEach(function (name) {
    var p = null;
    Process.enumerateModules().forEach(function (m) {
        if (p === null) { var e = Module.findExportByName(m.name, name); if (e) p = e; }
    });
    if (p === null) { log('no export ' + name); return; }
    Interceptor.attach(p, {
        onEnter: function (args) {
            log('=== ' + name + ' fired ===');
            var bt = Thread.backtrace(this.context, Backtracer.ACCURATE);
            bt.forEach(function (a) {
                var m = Process.findModuleByAddress(a);
                if (m && m.name.toLowerCase().indexOf('iwd2-re') >= 0) {
                    log('  OURS  ' + a + '  (' + m.name + '+' + a.sub(m.base) + ')');
                } else {
                    var sym = DebugSymbol.fromAddress(a);
                    log('  ' + a + '  ' + (sym ? sym.toString() : (m ? m.name : '?')));
                }
            });
            log('=== end bt ===');
        }
    });
    log('hooked ' + name + ' @ ' + p);
});
"""

def main():
    pid = None
    dev = frida.get_local_device()
    for p in dev.enumerate_processes():
        if p.name.lower() == 'iwd2-re.exe':
            pid = p.pid
            break
    if pid is None:
        open(OUT, 'a').write("iwd2-re.exe not found\n")
        return
    session = frida.attach(pid)
    script = session.create_script(JS % OUT.replace('\\', '\\\\'))
    fd = open(OUT, 'a')
    def on_msg(m, d):
        if m.get('type') == 'send':
            fd.write(m['payload']['f'] + '\n')
            fd.flush()
            os.fsync(fd.fileno())
    script.on('message', on_msg)
    script.load()
    fd.write("attached pid %d; armed\n" % pid); fd.flush(); os.fsync(fd.fileno())
    while True:
        time.sleep(0.5)

if __name__ == '__main__':
    main()
