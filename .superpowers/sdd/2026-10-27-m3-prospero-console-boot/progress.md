# SDD ledger — plan: docs/superpowers/plans/2026-10-27-m3-prospero-console-boot.md

Sesión de continuación (27-10-2026): la sesión previa ("Programa scene PS5") murió por cuota tras lanzar run9 (A/B IO nativo de F2) sin analizar resultados; M2 ya estaba cerrado y commiteado (c3f0fe6), así que se continúa con el hito M3 según el spec aprobado. run9/F2 queda como hallazgo documentado en el cierre M2 (mitigación de entorno WSL/Pulse, no aplicable a PS5 con RADV hardware).

## Contexto de investigación (pre-plan)

- **Stack de referencia verificado** (Nazky/UnleashedRecomp-Prospero, branch `prospero`): cross-compila `x86_64-sie-ps5` (`-msse4.1 -mssse3 -mcx16 -femulated-tls`), enlaza estáticamente PS5_PayloadSDK (libc.a, libps5platform.a, libc++.a, libc++abi.a, libunwind.a) + libvulkan_radeon.ps5.a (PS5_Mesa, `-Dradv-winsys=ps5`), firma FSELF con ps5-native-tool, despliega en `/data/homebrew/<title>` montado como `/app0`, lanza por payload controller al 9021. Runtime clave: reserva de 4 GiB del guest en VA 0x1000000000 vía sceKernelAllocateDirectMemory + sceKernelMapDirectMemory (BigApp 12 GiB DMEM); stacks de 2 MiB por hilo guest vía `--wrap=pthread_create` de libps5platform; runtime `ps5-emutls-cxa.o` que drena destructores thread_local antes de liberar emutls (el crash de heap conocido de FreeBSD libkernel).
- **Decisión M3 (aldabilla del spec):** para el ciclo repetible NO se necesita ni FSELF ni BigApp ni RADV: el cargador de ELF del ecosistema (9021) ejecuta ELF dinámicos directamente. Se adopta `ps5-payload-sdk` oficial (release binaria) — exactamente el prerrequisito `clang-18/lld-18` que el spec llama "clang18 de Prospero".
- **hello_world del SDK verificado por fuente:** `sceKernelSendNotificationRequest` con `notify_request_t` → burbuja en el home (visibilidad total para el usuario).

## Rulings

- Ruling (entorno): se ejecuta sobre el checkout principal `C:\ttt2` (main) sin worktree — mismo criterio M1/M2. WSL2 `Ubuntu-24.04` de M2 se reutiliza (estaba Stopped; se arrancó on-demand).
- Ruling (downloads): GitHub solo desde Windows (regla M2). `ps5-payload-sdk.zip` (8.838.865 B) y `sdk-master.zip` descargados a `C:\ttt2\.superpowers\m3-downloads\` con curl.exe; transferencia a WSL por `/mnt/c`.
- Ruling (SDK no invasivo): el zip binario se despliega en `/opt/ps5-payload-sdk` SIN construir desde fuente; el repo solo consume `toolchain/prospero.mk` + `bin/`. El zip incluye `samples/` completos (no hace falta clonar el repo del SDK).

## Ejecución

- **Task 2 (SDK): COMPLETE.** apt: `unzip`, luego `llvm-18 lld-18` (imprescindible: los wrappers del SDK resuelven el host toolchain con `llvm-config-18..15`; sin él mueren con `/llvm-clang: No such file or directory`). Verificación: `prospero-clang --version` → `Ubuntu clang version 18.1.3` + `Target: x86_64-sie-ps5`.
- **Task 3 (hello_world): COMPLETE.** `make -C /opt/ps5-payload-sdk/samples/hello_world` → `hello_world.elf` ELF64 PIE x86-64 dinámico, sin warnings (`-Wall -Werror`). `file`/`readelf` OK.
- **Task 4 (smoke del repo): COMPLETE.** `tools/ps5/smoke/main.c` (notificación + `sceKernelDebugOutText`), `tools/ps5/Makefile` (include de prospero.mk, rutas SELF_DIR absolutas), `tools/ps5/send_smoke.sh` (prospero-deploy = socat). Build OK: `tools/ps5/build/ttt2_smoke.elf` ELF64 PIE x86-64. `.gitignore`: `!/tools/ps5/`.
- **Task 5 (consola): PENDIENTE usuario** — IP de la PS5 + confirmación de cargador (Relapse 9021 directo vs elfldr vía 9020).
- **socat:** instalado (requisito de `prospero-deploy`).

## Notas de protocolo

- Protocolo PS→bash de M2 respetado: scripts .sh escritos por write-tool en `C:\ttt2\.superpowers\`, ejecutados vía `wsl bash -c "tr -d '\r' < /mnt/c/... > /root/... && bash /root/..."`. 4/4 ejecuciones limpias.
- `prospero-deploy` es un script bash+socat (no binario): corre dentro de WSL. Fallback Windows si WSL no alcanzara la LAN: `System.Net.Sockets.TcpClient` por PowerShell (Task 5 Step 1 decide).
