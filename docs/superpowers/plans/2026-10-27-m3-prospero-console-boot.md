# Plan de implementación: M3 — Toolchain Prospero y arranque mínimo en consola

> **ESTADO: COMPLETADO (27-10-2026).** Tasks 1-6 ejecutados: toolchain Prospero vivo en WSL2, ciclo compilar→cargar→ejecutar validado en la consola del usuario (3 burbujas de notificación, repetible), FTP residente desplegado. El registro está en el ledger y en "Cierre M3" al final.

**Goal:** Ciclo repetible **compilar → cargar → ejecutar** en la PS5 del usuario (fw 13.60, jailbreak Relapse con cargador de ELF del ecosistema ps5-payload-dev): cross-compilar payloads con `clang-18` (`Target: x86_64-sie-ps5`) y el `ps5-payload-sdk` desde WSL2, enviarlos al cargador de ELF (puerto 9021) y validar la ejecución con una **notificación visible en la pantalla de inicio** de la consola.

**Architecture:** Se adopta la vía probada del scene (precisión del spec M3): `ps5-payload-sdk` (zip oficial de releases) instalado en `/opt/ps5-payload-sdk` dentro de WSL2, cuyo wrapper `prospero-clang` resuelve el clang-18 del host y define el sysroot del SDK (`target/`). Los payloads (hoy el smoke del repo; mañana el port) se envían con `prospero-deploy` (socat → TCP `PS5_HOST:9021`) al cargador de ELF que Relapse deja residente. El upload/play cycle NO requiere FSELF: eso es para el modo BigApp (M4+). El código del humo vive en el repo (`tools/ps5/`), de modo que el ciclo que se valida es el del proyecto, no uno ajeno.

**Tech Stack:** WSL2 + Ubuntu 24.04 (ya del M2: clang 18.1.3), ps5-payload-sdk (release binaria + samples), `llvm-18`/`lld-18`/`unzip`/`socat` añadidos, Makefile propio sobre `toolchain/prospero.mk`, cargador de ELF de Relapse (9021; alternativa documentada: `elfldr-ps5-1360.elf` vía 9020→9021).

**Spec:** `docs/superpowers/specs/2026-10-26-ttt2-ps5-port-design.md` — hito M3 (líneas 122-127) y capa "Ejecución" (línea 80: cargador ELF de Relapse, payloads en 9021).

## Global Constraints

- NUNCA se commitean assets del juego, binarios del port ni el SDK: `.superpowers/m3-downloads/` vive fuera del allowlist de `.gitignore` y el ELF construido (`tools/ps5/build/`) se ignora (`build/` ya está en el allowlist negativo global).
- Toda descarga se hace en Windows (WSL no llega a GitHub, regla M2); al WSL se pasa por `/mnt/c`.
- El SDK no se modifica; si el Makefile del repo necesita algo del SDK, va por variables (`PS5_PAYLOAD_SDK`), jamás tocando ficheros de `/opt`.
- La validación en consola exige acción HUMANA (arrancar Relapse/cargador, leer la IP de la PS5): el agente no eleva ni toca la consola por sí; el envío lo puede hacer el agente por red.
- Repetibilidad exigida: el ciclo build+send se ejecuta **dos veces** seguidas sin error (Task 5).

## Review Focus

1. **Cargador no residente** → `prospero-deploy` cuelga (socat sin fin) o conexión rechazada. Test: el usuario confirma el estado del jailbreak y el puerto ANTES del envío; si Relapse trae el cargador en 9020 (rudimentario) en vez de 9021, se bota `elfldr-ps5-1360.elf` (ya está en `Downloads`) a 9020 y se reintenta por 9021.
2. **WSL no alcanza la LAN** → send falla por timeout aunque la consola esté viva. Test: antes del envío, `Test-NetConnection` desde Windows; fallback: envío por PowerShell (`TcpClient`) si socat/WSL no pasa.
3. **Notification sin efecto visible** → ¿stub equivocado o mensaje truncado? Test: la notificación de `hello_world` del SDK ("Hello, world!") es el A/B canónico: si el sample no se ve, el problema es del cargador/consola; si el sample sí y el smoke no, es del smoke.
4. **ELF rechazado por el loader** (ABI/dynamic linking) → error inmediato en consola. Test: `readelf -d` del ELF muestra `NEEDED` solo de módulos que el SDK enlaza por defecto (`libc.prx`/`libkernel`): igual que el sample, que es el binomio probado.
5. **CRLF rompe los .sh** (protocolo M2) → todo script que cruza PS↔WSL pasa por `tr -d '\r'` explícito (5/5 veces probado en M2).

---

### Task 1: Recon de consola (ACCION HUMANA)

- [x] **Step 1 (usuario):** jailbreak Relapse activo y cargador de ELF escuchando. La PS5 está en 13.60 y el usuario ya maneja inyección de payloads (`PS.Payload.Injector.Pro`, `elfldr-ps5-1360.elf` en Downloads).
- [ ] **Step 2 (usuario):** comunicar la **IP local de la PS5** (Ajustes → Red) y confirmar si el cargador está en 9021 directo (Relapse) o si hay que botar `elfldr-ps5-1360.elf` a 9020.

### Task 2: SDK instalado en WSL2 (agente) — HECHO

- [x] Descarga en Windows: `ps5-payload-sdk.zip` (8.8 MB, release binaria con `bin/ toolchain/ target/ samples/ ldscripts/`) → `.superpowers/m3-downloads/`.
- [x] Instalación en WSL: `unzip -d /opt` → `/opt/ps5-payload-sdk`; apt añadidos `llvm-18 lld-18 unzip socat` (el wrapper busca `llvm-config-18` para resolver el bindir del host).
- [x] Verificación: `prospero-clang --version` → `Target: x86_64-sie-ps5` (clang 18.1.3 Ubuntu).

### Task 3: Build del hello_world del SDK (agente) — HECHO

- [x] `make -C /opt/ps5-payload-sdk/samples/hello_world` → `hello_world.elf` (ELF64 **PIE x86-64**, enlazado dinámico, sin errores).
- [x] Mecánica entendida: `sceKernelSendNotificationRequest` → burbuja visible en el XMB de la PS5 (prueba de vida canónica del scene).

### Task 4: Smoke payload del repo (agente) — HECHO

- [x] `tools/ps5/smoke/main.c`: notificación "TTT2 PS5 smoke: build/load OK" + `sceKernelDebugOutText` (el canal de debug que M4+ usará).
- [x] `tools/ps5/Makefile` sobre `toolchain/prospero.mk` (rutas absolutas del Makefile; `build/` fuera del allowlist de git).
- [x] `tools/ps5/send_smoke.sh`: build-if-needed + `prospero-deploy` a `PS5_HOST:PS5_PORT`.
- [x] Build verificado en WSL: `tools/ps5/build/ttt2_smoke.elf` (ELF64 PIE x86-64).
- [x] `.gitignore`: allowlist ampliada con `!/tools/ps5/`.

### Task 5: Envío y validación en consola (HUMANO + agente) — HECHO

- [x] **Step 1 (agente):** red verificada: PS5 en 192.168.0.24, elfldr del usuario escuchando en 9021 (hallado por escaneo; el usuario lo abrió desde su payload loader sobre kstuff/Relapse).
- [x] **Step 2 (usuario):** las burbujas "Hello, world!" (SDK) y "TTT2 PS5 smoke: build/load OK" (repo) aparecieron en pantalla ✓.
- [x] **Step 3 (agente):** ciclo repetible confirmado: 3 envíos (hello_world + smoke + smoke repetido), las 3 burbujas visibles ✓.
- [x] **Step 4 (agente, infra M4/M5):** `ftpsrv` v0.21.1 residente en la consola, FTP en puerto 2121 (banner recibido por socket).

### Task 6: Cierre M3 (agente) — HECHO

- [x] Ledger + commit + push (ver "Cierre M3" abajo).

---

## Resultados y desvíos (se completa con el cierre)

- **Desvío registrado (Task 2):** el SDK binario trae sus wrappers pero resuelven el toolchain del HOST vía `llvm-config-18..15`; sin el paquete `llvm-18` el wrapper muere con `/llvm-clang: No such file or directory`. Apt lo arregla (1 minuto); el README del SDK solo pedía clang-18/lld-18.
- **Desvío registrado (Task 1):** la consola del usuario no usa el 9020 de ps5-payload-elfldr clásico: su stack es Relapse/kstuff (autoloader + shadowmount + web file manager) y abre el elfldr **directamente en 9021**. El flujo PC→PS5 funciona sin la etapa 9020. Servicios de su stack visibles: 8084, 8888, 9049, 9050, 9295; el ELF loader: 9021; ftpsrv añadido: 2121.
- **Desvío registrado (Task 5):** el envío se hizo con `socat -t 3` en vez de `prospero-deploy` (equivalente, sin el `-t 9999999` que se cuelga si el loader no cierra el socket).

---

## Cierre M3 (27-10-2026)

**Resultado del hito: COMPLETADO.** Ciclo compilar→cargar→ejecutar en la PS5 real, repetible y con prueba visible:

1. **Toolchain**: `ps5-payload-sdk` (release binaria) en `/opt/ps5-payload-sdk`; `prospero-clang` = clang 18.1.3 con `Target: x86_64-sie-ps5`; apt host: `llvm-18 lld-18 unzip socat`.
2. **Builds**: `hello_world.elf` (SDK sample) y `ttt2_smoke.elf` (repo, `tools/ps5/`) — ELF64 PIE x86-64, sin warnings.
3. **Envío**: `socat → TCP 192.168.0.24:9021` (elfldr del usuario sobre kstuff). 3/3 burbujas de notificación confirmadas por el usuario ("Hello, world!", "TTT2 PS5 smoke: build/load OK", repetición) — **el ciclo es repetible**.
4. **Infra M4**: `ftpsrv` v0.21.1 residente en la consola (FTP :2121), listo para subir datos del juego y builds del port.
5. **El flujo del proyecto**: `tools/ps5/Makefile` (build) + `tools/ps5/send_smoke.sh` (deploy) — el ciclo que M4/M5 reutilizarán para el port entero.

### Base para M4

- El ELF dinámico de payload corre en consola; M4 enlaza el port completo (estático, DXVK-native + RADV `libvulkan_radeon.ps5.a`) — el pipeline probado de Nazky, ya compilado y vivo en Linux desde M2, ahora apuntado a la consola.
- Radar del run: la PS5 del usuario tiene kstuff/autoloader activo; para el modo BigApp (FSELF + `/data/homebrew` + libc.prx) M4/M5 evaluará si conviene o si el runtime por elfldr basta (el spec deja la decisión documentada en M4).
- Herramienta de descarga: GitHub solo desde Windows (regla M2); tarballs a `.superpowers/m3-downloads/`.
