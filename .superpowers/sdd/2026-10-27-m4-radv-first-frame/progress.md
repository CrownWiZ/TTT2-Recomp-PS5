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
