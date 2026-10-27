# SDD ledger — plan: docs/superpowers/plans/2026-10-26-m2-wsl2-linux-build.md

Ruling (setup): ejecutar sobre el checkout principal `C:\ttt2` (main) sin worktree — mismo criterio que M1 (consentimiento del setup anterior; ningún cambio de código fuente, solo docs+scripts .superpowers).

Ruling (Task 1): WSL2 no estaba instalado; tras `wsl --update` del usuario (WSL 3.0.3.0, kernel 6.18.54.1), el agente corrió `wsl --install -d Ubuntu-24.04 --no-launch` sin nueva elevación. WSLg vivo tras `wsl --shutdown` (DISPLAY=:0, WAYLAND_DISPLAY=wayland-0, PulseServer montado). Usuario Linux = root (contraseña no requerida para los comandos del agente).

Ruling (red WSL): **github.com NO responde desde dentro de WSL** (ls-remote/clone/timeout ciegos); gitlab.winehq.org y dl.winehq.org sí. Para el espejo de DXVK se usa `TTT2_DXVK_MIRROR` (clone `--shared` desde `C:\ttt2\.superpowers\dxvk-native-mirror` clonado desde Windows; commit pinneado c8dc91f javerificado). Implicación M3/M4: todos los tarballs deben existir localmente antes del cross-build.

Ruling (quoting PowerShell↔bash): los `$var` y `\$var` de one-liners se destruyen entre capas; y un `tr -d "\r"` con el backslash mal consumido convierte `tr -d r` y BORRA todas las 'r' del destino (diagnóstico por `od -c`: /root→/oot, configure→configue). Protocolo establecido: scripts .sh escritos siempre por el write-tool en `C:\ttt2\.superpowers\`, transferidos con `tr -d '\r'` (la variante `'\r'` con single-dentros-de-double SÍ funciona, probada 5x) y nombres explícitos sin variables.

Task 1: complete (WSL2 + Ubuntu 24.04 corriendo; 955 GB en /. Task 2 (toolchain+rsync): complete (clang 18.1.3, ninja, cmake, meson, glslang, vulkan-tools; LAVAPIPE = llvmpipe Mesa 25.2.8 conformance 1.3.1; rsync 1.6 GB a ~/ttt2, re/ppc 312 ficheros, fix CRLF→LF sobre *.sh/*.py; apt añadido después por fallos runtime: libxtst-dev, libssl-dev, make, autoconf/automake/libtool, libjson-perl, mingw-w64-tools, libvulkan-dev, pulseaudio-utils).

Task 3 (vkd3d): complete con desvío registrado — vkd3d-1.19 tarball de https://dl.winehq.org/vkd3d/source/ (autotools, headers pre-generados; requiere widl+libjson-perl; configure con --disable-dependency-tracking; los headers Vulkan de Ubuntu son viejos → Vulkan-Headers + SPIRV-Headers + **vk_video** de Khronos copiados a /usr/local/include). .../vkd3d/usr/{include/vkd3d,lib} ✓ (lib + lib64-shape no; headers y libs OK).

Task 4 (SDL3): complete (3.4.0 fuente del checkout; clang+Ninja; prefix ~/.cache/ttt2-platform/sdl3/usr; además copiado a /usr/local {lib,include/SDL3,pkgconfig} porque DXVK-native usa find_library estándar y CPATH para headers WSI).
Task 5 (DXVK-native): complete (mirror local → source pinneado parcheado → meson WSI=sdl3 → ninja 178/178 → meson install; libs en install/lib64, **symlink install/lib→lib64** para el layout que exigen script y CMake).
Task 6 (FFmpeg): complete (configure out-of-tree en tools/ffmpeg_dl/build-linux-native con --cc=clang --cxx=clang++; CONFIG_XMA2_DECODER/WMAPRO_DECODER/XMA_PARSER = 1 ✓ — el subset exacto de M1).
Task 7 (build juego): complete — `tekken_recomp_linux` ELF x86-64 100 MB, 603/603 targets, por tools/build_dxvk11_linux.sh SIN modificar + manifest (source af44c09e, DXVK c362b402). ENV usados: TTT2_VKD3D_ROOT, CMAKE_PREFIX_PATH (sdl3), TTT2_BUILD_JOBS=6.

Task 8 (validación): complete con hallazgos — run1 boot+menú+\[gpu-hw\] llvmpipe B000 + swap flip 1280x720 ~40 presents/s; cinemática Bandai animada y audible (WSLg audio correcto); audio del streaming de menú se atasca tras el logo (F2); mando virtual scriptado `TTT2_SDL_TEST_PAD_SCRIPT` navegó menús y combate (F4: sin input físico en WSL2, sin mapping de teclado); combate renderizado ~139k tris a ~23 fps sobre llvmpipe; run4: hang a ~5s (no reproducible aún, no-OOM). MD5 de tekken.img pre/post = D89DD7D64D83A5CE58D079B4919AC443 (datos intactos). Evidencia: .superpowers/m2-wsl-run{1..5w}.log.

## Cierre M2 (2026-10-26)

- Hito: **COMPLETADO** — build Linux funcional por el script upstream sin modificaciones; juego corriendo en Vulkan software sobre el pipeline D3D11 real del port; datos de juego solo-lectura intactos.
- F1 (bug core port, A/B usuario): skip de cinemática/attract → voz de audio en loop. Windows también lo hacía antes (el usuario lo confirmó). El port nunca observa resets de contexto XMA (`resets=0` en toda la sesión). Fix candidato: cablear el mecanismo abort/stop de voz (XmaRelease/Reset) en runtime/kernel/xma.cpp + xma_core.
- F2 (Linux/WSL): audio de menú-idle atascado tras el logo (anillo lleno, consumidor parado); en gameplay con avance sonó (kicks 23k+, loop de música). Diagnóstico futuro con TTT2_XMA_LOG=1. Reproducción sin pad físico: mando virtual.
- F3 (sin clasificar): "buggeado al escoger equipo" — plataforma y síntoma exacto por confirmar con el usuario (posible encadenado con F1).
- F4 (estructura WSL2): USB/HID invisible sin usbipd-win → el pad físico no controla en WSL (el teclado tampoco: host_input_sdl es SDL_Gamepad-only). Para PS5 no aplica (Linux-on-PS5 ve /dev/input directo).
- Aprendizajes de shell registrados arriba (constitución de protocolo PS↔bash en futuros hitos).
- Nota de rendimiento: LAVAPIPE con llvmpipe basta para validación funcional; la consola usará RADV sobre hardware real (M4).

## F2 RESUELTO (27-10-2026) - condicion de entorno, no bug del port

Serie A/B completa (run1-run7 en WSL + run9n nativo + runs Windows del usuario):

| Entorno | IO | starved | max_callback_us |
|---|---|---|---|
| WSL2 + 9P (/mnt/c) run4/run8w | 9P | juego congelado ~5 s (stats parados) | 12892 |
| WSL2 + FS nativo run9n | ext4 | 270 starves, 5.4 MB perdidos | 6809 |
| Windows (run8/9/10.txt del usuario) | NTFS | 0 en las 6 salidas | 290-1040 |

- **Hipotesis 9P REFUTADA**: con IO nativo el audio sigue muriendo de hambre igual. La causa es la presion de CPU de llvmpipe + planificacion de WSLg/PulseAudio sobre el callback de audio; ni el FS ni PULSE_LATENCY_MSEC=80 lo arreglan.
- **Windows limpio por defecto**: los 3 runs Windows de A/B que hizo el usuario (run8/9/10.txt en C:\ttt2, ademas de run2..7) no tienen ni una starve. Baseline perfecto.
- Los "hangs" de run4/run8w eran el juego congelado (stats parados) con cierre por ventana del usuario ("[window] closed - quitting"), no crash del proceso.
- **Veredicto F2: cerrado como condicion de entorno WSL2+LAVAPIPE.** No aplicable a PS5 (RADV sobre hardware real + sceAudioOut nativo, sin llvmpipe ni PulseAudio). Mitigaciones si se repite en WSL: LP_NUM_THREADS=2 + PULSE_LATENCY_MSEC=80 (mejoran, no curan).
- Evidencia: /root/run9n.log (WSL, FS nativo), C:\ttt2\run{8,9,10}.txt (Windows, usuario).
- Quedan abiertos: **F1** (skip cinematatica -> voz en loop; bug CORE, tambien en Windows; los loops=127/240 de los runs Windows son los loops legitimos del attract, el bug es que no se aborta la voz al saltar; resets=0 siempre = mecanismo de reset sin cablear) y **F3** (sin clasificar).
## F1 Y F3 RESUELTOS (27-10-2026) - no reproducibles en el build actual; el usuario confirma Windows limpio

Sesion de continuacion (fase 3 del usuario = run9). Tras cerrar F2 se ataco F1 con trazas de voces en ambas plataformas:

- **WSL, skip scriptado (m3-f1a-wsl-skip-trace.log)**: pad virtual pulsa A a los 100-112 s durante la cinemática; traza TTT2_XMA_LOG=1 completa. Resultado: el juego libera las voces por el camino de teardown (sub_82D3DE40 -> XMAReleaseContext, lr=82D3DE80, bt 82D3E600 82D08794 82D05648...), el contador de loops se CONGELA tras el skip (53 para siempre) y ninguna voz sigue decodificando. El decodificador no repite nada.
- **Windows, traza completa con round jugado (m3-f1w-windows-trace.log)**: TTT2_XMA_LOG=1 + pad script (A cada 5 s, 45-90 s); el usuario navego, salto la cinematica y JUGO UN ROUND COMPLETO que califica de perfecto. Loops creciendo lineal (0->154, BGM legitima), cero voces atascadas, releases/creates normales por escena.
- **Veredicto usuario**: "me he pasado un round entero perfecto el problema es de linux claramente"; skip en Windows -> sin loop; el bugeo de seleccion de equipo (F3) -> solo en WSL.
- **F1: CERRADO - no reproducible en el build actual.** Los reportes historicos de loop se pliegan en F2 (audio hambriento de WSL glitchando) o en builds anteriores ya sustituidos. Los loops=127/240/484 de los runs eran loops legitimos del attract/BGM.
- **F3: CERRADO - solo en WSL** = carga lenta de combate bajo llvmpipe + audio hambriento; en Windows nunca se ha visto.
- **Balance del bloque de audio**: el port esta sano en audio (y en gameplay) en Windows; todo el catalogo de sintomas de WSL (loop, desync, silencio, colgon de seleccion de equipo) es F2 = entorno WSL2+LAVAPIPE, inexistente en PS5 (RADV hardware + sceAudioOut). No hay fix que hacer en el port.
- Evidencia anadida: .superpowers/m3-f1a-wsl-skip-trace.log, .superpowers/m3-f1w-windows-trace.log, /root/run9n.log (WSL), C:\ttt2\run{8,9,10}.txt (usuario, Windows).