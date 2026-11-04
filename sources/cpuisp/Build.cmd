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
set "INCLUDE=%GPU_VC%\include;%GPU_SDK%\Include\10.0.26100.0\ucrt;%GPU_SDK%\Include\10.0.26100.0\shared;%GPU_SDK%\Include\10.0.26100.0\km;%GPU_SDK%\Include\wdf\kmdf\1.15"
set "PATH=%GPU_VC%\bin\%GPU_HOST%\arm64;%PATH%"
set "LIB=%GPU_SDK%\Lib\10.0.26100.0\um\arm64;%GPU_SDK%\Lib\10.0.26100.0\km\arm64;%GPU_SDK%\Lib\wdf\kmdf\arm64\1.15;%GPU_VC%\lib\arm64"

if not exist "out\obj\NabuCamHwFront" mkdir "out\obj\NabuCamHwFront"
if not exist "out\FrontHardware" mkdir "out\FrontHardware"
cd /d "%~dp0kernel\NabuCamHwFront"
echo Building CPUISP4 NabuCamHwFront with original WDK options...
"%GPU_CL%" /c /IARM64\DEBUG\ /Zi /nologo /W4 /WX /diagnostics:column /O2 /Oi /Ot /Oy- /GL /D _ARM64_ /D ARM64 /D _USE_DECLSPECS_FOR_SAL=1 /D STD_CALL /D DEPRECATE_DDK_FUNCTIONS=1 /D MSC_NOOPT /D _WIN32_WINNT=0x0A00 /D WINVER=0x0A00 /D WINNT=1 /D NTDDI_VERSION=0xA000010 /D DBG=1 /D _ARM64_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE=1 /GF /Gm- /Zp8 /GS /guard:cf /Gy /fp:precise /Qspectre /Zc:wchar_t- /Zc:forScope /Zc:inline /GR- /Fo"%~dp0out\obj\NabuCamHwFront\\" /Fd"%~dp0out\obj\NabuCamHwFront\vc143.pdb" /wd4064 /wd4627 /wd4627 /wd4366 /wd4748 /wd4603 /wd4627 /wd4986 /wd4987 /FI"%GPU_SDK%\INCLUDE\10.0.26100.0\SHARED\WARNING.H" /analyze- /FC /kernel -cbstring /d1import_no_registry /d2AllowCompatibleILVersions /d2Zi+ "%~dp0kernel\NabuCamHwFront\NabuCamHwFront.c" "%~dp0kernel\NabuCamHwFront\Platform.c" "%~dp0kernel\NabuCamHwFront\Spmi.c"
if errorlevel 1 exit /b 1
"%GPU_LINK%" /OUT:"%~dp0out\FrontHardware\NabuCamHwFront.sys" /VERSION:"10.0" /INCREMENTAL:NO /NOLOGO /WX WDFDRIVERENTRY.LIB WDFLDR.LIB ARM64RT.LIB "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\BUFFEROVERFLOWFASTFAILK.LIB" "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\NTOSKRNL.LIB" "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\HAL.LIB" "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\WMILIB.LIB" "%GPU_SDK%\LIB\WDF\KMDF\ARM64\1.15\WDFLDR.LIB" "%GPU_SDK%\LIB\WDF\KMDF\ARM64\1.15\WDFDRIVERENTRY.LIB" /NODEFAULTLIB /NODEFAULTLIB:OLDNAMES.LIB /MANIFEST:NO /DEBUG /PDB:"%~dp0out\obj\NabuCamHwFront\NabuCamHwFront.pdb" /SUBSYSTEM:NATIVE,"10.00" /STACK:"0x40000","0x2000" /Driver /OPT:REF /OPT:ICF /LTCG /ENTRY:"FxDriverEntry" /RELEASE /IMPLIB:"%~dp0out\obj\NabuCamHwFront\NabuCamHwFront.lib" /MERGE:"_TEXT=.text;_PAGE=PAGE" /MACHINE:ARM64 /PROFILE /guard:cf /kernel /machine:arm64 /IGNORE:4078,4221,4198 /osversion:10.0 /pdbcompress /debugtype:pdata "%~dp0out\obj\NabuCamHwFront\*.obj"
if errorlevel 1 exit /b 1
copy /y "NabuCamHwFront.inf" "%~dp0out\FrontHardware\NabuCamHwFront.inf" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist "out\obj\NabuCamHwRear" mkdir "out\obj\NabuCamHwRear"
if not exist "out\RearHardware" mkdir "out\RearHardware"
cd /d "%~dp0kernel\NabuCamHwRear"
echo Building CPUISP4 NabuCamHwRear with original WDK options...
"%GPU_CL%" /c /I..\NABUCAMAVSTREAMREAR /IARM64\DEBUG\ /Zi /nologo /W3 /WX- /diagnostics:column /O2 /Oi /Ot /Oy- /GL /D NABU_REAR_CAPTURE_LIBRARY /D _ARM64_ /D ARM64 /D _ARM64_ /D ARM64 /D _USE_DECLSPECS_FOR_SAL=1 /D STD_CALL /D DEPRECATE_DDK_FUNCTIONS=1 /D MSC_NOOPT /D _WIN32_WINNT=0x0A00 /D WINVER=0x0A00 /D WINNT=1 /D NTDDI_VERSION=0xA000010 /D DBG=1 /D _ARM64_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE=1 /GF /Gm- /Zp8 /GS /guard:cf /Gy /fp:precise /Qspectre /Zc:wchar_t- /Zc:forScope /Zc:inline /GR- /Fo"%~dp0out\obj\NabuCamHwRear\\" /Fd"%~dp0out\obj\NabuCamHwRear\vc143.pdb" /wd4100 /wd4201 /wd4214 /wd4152 /wd4064 /wd4627 /wd4627 /wd4366 /wd4748 /wd4603 /wd4627 /wd4986 /wd4987 /FI"%GPU_SDK%\INCLUDE\10.0.26100.0\SHARED\WARNING.H" /analyze- /FC /kernel -cbstring /d1import_no_registry /d2AllowCompatibleILVersions /d2Zi+ "%~dp0kernel\NabuCamHwRear\NabuCamHwRear.c" "%~dp0kernel\NabuCamAvStreamRear\HwCapture.c" "%~dp0kernel\NabuCamAvStreamRear\Platform.c" "%~dp0kernel\NabuCamAvStreamRear\Csiphy.c" "%~dp0kernel\NabuCamAvStreamRear\Spmi.c"
if errorlevel 1 exit /b 1
"%GPU_LINK%" /OUT:"%~dp0out\RearHardware\NabuCamHwRear.sys" /VERSION:"10.0" /INCREMENTAL:NO /NOLOGO /WX WDFDRIVERENTRY.LIB WDFLDR.LIB NTSTRSAFE.LIB ARM64RT.LIB "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\BUFFEROVERFLOWFASTFAILK.LIB" "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\NTOSKRNL.LIB" "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\HAL.LIB" "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\WMILIB.LIB" "%GPU_SDK%\LIB\WDF\KMDF\ARM64\1.15\WDFLDR.LIB" "%GPU_SDK%\LIB\WDF\KMDF\ARM64\1.15\WDFDRIVERENTRY.LIB" /NODEFAULTLIB /NODEFAULTLIB:OLDNAMES.LIB /MANIFEST:NO /DEBUG /PDB:"%~dp0out\obj\NabuCamHwRear\NabuCamHwRear.pdb" /SUBSYSTEM:NATIVE,"10.00" /STACK:"0x40000","0x2000" /Driver /OPT:REF /OPT:ICF /LTCG /ENTRY:"FxDriverEntry" /RELEASE /IMPLIB:"%~dp0out\obj\NabuCamHwRear\NabuCamHwRear.lib" /MERGE:"_TEXT=.text;_PAGE=PAGE" /MACHINE:ARM64 /PROFILE /guard:cf /kernel /machine:arm64 /IGNORE:4078,4221,4198 /osversion:10.0 /pdbcompress /debugtype:pdata "%~dp0out\obj\NabuCamHwRear\*.obj"
if errorlevel 1 exit /b 1
copy /y "NabuCamHwRear.inf" "%~dp0out\RearHardware\NabuCamHwRear.inf" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist "out\obj\NabuCamAvStreamFrontVirtual" mkdir "out\obj\NabuCamAvStreamFrontVirtual"
if not exist "out\FrontVirtual" mkdir "out\FrontVirtual"
cd /d "%~dp0kernel\NabuCamAvStreamFrontVirtual"
echo Building CPUISP4 NabuCamAvStreamFrontVirtual with original WDK options...
"%GPU_CL%" /c /I"%GPU_SDK%\INCLUDE\10.0.26100.0\KM\\" /IARM64\DEBUG\ /Zi /nologo /W3 /WX- /diagnostics:column /O2 /Oi /Ot /Oy- /GL /D _ARM64_ /D ARM64 /D _ARM64_ /D ARM64 /D _USE_DECLSPECS_FOR_SAL=1 /D STD_CALL /D DEPRECATE_DDK_FUNCTIONS=1 /D MSC_NOOPT /D _WIN32_WINNT=0x0A00 /D WINVER=0x0A00 /D WINNT=1 /D NTDDI_VERSION=0xA000010 /D DBG=1 /D _ARM64_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE=1 /GF /Gm- /Zp8 /GS /Gy /fp:precise /Zc:wchar_t- /Zc:forScope /Zc:inline /GR- /Fo"%~dp0out\obj\NabuCamAvStreamFrontVirtual\\" /Fd"%~dp0out\obj\NabuCamAvStreamFrontVirtual\vc143.pdb" /wd4201 /wd4214 /wd4100 /wd4152 /wd4064 /wd4627 /wd4627 /wd4366 /wd4748 /wd4603 /wd4627 /wd4986 /wd4987 /FI"%GPU_SDK%\INCLUDE\10.0.26100.0\SHARED\WARNING.H" /analyze- /FC /kernel -cbstring /d1import_no_registry /d2AllowCompatibleILVersions /d2Zi+ "%~dp0kernel\NabuCamAvStreamFrontVirtual\NabuCamAvStreamFrontVirtual.c" "%~dp0kernel\NabuCamAvStreamFrontVirtual\BackendBridgeFront.c"
if errorlevel 1 exit /b 1
"%GPU_LINK%" /OUT:"%~dp0out\FrontVirtual\NabuCamAvStreamFrontVirtual.sys" /VERSION:"10.0" /INCREMENTAL:NO /NOLOGO /LIBPATH:"%GPU_SDK%\LIB\WDF\KMDF\ARM64\1.15" /LIBPATH:"%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64" /WX WDFDRIVERENTRY.LIB WDFLDR.LIB KS.LIB KSGUID.LIB ARM64RT.LIB "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\BUFFEROVERFLOWFASTFAILK.LIB" "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\NTOSKRNL.LIB" "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\HAL.LIB" "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\WMILIB.LIB" "%GPU_SDK%\LIB\WDF\KMDF\ARM64\1.15\WDFLDR.LIB" "%GPU_SDK%\LIB\WDF\KMDF\ARM64\1.15\WDFDRIVERENTRY.LIB" /NODEFAULTLIB /NODEFAULTLIB:OLDNAMES.LIB /MANIFEST:NO /DEBUG /PDB:"%~dp0out\obj\NabuCamAvStreamFrontVirtual\NabuCamAvStreamFrontVirtual.pdb" /SUBSYSTEM:NATIVE,"10.00" /STACK:"0x40000","0x2000" /Driver /OPT:REF /OPT:ICF /LTCG /ENTRY:"FxDriverEntry" /RELEASE /IMPLIB:"%~dp0out\obj\NabuCamAvStreamFrontVirtual\NabuCamAvStreamFrontVirtual.lib" /MERGE:"_TEXT=.text;_PAGE=PAGE" /MACHINE:ARM64 /PROFILE /kernel /machine:arm64 /IGNORE:4078,4221,4198 /osversion:10.0 /pdbcompress /debugtype:pdata "%~dp0out\obj\NabuCamAvStreamFrontVirtual\*.obj"
if errorlevel 1 exit /b 1
copy /y "NabuCamAvStreamFrontVirtual.inf" "%~dp0out\FrontVirtual\NabuCamAvStreamFrontVirtual.inf" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist "out\obj\NabuCamAvStreamRearVirtual" mkdir "out\obj\NabuCamAvStreamRearVirtual"
if not exist "out\RearVirtual" mkdir "out\RearVirtual"
cd /d "%~dp0kernel\NabuCamAvStreamRearVirtual"
echo Building CPUISP4 NabuCamAvStreamRearVirtual with original WDK options...
"%GPU_CL%" /c /I"%GPU_SDK%\INCLUDE\10.0.26100.0\KM\\" /IARM64\DEBUG\ /Zi /nologo /W3 /WX- /diagnostics:column /O2 /Oi /Ot /Oy- /GL /D _ARM64_ /D ARM64 /D _ARM64_ /D ARM64 /D _USE_DECLSPECS_FOR_SAL=1 /D STD_CALL /D DEPRECATE_DDK_FUNCTIONS=1 /D MSC_NOOPT /D _WIN32_WINNT=0x0A00 /D WINVER=0x0A00 /D WINNT=1 /D NTDDI_VERSION=0xA000010 /D DBG=1 /D _ARM64_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE=1 /GF /Gm- /Zp8 /GS /Gy /fp:precise /Zc:wchar_t- /Zc:forScope /Zc:inline /GR- /Fo"%~dp0out\obj\NabuCamAvStreamRearVirtual\\" /Fd"%~dp0out\obj\NabuCamAvStreamRearVirtual\vc143.pdb" /wd4201 /wd4214 /wd4100 /wd4152 /wd4064 /wd4627 /wd4627 /wd4366 /wd4748 /wd4603 /wd4627 /wd4986 /wd4987 /FI"%GPU_SDK%\INCLUDE\10.0.26100.0\SHARED\WARNING.H" /analyze- /FC /kernel -cbstring /d1import_no_registry /d2AllowCompatibleILVersions /d2Zi+ "%~dp0kernel\NabuCamAvStreamRearVirtual\NabuCamAvStreamRearVirtual.c" "%~dp0kernel\NabuCamAvStreamRearVirtual\BackendBridgeRear.c"
if errorlevel 1 exit /b 1
"%GPU_LINK%" /OUT:"%~dp0out\RearVirtual\NabuCamAvStreamRearVirtual.sys" /VERSION:"10.0" /INCREMENTAL:NO /NOLOGO /LIBPATH:"%GPU_SDK%\LIB\WDF\KMDF\ARM64\1.15" /LIBPATH:"%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64" /WX WDFDRIVERENTRY.LIB WDFLDR.LIB KS.LIB KSGUID.LIB ARM64RT.LIB "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\BUFFEROVERFLOWFASTFAILK.LIB" "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\NTOSKRNL.LIB" "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\HAL.LIB" "%GPU_SDK%\LIB\10.0.26100.0\KM\ARM64\WMILIB.LIB" "%GPU_SDK%\LIB\WDF\KMDF\ARM64\1.15\WDFLDR.LIB" "%GPU_SDK%\LIB\WDF\KMDF\ARM64\1.15\WDFDRIVERENTRY.LIB" /NODEFAULTLIB /NODEFAULTLIB:OLDNAMES.LIB /MANIFEST:NO /DEBUG /PDB:"%~dp0out\obj\NabuCamAvStreamRearVirtual\NabuCamAvStreamRearVirtual.pdb" /SUBSYSTEM:NATIVE,"10.00" /STACK:"0x40000","0x2000" /Driver /OPT:REF /OPT:ICF /LTCG /ENTRY:"FxDriverEntry" /RELEASE /IMPLIB:"%~dp0out\obj\NabuCamAvStreamRearVirtual\NabuCamAvStreamRearVirtual.lib" /MERGE:"_TEXT=.text;_PAGE=PAGE" /MACHINE:ARM64 /PROFILE /kernel /machine:arm64 /IGNORE:4078,4221,4198 /osversion:10.0 /pdbcompress /debugtype:pdata "%~dp0out\obj\NabuCamAvStreamRearVirtual\*.obj"
if errorlevel 1 exit /b 1
copy /y "NabuCamAvStreamRearVirtual.inf" "%~dp0out\RearVirtual\NabuCamAvStreamRearVirtual.inf" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
echo SUCCESS: four CPUISP4 ARM64 drivers built with historical WDK options.
exit /b 0
:usage
echo Usage: Build.cmd "E:\EWDK"
exit /b 2
:missing
echo Required EWDK compiler or shader compiler missing.
echo Expected MSVC 14.44.35207 and Windows SDK 10.0.26100.0.
exit /b 2
