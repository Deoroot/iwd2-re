' Hidden launcher for the session-1 Frida/game payload.
'
' Runs scripts\vm_s1_payload.cmd with a HIDDEN window (intWindowStyle 0 = SW_HIDE)
' so the cmd console never pops to the foreground and steals focus from the running
' game. IWD2 stops rendering (Realize=0) and throttles AI when it loses focus, which
' silently corrupted Frida ATTACH traces (the cmd window stole focus every launch).
' A SPAWNed game still takes focus normally -- it is a GUI app and should be focused.
'
' Invoked by vm_s1.cmd's scheduled task (still /it -> interactive session 1, required
' so the game renders + receives input). bWaitOnReturn = False: fire-and-forget.
CreateObject("WScript.Shell").Run "cmd /c C:\iwd2-re\scripts\vm_s1_payload.cmd", 0, False
