# Plan de implementación: M4 — DXVK-native y RADV sobre PS5: primer frame en pantalla

> **ESTADO: en ejecución (27-10-2026).** Task 1 (descargas) y Task 2 (bootstrap SDK+zlib) en curso; el resto por tareas.

**Goal:** Ver el primer frame del port (la UI del menú inicial de TTT2) en la TV de la consola: RADV de Mesa para PS5 (`libvulkan_radeon.ps5.a`, Vulkan 1.4, swapchain `wsi_common_videoout.c` → `VK_KHR_display` sobre `sceVideoOut`) + DXVK-native enlazado estático dentro del ELF del port. Sin jugabilidad todavía: pipeline de presentación estable + verificación del set de features Vulkan contra lo que DXVK requiere (spike primero, según el spec).

**Architecture:** Se adopta la pila de referencia probada en títulos (vkQuake 2026-09-28, RetroArch PS5 v0.5.0-alpha.5): **mihawk-99/PS5_Mesa** (fork Mesa 26.2.0, winsys `ps5`: memoria directa GPU/CPU en 0x200000000/0x4000000000, sumisiones AGC, sync binario) + **mihawk-99/PS5_PayloadSDK** (libc.a, libps5platform.a — heaps en memoria directa, stacks de 2 MiB por hilo con wraps de pthread — libc++.a/libc++abi.a/libunwind.a) + **PS5_Vulkan** (cross-file `tooling/radv/ps5-cross.ini`, linker script `tooling/psbc/ps5-pie-unwind.ld`, receta `tools/radv-link.sh`: whole-archive del RADV + wraps pthread/malloc + defsyms ps5_ + version-script local). El pin de Nazky (`UnleashedRecomp-Prospero`) provee el `clc-cache.tar.xz` (shaders OpenCL/SPIR-V precompilados de Mesa) y la revisión de Mesa probada con títulos. El port (D3D11) habla con RADV vía DXVK-native compilado como objetos estáticos — WSI propia de DXVK apuntada a `VK_KHR_display` (la del driver ya vive sobre VideoOut).

**Tech Stack:** WSL2 (clang-18/lld-18/llvm-18, meson, ninja, glslang ya de M2) + `pip mako ply pyyaml`; zlib 1.3.2 estático; los cinco árboles fuente en `.superpowers/m4-downloads/` (Windows descarga, WSL consume por `/mnt/c` — regla M2); envío a consola por 9021 (elfldr del usuario) y FTP :2121 (ftpsrv residente de M3).

**Spec:** `docs/superpowers/specs/2026-10-26-ttt2-ps5-port-design.md` — hito M4 (líneas 129-134), TLS emulado (86-88), guard página 0 (93-95), verificación de features (89-92).

## Global Constraints

- Revisiones PINNEadas: Mesa `0b2d6d1a61d9bbf89cf8beb88a696144f67c61f8`, SDK `95c08f27386fc698f6bbe21dde3030140a41d10b` (las que Nazky usa con títulos). Nada de HEADs móviles sin registrar en el ledger.
- GitHub solo desde Windows; al WSL por `/mnt/c` (regla M2).
- El RADV se construye release (`-Db_ndebug=true`) como para títulos; un build debug con asserts queda como opción para diagnóstico.
- Primero spike (vkinfo), después WSI, después el port completo — orden del spec: "chequeo del set de features… con spike antes de portar nada de gameplay".
- Los stubs `sce*` necesarios salen del SDK fork (platform) — no se inventan syscalls.

## Review Focus

1. **Features de RADV vs DXVK**: la lista de extensiones/features del spike debe cubrir lo que DXVK 1.9.2b exige (timeline semaphores, dedicated allocation, dynamic rendering o su equivalente…). Si falta algo, se documenta como hueco ANTES de portar el port (recorte al subconjunto real, como dice el spec).
2. **Emutls**: el runtime `ps5-emutls-cxa.o` de Nazky (drain de destructores thread_local antes de liberar emutls) se prueba en el corte del primer run largo (crash en salida de hilos = el fallo conocido).
3. **Guard de página 0**: `host_vm` del port lo activa solo en Linux; en PS5 queda desactivado por plataforma (spec 93-95).
4. **elfldr vs BigApp**: el primer frame se valida por elfldr (9021, ciclo rápido ya probado); si el RADV exige `/app0` (shader cache), se usa FTP+`/data` y se decide BigApp para M5.
5. **WSL↔Windows**: scripts .sh siempre vía write-tool + `tr -d '\r'` (protocolo M2).

---

### Task 1: Descargas locales (Windows) — HECHO

- [x] Clon shallow por SHA: `PS5_PayloadSDK@95c08f27` ✓, `PS5_Mesa@0b2d6d1a` ✓ (export limpio vía `git archive` desde WSL — el checkout de Windows venía con CRLF del autocrlf), `PS5_Vulkan@main` ✓.
- [x] `clc-cache.tar.xz` de Nazky (253 KB, 6 ficheros) ✓ + `zlib-1.3.2.tar.gz` ✓ + release upstream del SDK `v0.42` (hash verificado `8cfbc7cd…`) ✓.

### Task 2: Bootstrap del SDK fork + zlib estático (WSL) — HECHO

- [x] `platform/tools/setup-sdk.sh` → `~/m4/native/ps5-payload-sdk` (v0.42 + fork 95c08f27); `target/lib` con `libps5platform.a` (372 KB), `libc++.a`, `libc++abi.a`, `libunwind.a`, `libc.a`, `crt1.o` ✓.
- [x] zlib 1.3.2 estático (`~/m4/zlib-root`) ✓ — con `/usr/lib/llvm-18/bin` en PATH (`llvm-ar` a secas no existe en Ubuntu).
- [x] Python mako/ply/pyyaml ✓; **meson 1.12.1 por pip** (el 1.3.2 de Ubuntu es viejo: Mesa 26.2 exige ≥ 1.4).

### Task 3: Spike RADV → `ps5-vkinfo` — en curso

- [x] Setup meson OK: cross-file (`ps5-cross.ini` de PS5_Vulkan + constants del SDK fork), opciones de títulos (release, `-Dradv-winsys=ps5 -Dllvm=disabled`), zlib por packagecache offline, **clc-cache de Nazky con wrappers `mesa_clc`/`vtn_bindgen2`** (el PATH del clc-bin era necesario ANTES del `meson setup`).
- [x] Fuente del spike escrita: `tools/ps5/vkinfo/main.c` — enumera instance/device extensions, features core 1.1-1.3 + las EXT de DXVK (depth clip, custom border color, robustness2, extended dynamic state, 4444, demote, transform feedback, texel alignment, push descriptor), queues, memory heaps/types, y soporte de los ~31 formatos D3D11 críticos; informe a `/data/ttt2-vkinfo.txt` (FTP) + notificación.
- [ ] Build del `libvulkan_radeon.ps5.a` (en curso, background).
- [ ] Build + envío del spike; veredicto de features DXVK vs RADV-PS5.

### Task 4: DXVK-native WSI PS5 (objetos estáticos) — HECHO

- [x] Parche WSI: backend `display` (VK_KHR_display) + espejos estáticos `dxgi_static`/`d3d11_static`, `lib_vulkan` opcional, backend exts con KHR_surface+KHR_display (parche `dxvk-native-1.9.2b-ps5-display-wsi.patch`, en el manifest tras shared-wsi).
- [x] Estáticos para PS5 (WSL, SDK mihawk): libutil/libspirv/libwsi/libvkcommon/libdxvk/libdxbc/dxgi_static/d3d11_static — nm: D3D11CreateDevice(+AndSwapChain), CreateDXGIFactory/1/2 exportados; 2.108 símbolos T; objetos completos en disco (Task 5 enlaza desde el build dir WSL).

### Task 5: Host `-ps5` del port + build completo

- [ ] `port/` target PS5: filesystem (FTP//data), host_vm (memoria directa, sin guard página 0), input (scePad), audio (sceAudioOut), video shell.
- [ ] Enlace: port + DXVK estático + RADV whole + FFmpeg XMA + vkd3d-shader + SDK (receta radv-link + emutls-cxa).

### Task 6: Primer frame en la TV

- [ ] FSELF no necesario (elfldr); upload por FTP, launch por 9021.
- [ ] Frame del menú en pantalla + screenshot + trazas.

### Task 7: Cierre

- [ ] Ledger, plan checkboxes, README de hitos, commit + push.
