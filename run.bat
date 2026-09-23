@echo off
setlocal
set "HERE=%~dp0"
"%HERE%build\port\bin\sotc.exe" %*
