@echo off
rem Configure minimal FFTmpeg n7.1 (XMA subset) with the MSVC toolchain, following
rem the exact configure line documented in port\third_party\ffmpeg_xma\CMakeLists.txt.
rem Produces config.h / avconfig.h / codec lists in C:\ttt2\tools\ffmpeg_dl\build.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( echo vcvars failed & exit /b 1 )
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin;C:\msys64\usr\bin;%PATH%"
if not exist "C:\ttt2\tools\ffmpeg_dl\build" mkdir "C:\ttt2\tools\ffmpeg_dl\build"
cd /d "C:\ttt2\tools\ffmpeg_dl\build"
bash ../FFmpeg-n7.1/configure --toolchain=msvc --target-os=win64 --arch=x86_64 --disable-everything --disable-programs --disable-doc --disable-avdevice --disable-avformat --disable-swresample --disable-swscale --disable-postproc --disable-avfilter --disable-network --disable-asm --disable-debug --disable-autodetect --enable-decoder=wmapro,xma1,xma2 --enable-parser=xma
set "RESULT=%errorlevel%"
echo FFMPEG_CONFIGURE_EXIT=%RESULT%
exit /b %RESULT%
