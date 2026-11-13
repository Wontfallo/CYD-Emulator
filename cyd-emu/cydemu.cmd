@echo off
rem cydemu launcher: cydemu <command> [args]   (see: cydemu --help)
rem Uses the bundled Python in python\ when present (portable build), otherwise python on PATH.
setlocal
if exist "%~dp0python\python.exe" (
    "%~dp0python\python.exe" -m cydemu %*
) else (
    set "PYTHONPATH=%~dp0;%PYTHONPATH%"
    python -m cydemu %*
)
