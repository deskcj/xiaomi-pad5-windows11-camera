$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$dir=Join-Path $root 'candidate-focus-b\AVStream'
$proof=Get-Content -Raw -LiteralPath (Join-Path $root 'research\prepared-focus075b.json')|ConvertFrom-Json
if($proof.status -ne 'PASS075B_OFFLINE_PACKAGE_PREPARED_UNSIGNED_NOT_INSTALLED'){throw 'Offline proof required'}
if((Get-FileHash -LiteralPath (Join-Path $dir 'QcDeviceMFT8150.dll')).Hash -ne $proof.candidateSHA){throw 'Unsigned candidate changed'}
$sign='H:\Vlad\EWDK\Program Files\Windows Kits\10\bin\10.0.26100.0\arm64\signtool.exe'
$inf2cat='H:\Vlad\EWDK\Program Files\Windows Kits\10\bin\10.0.26100.0\x86\Inf2Cat.exe'
$cert='F38EDD5C105095C8342556158CE66253F045965D'
& $sign sign /fd SHA256 /sha1 $cert (Join-Path $dir 'QcDeviceMFT8150.dll') *> (Join-Path $root 'tests\mft-sign075b.log')
if($LASTEXITCODE){throw '075 MFT signing failed'}
& $inf2cat ('/driver:'+$dir) /os:10_GE_ARM64 /uselocaltime *> (Join-Path $root 'tests\inf2cat075b.log')
if($LASTEXITCODE){throw '075 Inf2Cat failed'}
$cat=Join-Path $dir 'CodexQisp075AVStream.cat'
& $sign sign /fd SHA256 /sha1 $cert $cat *> (Join-Path $root 'tests\cat-sign075b.log')
if($LASTEXITCODE){throw '075 catalog signing failed'}
foreach($f in Get-ChildItem -LiteralPath $dir -File|Where-Object Extension -ne '.cat'){
 & $sign verify /pa /c $cat $f.FullName *> (Join-Path $root ('tests\verify075b-'+$f.Name+'.log'))
 if($LASTEXITCODE){throw ('075 catalog verification failed '+$f.Name)}
}
[ordered]@{status='PASS_SIGNED_CANDIDATE_NOT_INSTALLED';version='1.0.2324.75';files=@(Get-ChildItem -LiteralPath $dir -File|ForEach-Object{[ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $root 'research\signed-candidate075b.json') -Encoding UTF8
'PASS_SIGNED_AVSTREAM075_NOT_INSTALLED'
