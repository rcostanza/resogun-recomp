@echo off
rem Starts the game with the glitch diagnostics armed (idle until the other .bat files are used).
rem The game log goes to diagnose\resogun.log.
set "HERE=%~dp0"
del "%HERE%capture.trigger" "%HERE%reload.trigger" 2>nul
> "%HERE%toggles.txt" echo 0
set "RESOGUN_TOGGLE_FILE=%HERE%toggles.txt"
set "RESOGUN_CAPTURE_TRIGGER=%HERE%capture.trigger"
set "RESOGUN_CAPTURE_DIR=%HERE%captures"
set "RESOGUN_RELOAD_TRIGGER=%HERE%reload.trigger"
rem Every routine event in the log too, not only faults and errors.
set "RESOGUN_VERBOSE=1"
start "" /b "%HERE%..\resogun.exe" %* > "%HERE%resogun.log" 2>&1
