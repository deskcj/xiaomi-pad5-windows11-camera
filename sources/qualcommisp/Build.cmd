@echo off
setlocal
cd /d "%~dp0"
set "QISP_EWDK=%~1"
if not defined QISP_EWDK exit /b 2
set "QISP_HOST=Hostarm64"
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set "QISP_HOST=Hostx64"
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set "QISP_HOST=Hostx64"
set "QISP_BIN=%QISP_EWDK%\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\%QISP_HOST%\arm64"
if not exist out mkdir out
"%QISP_BIN%\cl.exe" /nologo /W4 /WX /O2 /Oi /GS- /Zl /LD src\RearFill062.c /Fo:out\RearFill062.obj /Fe:out\RearFill062.dll /link /NOENTRY /NODEFAULTLIB /MACHINE:ARM64 /MANIFEST:NO /INCREMENTAL:NO /OPT:REF /OPT:ICF
if errorlevel 1 exit /b 1
"%QISP_BIN%\armasm64.exe" -o out\FocusFlags075b.obj src\FocusFlags075b.asm
if errorlevel 1 exit /b 1
echo SUCCESS: project-owned exposure callback and focus block built. Vendor drivers are not rebuilt.
exit /b 0
