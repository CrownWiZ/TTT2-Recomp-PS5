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
- **Ruling (PINS — crítico): los pins de Nazky y de mihawk DIFIEREN y no se mezclan.** Nazky: Mesa `0b2d6d1a` + SDK `95c08f2`. mihawk: Mesa `1ba9874f` + SDK `b83202be`. El primer RADV build (pin Nazky) produjo un archivo donde los entry points Vulkan (`radv_EnumeratePhysicalDevices` etc.) son referencias débiles SIN definición fuerte — el native-tool lo rechaza ("no public SDK stub exports required symbol"). Reconstruyendo TODO con los pins de mihawk (su receta radv-link + native-tool es la que hay que satisfacer). Lección: con este stack, pin de Mesa + pin de SDK + receta de enlace van SIEMPRE juntos del mismo autor.
- Ruling (Static-ICD): el RADV estático no exporta los `vk*` — el patrón del stack (radv_smoke.c:2484) es `vk_icdGetInstanceProcAddr(NULL, "vkCreateInstance")` y todo por punteros. El spike vkinfo va por esa vía. Para DXVK (que llama directo) esto se resolverá con trampolines generados en la capa del port (Task 5).
- Ruling (compis): el wrapper del SDK no añade `-isystem target/include/c++/v1`; para C++ se usa el wrapper `PS5_Vulkan/tooling/prospero-clang18` (c++/v1 ANTES de target/include). Y `-I~/...` no expande tilde: `-I$HOME/...` siempre.
- Ruling (link BigApp): receta exacta de build-radv-title.sh: lld con ps5-pie-unwind.ld (+ -L tooling/native para el INCLUDE ps5-pie.ld), whole-archive RADV, grupo libc++/libc++abi/libunwind/builtins/libps5platform, wraps pthread/malloc/fclose, defsyms ps5_ (condicionales al nm del platform), `--as-needed target/lib/*.so` (los stubs .so dan los imports dinámicos: libc vía libSceLibcInternal, VideoOut, Agc vía stubs propios con soname libSceAgc.prx), version-script app-symbols.map, -e _start. Con este flujo NO hace falta -lc (los duplicados thrd_* del route elfldr desaparecen).

## Ejecución

- **Task 1 (descargas): COMPLETE.** PS5_Vulkan (clon), PS5_PayloadSDK@95c08f2 y PS5_Mesa@0b2d6d1a (fetch por SHA + checkout FETCH_HEAD), clc-cache.tar.xz, zlib-1.3.2.tar.gz, ps5-payload-sdk-v042.zip → `.superpowers/m4-downloads/`.
- **Task 2 (bootstrap): COMPLETE.** SDK fork instalado en `~/m4/native/ps5-payload-sdk` (v0.42+fork); zlib estático en `~/m4/zlib-root`; mako/ply/pyyaml; meson 1.12.1.
- **Task 3 (spike RADV): COMPLETE — veredicto GO.**
  - Archivo PS5 `libvulkan_radeon.ps5.a` (272.8 MB, pin mihawk 1ba9874f, 794/794 targets) construido en WSL.
  - Enlace BigApp del spike: llvm-pie.elf enlazado tras la saga de los entrypoints débiles (--weak de Mesa = referencias débiles sin definición; pasadas de --defsym al stub de Mesa `vk_entrypoint_stub`); el native-tool bloquea: exige imports-limpios (capacidad "application exports" pendiente en el tool) → la pila completa de títulos (ps5vk + runtime) es necesario para el runtime real: queda para M4/M5.
  - **HOST-MODEL (el camino bueno)**: el mismo código RADV compila nativamente en el PC (ICD compartido, "models the console" por diseño del fork de mihawk): `libvulkan_radeon.so` 99.8 MB, `vulkaninfo` en vivo: apiVersion **1.4.354**, deviceName **"PlayStation 5 GPU (RADV NAVI21)"**, driver Mesa 26.2.0.
  - **Tabla DXVK-requisitos vs RADV-PS5**: Vulkan 1.3/1.4 core completo (dynamicRendering, synchronization2, demote, inlineUniformBlock, pipelineCreationCacheControl, robustImageAccess…); device extensions críticas: timeline_semaphore, dedicated_allocation, **depth_clip_enable, custom_border_color, robustness2, extended_dynamic_state, 4444_formats, shader_demote, transform_feedback (streamout de D3D11), texel_buffer_alignment, push_descriptor, external_memory_host, maintenance4, index_type_uint8, image_format_list, depth_stencil_resolve, draw_indirect_count, fragment_shader_barycentric, swapchain_colorspace** — todas OK. textureCompressionBC=true (BC1-7 de D3D11). Queue única GRAPHICS+COMPUTE+TRANSFER+SPARSE. Heaps 4 GiB + 8 GiB (el modelo 12 GiB DMEM de la consola).
  - FALTANTES no-bloqueantes para homebrew fullscreen: VK_KHR_display_swapchain (solo re-creación de swapchain) y VK_KHR_external_memory_fd (compartición entre procesos; irrelevantes single-process).
  - Evidencia: `/root/m4/vkinfo_txt.txt` (1648 líneas) → `.superpowers/m4-radv-ps5-vulkaninfo.txt`; el JSON por vulkaninfo -j sale vacío (probable bug del loader con el modelo INTEGRATED sin PCI válido).
  - Rulling del host-build: el ac_surface.c del árbol importado (`radv-src2`) lleva un shim de typedefs kernel (`__u64` etc., marcado M4_U64SHIM2) porque en host glibc no vienen; el cross-build no lo necesita (sysroot). Docs+libdrm-dev añadidos al host.
- **Task 4 (DXVK WSI): PENDIENTE** — con el GO, el siguiente corte: DXVK-native como objetos estáticos para PS5 (WSI sobre VK_KHR_display).
- **Task 5 (port host -ps5 + build): PENDIENTE**.

## SPLASH CERRADO (28-10 mediodía)

- **Resuelto.** Eran tres capas apiladas: (1) `Glyph()` decodificaba MSB-first y la tabla es **LSB-first** (bit 0 = píxel izquierdo, filas top-down) → cada glifo espejado; (2) a `kFont` le faltaba **la fila `<` (0x3C)** → todo ≥ '<' indexaba +1 (dígitos/espacio/`:` intactos — "QT5" conservaba el 5; las minúsculas "parecían flippeadas" porque +1 las mapea a sus gemelas p↔q — reporte del usuario confirmado como síntoma, no como bug aparte); (3) `Text()` tenía el reverse a ciegas → orden invertido. Fix: `(1u << col)`, fila `<` insertada (label `=` corregida), `Text()` orden natural. **Verificado con burst**: título/subtítulo/créditos legibles (burst_6). Línea TEST eliminada; build final enlazado.
- Nota de método: veredicto cerrado con ground-truth offline ANTES de tocar el C++ (`splash_fonttest.py` → `fonttest_td_lsb.png` = "TEST 2JRS" legible entre las 4 combinaciones). El PIL-test del "S" de anoche era trampa: S espejado se sigue leyendo S.
- Working tree: commit limpio del splash (splash.cpp/h + hooks kernel_video/gpu_hw_d3d11 + CMakeLists). `tools/ps5/vkinfo/main.c` + `entrypoints.c` quedan SIN commitear (spike Task 3; puede estar a medio editar — decidir al retomar Task 4).

## Task 4 (en curso, 28-10 tarde) — DXVK-native estático para PS5, WSI display

- **Diseño del backend `display`**: un solo display (la TV de sceVideoOut), tokens HWND/HMONITOR=1 acuñados por el video shell del port (Task 5); resolución via env `TTT2_VIDEO_W/H` (sin round-trip Vulkan para las coordenadas de desktop); el presenter resuelve `vkCreateDisplayPlaneSurfaceKHR`/enumeración por instancia con `vki->sym(...)` (el InstanceFn de DXVK no lleva KHR_display y el loader NO se toca: en Task 5 el port enlaza `vkGetInstanceProcAddr` como trampolín sobre `vk_icdGetInstanceProcAddr` — patrón entrypoints.c ya commiteado en e53efe1).
- **Verificado contra el spike antes de diseñar**: `VK_KHR_display` rev 23 y `VK_KHR_surface` rev 25 a nivel instance + **`VK_KHR_swapchain` rev 70 a nivel device** en `m4-radv-ps5-vulkaninfo.txt` → el flujo estándar de DXVK (surface KHR_display → swapchain) aplica sin recortes.
- **Parche** `dxvk-native-1.9.2b-ps5-display-wsi.patch` generado como dif en-caché sobre la base de los 11 parches del manifest aplicados en orden (flujo: aplicar en el espejo → commit base → editar → `git diff --cached`). Añade: backend `display` completo (mode/monitor/presenter/window), `native_display.h`, `dxvk_display_exts.cpp` (extensiones instance: KHR_surface+KHR_display), espejos estáticos `dxgi_static`/`d3d11_static` (los targets reales son shared_library), `lib_vulkan` opcional (required:false; en consola lo sustituyen los trampolines del Task 5).
- **Ruling (LF):** los .patch del repo habían salido CRLF por autocrlf → `git apply` los da por corruptos. Normalizados a LF todos; el espejo con `core.autocrlf=false` y árbol reescrito en LF. La normalización cambia la firma del manifest → cache de preparación nuevo (esperado).
- **Cross:** `ps5-cross-dxvk.ini` con system=linux (código del camino M2; la PS5 es x86-64), cpp = wrapper `prospero-clang18` (c++/v1 primero, -femulated-tls), headers Vulkan 1.4 desde el árbol Mesa (`-isystem`), stubs vacíos `libvulkan.a`/`libpthread.a` solo para configurar (Task 5 los sustituye por los de verdad), solo librerías estáticas (los .so no se construyen).

- **Task 4 (DXVK WSI): COMPLETE (28-10).** Cross-build verde con los 8 estáticos y entry points exportados (D3D11CreateDevice/AndSwapChain, CreateDXGIFactory/1/2; 2.108 símbolos T; miembros completos en disco — el formato lean del prospero-ar es apariencia; Task 5 enlaza en WSL desde el build dir; los copys de `.superpowers/m4-dxvk-ps5/` son referencia). Iteraciones: cross-compiler correcto = prospero-clang++ del PROPIO SDK (el prospero-clang18 externo no resuelve prospero-lld); el build dir de meson ata el source-path del primer configure → caché nuevo exige rm; la preparación de fuente vive ya en el lado WSL (fs nativo, sin /mnt/c en el camino de compilación).
- **Task 5 (port host -ps5 + build): SIGUIENTE.**
- Recordatorio de protocolo: aviso al usuario ANTES de cada lanzamiento/captura automática.

## CHECKOLL Task 5 (28-10 noche) — estado honesto

- **COMPLETOS Y SÓLIDOS:** `host_thread_ps5.cpp`, `audio_output_ps5.cpp`, `host_window_ps5.{h,cpp}`, `host_input_ps5.cpp`, `host_runtime_ps5.cpp`, `kernel/sce_ps5.h` — todos espejando los contratos de los Linux ya validados.
- **A MEDIA TINTA (NO compila):** `host_vm_ps5.cpp` — la primera mitad está escrita (Armed/Dirty, pipe, spin-wake, single-step) pero con campos inventados del signal context (`__mc_saved_registers.??`): FALTA sondear la `ucontext_t`/`siginfo_t` de la libc del SDK (familia BSD vs Linux gregs) antes de cerrar el fichero. Faltan además: CancelNullPageStep, ReserveGuest (con el fork de memoria directa para el alias del espejo — NO memfd), ReleaseGuest, y las primitives restantes (QueryReadRegion con memcpy del rango informado, Protect, ResetWriteWatch, TakeWriteWatch, PrepareHostWrite, ProtectForRollback, LastError).
- **PENDIENTES DE LA LISTA ANTERIOR:** host_fault_ps5, 2 gates (#error con TTT2_HOST_PS5), trampolines Vulkan, script de build+link.

**Próximo paso concreto (otra sesión):**
1. Sondear la libc: `prospero-nm target/lib/libkernel.so | grep -E 'sigaction|sigaltstack'` + los headers de signal/ucontext/sys/_types — el mapa de la superficie de señales.
2. Terminar host_vm_ps5.cpp con eso (el swap syscall está diseñado: pipe libc + spin-yield, alias en memoria directa para el espejo).
3. Resto de los forks + script de build + PRIMERA compilación del ELF del port sobre los estáticos del Task 4.
4. Task 6 = subir por FTP (:2121, ftpsrv residente) y lanzar por 9021 (elfldr) → la primera imagen del port en la TV.

## Task 5 (en curso, 28-10 tarde) — host `-ps5` del port

**Escrito (port/src/runtime/):**
- `host_thread_ps5.cpp` — pthreads sin SDL, semánticas 1:1 del impl SDL (gate de arranque, cancel-on-Close, detach, Pause=_mm_pause, id=hash de pthread_t; PinToGuestCpu=false como el portable).
- `audio_output_ps5.cpp` — API idéntica de audio_output.h sobre sceAudioOut bloqueante (stereo S16 downmix, crédito +1 por submit completado = regla de OnStreamDataComplete; reloj silencioso si falla el device). Define ConvertAudioPcm (el SDL audio_output.cpp queda EXCLUIDO del build PS5).
- `host_window_ps5.{h,cpp}` — shell de video: una TV (VideoOut MAIN), 2 buffers BGRX exclusivos en memoria directa (allocate+map, 1 MiB) para splash/fallback + token 1 como HWND de la WSI `display` (kDisplayWindowToken en la cabecera del port).
- `host_input_ps5.cpp` — scePad → Snapshot bloqueado; botones → valores Xbox del ABI (cross=A, circle=B, square=X, triangle=Y).
- `host_runtime_ps5.cpp` — layout fijo /data/homebrew/ttt2 (game_files_x + saves/config/cache/logs), sin /proc ni fork/exec; guard de página 0 desactivado por plataforma; Ps5Notify (toast, ciclo M3).
- `kernel/sce_ps5.h` — declaraciones a mano (convención escena, como el spike M3): Notification, AudioOut, VideoOut (RegisterBuffers2/SubmitFlip2), Pad, memoria directa.
- `host_window_sdl.h` ajustada: ya no incluye SDL (fwd-decl `struct SDL_Window;`) — el contrato compila sin gestor de ventanas.

**Pendiente del Task 5:**
1. `host_file_ps5` (fork de host_file_linux; O_PATH→O_RDONLY|O_DIRECTORY; leer host_file.h + host_file_common primero).
2. `host_vm_ps5` — el fork serio: write-watch de Linux = /proc soft-dirty → en PS5 por mprotect+SIGSEGV (path de rollback). Leer host_vm_linux.cpp COMPLETO antes.
3. `phys_mirror` posix (revisar si phys_mirror_linux.cpp ata a __linux__).
4. `host_fault_ps5` (copia del linux sin CancelNullPageStep).
5. Relax de gates (#error) en host_random_linux / host_storage_linux con TTT2_HOST_PS5.
6. Trampolines Vulkan (vkGetInstanceProcAddr→vk_icdGetInstanceProcAddr + vkGetDeviceProcAddr vía GIPA; patrón entrypoints.c).
7. `tools/ps5/build_port_ps5.sh` — lista explicit del target Linux (CMakeLists 269-282 + kernel glob excl. windows-y + gpu_render/native_d3dcompiler + PPC con splitter) + radv-link recipe + elfldr 9021.
