@echo off
setlocal
set "HERE=%~dp0"
set "TARGET=%1"
if "%TARGET%"=="" set "TARGET=sotc"
call "%HERE%Tools\win\vsenv.cmd" cmake --build "%HERE%build\port" --target %TARGET%
