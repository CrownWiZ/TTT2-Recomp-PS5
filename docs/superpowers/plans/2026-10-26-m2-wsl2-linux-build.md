# Plan de implementación: M2 — build Linux funcional en WSL2 (DXVK-native + LAVAPIPE)

> **ESTADO: COMPLETADO (26-10-2026).** Ver sección "Cierre M2" al final. Tasks 1-8 ejecutados con result notes; los checkboxes de abajo son el registro original del plan (los desvíos practicados están en las notas del cierre).

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Demostrar portabilidad: el port compila como ELF Linux (`tekken_recomp_linux`) y arranca en WSL2 con el pipeline HW real del port (D3D11 → DXVK-native → Vulkan → LAVAPIPE software), descubriendo los mínimos de bibliotecas que M3/M4 reutilizarán en la PS5.

**Architecture:** Se reutiliza la rama multiplataforma del upstream SIN modificaciones salvo decisiones de entorno: `tools/prepare_dxvk_native.py` prepara el source DXVK pinneado y parcheado; `tools/build_dxvk11_linux.sh` construye DXVK-native (WSI=sdl3) y el target `tekken_recomp_linux` de `port/CMakeLists.txt` (sección Linux, línea 33+); `tools/run_dxvk11_linux.sh` + `tools/step8_paths.sh` definen el layout de ejecución (todo se rige por variables `TTT2_*`, sin tocar scripts). El checkout de trabajo vive en el FS nativo de WSL (`~/ttt2`, rsync del checkout Windows sin datos de juego); los datos del juego se leen SOLO LECTURA desde `/mnt/c/ttt2/...` (la instalación validada de M1). El renderizado visual es de baja fidelidad por diseño: la validación gráfica real es en consola (M4+), y el documento del spec lo fija así.

**Tech Stack:** WSL2 + Ubuntu 24.04 (WSLg para ventana X11/Wayland), clang/clang++ + lld + Ninja + CMake, Meson (DXVK-native + vkd3d), SDL3 3.4.0 (fuente ya en el checkout), vkd3d 1.16 (fuente, prefix `usr/`), FFmpeg n7.1 configure mínimo (wmapro/xma1/xma2), Mesa LAVAPIPE (`mesa-vulkan-drivers`) + `libvulkan1` + `vulkan-tools`.

**Spec:** `docs/superpowers/specs/2026-10-26-ttt2-ps5-port-design.md` (hitos M2, líneas 113-120) + `docs/superpowers/m0-platform-surface.md` (mapa de contrapartes `_linux`).

## Global Constraints

- NUNCA se commitean assets del juego ni derivados (`re\`, `install-test`, `Updates`, binarios del juego): ni en el checkout Windows ni en el de WSL se suben a remoto.
- El build Linux NO debe modificar fuentes del port (port/) ni scripts upstream: toda personalización va por variables de entorno que los scripts ya aceptan (`TTT2_BASE_ROOT`, `TTT2_BUILD_JOBS`, `TTT2_LINUX_GAME_BUILD`, `TTT2_VKD3D_ROOT`, `TTT2_OVERRIDE_LINUX_SDL` si existe). Si un cambio de fuente resulta imprescindible, se registra en el ledger como ruling ANTES de tocar.
- Todos los commits se hacen desde el checkout Windows `C:\ttt2` (git identity inline `-c user.name=saull -c user.email=saullago8888@gmail.com`); los ficheros bajo `docs\` y `.superpowers\` requieren `git add -f`.
- WSL2 se instala con la acción HUMANA del Task 1 (requiere admin + posible reinicio). El agente no eleva privilegios.
- Criterio de éxito funcional, no visual: el juego ARRANCA (menú principal visible en ventana WSLg, lógica viva, sin crash, cierre limpio). El framerate no se mide (spec M2: "el framerate será bajo y no importa").
- El pipeline HW es el default desde M1: en el run de WSL se verifica VULKAN ANTES de lanzar el juego; si Vulkan no enumera dispositivo, `Guarded` en hosts no-Windows hace `std::exit(EXIT_FAILURE)` (política documentada en el plan M1) — no es crash, es el comportamiento esperado; se corrige el entorno, no el código.

## Review Focus

1. **Vulkan ausente en WSL** → el proceso muere con mensaje `[gpu-hw]` (esperado por diseño). Test: Task 8 exige `vulkaninfo --summary` con `driverName` no vacío ANTES de lanzar el binario.
2. **FFmpeg misconfigurada (falta xma/wmapro)** → el CMake de `third_party/ffmpeg_xma` falla o el juego no decodifica audio con crash en cinemática. Test: Task 6 comprueba `config_components.h` con `CONFIG_XMA2_PARSER`/decoders = 1 (idéntico patrón al Task 5 de M1) y el run de Task 8 reproduce la cinemática sin abort.
3. **Rutas con 9P (trabajo sobre /mnt/c)** → build interminable. Test: Task 2 exige rsync del árbol de build al FS nativo (`~/ttt2`) y `find ~/ttt2/port -name CMakeLists.txt` en el FS Linux; los pasos de build miden tiempo Wall (<15 min en total en 12 cores; si excede, parar y revisar dónde corre).
4. **Datos del juego modificados desde WSL** → viola la regla de sólo lectura. Test: Task 2 prepara `/mnt/c` como sólo lectura contractual: los comandos de WSL jamás escriben fuera de `~/...`; Task 8 verifica MD5 de `tekken.img` antes/después del run (sin cambio).
5. **Binario Linux con artefactos de Windows** (CRLF en .sh de WSL, cache CMake contaminado) → el checkout rsync excluye `port/build-*`, `*.gen.cpp` no existe en Linux (regenerados en su build) y los .sh conservan LF (rsync binario desde NTFS preserva bytes; se verifica `file ~/ttt2/tools/build_dxvk11_linux.sh` → LF, no CRLF).

---

### Task 1: Instalar WSL2 + Ubuntu 24.04 (ACCION HUMANA)

**Files:**
- Create (estado del sistema, no commitear): instancia WSL `Ubuntu-24.04`.

**Interfaces:**
- Consumes: Windows 10 22H2+ / 11 con virtualización habilitada.
- Produces: `wsl -l -v` muestra `Ubuntu-24.04` y `wsl -e bash -lc` ejecuta comandos; WSLg presenta ventanas.

- [ ] **Step 1: El usuario (o el agente con ventana admin ya disponible) ejecuta, elevado:**

```powershell
wsl --install -d Ubuntu-24.04 --no-launch
```

Y reinicia SI Windows lo pide. Al primer arranque de Ubuntu se crean usuario Linux + contraseña (cualquier valor, p.ej. `ttt2/1234`, registrado en `env-audit.md`).

- [ ] **Step 2: Verificar estado y WSLg**

```powershell
wsl -l -v
wsl.exe -d Ubuntu-24.04 -e bash -lc "uname -r; echo DISPLAY=$DISPLAY; sudo -n true 2>/dev/null || echo 'sudo pide contraseña por ahora (normal la primera vez)'"
```

Expected: `Ubuntu-24.04` RUNNING; `uname -r` imprime kernel WSL2 (≠ terminado en "microsoft"); variable DISPLAY con valor (WSLg).

- [ ] **Step 3: Red y sudo sin prompt para el agente (se registra la contraseña en env-audit.md; si el usuario prefiere, Task 2 corre la I De sudo a mano):**

```powershell
wsl.exe -d Ubuntu-24.04 -e bash -lc "curl -sI https://github.com | head -n1"
```

Expected: `HTTP/2 200` (o similar). Sin red: revisar `/etc/wsl.conf` y DNS (resolv.conf).

---

### Task 2: Toolchain Linux + rsync del checkout al FS nativo de WSL

**Files:**
- Create (en WSL, no commitear): `~/ttt2/` (árbol de trabajo Linux), `~/prefix/` (instalaciones locales).

**Interfaces:**
- Consumes: checkout Windows `C:\ttt2` (Task 1-8 de M1 completados: `re\ppc` con 312 ficheros, `tools\XenonRecomp` con `XenonUtils`+simde, `tools\ffmpeg_dl\FFmpeg-n7.1`, `tools\SDL3\src\SDL3-3.4.0`, `port\`).
- Produces: `~/ttt2` con el árbol completo que consumen Tasks 3-7; `~/ttt2/re/ppc` con 312 ficheros.

- [ ] **Step 1: Instalar toolchain y runtime gráfico/de audio**

```bash
sudo apt update && sudo apt install -y --no-install-recommends \
  clang lld ninja-build cmake meson glslang-tools python3 python3-pip \
  git curl rsync file pkg-config bison flex \
  libvulkan1 vulkan-tools mesa-vulkan-drivers \
  libx11-dev libwayland-dev libxkbcommon-dev libwayland-egl-backend-dev wayland-protocols
python3 -m pip install --user --break-system-packages pillow
```

Expected: `apt` exit 0; `clang --version` y `vulkaninfo --version` responden.

- [ ] **Step 2: rsync del checkout (excluye build dirs, datos evitables y basura Windows)**

```bash
rsync -a --delete \
  --exclude 'port/build*' --exclude 'tools/XenonRecomp/build' \
  --exclude 'install-test' --exclude 'Updates' --exclude 're/decrypted' \
  --exclude '.git' --exclude '.superpowers' --exclude 'docs' \
  --exclude 'tools/steamworks_sdk_165' --exclude 'tools/SDL3/install' --exclude 'tools/SDL3/shim' \
  /mnt/c/ttt2/ ~/ttt2
test -n "$(python3 -m pip list 2>/dev/null | grep -i pillow)" && echo pillow-ok
file ~/ttt2/tools/build_dxvk11_linux.sh | grep -q CRLF && echo 'ATENCIÓN: CRLF en scripts' || echo LF-ok
find ~/ttt2/re/ppc -maxdepth 1 -type f | wc -l
```

Expected: `pillow-ok`, `LF-ok`, `312` (o 312 posicionales), CMakeLists de `~/ttt2/port` present. (`re/ppc` y `tools/XenonRecomp/XenonUtils` son entradas del build; `re/decrypted` NO se copia.

- [ ] **Step 3: Registrar en el ledger el inventario del árbol (pesos y top-level dirs) para las siguientes tareas.**

---

### Task 3: vkd3d 1.16 (fuente) en el layout `usr/` que el port espera

**Files:**
- Create (WSL): `~/.cache/ttt2-platform/vkd3d/{usr/include/vkd3d/vkd3d_shader.h,usr/lib/libvkd3d-shader.so*}`.

**Interfaces:**
- Consumes: meson + ninja (Task 2).
- Produces: prefix avecdad que `TTT2_VKD3D_ROOT` consume (CMakeLists del port: `find_path(HINTS .../usr/include/vkd3d)` + `find_library(vkd3d-shader HINTS .../usr/lib)`); el run script pone `.../vkd3d/usr/lib` en `LD_LIBRARY_PATH`.

- [ ] **Step 1: Clonar y construir**

```bash
mkdir -p ~/.cache/ttt2-platform && cd ~/.cache/ttt2-platform
git clone --depth 1 --branch v1.16 https://gitlab.winehq.org/wine/vkd3d.git vkd3d-src || \
git clone --depth 1 --branch v1.16 https://github.com/wine-mirror/vkd3d.git vkd3d-src
cd vkd3d-src && meson setup build --buildtype release --default-library=shared
ninja -C build
DESTDIR=~/.cache/ttt2-platform/vkd3d ninja -C build install
```

Expected: `vkd3d_shader.h` y `libvkd3d-shader.so` en `~/.cache/ttt2-platform/vkd3d/usr/{include/vkd3d,lib}`. Si la v1.16 no existe en el mirror real, usar la última etiqueta estable anunciada por el repo y ANOTARLA en el ledger.

---

### Task 4: SDL3 3.4.0 Linux en prefix local (rutas estándar del upstream, SIN shim)

**Files:**
- Create (WSL): `~/.cache/ttt2-platform/sdl3/usr/{include/SDL3/SDL.h,lib/cmake/SDL3/SDL3Config.cmake,lib/libSDL3.so}`.

**Interfaces:**
- Consumes: fuente `~/ttt2/tools/SDL3/src/SDL3-3.4.0` (rsync Task 2).
- Produces: `CMAKE_PREFIX_PATH=~/.cache/ttt2-platform/sdl3/usr` que consume Task 7 (find_package SDL3 3.4.0 CONFIG — el shim de Windows NO se usa: la instalación Linux exporta SDL3::SDL3-shared estándar).

- [ ] **Step 1: Construir e instalar**

```bash
cmake -S ~/ttt2/tools/SDL3/src/SDL3-3.4.0 -B ~/.cache/ttt2-platform/sdl3/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DCMAKE_INSTALL_PREFIX=~/.cache/ttt2-platform/sdl3/usr
ninja -C ~/.cache/ttt2-platform/sdl3/build
cmake --install ~/.cache/ttt2-platform/sdl3/build
```

Expected: `lib/cmake/SDL3/SDL3Config.cmake` existe; `pkg-config --modversion sdl3` (con PKG_CONFIG_PATH si toca) imprime 3.4.0.

---

### Task 5: DXVK Native (parches del upstream) vía `prepare_dxvk_native.py` + meson

**Files:**
- Create (WSL, caché): `~/.cache/ttt2-platform/shared-dxvk/source-<sig>*/` y `.../linux/{build,install}`.

**Interfaces:**
- Consumes: `~/ttt2/tools/patches/dxvk-native.json` + parches; meson, glslang (Task 2); red para el clon pinneado.
- Produces: `libdxvk_d3d11.so` + `libdxvk_dxgi.so` + headers nativos `include/native/directx/d3d11.h` que Task 7 requiere (inv/free: los consume por rutas exactas del script).

- [ ] **Step 1: Preparar el source (el script clona el pin y aplica parches, verificado por SHA)**

```bash
cd ~/ttt2 && python3 tools/prepare_dxvk_native.py
```

Expected: imprime/crea el source dir sin error de digest.

- [ ] **Step 2: Meson + install (las MISMAS flags del script upstream, replicadas para no alterar el .sh: Task 7 lanzará el .sh completo; en este paso se valida que las dependencias del toolchain bastan ANTES del build grande)**

```bash
source_root=$(python3 ~/ttt2/tools/prepare_dxvk_native.py)
meson setup ~/.cache/ttt2-platform/shared-dxvk/linux/build "$source_root" \
  --buildtype release --default-library=shared \
  -Denable_tests=false -Denable_d3d9=false -Denable_d3d10=false -Ddxvk_native_wsi=sdl3
ninja -C ~/.cache/ttt2-platform/shared-dxvk/linux/build -j8
meson install -C ~/.cache/ttt2-platform/shared-dxvk/linux/build --destdir ~/.cache/ttt2-platform/shared-dxvk/linux/install
```

Expected: `libdxvk_d3d11.so`/`libdxvk_dxgi.so` en `.../install/usr/lib` (o `install/lib`, el Task 7 lo pinta con el mismo layout que use el script real; registrar en el ledger el layout resultante).

---

### Task 6: FFmpeg n7.1 configure mínimo (XMA subset) para Linux

**Files:**
- Create (WSL): `~/ttt2/tools/ffmpeg_dl/build-linux-native/config.h` (y `ffbuild/config.sh`).

**Interfaces:**
- Consumes: fuente `~/ttt2/tools/ffmpeg_dl/FFmpeg-n7.1` (rsync).
- Produces: `FFMPEG_BUILD` dir que el CMakeLists del port exige (`EXISTS .../config.h`, líneas 200-201) y que `third_party/ffmpeg_xma` compila.

- [ ] **Step 1: Configure fuera-de-arbol EXACTAMENTE con el subset del upstream (la línea documentada en port\third_party\ffmpeg_xma\CMakeLists.txt y usada por el Task 5 de M1, cambiando `--toolchain=msvc --target-os=win64` por `--target-os=linux` y SIN --disable-asmenes si no topa)**

```bash
mkdir -p ~/ttt2/tools/ffmpeg_dl/build-linux-native && cd ~/ttt2/tools/ffmpeg_dl/build-linux-native
bash ../FFmpeg-n7.1/configure --target-os=linux --arch=x86_64 \
  --disable-everything --disable-programs --disable-doc \
  --disable-avdevice --disable-avformat --disable-swresample --disable-swscale \
  --disable-postproc --disable-avfilter --disable-network --disable-debug --disable-autodetect \
  --enable-decoder=wmapro,xma1,xma2 --enable-parser=xma
grep -E 'define CONFIG_XMA2_DECODER|define CONFIG_WMAPRO_DECODER|define CONFIG_XMA_PARSER' config_components.h
```

(Pasar `--disable-asm` SOLO si aparecen errores de instrucciones/ensamblador en el build a posteriori; el subset es igual que M1, sin `--toolchain=msvc` ni `--target-os=win64`.)

Expected: exit 0 y los tres `#define ... 1`.

---

### Task 7: Build de `tekken_recomp_linux` con el script upstream (sin modificaciones)

**Files:**
- Create (WSL, caché): `~/.cache/ttt2-platform/build-shared-linux/tekken_recomp_linux` (+librarian para el manifest).

**Interfaces:**
- Consumes: Tasks 3-6 (vkd3d, SDL3, DXVK, FFmpeg) + `~/ttt2/re/ppc` + `~/ttt2/tools/XenonRecomp/XenonUtils|simde`.
- Produces: ELF `tekken_recomp_linux` que Task 8 ejecuta; manifest de composición escrito por `write_shared_build_manifest.py`.

- [ ] **Step 1: Lanzar el script con los knobs de entorno**

```bash
cd ~/ttt2
export CMAKE_PREFIX_PATH=~/.cache/ttt2-platform/sdl3/usr
export TTT2_BUILD_JOBS=12
tools/build_dxvk11_linux.sh 2>&1 | tee /tmp/m2-build.log
```

NOTA: si el script core falla por rutas de vkd3d/SDL instaladas ≠ las suyas, se rige TODO con sus overrides (`TTT2_VKD3D_ROOT=~/.cache/ttt2-platform/vkd3d`, `TTT2_LINUX_GAME_BUILD`, `TTT2_DXVK_LINUX_BUILD/PREFIX`) antes de tocar el script. El script re-ejecuta meson/cmake ídem.

Expected: `Built shared Linux game: .../tekken_recomp_linux`; `file` del binario imprime `ELF x86-64`; manifest de composición escrito por `write_shared_build_manifest.py`.

- [ ] **Step 2: Commit del documento de entorno (si hubo knobs; nada más — el build product no se commitea).**

---

### Task 8: Validación funcional en WSL2/LAVAPIPE (cierre de M2)

**Files:**
- Create (WSL): logs en `~/ttt2/.superpowers/runs-wsl/` (o donde indique el run script).

**Interfaces:**
- Consumes: binario (Task 7), datos de juego SOLO LECTURA en `/mnt/c/ttt2/install-test/game/` (install validada M1) + `/mnt/c/ttt2/re/decrypted/Default_patched_tu3.bin` como imagen (o `tekken.img` de la install: usar MISMA imagen que M1 para comparar).
- Produces: evidencia de arranque (log + observación en pantalla) y veredicto de cierre del hito.

- [ ] **Step 1: Vulkan presente ANTES de lanzar (Review Focus 1)**

```bash
vulkaninfo --summary | grep -A2 driverName
```

Expected: al menos una entrada (lvp/lavapipe) con driverName. VACÍO = Task 8 falla aquí: apt de mesa no terminó o hay que exportar `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`.

- [ ] **Step 2: Lanzar el juego con los datos de M1 y perfil bajo (el audio va por Pulse de WSLg)**

```bash
cd ~/ttt2
export TTT2_IMAGE=/mnt/c/ttt2/install-test/game/tekken.img
export TTT2_VFS_ROOT=/mnt/c/ttt2/install-test/game
export TTT2_BINARY=~/.cache/ttt2-platform/build-shared-linux/tekken_recomp_linux
tools/run_dxvk11_linux.sh 2> /tmp/m2-run.log &
sleep 45 && grep -E '\[gpu-hw\]|\[io\] read #|Validation|error' /tmp/m2-run.log | head -n 20
```

Expected: ventana del juego visible en WSLg con el MENÚ principal; log con `[gpu-hw] active on` (device = lvp/lavapipe), lecturas `[io] read` creciendo, sin abort.

- [ ] **Step 3: Screenshot + round de humo (funcional, NO fidelidad)**

Captura de pantalla desde Windows (mismo método M1) archivada bajo `C:\ttt2\.superpowers\`; el usuario si quiere navega el menú (el mando aún no está cableado en Linux: el teclado mapea por SDL; si el menú no responde al teclado, registrar el comportamiento y no bloquear).

- [ ] **Step 4: Integridad de los datos tras el run (Review Focus 4)**

```powershell
(Get-FileHash C:\ttt2\install-test\game\tekken.img -Algorithm MD5).Hash
```

Expected: idéntico al pre-run (registrar el valor antes del Step 2).

- [ ] **Step 5: Cierre limpio, commit docs, push: plan con checkboxes, ledger, README del repo (si toca), commit (ficheros docs con -f), `git push origin main`.**

---

## Cierre M2 (26-10-2026)

**Resultado del hito: COMPLETADO.** `tekken_recomp_linux` (ELF x86-64, 100 MB) construido con `tools/build_dxvk11_linux.sh` SIN modificar (603/603 targets), y el juego corriendo en WSL2 con el pipeline HW real del port: D3D11 → DXVK-native → Vulkan 1.3 → LAVAPIPE (llvmpipe, Mesa 25.2.8). Menú principal, navegación por mando virtual scriptado, combate renderizado (hasta ~139k tris/fotograma a ~23 presents/s). Datos de juego intactos tras 5 runs (MD5 pre/post idéntico).

### Desvíos practicados sobre el plan (todos registrados con su porqué)

1. **WSL2 + Ubuntu 24.04 instalado por el agente** (no por el usuario): tras `wsl --update` del usuario (3.0.3.0), `wsl --install -d Ubuntu-24.04 --no-launch` corrió sin necesitar más elevación sobre la sesión del usuario. Task 1 resultó más corto de lo previsto.
2. **GitHub inalcanzable desde WSL** (network layer): resuelto por espejos — DXVK clona con `TTT2_DXVK_MIRROR` (mirror local en `C:\ttt2\.superpowers\dxvk-native-mirror`, montado desde Windows, donde GitHub sí responde); vkd3d se baja como **tarball oficial de `dl.winehq.org`** (los mirror-github de tags fallaron: prefijo real de tags es `vkd3d-x.y`, y la rama git no trae los headers generados).
3. **vkd3d elegido: 1.19 (tarball autotools)** — el plan editable decía "1.16/meson" para adaptar: la serie 1.17+ es solo autotools; el tarball trae headers pre-generados. Requiere `libjson-perl`, `mingw-w64-tools` (widl), `autoconf/automake/libtool`, `make` y headers Vulkan de Khronos ≥1.3.228 copiados a `/usr/local/include` (los `libvulkan-dev` de Ubuntu 24.04 son viejos; hay que añadir `vk_video/` también).
4. **SDL3 instalado además en /usr/local** (lib + `SDL3` headers + `sdl3.pc`): DXVK-native `find_library('SDL3')` usa glibc default paths, no pkg-config; y sus headers de WSI se resuelven con `CPATH=/usr/local/include`. Apta extra del host: `libxtst-dev` (XTest de SDL3).
5. **FFmpeg**: `--cc=clang --cxx=clang++` (gcc no instalado); el subset exacto de M1 funcionó igual: `--enable-decoder=wmapro,xma1,xma2 --enable-parser=xma` → `CONFIG_XMA2_DECODER/WMAPRO/XMA_PARSER = 1`.
6. **Layout DXVK `lib64` → symlink `lib`**: meson instaló en `install/lib64`; el script upstream y el CMake esperan `install/lib`. Symlink en vez de tocar scripts (constraint del plan respetado).
7. **OpenSSL dev necesario** en el host (`libssl-dev`, find_package OpenSSL 3.0 en `port/CMakeLists.txt:35`).
8. **Interacción de shell PS↔bash aprendida (a ledger)**: `$var` y `\$var` en one-liners se engullen entre capas — los scripts .sh van SIEMPRE por fichero en `.superpowers` + `tr -d '\r'` explícito por fichero, con nombres explícitos (el `tr` con backslash mal escapado borra todas las 'r' del fichero destino — diagnóstico por `od -c`).

### Hallazgos portables (input para M3/M4)

- **F1 (bug core del port, A/B confirmado por el usuario): saltar las cinemáticas/attract deja la voz de audio en loop** — Windows también lo hacía "antes" (verificación A/B del usuario). El decodificador honra el bit de loop del contexto XMA y el port nunca observa un reset/abort del stream (`resets=0` en toda la sesión en Linux). Pista nº 1: wiring de abort/stop de voz (contextos XMA2 en A0001000, mecanismo de reset del juego) — reparable en el port con beneficio directo PC/PS5.
- **F2 (Linux/WSL): el streaming de audio del menú-idle se atasca tras el logo** (anillo XMA lleno, consumidor parado, counters congelados en 2048); en gameplay con avance del flujo el audio fluyó (kicks 23k+, loops de música creciendo). WSLg→Windows el trayecto de audio es correcto (el logo sonó). Investigación con `TTT2_XMA_LOG=1` pendiente — el pad virtual (`TTT2_SDL_TEST_PAD_SCRIPT=<ms>:<BTN>:<hold>,...`) permite reproducirlo sin input físico.
- **F3 (a clasificar): el usuario reportó "buggeado al escoger equipo"** — captura pendiente en qué plataforma y qué síntoma exacto (congelación/texturas/no-responde).
- **F4 (estructura WSL2)**: el USB/HID de Windows no llega a WSL2 sin `usbipd-win` — el mando físico no puede control el juego ahí; el pad virtual scriptado cubre las pruebas (y el teclado no está mapeado: el host input Linux es SDL_Gamepad-only).
- **Rendimiento en software Vulkan aceptable**: menú ~30-40 presents/s, combate ~22-23, GPU llvmpipe consume ~3.5-6 cores. En PS5 (RADV hardware) sobra.

### Evidencia

- Logs: `.superpowers/m2-wsl-run{1,2,3,4,5w}.log` (run1: boot+menú+audio-stall; run2: pad virtual + audio en gameplay; run3/run4: secuencias; run5w: ventana limpia del usuario, cinemática attract audible, loop al saltar, bugeo en selección de equipo).
- Binarios: `~/.cache/ttt2-platform/build-shared-linux/tekken_recomp_linux`; dependencias en `~/.cache/ttt2-platform/{vkd3d,sdl3,shared-dxvk}`.
- Manifest del build: sha de source Linux `af44c09e...` + DXVK `c362b402...`.

### M2 → M3/M4

- El **pipeline exacto de PS5 ya está compilado y vivo en Linux** (DXVK-native + vkd3d + SDL3 + FFmpeg XMA): M3/M4 solo cambian el host (env `-ps5`) manteniendo esta combinación de bibliotecas.
- Los fallos de red de WSL against GitHub obligan a M3/M4 a tener todos los tarballs/mirrors **locales** antes del cross-build.
