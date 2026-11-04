$ErrorActionPreference='Stop'
$r=Split-Path $PSScriptRoot
$vc='H:\Vlad\EWDK\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$kit='H:\Vlad\EWDK\Program Files\Windows Kits\10';$bin=Join-Path $vc 'bin\Hostarm64\arm64'
$env:INCLUDE=(Join-Path $vc 'include')+';'+(Join-Path $kit 'Include\10.0.26100.0\um')+';'+(Join-Path $kit 'Include\10.0.26100.0\shared')+';'+(Join-Path $kit 'Include\10.0.26100.0\ucrt')
$env:LIB=(Join-Path $vc 'lib\arm64')+';'+(Join-Path $kit 'Lib\10.0.26100.0\um\arm64')+';'+(Join-Path $kit 'Lib\10.0.26100.0\ucrt\arm64')
$env:PATH=$bin+';'+$env:PATH
Push-Location (Join-Path $r 'src\focus-audit')
try{
 & (Join-Path $bin 'cl.exe') /nologo /EHsc /std:c++17 /W4 /O2 /DUNICODE /D_UNICODE CamProbe409.cpp ('/Fo:'+(Join-Path $r 'tests\FocusAudit075.obj')) ('/Fe:'+(Join-Path $r 'bin\QispFocus075.exe')) /link /MACHINE:ARM64 mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib wmcodecdspuuid.lib ole32.lib oleaut32.lib propsys.lib shlwapi.lib windowscodecs.lib strmiids.lib
 if($LASTEXITCODE){throw 'Rear capture compilation failed'}
}finally{Pop-Location}
'PASS_CAPTURE063_BUILD_OFFLINE'
