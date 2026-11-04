$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$root=[IO.Path]::GetFullPath('C:\Users\Xiaomi\Documents\Codex\2026-10-23\new-chat\outputs')
$archive=[IO.Path]::GetFullPath('H:\Vlad\Codex\QualcommISP\checkpoints\C-historical-notes243')
$pairs=,@('Night079-checkpoint-20261024-032243','NIGHT079-CHECKPOINT-LATEST.txt')
$rows=@();[IO.Directory]::CreateDirectory($archive)|Out-Null
foreach($pair in $pairs){
 $name=$pair[0];$source=[IO.Path]::GetFullPath((Join-Path $root $name));$dest=[IO.Path]::GetFullPath((Join-Path $archive $name));$pointer=Join-Path $root $pair[1]
 if((Split-Path -Parent $source) -ne $root -or -not $source.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or $name -notmatch '^Night0(?:79)-checkpoint-20261024-[0-9]{6}$'){throw 'Source scope differs'}
 if((Split-Path -Parent $dest) -ne $archive -or -not $dest.StartsWith($archive+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Archive scope differs'}
 if(-not(Test-Path -LiteralPath (Join-Path $source 'manifest.json')) -or (Test-Path -LiteralPath $dest) -or ([IO.File]::ReadAllText($pointer)).Trim() -ne $source){throw 'Source/pointer/previous archive differs'}
 if(@(Get-ChildItem -LiteralPath $source -Recurse -Force|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count -or ((Get-Item -LiteralPath $source).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'No reparse-point traversal'}
 Copy-Item -LiteralPath $source -Destination $dest -Recurse
 $files=@(Get-ChildItem -LiteralPath $source -Recurse -File);$copies=@(Get-ChildItem -LiteralPath $dest -Recurse -File)
 if($files.Count -ne $copies.Count){throw 'Archive file count differs'}
 $bytes=0
 foreach($f in $files){$p=Join-Path $dest $f.FullName.Substring($source.Length+1);if((Get-FileHash -LiteralPath $f.FullName).Hash -ne (Get-FileHash -LiteralPath $p).Hash){throw 'Archive SHA differs'};$bytes+=$f.Length}
 # Both absolute targets were checked. Only this old duplicate workspace snapshot is retired after every file matches on H.
 [IO.File]::WriteAllText($pointer,$dest+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
 Remove-Item -LiteralPath $source -Recurse -Force
 $rows+=@([ordered]@{source=$source;archive=$dest;files=$files.Count;bytes=$bytes;allSHA256Verified=$true;pointerUpdated=$pointer})
}
[ordered]@{status='PASS243_HISTORICAL_DUPLICATE_WORKSPACE_COPIES_ARCHIVED_SHA_VERIFIED';rows=$rows;currentDriverRollbackPackagesUntouched=$true;freeC=[IO.DriveInfo]::new('C:\').AvailableFreeSpace}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build243\research\archive243.json' -Encoding UTF8
'PASS243_ARCHIVE'
