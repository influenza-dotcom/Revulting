#!/bin/bash
OUT=/c/Users/dalla/Vulture/VultureNetHack
cd "$OUT" || exit 2
cat > /tmp/gdbcmd <<'EOF'
set pagination off
set confirm off
set breakpoint pending on
break abort
break _assert
break __glibcxx_assert_fail
run
bt 40
quit
EOF
timeout 120 gdb -q -batch -x /tmp/gdbcmd ./NetHackV-console.exe 2>&1 | tail -50
