"""Minimal Frida smoke: attach to the running iwd2-re.exe and read its main module base.
Proves Frida can instrument a session-1 process from a host-driven (session-0) SSH shell.
Run on the VM: python C:\\iwd2-re\\scripts\\frida_attach_test.py
"""
import sys
import frida

got = {}

def on_message(message, data):
    if message.get("type") == "send":
        got.update(message["payload"])
    else:
        print("FRIDA_ERR", message, file=sys.stderr)

try:
    session = frida.attach("iwd2-re.exe")
except Exception as exc:
    print("ATTACH_FAILED:", exc)
    raise SystemExit(2)

script = session.create_script(
    r"""
    var m = Process.enumerateModules()[0];
    send({ name: m.name, base: m.base.toString(), size: m.size });
    """
)
script.on("message", on_message)
script.load()
session.detach()

print(f"FRIDA_OK module={got.get('name')} base={got.get('base')} size={got.get('size')}")
