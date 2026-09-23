@echo off
setlocal
set "HERE=%~dp0.."
python "%HERE%\Tools\generate.py" %*
