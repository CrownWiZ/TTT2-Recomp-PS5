#!/bin/sh
r=/root/m4/radv-build2
echo "== radv_link_flags assembly =="
grep -n 'target/lib\|as-needed\|no-undefined\|undefs\|z,defs' "$r/tools/radv-link.sh" | head -30
echo "== my link line as copied =="
grep -n 'target/lib' /root/port_ps5.sh | head
