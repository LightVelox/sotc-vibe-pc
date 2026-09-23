@echo off
setlocal
set "HERE=%~dp0"
call "%HERE%Tools\win\vsenv.cmd" cmake -S "%HERE%." -B "%HERE%build\port" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo %*
