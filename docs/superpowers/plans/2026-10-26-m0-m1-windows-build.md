# Plan de implementación: M0+M1 — build del juego en Windows (base validada)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dejar el port de TTT2 compilado desde fuente y funcionando en el PC Windows del usuario usando SU copia del juego (imagen de disco + TU3), punto de referencia para todas las fases PS5 posteriores.

**Architecture:** Ejecución de la pipeline upstream documentada en `BUILDING.md` en `C:\ttt2`: recompilación estática del XEX con XenonRecomp (parcheado), generación de C++ desde el Default.xex + TU3, FFmpeg n7.1 para XMA, build con `tools\build_port.cmd`, generación de datos del juego con el instalador, arranque de validación con `TTT2_VFS_ROOT`.

**Tech Stack:** Visual Studio 2022 Build Tools (MSVC x64), LLVM/Clang (clang-cl), CMake + Ninja, Python 3.10+ con Pillow, XenonRecomp @ `ddd128b`, FFmpeg n7.1, Git.

**Spec:** `docs/superpowers/specs/2026-10-26-ttt2-ps5-port-design.md` (hitos M0 y M1)

## Global Constraints

- Checkout del proyecto SIEMPRE en `C:\ttt2` (los scripts upstream asumen esa ruta exacta; ya está cubierta).
- NUNCA se commitean assets del juego ni archivos generados desde la copia del usuario (`re\`, `Updates\`, `port\build-*\`, datos generados). Prohibido subirlos a ningún remoto.
- La copia del juego debe ser el disco retail **Tekken Tag Tournament 2 (USA/Europe)** de Xbox 360; el instalador la valida y rechaza otras regiones.
- No se instala Steamworks SDK en esta fase (decisión del diseño: online es M6; BUILDING la marca opcional).
- Tu PC con GPU NVIDIA: fidelidad visual de referencia = el build Windows con D3D11; todo error gráfico se compara contra esta fase, no contra la consola.
- El usuario opera la consola/GUI solo cuando un paso lo marca como acción humana; el agente ejecuta el resto por consola.

## Review Focus

Los cinco modos de fallo más probables de esta fase, cada uno con su test:

1. **Ruta con espacios/paréntesis** rompe los scripts upstream → comportamiento esperado: los builds fallan con error de parseo de rutas. Test: Task 1 verifica que el checkout está en `C:\ttt2` y que las rutas de trabajo no contienen espacio ni paréntesis (`Test-Path 'C:\ttt2`; `$env:TEMP` sin paréntesis).
2. **Región incorrecta o TU equivocado** del contenido del usuario → el instalador/el pipeline rechaza o genera un recomp corrupto. Test: Task 2 valida con `stfs.py` y el build falla de forma explícita; el paso verifica el nombre del TU (terminado en O2, TU3).
3. **Faltan herramientas o versión incorrecta** (clang-cl ausente, Python sin Pillow, Ninja ausente) → falla en la primera invocación del script correspondiente. Test: Task 1 imprime versiones de cada herramienta y el agente cruza contra BUILDING.md.
4. **XenonRecomp mal parcheado o commit equivocado** → la generación de C++ produce errores de sintaxis o `// ERROR` persistente. Test: Task 4 exige cero sitios `// ERROR` tras re-runs; ✓ cuenta con `Select-String -Path re\recomp\*.c* -Pattern "ERROR"` (0 coincidencias fuera de esperas documentadas).
5. **Datos del juego mal generados** (ISO dañada, discos parciales) → el juego crashea al arranque con VFS incompleta. Test: Task 8 arranca el juego y exige llegar al menú principal; si falla, comparar contra el log de stderr (`run.txt`).

---

### Task 1: Auditoría e instalación del entorno de compilación

**Files:**
- Create: `C:\ttt2\docs\superpowers\env-audit.md` (registro de versiones instaladas)

**Interfaces:**
- Consumes: `BUILDING.md` (lista de requisitos exacta).
- Produces: entorno con las herramientas exactas que Task 2–7 invocan (`cmake`, `ninja`, `clang-cl`, `python` con `Pillow`, msbuild de VS2022).

- [x] **Step 1: Verificar ruta del checkout sin espacios ni paréntesis**

```powershell
Test-Path C:\ttt2; git -C C:\ttt2 status --porcelain
```
Expected: `True` + árbol limpio.

- [x] **Step 2: Auditar herramientas presentes: versiones o ausencias**

```powershell
foreach ($t in 'python','cmake','ninja','clang-cl') { & $t --version 2>$null | Select-Object -First 1 }
where.exe msbuild 2>$null
python -c "import PIL; print('Pillow', PIL.__version__)" 2>$null
```
Expected: listado real por herramienta, o ausencia. Guardar resultados en `docs/superpowers/env-audit.md`.

- [x] **Step 3: Instalar lo que falte (acción: instalador de VS2022 Build Tools con workload C++ x64 + componente “C++ Clang tools for Windows”; winget puede servir para CMake/Ninja/LLVM/Python)**

Instalar sin ejecutar el juego; documentar en `env-audit.md` qué se instaló y su versión exacta.

- [x] **Step 4: Re-auditar y comprobar que las 5 herramientas responden**

```powershell
cmake --version; ninja --version; clang-cl --version
python -c "import PIL; print('Pillow OK')"
where.exe msbuild | Select-Object -First 1
```
Expected: todas las versiones impresas, sin error.

- [x] **Step 5: Commit**

```bash
git add docs/superpowers/env-audit.md && git commit -m "chore(m1): registro de auditoría del entorno de build"
```

---

### Task 2: Volcado de la copia del juego a las rutas que espera la pipeline (acción del usuario)

**Files:**
- Create (datos del usuario, NO commitear): `C:\ttt2\re\Default.xex`, carpeta `C:\ttt2\Updates\` con el TU3, y extracción a `C:\ttt2\re\extracted_tu\`.

**Interfaces:**
- Consumes: imagen del disco del usuario + Title Update 3 (el tu requerido es `TU_174Q22P_0000008000000.00000000000O2`, termina en «O2»).
- Produces: `re\extracted_tu\` completo (el recomp y el instalador de Task 4–7 lo leen).

- [x] **Step 1: El usuario copia (acción humana)**: su `Default.xex` (extraído del ISO con el flujo habitual del scene o del disco ya capturado por el upstream) a `C:\ttt2\re\Default.xex`, y el TU3 a `C:\ttt2\Updates\`.

- [x] **Step 2: Extraer con la herramienta del upstream**

```powershell
cd C:\ttt2; python port\tools\stfs.py re\Default.xex Updates re\extracted_tu
```
(La firma exacta del invocable `stfs.py` se confirma leyendo su `--help` antes de correr; el objetivo es: consumir los archivos de arriba y poblar `re\extracted_tu\`.)

**Nota (realidad medida — ver ledger):** `stfs.py` NO tiene CLI (`__main__` usa rutas en duro); se invoca como librería (import STFS + leer `Updates/`) sin tocar el upstream. El comportamiento documentado aquí es el objetivo, no la invocación literal.

- [x] **Step 3: Verificar extracción completa**

```powershell
(Get-ChildItem C:\ttt2\re\extracted_tu -Recurse -File).Count
```
Expected: > 0 archivos y NO vacío; el script no imprime error de región o de TU inválido. Si falla: revisar con el usuario la región del disco (debe ser USA/EU) y que el TU sea el 3.

- [x] **Step 4: Sin commit (datos del usuario).** Registrar en `env-audit.md` que los archivos existen y están excluidos por `.gitignore`.

---

### Task 3: XenonRecomp @ `ddd128b` parcheado y compilado

**Files:**
- Create: `C:\ttt2\tools\XenonRecomp\` (clon con los parches de `tools\patches\XenonRecomp.patch`).

**Interfaces:**
- Consumes: `tools\patches\XenonRecomp.patch`, commit objetivo `ddd128b` de `hedge-dev/XenonRecomp`.
- Produces: el binario del recompilador que Task 4 invoca: `tools\XenonRecomp\XenonRecomp.exe` y `XenonUtils\ppc_context.h`.

- [x] **Step 1: Clonar y anclar el commit**

```powershell
git clone --recursive https://github.com/hedge-dev/XenonRecomp tools\XenonRecomp
git -C tools\XenonRecomp checkout ddd128b
```
Expected: checkout sin conflicto.

- [x] **Step 2: Aplicar el parche del upstream**

```powershell
git -C tools\XenonRecomp apply ..\patches\XenonRecomp.patch
```
Expected: aplica limpio; si hay conflicto, es señal de commit equivocado (revisar `ddd128b`).

- [x] **Step 3: Compilar**

```powershell
cd C:\ttt2; .\tools\build_xenonrecomp.cmd
```
Expected: proceso concluye; `tools\XenonRecomp\XenonRecomp.exe` existe (`Test-Path`).

**Nota (ruta real — ver ledger):** los binarios quedan en `tools\XenonRecomp\build\XenonRecomp\XenonRecomp.exe` y `build\XenonAnalyse\XenonAnalyse.exe`.

- [x] **Step 4: Commit (el checkout es de terceros, queda excluido por .gitignore del upstream si procede; registrar fine en `env-audit.md` que el parche aplicó en `ddd128b`)**

---

### Task 4: Generación del código C++ del juego (pipeline del recomp)

**Files:**
- Create: `C:\ttt2\re\ppc\*` (C++ generado, NO commitear).

**Interfaces:**
- Consumes: `tools\XenonRecomp\XenonRecomp.exe` (Task 3), `re\recomp\tekken.toml`, `re\Default.xex` + TU aplicado (Task 2).
- Produces: `re\ppc\` con el C++ generado sin ningún sitio `// ERROR` — prerrequisito del build (Task 6).

- [x] **Step 1: Análisis del XEX parcheado (TU3) y generación de switches que faltan**

```powershell
cd C:\ttt2
# fv: la invocación exacta de XenonAnalyse está en BUILDING.md; confirmar flags leyendo el script/tool antes de correr.
python port\tools\gen_missing_switches.py
```
Expected: `re\ppc\` empieza a poblarse; scripts salen sin traceback.

- [x] **Step 2: Ejecutar la recompilación**

```powershell
tools\XenonRecomp\XenonRecomp.exe re\recomp\tekken.toml tools\XenonRecomp\XenonUtils\ppc_context.h
```

- [x] **Step 3: Iterar faltantes hasta cero**

```powershell
python port\tools\gen_missing_functions.py
# re-run XenonRecomp (Step 2) mientras queden sitios //
Select-String -Path re\ppc\* -Pattern '// ERROR'
```
Expected: bucle gen → recomp → gen termina cuando `Select-String` devuelve 0 ocurrencias.

- [x] **Step 4: Los cuatro pasos de post-procesado del BUILDING, en orden, cada uno sin traceback**

```powershell
python port\tools\debug_probes.py
python port\tools\check_recomp_acceptance.py
python port\tools\gen_kernel_stubs.py
```
Expected: acceptance report OK; error = detener y depurar (ver Review Focus 4).

- [x] **Step 5: Commit** — nada que commitear (código generado); registrar el resultado del acceptance en el log de la tarea.

---

### Task 5: FFmpeg n7.1 para decodificación XMA

**Files:**
- Create: `C:\ttt2\tools\ffmpeg_dl\FFmpeg-n7.1\` (fuente configurada por `port\third_party\ffmpeg_xma\CMakeLists.txt`; NO commitear).

**Interfaces:**
- Consumes: instrucciones exactas en el encabezado de `port\third_party\ffmpeg_xma\CMakeLists.txt` (abajo de Task 6 lo referencia).
- Produces: configure listado para el build del game (Task 6 lo enlaza estáticamente).

- [x] **Step 1: Descargar/extraer la fuente FFmpeg n7.1 a `tools\ffmpeg_dl\FFmpeg-n7.1` (acción automática por git o descarga del tarball oficial n7.1)**

- [x] **Step 2: Aplicar la configuración exigida por el CMakeLists (confirmar flags exactos leyendo el encabezado del archivo en el checkout) y validar que las variables que el CMakeLists espera quedaron definidas**

Expected: el CMake de Task 6 no falla con “FFmpeg not found/configured”.

---

### Task 6: Build del juego (el ejecutable nativo de Windows)

**Files:**
- Create: `C:\ttt2\port\build-o1\tekken_recomp.exe` (NO commitear).

**Interfaces:**
- Consumes: `re\ppc\` sin errores (Task 4), FFmpeg configurado (Task 5), toolchain (Task 1).
- Produces: **el binario de referencia para todas las fases** que Task 8 ejecuta; su log stderr es la base de depuración del proyecto.

- [x] **Step 1: Confirmar el significado de cada flag leyendo `tools\build_port.cmd`, luego ejecutar el build del puerto**

```powershell
cd C:\ttt2
.\tools\build_port.cmd build-o1 1 8 ON
```
Nota: `BUILDING.md` usa `build-o1 1 8 ON` con Steamworks instalado; con Steamworks ausente se confirma el flag de Steamworks en el propio script (`ON`/`OFF` o `1`/`0`) y se ejecuta con el valor que desactiva Steam. No se adivina: se lee el script primero.

Expected: build termina; `port\build-o1\tekken_recomp.exe` existe (`Test-Path` → True) y el log de Ninja/MSBuild no tiene errores fatales.

✅ **RESULTADO (2026-10-26):** build exit 0 en `build-o1` (ppc-opt=1) y `build-o2` (ppc-opt=2, 62,019,072 B). El o2 es el binario de rendimiento. Builds vía `tools\build_port_sdl3.cmd` (SDL3 3.4.0 desde fuente + shim `tools\SDL3\shim\cmake\SDL3Config.cmake`). Fin de sesión M1: `TTT2_GPU_HW` pasa a **On por defecto** en `gpu_hw_control.h` (run8/run9: backend D3D11 activo → cinemática fluida, combate disfrutable, ruido del escenario eliminado).

---

### Task 7: Datos del juego (instalador) y arranque de validación

**Files:**
- Create (NO commitear): directorio de instalación con `game\tekken.img` y assets.

**Interfaces:**
- Consumes: `build_setup.cmd` (Task 7 genera el instalador) y los datos del usuario (Task 2).
- Produces: datos de juego válidos (`game\tekken.img`): el juego corre en el PC del usuario.

- [x] **Step 1: Compilar el instalador**

```powershell
cd C:\ttt2; .\tools\build_setup.cmd build-o1 8
```
Expected: `port\build-o1\setup\ttt2_setup.exe` existe.

- [x] **Step 2: Ejecutar el instalador (acción humana en GUI)**: el usuario lo lanza y apunta su imagen de disco del juego + el TU3.

Expected: el instalador valida la copia (región USA/EU, TU3) y crea `game\tekken.img`; el paso «Preparing the game's shaders» completa sin error.

✅ **RESULTADO (2026-10-26):** instalador en `port\build-o1\setup\ttt2_setup.exe` (+ gemelo asInvoker `ttt2_setup_probe.exe` con CLI `/silent <iso> <tu> <folder>`). Instalación silenciosa validada → `C:\ttt2\install-test\` (todos los dataNNN.bin + update\ + Default.xex; `tekken.img` = 18,612,224 B, tamaño exacto del TU3 parcheado). Nota de diagnóstico: data025/051 instalados difieren del ISO en MD5/tamaño pero son **falsos positivos**: son contenedores LZX que el instalador expande; los tamaños instalados (703,658,432 / 165,763,536 B) son los expandidos de diseño (verificados contra el árbol XDVDFS y el catálogo nrfo: sin corrupción).

- [x] **Step 3: Arranque de validación**

```powershell
$env:TTT2_VFS_ROOT = "<ruta de instalación>\game"
C:\ttt2\port\build-o1\tekken_recomp.exe "<ruta de instalación>\game\tekken.img" > C:\ttt2\run.txt 2>&1
```
Expected: el juego muestra el menú prinicpal; el usuario puede entrar a un combate arcade y ejecutar un round completo. El ejecutable es de consola: su stderr queda en `run.txt` (única evidencia sobre fallos). Criterio de éxito M1: pantalla de menú + un round jugado + VFS logueada sin errores críticos.

✅ **RESULTADO (2026-10-26, cierre M1):** CUMPLIDO con el binario o2 + backend D3D11 (`TTT2_GPU_HW` = On por defecto tras run9, configuración cero). Evidencia: run9 exit=0, `[gpu-hw] active on NVIDIA GeForce RTX 5070`, shader warm-up 5,644 shaders / 0 failed, audio sin starves, ~406 MB leídos por VFS; el usuario jugó **dos combates completos sin ruido ni stutter** ("es disfrutable", "el ruido ni rastro"). Diagnóstico de la sesión: el ruido DXT2_3 del escenario y la lentitud/CPU 80% provenían del **rasterizador de software por defecto** (intérprete de shaders); el instalador y los datos del disco quedaron descartados como causa.

- [x] **Step 4: Sin commit (binarios y datos). Registrar el éxito y la ruta de instalación exacta en el log de la tarea** — ruta: `C:\ttt2\install-test\game\`; ejecutable de referencia: `C:\ttt2\port\build-o2\tekken_recomp.exe`.

---

### Task 8: M0 — Informe de superficie de plataforma (cierre del spike)

**Files:**
- Create: `C:\ttt2\docs\superpowers\m0-platform-surface.md`.

**Interfaces:**
- Consumes: recorridos mencionados en el spec M0 (`port\src\runtime\` y scripts Android `tools\build_android_*.sh`).
- Produces: cultura común de Task M3+: el informe lista qué archivos son kernel host y qué sustituciones exige el caso PS5 (activar para escanear antes de escribir M3).

- [x] **Step 1: Inventario**

```powershell
Get-ChildItem C:\ttt2\port\src\runtime -Recurse -File | Select-Object FullName
Get-ChildItem C:\ttt2\tools -Filter "build_android_*.sh" | Select-Object Name
```

- [x] **Step 2: Clasificar en el informe cada archivo host en trazado X (reutilizable tal cual para PS5) / Y (sustitutable con patrón existente en el upstream, e.g. `*_android*` → `*_ps5`) / Z (exclusivo Windows, poco probable)**. Anotar para cada Y el patrón fuente (Linux o Android) — es la lista base de planificación del M3.

- [x] **Step 3: Commit del informe**

```bash
git add docs/superpowers/m0-platform-surface.md && git commit -m "docs(m0): informe de superficie de host-por-plataforma"
```

**Criterio de cierre M0+M1:** juego corriendo en el PC del usuario (Task 7) + informe de plataforma (Task 8) + entorno/auditoría registrada (Task 1).

---

## Cierre M0+M1 (2026-10-26)

**Criterio de cierre CUMPLIDO**: juego corriendo en el PC del usuario (Task 7, dos combates completos jugados por el usuario, it is enjoyable / el ruido ni rastro) + informe de plataforma (Task 8, docs/superpowers/m0-platform-surface.md, commit 738b7b4) + entorno registrado (Task 1, env-audit.md).

### Hallazgos de la sesión de validación (resumen para las fases PS5)

1. **El pipeline real del port es el backend hardware D3D11** (gpu_hw_d3d11.*): TTT2_GPU_HW no definido dejaba el interprete de shaders en CPU (80% CPU, GPU idle, cinemáticas sin animar, ruido DXT2_3 en escenarios, DXT4_5/DXT1 funcionando). Con TTT2_GPU_HW=1: RTX 5070 activa, cinemática animada, combates fluidos, uploads por formato correctos (19t/20t/18t suben) y **el usuario jugó dos combates sin ruido ("el ruido ni rastro")**. Cambio aplicado: RequestedMode() en gpu_hw_control.h devuelve Mode::On por defecto (=0/false/off lo fuerza Off para diagnóstico). Implicaciones M4: en PS5 el backend equivalente va sobre DXVK-native/RADV. NOTA DE POLÍTICA (revisión 1a46b06): en Windows un fallo del backend hace fallback limpio al intérprete CPU (Guarded devuelve false); en hosts NO Windows Guarded hace std::exit(EXIT_FAILURE) a propósito - el fallo de RADV/DXVK-native en M4 terminará el proceso limpio con mensaje, NO degradará a software. Los planes M4 deben asumir esa política.
2. **El instalador no corrompe datos**: data025/051 difieren del ISO porque son contenedores LZX expandidos en instalación (nrfo/ofsd); tamaños instalados = los expandidos de diseño. Falsa alarma arqueada con hashes + árbol XDVDFS.
3. **Herramientas de diagnóstico añadidas** (gated por env, apagadas de serie): TTT2_MIRRORDUMP=<start>,<size> en phys_mirror.cpp (dump del contenido físico A de chunks cacheados), TTT2_TEXRAW, TEXDUMP/FRAMETIME previos. Logs de las corridas: run5-run9 en C:\ttt2\.
4. **Build de rendimiento** = ppc-opt 2 (uild-o2); el o1 queda como build de referencia de Task 6. Binario validado final: port/build-o2/tekken_recomp.exe + instalación C:\ttt2\install-test\game\.
