#!/bin/sh
SDK=/root/m4/native/ps5-payload-sdk-mihawk
export PATH=/usr/lib/llvm-18/bin:/usr/bin:/bin
echo "== ucontext layout (SDK libc) =="
dump="$(llvm-dwarfdump --version >/dev/null 2>&1 && echo yes || echo no)"
if ls "$SDK/target/include/sys/ucontext.h" >/dev/null 2>&1; then
  sed -n '1,60p' "$SDK/target/include/sys/ucontext.h"
elif ls "$SDK/target/include/ucontext.h" >/dev/null 2>&1; then
  sed -n '1,60p' "$SDK/target/include/ucontext.h"
else
  echo "NO ucontext header"
fi
echo "== signal.h siginfo bits =="
grep -nE 'si_code|si_addr|TRAP_TRACE|SEGV_ACCERR' "$SDK/target/include/signal.h" "$SDK/target/include/sys/signal.h" 2>/dev/null | head -10
echo "== libkernel.so exports (signals/context) =="
llvm-nm -D "$SDK/target/lib/libkernel.so" 2>/dev/null | grep -iE 'sigaction|sigaltstack|sigprocmask|futex' | head -10
echo "== libSceLibcInternal exports =="
llvm-nm -D "$SDK/target/lib/libSceLibcInternal.so" 2>/dev/null | grep -iE '^(sig|_?sig|__xuname|gettid)' | head -10
