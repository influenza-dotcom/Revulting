@echo off
REM ---------------------------------------------------------------
REM  Fallback launcher: uses SDL's Wayland driver instead of X11.
REM  Try this if the normal launcher shows a blank/white window.
REM ---------------------------------------------------------------
title Vulture - NetHack 5.0.0 (Wayland)
echo Starting Vulture / NetHack 5.0.0 in Wayland mode ...
wsl.exe -d Ubuntu -- env VDRIVER=wayland /root/play-vulture.sh
if errorlevel 1 (
  echo.
  echo The game exited with an error. Press any key to close.
  pause >nul
)
