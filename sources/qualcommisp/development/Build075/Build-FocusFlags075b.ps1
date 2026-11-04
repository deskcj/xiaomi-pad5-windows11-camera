$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$asm='H:\Vlad\EWDK\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostarm64\arm64\armasm64.exe'
& $asm -o (Join-Path $root 'tests\FocusFlags075b.obj') (Join-Path $root 'src\FocusFlags075b.asm') *> (Join-Path $root 'tests\FocusFlags075b-build.log')
if($LASTEXITCODE){throw 'Focus flags assembler failed'}
'PASS075B_ASSEMBLED_OFFLINE'
