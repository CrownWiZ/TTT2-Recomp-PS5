@echo off
rem Wrapper: build the port with our locally-built SDL3 config shim on the prefix path.
rem The shim mirrors SDL3's per-config import properties onto the plain ones the port reads.
cd /d C:\ttt2\port
set "CMAKE_PREFIX_PATH=C:\ttt2\tools\SDL3\shim"
call C:\ttt2\tools\build_port.cmd %*
exit /b %errorlevel%
