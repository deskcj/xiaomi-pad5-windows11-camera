$ErrorActionPreference='Stop'
try{
 & 'H:\Vlad\Codex\QualcommISP\Build249\tools\Trim-Closed249a.ps1' -OutputFile 'H:\Vlad\Codex\QualcommISP\Build249\research\memory-trim249a.json'
 & 'H:\Vlad\Codex\QualcommISP\Build249\tools\Stage-Apply-Capture249a.ps1'
}catch{
 [ordered]@{status='FAILED249A_STAGE_OR_LAUNCH_INSPECT_JOURNALS';pid=$PID;time=(Get-Date -Format o);error=$_.Exception.Message;position=$_.InvocationInfo.PositionMessage;automaticRecovery=$false}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build249\research\launch-failure249a.json' -Encoding UTF8
}
