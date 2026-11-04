@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "GPU_EWDK_ROOT=%~1"
if not defined GPU_EWDK_ROOT set "GPU_EWDK_ROOT=%EWDK_ROOT%"
if not defined GPU_EWDK_ROOT goto usage
set "GPU_VC=%GPU_EWDK_ROOT%\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "GPU_SDK=%GPU_EWDK_ROOT%\Program Files\Windows Kits\10"
set "GPU_HOST=Hostarm64"
set "GPU_TOOL_ARCH=arm64"
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set "GPU_HOST=Hostx64"
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set "GPU_HOST=Hostx64"
if /i "%GPU_HOST%"=="Hostx64" set "GPU_TOOL_ARCH=x64"
set "GPU_CL=%GPU_VC%\bin\%GPU_HOST%\arm64\cl.exe"
set "GPU_LINK=%GPU_VC%\bin\%GPU_HOST%\arm64\link.exe"
set "GPU_FXC=%GPU_SDK%\bin\10.0.26100.0\%GPU_TOOL_ARCH%\fxc.exe"
if not exist "%GPU_CL%" goto missing
if not exist "%GPU_LINK%" goto missing
if not exist "%GPU_FXC%" goto missing
set "INCLUDE=%GPU_VC%\include;%GPU_SDK%\Include\10.0.26100.0\ucrt;%GPU_SDK%\Include\10.0.26100.0\shared;%GPU_SDK%\Include\10.0.26100.0\km;%GPU_SDK%\Include\wdf\kmdf\1.15"
set "LIB=%GPU_SDK%\Lib\10.0.26100.0\km\arm64;%GPU_SDK%\Lib\wdf\kmdf\arm64\1.15;%GPU_VC%\lib\arm64"
for %%d in (FrontHardware RearHardware FrontVirtual RearVirtual Runtime) do if not exist out\%%d mkdir out\%%d
for %%d in (front-hardware rear-hardware front-virtual rear-virtual pump host) do if not exist out\obj\%%d mkdir out\obj\%%d
if not exist user\gpuisp\build mkdir user\gpuisp\build
echo Building front hardware 066...
"%GPU_CL%" /nologo /c /kernel /O2 /Oi /W3 /WX /Zl /GS /D_ARM64_ /DARM64 /D_WIN64 /D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A00000C /DDBG=1 /Foout\obj\front-hardware\ kernel\NabuCamHwFront\NabuCamHwFront.c kernel\NabuCamHwFront\Platform.c kernel\NabuCamHwFront\Spmi.c
if errorlevel 1 exit /b 1
"%GPU_LINK%" /nologo /driver /subsystem:native /machine:arm64 /entry:FxDriverEntry /nodefaultlib /integritycheck /opt:ref /opt:icf /out:out\FrontHardware\NabuCamHwFront.sys out\obj\front-hardware\*.obj ntoskrnl.lib hal.lib wmilib.lib BufferOverflowFastFailK.lib libcntpr.lib ks.lib ksguid.lib wdfdriverentry.lib wdfldr.lib
if errorlevel 1 exit /b 1
echo Building rear hardware 065...
"%GPU_CL%" /nologo /c /kernel /O2 /Oi /W3 /WX /Zl /GS /D_ARM64_ /DARM64 /D_WIN64 /DNABU_REAR_CAPTURE_LIBRARY /D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A00000C /DDBG=1 /Foout\obj\rear-hardware\ kernel\NabuCamHwRear\NabuCamHwRear.c kernel\NabuCamAvStreamRear\HwCapture.c kernel\NabuCamAvStreamRear\Platform.c kernel\NabuCamAvStreamRear\Csiphy.c kernel\NabuCamAvStreamRear\Spmi.c
if errorlevel 1 exit /b 1
"%GPU_LINK%" /nologo /driver /subsystem:native /machine:arm64 /entry:FxDriverEntry /nodefaultlib /integritycheck /opt:ref /opt:icf /out:out\RearHardware\NabuCamHwRear.sys out\obj\rear-hardware\*.obj ntoskrnl.lib hal.lib wmilib.lib BufferOverflowFastFailK.lib libcntpr.lib ntstrsafe.lib wdfdriverentry.lib wdfldr.lib
if errorlevel 1 exit /b 1
call :virtual Front
if errorlevel 1 exit /b 1
call :virtual Rear
if errorlevel 1 exit /b 1
set "INCLUDE=%GPU_VC%\include;%GPU_SDK%\Include\10.0.26100.0\ucrt;%GPU_SDK%\Include\10.0.26100.0\shared;%GPU_SDK%\Include\10.0.26100.0\um;%GPU_SDK%\Include\10.0.26100.0\winrt"
set "LIB=%GPU_VC%\lib\arm64;%GPU_SDK%\Lib\10.0.26100.0\ucrt\arm64;%GPU_SDK%\Lib\10.0.26100.0\um\arm64"
call :shader FrontEndCS FrontEnd g_csFrontEnd
if errorlevel 1 exit /b 1
call :shader ColorDownCS ColorDown g_csColorDown
if errorlevel 1 exit /b 1
call :shader Nv12CS Nv12 g_csNv12
if errorlevel 1 exit /b 1
call :shader SharpYCS Sharp g_csSharp
if errorlevel 1 exit /b 1
call :shader SharpYCSep SharpSep g_csSharpSep
if errorlevel 1 exit /b 1
call :shader ChromaSmoothCS Chroma g_csChroma
if errorlevel 1 exit /b 1
call :shader ChromaGuide1CS Guide1 g_csGuide1
if errorlevel 1 exit /b 1
call :shader ChromaGuide2CS Guide2 g_csGuide2
if errorlevel 1 exit /b 1
echo Building GPU040 worker...
"%GPU_CL%" /nologo /W3 /EHsc /O2 /MT /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Iuser\gpuisp\build /Iuser\gpuisp\src /Iuser\bench\src user\GpuCameraPump.cpp user\bench\src\GpuIspRef.cpp user\gpuisp\src\GpuIspCore.cpp /Foout\obj\pump\ /Feout\Runtime\GpuCameraPump.exe /link /SUBSYSTEM:CONSOLE dxgi.lib d3d12.lib kernel32.lib user32.lib ole32.lib setupapi.lib
if errorlevel 1 exit /b 1
echo Building GPU040 service...
"%GPU_CL%" /nologo /W4 /WX /EHsc /O2 /MT user\GpuIspHost.cpp /Foout\obj\host\ /Feout\Runtime\GpuIspHost.exe /link /SUBSYSTEM:CONSOLE advapi32.lib kernel32.lib
if errorlevel 1 exit /b 1
copy /y kernel\NabuCamHwFront\NabuCamHwFront.inf out\FrontHardware\ >nul
copy /y kernel\NabuCamHwRear\NabuCamHwRear.inf out\RearHardware\ >nul
copy /y kernel\NabuCamAvStreamFrontVirtual\NabuCamAvStreamFrontVirtual.inf out\FrontVirtual\ >nul
copy /y kernel\NabuCamAvStreamRearVirtual\NabuCamAvStreamRearVirtual.inf out\RearVirtual\ >nul
copy /y profiles\*.txt out\Runtime\ >nul
echo SUCCESS: four ARM64 SYS files and two ARM64 EXE files built in out.
echo Sign the generated drivers with your own test certificate before installation.
exit /b 0
:virtual
echo Building %1 virtual camera 034...
"%GPU_CL%" /nologo /c /kernel /O2 /Oi /W3 /WX /Zl /GS /D_ARM64_ /DARM64 /D_WIN64 /D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A00000C /DDBG=1 /Foout\obj\%1-virtual\ kernel\NabuCamAvStream%1Virtual\NabuCamAvStream%1Virtual.c kernel\NabuCamAvStream%1Virtual\BackendBridge%1.c
if errorlevel 1 exit /b 1
"%GPU_LINK%" /nologo /driver /subsystem:native /machine:arm64 /entry:FxDriverEntry /nodefaultlib /integritycheck /opt:ref /opt:icf /out:out\%1Virtual\NabuCamAvStream%1Virtual.sys out\obj\%1-virtual\*.obj ntoskrnl.lib hal.lib wmilib.lib BufferOverflowFastFailK.lib libcntpr.lib ks.lib ksguid.lib wdfdriverentry.lib wdfldr.lib
exit /b %errorlevel%
:shader
"%GPU_FXC%" /nologo /T cs_5_0 /E %1 /Fh user\gpuisp\build\IspCs_%2.h /Vn %3 /Qstrip_reflect user\gpuisp\shaders\IspCs.hlsl
exit /b %errorlevel%
:usage
echo Usage: Build.cmd "E:\EWDK"
exit /b 2
:missing
echo Required EWDK compiler or shader compiler missing.
echo Expected MSVC 14.44.35207 and Windows SDK 10.0.26100.0.
exit /b 2
