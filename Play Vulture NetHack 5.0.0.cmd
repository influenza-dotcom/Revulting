@echo off
REM ---------------------------------------------------------------
REM  Vulture / NetHack 5.0.0  -  launcher
REM  The game itself lives inside WSL2 (Ubuntu) and draws its window
REM  on the Windows desktop through WSLg.
REM ---------------------------------------------------------------
title Vulture - NetHack 5.0.0
echo Starting Vulture / NetHack 5.0.0 ...
echo (the game window opens separately; this console can be closed)
wsl.exe -d Ubuntu -- /root/play-vulture.sh
if errorlevel 1 (
  echo.
  echo The game exited with an error. Press any key to close.
  pause >nul
)
