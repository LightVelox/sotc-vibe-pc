@echo off
if not defined VSROOT set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" goto novs
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
%*
exit /b %ERRORLEVEL%
:novs
echo Visual Studio 2022 not found. Set VSROOT to your Visual Studio installation directory.
exit /b 1
