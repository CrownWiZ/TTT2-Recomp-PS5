# Auditoría del entorno de compilación (M1)

Fecha: 2026-10-26 · PC: Windows, GPU NVIDIA dedicada, 8 GB+ RAM

| Herramienta | Estado | Versión / ruta |
|---|---|---|
| Python | ✅ | 3.11.0 |
| Pillow | ✅ | 13.0.0 (pip) |
| Visual Studio 2022 Build Tools | ✅ | workload VCTools + recommended, en `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` |
| CMake (bundled VS) | ✅ | 3.31.6-msvc6, `Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| Ninja (bundled VS) | ✅ | `Common7\IDE\...\CMake\Ninja\ninja.exe` |
| Clang/LLVM standalone | ✅ | clang-cl **23.1.4**, `C:\Program Files\LLVM\bin` (coincide con el PATH que los .cmd del upstream fijan) |
| Clang bundled de VS | ✅ (backup) | `VC\Tools\Llvm\bin\clang-cl.exe` |
| Ninja standalone | ✅ (backup) | 1.13.2 via winget |
| MSYS2 | ✅ | bash 5.3.20 en `C:\msys64\usr\bin` (necesario para el configure de FFmpeg) |
| Git | ✅ | (clones hechos) |
| GitHub CLI | ✅ | 2.102.0, autenticado como CrownWiZ |
| XenonRecomp | ✅ | @ `ddd128b` + parche; compilado: `tools\XenonRecomp\build\XenonRecomp\XenonRecomp.exe` + `build\XenonAnalyse\XenonAnalyse.exe` |
| FFmpeg n7.1 | ✅ fuente | `tools\ffmpeg_dl\FFmpeg-n7.1` (+ script propio `tools\configure_ffmpeg_msvc.cmd`) |
| Steamworks SDK | ❌ omitido a propósito | decisión de diseño (online = M6) |

Notas:
- Los .cmd del upstream (`build_port.cmd`, `build_setup.cmd`, `build_xenonrecomp.cmd`) usan rutas fijas de Build Tools + `C:\Program Files\LLVM\bin`: el entorno instalaído arriba coincide con todas.
- El checkout está en `C:\ttt2` exactamente como exige BUILDING.md.
- Remotes: `upstream` = shihabal3amri/tekken-tag-tournament-2-pc, `origin` = CrownWiZ/TTT2-Recomp-PS5.
