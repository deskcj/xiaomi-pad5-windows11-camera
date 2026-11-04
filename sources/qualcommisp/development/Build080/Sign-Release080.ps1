$ErrorActionPreference='Stop'
$b=Split-Path $PSScriptRoot;$dir=Join-Path $b 'candidate\AVStream'
if(Test-Path -LiteralPath (Join-Path $b 'research\signed080.json')){throw 'Preserve signed manifest'}
$p=Get-Content -Raw -LiteralPath (Join-Path $b 'research\prepared080.json')|ConvertFrom-Json
$r=Get-Content -Raw -LiteralPath (Join-Path $b 'research\release-replay080.json')|ConvertFrom-Json
if($p.status -ne 'PASS080_SINGLE_STOCK_RELEASE_CALL_PREPARED_UNSIGNED_NOT_INSTALLED' -or $r.status -ne 'PASS080_EXACT_ORIGINAL_ARM64_IPE_FLUSH_RELEASE_REPLAY' -or @($r.cases).Count -ne 21){throw 'Exact release proof required'}
foreach($name in 'unsigned-native-final080.log','unsigned-unwind080.log','unsigned-loader080.log'){
 if([IO.File]::ReadAllText((Join-Path $b ('tests\'+$name))) -notmatch 'PASS'){throw 'Offline native proof missing'}
}
$sys=Join-Path $dir 'qccamavs8150.sys'
if((Get-FileHash -LiteralPath $sys).Hash -ne $p.unsignedSHA256){throw 'Unsigned candidate changed'}
$sign='H:\Vlad\EWDK\Program Files\Windows Kits\10\bin\10.0.26100.0\arm64\signtool.exe'
$inf2cat='H:\Vlad\EWDK\Program Files\Windows Kits\10\bin\10.0.26100.0\x86\Inf2Cat.exe'
$cert='F38EDD5C105095C8342556158CE66253F045965D'
& $sign sign /fd SHA256 /sha1 $cert $sys *> (Join-Path $b 'tests\sys-sign080.log');if($LASTEXITCODE){throw 'SYS signing failed'}
& $inf2cat ('/driver:'+$dir) /os:10_GE_ARM64 /uselocaltime *> (Join-Path $b 'tests\inf2cat080.log');if($LASTEXITCODE){throw 'Inf2Cat failed'}
$cat=Join-Path $dir 'CodexQisp080AVStream.cat'
& $sign sign /fd SHA256 /sha1 $cert $cat *> (Join-Path $b 'tests\cat-sign080.log');if($LASTEXITCODE){throw 'Catalog signing failed'}
foreach($f in Get-ChildItem -LiteralPath $dir -File|Where-Object Extension -ne '.cat'){
 & $sign verify /pa /c $cat $f.FullName *> (Join-Path $b ('tests\verify080-'+$f.Name+'.log'));if($LASTEXITCODE){throw ('Catalog verification failed: '+$f.Name)}
}
if((Get-FileHash -LiteralPath (Join-Path $dir 'QcDeviceMFT8150.dll')).Hash -ne $p.mftSHA256){throw 'MFT075 changed'}
[ordered]@{status='PASS080_SIGNED_CANDIDATE_NOT_INSTALLED';version='1.0.2324.80';files=@(Get-ChildItem -LiteralPath $dir -File|ForEach-Object{[ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $b 'research\signed080.json') -Encoding UTF8
'PASS080_SIGNED_CANDIDATE_NOT_INSTALLED'
