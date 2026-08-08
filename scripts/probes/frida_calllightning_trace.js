// Frida trace: Call Lightning (SPPR302) on original IWD2.exe.
// Usage: scripts/vm.sh frida scripts/frida_calllightning_trace.js

var log = [];
var MAX_LOG = 200;

function push(entry) { log.push(entry); if (log.length > MAX_LOG) log.shift(); }

function fmt(entry) {
    var parts = [entry.ts, entry.fn];
    for (var k in entry) {
        if (k !== 'ts' && k !== 'fn') parts.push(k + '=' + entry[k]);
    }
    return parts.join(' ');
}

setInterval(function() {
    if (log.length === 0) return;
    var batch = log.splice(0, log.length);
    var lines = [];
    for (var i = 0; i < batch.length; i++) lines.push(fmt(batch[i]));
    send(lines.join('\n'));
}, 1000);

// DecodeProjectile (0x51EAF0)
Interceptor.attach(ptr(0x51EAF0), {
    onEnter: function(args) {
        var t = args[0].toInt32();
        push({ts:Date.now(), fn:'DecodeProj', type:'0x'+t.toString(16), idx:(t>0x1000?t-0x1001:-1)});
    }
});

// DecodeSpellHitProjectile (0x560310)
Interceptor.attach(ptr(0x560310), {
    onEnter: function(args) {
        push({ts:Date.now(), fn:'DecodeSpellHit', idx:args[0].toInt32()});
    }
});

// CInfinity::CallLightning (0x5D1340)
Interceptor.attach(ptr(0x5D1340), {
    onEnter: function(args) {
        push({ts:Date.now(), fn:'CallLightning', x:args[0].toInt32(), y:args[1].toInt32()});
    }
});

// CGameEffect::FireSpell (0x4A3FF0)
Interceptor.attach(ptr(0x4A3FF0), {
    onEnter: function(args) {
        var e = this.context.ecx;
        push({ts:Date.now(), fn:'FireSpell', eID:'0x'+Memory.readU32(e.add(0x0C)).toString(16)});
    }
});

send({status:'ready', note:'Cast SPPR302 now'});
