# SDD ledger — plan: docs/superpowers/plans/2026-10-27-m4-radv-first-frame.md

M3 cerrado antes de esto (f8f5656): ciclo validado en consola (3/3 burbujas, elfldr 9021), ftpsrv residente (:2121). Bloque F1/F2/F3 cerrado: el port está sano (round perfecto del usuario en Windows); los síntomas de WSL son entorno (LAVAPIPE) y no aplican a PS5.

## Investigación (recetas del stack de referencia, verificadas por fuente)

- **PS5_Mesa (mihawk-99)** = fork Mesa 26.2.0 con RADV Vulkan 1.4 + winsys `ps5` (memoria directa CPU=GPU en 0x200000000 [4 GiB, 32-bit] / 0x4000000000 [256 GiB], sumisiones por `sceAgcDriverSubmitDcb`, sync binario) y swapchain `src/vulkan/wsi/wsi_common_videoout.c` (VK_KHR_display sobre VideoOut, vblank, 119.88/59.94 Hz medidos). **Se usa en títulos reales**: vkQuake (2026-09-28) y RetroArch PS5 (v0.5.0-alpha.5).
- **PS5_PayloadSDK (mihawk-99)** = fork del ps5-payload-sdk con la capa de plataforma: `libps5platform.a` (heaps en memoria directa, stacks 2 MiB por hilo vía wraps pthread, shims ps5_ de libc), libc++.a/libc++abi.a/libunwind.a del pin.
- **Receta de enlace (PS5_Vulkan/tools/radv-link.sh)**: whole-archive del RADV + grupo libc++/libc++abi/libunwind/builtins/libps5platform; wraps pthread_create/join/detach, fclose/fflush (memstream), familia malloc; defsyms ps5_ (~80 nombres libc); version-script para que los nombres enlazados no se exporten; linker script `tooling/psbc/ps5-pie-unwind.ld`.
- **Cross-file (PS5_Vulkan/tooling/radv/ps5-cross.ini)**: system=freebsd (host_machine), compis = bin/prospero-clang del SDK fork, `needs_exe_wrapper=true`, static.
- **Pins usados**: Mesa `0b2d6d1a61d9bbf89cf8beb88a696144f67c61f8`, SDK `95c08f27386fc698f6bbe21dde3030140a41d10b` (los de Nazky, probados con títulos), SDK upstream release v0.42 (hash `8cfbc7cd…`).
- **clc-cache** (Nazky/UnleashedRecomp-Prospero, `tooling/radv/clc-cache.tar.xz`): 6 ficheros precompilados (libpoly/libradv_cl .cpp/.h/.spv) que los wrappers `mesa_clc`/`vtn_bindgen2` sirven por basename → se salta construir host LLVMSPIRVLib/mesa_clc.

## Rulings

- Ruling (setup): checkout principal `C:\ttt2` (main), sin worktree — mismo criterio M1-M3.
- Ruling (CRLF): los clones hechos en Windows salen con CRLF por autocrlf; TODO árbol que WSL consume se exporta con `git archive` (blobs LF) — mordido una vez (setup-sdk.sh "pipefail: invalid option name").
- Ruling (PATH WSL): `/usr/lib/llvm-18/bin` delante para `llvm-ar`/`llvm-config`; `/usr/local/bin` delante para meson 1.12.1 por pip (el 1.3.2 del apt es insuficiente: Mesa 26.2 pide ≥1.4). `$PATH` SIEMPRE en comillas dobles (una vez lo escapó el quoting del one-liner).
- Ruling (quoting PS→bash): se reconfirma el protocolo M2 — one-liners con `$var`/comillas anidadas se destruyen entre capas; TODO va por fichero .sh (write-tool) + `tr -d '\r'`.
- Ruling (red): GitHub solo Windows (M2); el SDK upstream v0.42 y zlib se pre-descargan en Windows y el script los encuentra por hash en el cache.

## Ejecución

- **Task 1 (descargas): COMPLETE.** PS5_Vulkan (clon), PS5_PayloadSDK@95c08f2 y PS5_Mesa@0b2d6d1a (fetch por SHA + checkout FETCH_HEAD), clc-cache.tar.xz, zlib-1.3.2.tar.gz, ps5-payload-sdk-v042.zip → `.superpowers/m4-downloads/`.
- **Task 2 (bootstrap): COMPLETE.** SDK fork instalado en `~/m4/native/ps5-payload-sdk` (v0.42+fork); zlib estático en `~/m4/zlib-root`; mako/ply/pyyaml; meson 1.12.1.
- **Task 3 (spike): en curso.** Meson configure OK (radv-winsys=ps5, debugoptimized+b_ndebug=true, zlib por packagecache offline, clc-cache). `ninja libvulkan_radeon.a` corriendo en background. Fuente del spike `tools/ps5/vkinfo/main.c` escrita + script de build preparado (`.superpowers/m4_vkinfo_build.sh`: CC del SDK fork, whole-archive RADV, grupo libc++/platform, wraps+defsyms+version-script, ps5-pie-unwind.ld).
- Pendiente tras el RADV: build vkinfo → envío por 9021 → informe por FTP → veredicto features.
