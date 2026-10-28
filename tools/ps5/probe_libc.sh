#!/bin/sh
SDK=/root/m4/native/ps5-payload-sdk-mihawk
export PATH=/usr/lib/llvm-18/bin:/usr/bin:/bin
echo "== headers present =="
for h in ucontext.h sys/auxv.h sys/mman.h sched.h dirent.h pthread.h signal.h execinfo.h sys/syscall.h; do
  if [ -e "$SDK/target/include/$h" ]; then echo "OK   $h"; else echo "MISS $h"; fi
done
echo "== libc symbol scan =="
llvm-nm "$SDK/target/lib/libc.a" 2>/dev/null > /root/libc_syms.txt
for sym in sigaction sigaltstack sigsetjmp siglongjmp pthread_create mmap mprotect munmap \
           openat mkdirat fstatat renameat getrandom clock_gettime syscall dl_iterate_phdr; do
  if grep -q " $sym\$" /root/libc_syms.txt 2>/dev/null; then echo "libc OK   $sym"; else echo "libc MISS $sym"; fi
done
echo "== target/lib listing =="
ls "$SDK/target/lib"
