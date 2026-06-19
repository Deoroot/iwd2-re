@echo off
REM Payload executed in session 1 by vm_s1.cmd. Rewrite this per task (smoke, frida, ...).
REM Output is redirected to vm_s1_out.txt (the task has no console of its own).
cd /d C:\iwd2-re
python scripts\auto_start_game.py --slot 3 > C:\iwd2-re\vm_s1_out.txt 2>&1
