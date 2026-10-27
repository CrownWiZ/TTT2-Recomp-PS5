# Plan de implementación: M3 — Toolchain Prospero y arranque mínimo en consola

> **ESTADO: en ejecución (27-10-2026).** Tasks 1-4 (lado PC, ejecutados por el agente) ya están completados y verificados; Task 5 (validación en consola) y 6 (cierre) esperan la participación del usuario. Los checkboxes marcan el estado real.

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

### Task 5: Envío y validación en consola (HUMANO + agente)

- [ ] **Step 1 (agente):** con `PS5_HOST` del usuario: chequeo de red (ping/Test-NetConnection desde Windows), envío de `hello_world.elf` y del smoke del repo.
- [ ] **Step 2 (usuario):** confirmar las DOS burbujas en pantalla ("Hello, world!" y "TTT2 PS5 smoke: build/load OK").
- [ ] **Step 3 (agente):** repetir build+send una segunda vez completa (rebuild limpio incluido) para validar la **repetibilidad** del ciclo.
- [ ] **Step 4 (agente, opcional, infra M4/M5):** botar `ftpsrv` (release ps5-payload-dev) por 9021 para tener FTP residente que M4 usará para subir datos del juego.

### Task 6: Cierre M3 (agente)

- [ ] Ledger en `.superpowers/sdd/2026-10-27-m3-prospero-console-boot/progress.md`, commit del plan+ledger+tools (`git add -f` para `docs\` y `.superpowers\`), push a origin.
- [ ] Actualizar README del repo (roadmap de hitos: M3 estado).

---

## Resultados y desvíos (se completa con el cierre)

- **Desvío registrado (Task 2):** el SDK binario trae sus wrappers pero resuelven el toolchain del HOST vía `llvm-config-18..15`; sin el paquete `llvm-18` el wrapper muere con `/llvm-clang: No such file or directory`. Apt lo arregla (1 minuto); el README del SDK solo pedía clang-18/lld-18.
- (más notas al cierre)
