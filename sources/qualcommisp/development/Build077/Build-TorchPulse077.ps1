# Offline ARM64 compilation only. Does not open a camera or execute output.
$ErrorActionPreference='Stop'
$taskRoot=Join-Path (Split-Path $PSScriptRoot) 'src'
$vc='H:\Vlad\EWDK\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$kit='H:\Vlad\EWDK\Program Files\Windows Kits\10'
$toolBin=Join-Path $vc 'bin\Hostarm64\arm64'
$priorInclude=$env:INCLUDE;$priorLib=$env:LIB;$priorPath=$env:PATH
try {
 $env:INCLUDE=(Join-Path $vc 'include')+';'+(Join-Path $kit 'Include\10.0.26100.0\um')+';'+(Join-Path $kit 'Include\10.0.26100.0\shared')+';'+(Join-Path $kit 'Include\10.0.26100.0\ucrt')+';'+(Join-Path $kit 'Include\10.0.26100.0\winrt')+';'+(Join-Path $kit 'Include\10.0.26100.0\cppwinrt')
 $env:LIB=(Join-Path $vc 'lib\arm64')+';'+(Join-Path $kit 'Lib\10.0.26100.0\um\arm64')+';'+(Join-Path $kit 'Lib\10.0.26100.0\ucrt\arm64')
 $env:PATH=$toolBin+';'+$env:PATH
 & (Join-Path $toolBin 'cl.exe') /nologo /EHsc /std:c++17 /W4 /Od /DUNICODE /D_UNICODE (Join-Path $taskRoot 'TorchPulse077.cpp') ('/Fo:'+(Join-Path $taskRoot 'TorchPulse077.obj')) ('/Fe:'+(Join-Path $taskRoot 'TorchPulse077.exe')) /link /MACHINE:ARM64 windowsapp.lib ole32.lib
 if($LASTEXITCODE){throw 'TorchPulse077 offline compilation failed'}
 'PASS_OFFLINE_BUILD_ONLY_NOT_EXECUTED'
}finally{$env:INCLUDE=$priorInclude;$env:LIB=$priorLib;$env:PATH=$priorPath}
