param([Parameter(Mandatory=$true)][string]$OutputFile,[System.IO.FileStream]$CallerLock)
# Reversible residency trim, only with independently closed cameras. No kill.
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(Test-Path -LiteralPath $OutputFile){throw 'Preserve previous memory receipt'}
New-Item -ItemType Directory -Path (Split-Path -Parent $OutputFile) -Force|Out-Null
$r=[ordered]@{status='FAILED249A_BETWEEN_SESSION_RESIDENCY_TRIM';started=(Get-Date -Format o);processTermination=$false;driverChanges=$false;tuningChanges=$false;targets=@();errors=@()}
$lock=$null
try {
 $prior=Get-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build247\research\final-state247b.json' -Raw|ConvertFrom-Json
 if($prior.status -ne 'PASS243_CURRENT080_075B_ACCEPTED_DATA243_NO_OWNERS_NO_NEW_CRASH_READONLY' -or $prior.errors.Count -or [datetimeoffset](Get-CimInstance Win32_OperatingSystem).LastBootUpTime -ne [datetimeoffset]$prior.boot){throw 'Current216 closed baseline required'}
 if(@(Get-Process -Name 'QispCapture*','CamProbe*','WindowsCamera','WinRoi*','WinFocus*','ReadMft*','tracerpt','pnputil','devcon','java' -ErrorAction SilentlyContinue).Count -or (Get-Service FrameServer).Status -ne 'Stopped'){throw 'Only between fully closed camera sessions'}
 if($CallerLock){if(-not $CallerLock.CanWrite -or $CallerLock.Name -ne 'C:\ProgramData\CodexQispBuild062\operation.lock' -or $CallerLock.SafeFileHandle.IsClosed){throw 'Exact active in-process operator lock required'}}else{$lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
 $r.freeBeforeMiB=[math]::Round((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory/1024)
 if(-not ('QispResidencyTrim201' -as [type])){Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class QispResidencyTrim201 {
 [DllImport("psapi.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)]
 public static extern bool EmptyWorkingSet(IntPtr process);
}
'@}
 $candidates=@(Get-CimInstance Win32_Process|Where-Object {
   ($_.Name -eq 'ChatGPT.exe' -and $_.ExecutablePath -like 'C:\Program Files\WindowsApps\OpenAI.Codex_*\app\ChatGPT.exe' -and ($_.CommandLine -match '(?:^| )--type=renderer(?: |$)' -or $_.CommandLine -match '(?:^| )--no-startup-window(?: |$)'))
 })
 foreach($candidate in $candidates){
  $p=Get-Process -Id $candidate.ProcessId -ErrorAction Stop
  if([math]::Abs($p.StartTime.ToUniversalTime().Ticks-$candidate.CreationDate.ToUniversalTime().Ticks) -gt 9 -or ($p.ProcessName+'.exe') -ne $candidate.Name){throw 'Trim PID identity changed'}
  $entry=[ordered]@{pid=$p.Id;name=$p.ProcessName;creation=$p.StartTime.ToString('o');workingSetBeforeMiB=[math]::Round($p.WorkingSet64/1MB);trimmed=$false}
  $entry.trimmed=[QispResidencyTrim201]::EmptyWorkingSet($p.Handle)
  if(-not $entry.trimmed){$entry.win32Error=[Runtime.InteropServices.Marshal]::GetLastWin32Error()}
  $p.Refresh();$entry.workingSetAfterMiB=[math]::Round($p.WorkingSet64/1MB)
  $r.targets+=@($entry)
 }
 Start-Sleep -Seconds 3
 $r.freeAfterMiB=[math]::Round((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory/1024)
 $r.status='PASS249A_UI_RESIDENCY_TRIM_BETWEEN_CLOSED_SESSIONS_NO_PROCESS_TERMINATION'
} catch {$r.errors+=@($_.Exception.Message)}
finally {$r.completed=(Get-Date -Format o);$r|ConvertTo-Json -Depth 7|Set-Content -LiteralPath $OutputFile -Encoding UTF8;if($lock){$lock.Dispose()}}
if($r.status -notlike 'PASS*'){throw ($r.errors -join '; ')}

