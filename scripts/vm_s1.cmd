@echo off
REM Run scripts\vm_s1_payload.cmd in the VM's INTERACTIVE console session (session 1),
REM so GUI apps (the game) actually render + receive input. Invoked from the host over SSH,
REM where processes otherwise land in session 0 (Services, no desktop).
REM
REM Mechanism: a "run only when user is logged on" scheduled task (/it) with /ru = the
REM logged-on console user and NO /rp -> Task Scheduler uses an interactive token, no
REM password stored, and the action executes in that user's desktop session.
setlocal
set "TN=iwd2_s1"
schtasks /end    /tn %TN%    >nul 2>&1
schtasks /delete /tn %TN% /f >nul 2>&1
schtasks /create /tn %TN% /tr "C:\iwd2-re\scripts\vm_s1_payload.cmd" /sc once /st 23:59 /ru %USERNAME% /it /f || (echo SCHTASKS_CREATE_FAILED & exit /b 1)
schtasks /run /tn %TN% || (echo SCHTASKS_RUN_FAILED & exit /b 1)
echo OK: payload launched in session 1 as %USERNAME%
