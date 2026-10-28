#!/bin/sh
E=/root/m4/port-ps5/llvm-pie.elf
NM=/usr/lib/llvm-18/bin/llvm-nm
RE=/usr/lib/llvm-18/bin/llvm-readelf
echo "== _start =="
$NM "$E" 2>/dev/null | grep -wE '_start|_init|module_start' | head
echo "== header entry =="
$RE -h "$E" | grep Entry
echo "== who needs PosixForWebKit =="
$NM -u "$E" 2>/dev/null | grep -E 'scePthreadRename|scePthreadGetthreadid|rename' | head -5
echo "== dynsym _start =="
$RE --dyn-syms "$E" 2>/dev/null | grep -w '_start' | head -3
