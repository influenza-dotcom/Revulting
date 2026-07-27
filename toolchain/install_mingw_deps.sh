#!/bin/bash
# Install the mingw-w64 toolchain + every library the port needs, inside MSYS2.
# Run from an MSYS2 shell (msys2_shell.cmd -defterm -no-start -msys2).
set -x
pacman-key --init 2>/dev/null
pacman -Syu --noconfirm --disable-download-timeout 2>&1 | tail -5

pacman -S --noconfirm --needed --disable-download-timeout \
  base-devel git make bison flex \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-SDL2 \
  mingw-w64-x86_64-SDL2_mixer \
  mingw-w64-x86_64-SDL2_ttf \
  mingw-w64-x86_64-libpng \
  mingw-w64-x86_64-zlib \
  mingw-w64-x86_64-libogg \
  mingw-w64-x86_64-libvorbis \
  mingw-w64-x86_64-libtheora \
  2>&1 | tail -20

set +x
echo "===== TOOLCHAIN CHECK ====="
for t in /mingw64/bin/gcc.exe /mingw64/bin/g++.exe /usr/bin/make.exe \
         /usr/bin/bison.exe /usr/bin/flex.exe /mingw64/bin/pkgconf.exe \
         /mingw64/bin/sdl2-config; do
  printf '%-34s ' "$(basename $t)"; test -e "$t" && echo PRESENT || echo MISSING
done
echo "--- headers ---"
for h in /mingw64/include/SDL2/SDL.h /mingw64/include/SDL2/SDL_mixer.h \
         /mingw64/include/SDL2/SDL_ttf.h /mingw64/include/png.h \
         /mingw64/include/ogg/ogg.h /mingw64/include/theora/theora.h; do
  printf '%-42s ' "$h"; test -e "$h" && echo ok || echo MISSING
done
echo "--- versions ---"
/mingw64/bin/gcc.exe --version 2>/dev/null | head -1
/mingw64/bin/sdl2-config --version 2>/dev/null
