# M0 — Informe de superficie de host-por-plataforma

Fecha: 2026-10-26 · Ámbito: `port/src/runtime` (~170 archivos) + scripts Android de `tools/`

Qué significa cada cosa para el port a PS5. Categorías:

- **X = reutilizable tal cual**: lógica del juego/kernel portable, no toca el OS.
- **Y = capa por plataforma, con patrón existente que se copia o se fuerza**: ya
  existe un `_linux` o un `_android`/`_sdl` que hace el mismo trabajo; para PS5
  se añade el análogo `_ps5` (o se reutiliza el `_linux` con ajustes menores).
  El trabajo real del M3–M4 vive aquí: unos 8 a 12 módulos pequeños.
- **Z = exclusivo Windows, se descarta en PS5**: los `_windows` (el CMake del
  upstream ya selecciona por plataforma).

## Escaneo por carpeta

### port/src/runtime (capa física del host)

| Archivo | Categoría | Nota para PS5 |
|---|---|---|
| guest_memory.h, guest_time.*, host_clock.h, runtime_math.cpp, skin_* y packed_field_native.h | X | portable tal cual |
| host_runtime_linux.cpp | Y | base directa para `_ps5` (el entorno ps5-payload-dev es casi POSIX) |
| host_vm_linux.cpp / host_vm_android.cpp | Y | el patrón `_android` es la referencia 64-bit; el mmap flexible viene del `_linux`; en PS5 se apoya en memoria directa (`sceKernelAllocateDirectMemory`, como en Prospero) |
| host_fault_linux.cpp | Y | SIGSEGV del mismo estilo → `_ps5` |
| host_file_linux.cpp | Y | FS POSIX del homebrew → `_ps5` |
| host_random_linux.cpp, host_process.cpp, host_sleep.cpp | Y | POSIX trivial o interfaz SDL existente |
| host_storage_linux.cpp | Y | rutas de guardado/content → `/data/homebrew` |
| host_udp_linux.cpp | Y | sockets BSD, reuso casi directo |
| host_input_sdl / host_window_sdl | Y | en PS5 se descarta SDL como shell (decisión del diseño): shell mínimo propio sobre `sceVideoOut` + `scePad` |
| host_window_win32*, *_windows.* (fault, file, random, runtime, storage, udp, vm) | Z | descartados en PS5 |
| game.rc.in | Z | recursos de Windows |
| main.cpp | Y | punto de entry análogo a `android_game_entry.cpp` → `ps5_game_entry.cpp` |

### port/src/runtime/kernel (traducción de la API del juego)

| Archivo | Categoría | Nota para PS5 |
|---|---|---|
| kernel_content/io/memory/misc/objects/thread/video/xam/xsession/xsocket/xlive… | X | la capa independiente del guest; no cambia entre plataformas |
| gpu_hw_d3d11.h + gpu_hw_* (formatos, traducción, profiling) | Y | la interfaz sobre la que DXVK-native provee D3D11 real; en PS5 se enlaza DXVK-native estáticamente (decisión del diseño) |
| native_d3dcompiler.cpp + d3dx_ctab_cache.cpp | Y | en Linux/Android ya tienen sustituto vía vkd3d-shader; PS5 sigue esa vía |
| audio_output.cpp + kernel_audio.cpp | Y | hoy usan la salida de audio del host; PS5 → `sceAudioOut` |
| steam_bridge*.cpp | Y | en PS5 se usa el patrón stub (`steam_bridge_linux_stub.cpp`); online es M6 |
| rollback* / matchmaking / server_* / community_* | X | portable; se retoma en M6 |
| save_store.cpp | Y | en PS5 escribe en `/data/homebrew` |

### Scripts Android (referencia de cross-compile; patrón guía para PS5)

| Script | Contrapartida PS5 |
|---|---|
| tools/build_android_dxvk_native.sh | build_ps5_dxvk_native.sh (adaptado a Prospero) |
| tools/build_android_ffmpeg_xma.sh | build_ps5_ffmpeg_xma.sh |
| tools/build_android_ppc_archive.py + prepare_android_ppc_headers.py | equivalentes ps5 |
| port/tools/build_android_vkd3d_shader.sh | build_ps5_vkd3d_shader.sh (target Prospero del fork Nazky) |

## Conclusión de superficie

Lo grande no vive en la capa de plataforma. El núcleo del port (kernel
portable, renderizador con interfaz separada) ya es independiente del OS. El
trabajo de plataforma para PS5 se condensa en una lista corta de módulos Y,
todos con precedente: runtime/threads/faults/file/udp (casi POSIX puro),
vm (memoria directa estilo Prospero), input/window (shell mínimo sobre
`sceVideoOut` y `scePad`), audio (`sceAudioOut`), settings/storage (rutas de
`/data`), y el andamiaje de build (scripts al estilo de los Android).

**La pieza que el upstream NO ha cubierto en PS5** es el par DXVK-native
estático + RADV del Mesa del scene + WSI sobre `VK_KHR_display`: es el riesgo
mayor declarado en el spec, y no es un problema de superficie de archivos —
va al M4 con spike propio antes de portar nada de gameplay.
