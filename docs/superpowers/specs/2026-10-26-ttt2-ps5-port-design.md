# Diseño: port de Tekken Tag Tournament 2 (PC/360 recomp) a PS5

Fecha: 2026-10-26 · Estado: borrador 3, pendiente de aprobación del usuario
Base: fork de `shihabal3amri/tekken-tag-tournament-2-pc` (checkout local en `C:\ttt2`)

## Intención y por qué

Ejecutar de forma nativa, como homebrew, el port recompilado de Tekken Tag
Tournament 2 (Xbox 360 → Windows por XenonRecomp) en una PS5 con firmware
13.60 jailbroken vía exploit **Relapse** (cubre 7.00–13.60 y trae un cargador
de ELF que escucha en el puerto 9021, el ecosistema ps5-payload-dev).
Objetivo final: un ELF homebrew que funcione nativamente en la PS5, offline a
60 fps, con mando DualSense. El juego online queda como añadido posterior
opcional.

El trabajo es viable porque el upstream ya resolvió, para Windows, Android y
Linux, las capas difíciles del port: la recompilación estática del XEX, la
traducción de syscalls y gráficos de Xbox 360, el audio XMA con FFmpeg y — lo
más grande — un renderizador que reimplementa la API gráfica del juego sobre
DirectX 11. Este proyecto no reescribe esa capa: reaprovecha la
infraestructura del upstream que compila el mismo renderizador D3D11 sobre
Vulkan (DXVK-native) en plataformas no-Windows y la apunta a la PS5
(Prospero).

## Contexto del equipo (restricciones fuertes)

- El usuario tiene un nivel de C++ de curso de programación orientada a
  objetos y cero experiencia con APIs gráficas. Toda la escritura de código
  la hace el agente; el usuario compila, ejecuta, testea y reporta
  resultados.
- PC de desarrollo: Windows con GPU NVIDIA dedicada (soporta Vulkan 1.2+, pero
  la ruta RADV es solo de AMD, así que no aplica validación RADV en el PC) y
  8 GB+ de RAM. Consola: PS5 estándar con fw 13.60 y jailbreak Relapse
  funcionando. La validación gráfica real se hace en la consola: el usuario
  quiere el juego corriendo en la PS5 y las fases de PC/Linux son solo
  compilación y depuración funcional.
- Contenido del juego: el usuario posee su copia (imagen de disco + TU3). El
  repo no incluye ni distribuye assets del juego; el port los genera desde la
  copia del usuario, igual que hace upstream. Regla anti-objetivo: nunca
  commitear, empaquetar ni distribuir datos del juego ni el binario resultante
  con datos generados.

## Enfoques considerados

1. Reescribir el renderizador D3D11 a un backend Vulkan propio (plume o a
   medida, estilo UnleashedRecomp). Rechazado: meses de trabajo de gráficos
   de alto riesgo para alguien sin experiencia gráfica, cuando el upstream ya
   resolvió el problema por otra vía.
2. GNM/PSSL directo (API nativa de gráficos de PS5). Rechazado: requiere el
   SDK cerrado y conocimiento profundo del pipeline de Prospero. Coste y
   riesgo altos sin necesidad, porque la PS5 puede ejecutar Vulkan mediante
   RADV de Mesa compilado para Prospero, como demostró
   UnleashedRecomp-Prospero (de Nazky).
3. Ruta Android/Linux del upstream, con DXVK-native, hacia la PS5. ELEGIDO.
   El repo ya contiene un port Android completo con shell SDL3
   (port/src/runtime/host_*_android.cpp, port/android/, scripts
   tools/build_android_*.sh), DXVK-native 1.9.2b parcheado
   (tools/patches/dxvk-native-1.9.2b-*, incluyendo sdl3 y shared-wsi),
   vkd3d-shader 1.17 para plataformas no-Windows
   (port/tools/build_android_vkd3d_shader.sh) y un camino Linux con DXVK
   (tools/build_dxvk11_linux.sh, tools/run_dxvk11_linux.sh). El trade-off:
   dependemos de un RADV de PS5 (fork de Mesa mantenido por el scene) y de su
   WSI propia. El trabajo es de adaptación estructural, no de creación de una
   capa gráfica desde cero.

## Arquitectura elegida

Capa por capa, qué corre en la PS5 y de dónde viene el código:

| Capa | En la PS5 | Fuente del código |
|---|---|---|
| Código del juego | C++ x86-64 recompilado | XenonRecomp upstream, igual que hoy |
| Renderizador del juego | igual que en PC/Android: D3D11 | upstream (gpu_hw_d3d11.h y archivos vecinos) |
| Traducción D3D11 → Vulkan | DXVK-native, enlazado estáticamente al ejecutable | upstream ya lo parchea para Linux/Android; para PS5 se compila como objetos dentro del ELF, sin dlopen |
| Controlador Vulkan | RADV de Mesa para Prospero (radv-winsys=ps5) por VK_KHR_display | modelo UnleashedRecomp-Prospero de Nazky + fork de Mesa del scene |
| WSI y presentación | superficie VK_KHR_display directa a sceVideoOut | WSI propia de DXVK-native; se descarta SDL3-on-PS5 (no existe backend público) |
| Entrada | scePad con mapeo a la capa XInput del juego; DualSense con haptics opcionales | patrón de RetroArch PS5 |
| Guardado, config y contenido | /data/homebrew/<title>/ (escrita por el loader, subida por FTP/manager) con fallback a /app0 | patrón ps5-payload-dev + prueba de escritura al arranque como en Prospero |
| Audio | sceAudioOut | nuevo, pequeño; patrón RetroArch/Prospero |
| Ejecución | cargador de ELF de Relapse, gestor de payloads en el puerto 9021, homebrew estilo BigApp | stack actual del scene |

Decisiones estructurales asumidas tras la revisión del builder (quedan
registradas para el plan):

- Enlace estático de DXVK-native (D3D11, DXGI, WSI) dentro del ELF del juego.
- -femulated-tls junto con variables thread_local y libkernel: el precedente
  conocido como ps5-emutls-cxa exige coordinación específica; se prueba a
  fondo en el corte M4 (crash durante la salida de hilos de trabajo).
- La verificación del set de features Vulkan de la build de Mesa para PS5
  contra lo que requiere el pipeline de DXVK (timeline semaphores, dedicated
  allocation, dynamic rendering y similares) es la primera tarea del M4, con
  spike antes de portar nada de gameplay.
- El guard de página 0 de solo lectura de host_vm (código Linux) puede no ser
  mapable dentro del sandbox de la PS5: se activa condicionalmente por
  plataforma y el recomp se valida sin él.

## Hitos

M0 — Spike de arranque (1–2 días, desechable, ya aprobado por el usuario).
Comprueba dos cosas: (a) que el repositorio compila tal cual en el PC del
usuario y el juego se genera con su copia (la mitad del M1, cronometrada) y
(b) el tamaño real de la superficie de módulos host-por-plataforma (la lista
está en port/src/runtime/ y en los scripts Android): qué módulos hay que
sustituir concretamente para PS5. La salida es un informe corto con los
archivos y adaptaciones necesarias.

M1 — Compilación Windows base (prerrequisito). Compilar en el PC Windows del
usuario con el flujo upstream (BUILDING.md): XenonRecomp, generación del
código recomp, FFmpeg y build final. Verificar que el juego corre con la
copia del usuario. Este hito también genera los datos del juego que después
se subirán a la PS5.

M2 — Compilación Linux funcional (corto, sin gráficos reales). El objetivo no
es validación visual sino portabilidad: comprobar que la rama
multiplataforma del upstream (tools/build_dxvk11_linux.sh y los stubs Linux)
compila y arranca en WSL2 con renderizado por CPU (LAVAPIPE). El framerate
será bajo y no importa: solo se busca que arranque la lógica sin crash y que
compile la combinación de bibliotecas que PS5 necesitará. Si WSL2 resulta
incómodo, el hito se recorta parcialmente y se asume el coste al final, en
la consola; la decisión se documenta en el M4.

M3 — Toolchain Prospero y arranque mínimo en consola. Construir con clang18
de Prospero y los linker scripts del fork de Nazky
(tooling/native/ps5-pie.ld, vendor/ps5/sdk/stubs). Subir a la PS5 del usuario,
vía Relapse, un ejecutable mínimo de prueba (hello world con render dummy)
y validar que el ciclo de compilar y cargar a consola funciona de forma
repetible.

M4 — DXVK-native y RADV sobre PS5: primer frame en pantalla. Con el entorno
compilado y cargando: portar el shell de video del port Android a WSI
VK_KHR_display más sceVideoOut, enlazar DXVK-native estáticamente y arrancar
el renderizador del juego hasta presentar el primer frame (la UI del menú
inicial) en la TV. No se apunta todavía a jugabilidad: solo a un pipeline de
presentación estable y al chequeo del set de features Vulkan anotado arriba.

M5 — Port completo: guardado, mando, audio y opciones. scePad (DualSense,
haptics opcionales), sceAudioOut, guardados en /data/homebrew. Meta: jugable
completo offline a 60 fps estables, salida 3840x2160 con escala interna
configurable (patrón Prospero). Toda la validación de fidelidad visual y
framerate del juego se hace aquí, sobre la consola.

M6 — Opcional, tras M5: funciones online. Steamworks no existe en la PS5 y se
elimina. Se conserva el modo IP directo (estilo TTT2_NET=loopback) y el
servidor comunitario en Go del upstream, que no depende de Steam. Ranked y
leaderboards pueden trabajar contra un servidor comunitario. La limitación de
las capacidades de mapeo de host_vm puede condicionar el rollback netcode;
se revisa en ese momento, con la consola ya validada.

## Decisiones tomadas

- No se reescriben shaders: la traducción HLSL→SPIR-V queda a cargo de
  vkd3d-shader vía DXVK. Nada de portar la lógica de sombreado a PSSL.
- No se apunta a GNM/PSSL: la ruta RADV ya la recorrió el scene con éxito y
  minimiza el riesgo para alguien sin experiencia gráfica.
- Firmware objetivo: 13.60 vía Relapse, ejecutando por payload manager en el
  puerto 9021 (ecosistema ps5-payload-dev, estilo BigApp, contenido en
  /data/homebrew). De momento no se usan los daemons Lapy/kstuff del primitivo
  fork de Prospero: el stack actual de Relapse es suficiente.
- La validación gráfica real es en consola (decisión del usuario). Las fases
  de PC y Linux sirven para compilar y depurar funcionalmente, no para
  medir fidelidad visual.
- Mando: DualSense nativo con scePad y haptics opcionales; sin teclado ni
  ratón por ahora.
- Alcance inicial: solo offline (arcade, versus, práctica). El online es M6.

## Riesgos principales

1. DXVK-native sobre RADV de PS5 con set de features incompleto (en Android
   funcionó, pero sobre RADV de Android con WSI distinta). Es la primera
   tarea del M4; si algo bloquea, se recorta el problema al subconjunto real.
2. El shell de presentación: VK_KHR_display y sceVideoOut en lugar de
   display/udev. Es pequeño y bien precedido por RetroArch PS5 y Prospero.
3. Compatibilidad de MMU: el juego usa áreas de memoria con permisos raros y
   trampolines JIT. El port Android ya despeja esto para 64-bit
   (host_vm_android) y el equivalente para PS5 existe en ports concretados.
   El guard de página 0 de Linux se activa por plataforma.
4. Rendimiento: la PS5 estándar tiene 8 cores Zen 2, muy por encima de una
   Xbox 360; el margen por encima del recomp es sobrado incluso con el
   overhead de RADV.

## Queda a manos del builder

- El revisionado concreto de RADV/Mesa de PS5 y la versión de DXVK.
- La estrategia exacta para derivar el shell de PS5 (fork del port Android
  o shell nuevo mínimo).
- La organización de los remotes y forks de GitHub del proyecto del usuario.

## Cambios respecto al borrador 1

- Confirmado en el README de Relapse: el cargador de ELF usa el puerto 9021,
  es decir el ecosistema ps5-payload-dev. Actualizadas las capas de
  ejecución y la ubicación de contenido (/data/homebrew).
- El PC del usuario tiene GPU NVIDIA, así que no hay validación RADV en el
  PC. El M2 se reduce a una comprobación funcional en WSL2 con LAVAPIPE y
  toda la validación gráfica queda en la consola.
- Incorporados los hallazgos del builder-check: enlace estático de DXVK,
  cuidado con el TLS emulado, guard de página 0 condicional y WSI propia
  (queda descartada SDL3-on-PS5).

## Después

Este documento necesita la aprobación explícita del usuario antes de escribir
cualquier línea de código. El spike M0 tiene la excepción del flujo de
trabajo: puede ejecutarse como prueba desechable una vez aprobado.
