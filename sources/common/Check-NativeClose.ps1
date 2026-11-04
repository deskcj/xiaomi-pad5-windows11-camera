param([Parameter(Mandatory=$true)][string]$XmlFile,[Parameter(Mandatory=$true)][string]$OutputFile)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$reader=[Xml.XmlReader]::Create($XmlFile)
$index=0;$closed=0;$initialized=0;$lastClose=-1;$lastInit=-1;$lastStop=-1;$lost=0
$markers=[Collections.Generic.List[object]]::new()
try{
 while($reader.Read()){
  if($reader.NodeType -eq [Xml.XmlNodeType]::Element -and $reader.LocalName -eq 'Data'){
   $name=$reader.GetAttribute('Name');$value=$reader.ReadElementContentAsString()
   if($name -eq 'EventsLost'){$lost+=[int]$value}
   if($name -ne 'Message'){continue}
   $index++
   $kind=$null
   if($value -match 'successfully initialized IFE'){$initialized++;$lastInit=$index;$kind='IFE_INITIALIZED'}
   elseif($value -match 'successfully closed IFE!'){$closed++;$lastClose=$index;$kind='IFE_CLOSED'}
   elseif($value -match 'CCaptureFilter::StopVideo.*INITIALIZED'){$lastStop=$index;$kind='STOP_VIDEO_INITIALIZED'}
   elseif($value -match 'Failed to close IFE|DAL_ife_mgr_close is failed|Failed to stop IFE|Failed to close ife\d|IFE: Failed to (stop|close)'){$kind='IFE_CLOSE_FAILURE'}
   elseif($value -match '\bICP:\s*device close failed\b|\b(?:ICP|IPE)\b[^\r\n]*(?:Failed to (?:close|destroy)|(?:close|destroy)(?:\(\))?\s+(?:is\s+)?failed)|\bIpeCloseFailure\b'){$kind='ICP_CLOSE_FAILURE'}
   elseif($value -match '\bICPFW:SFR received\s+(?:Data Abort|Prefetch Abort|Undefined Instruction|Fatal|Assert|Exception)|\b(?:ICP|IPE)\b[^\r\n]*(?:Data Abort Exception|firmware\s+(?:abort(?:ed)?|fatal|crash(?:ed)?))'){$kind='FIRMWARE_ABORT'}
   # These explicit textual success markers are conditional additional evidence.
   # Completed035-038 traces contain none: ThreadProc exiting/reset IRQ ACK is
   # not an ICP close/destroy completion and must never be promoted to one.
   elseif($value -match '\bICP:\s*(?:device close (?:succeeded|successful(?:ly)?|success)\b|successfully closed (?:ICP|device)\b)|\b(?:ICP|IPE)\b[^\r\n]*\b(?:close|destroy)\b[^\r\n]*\bACK\b[^\r\n]*\b(?:status|result)\s*=\s*(?:0x0+|0)\b'){$kind='ICP_CLOSE_SUCCESS'}
   if($kind){$markers.Add([ordered]@{index=$index;kind=$kind;message=$value.Trim()})}
  }
 }
}finally{$reader.Dispose()}
$current=@($markers|Where-Object {$_.index -gt $lastInit})
$errors=@($current|Where-Object {$_.kind -in 'IFE_CLOSE_FAILURE','ICP_CLOSE_FAILURE','FIRMWARE_ABORT'})
$successes=@($current|Where-Object kind -eq 'ICP_CLOSE_SUCCESS')
$allSuccesses=@($markers|Where-Object kind -eq 'ICP_CLOSE_SUCCESS')
$orderedIfe=$initialized -gt 0 -and $lastStop -gt $lastInit -and $lastClose -gt $lastStop
$icpSuccessAvailable=$allSuccesses.Count -gt 0
$orderedIcp=(-not $icpSuccessAvailable) -or @($successes|Where-Object {$_.index -gt $lastStop -and $_.index -le $lastClose}).Count -gt 0
$reasons=@()
if($lost -ne 0){$reasons+=@('Trace events lost')}
if(-not $orderedIfe){$reasons+=@('Same opening must show IFE initialization, StopVideo INITIALIZED, then successful IFE close in order')}
if(-not $orderedIcp){$reasons+=@('An explicit ICP close/destroy success marker exists in this trace but none belongs to the current ordered closing interval')}
foreach($errorMarker in $errors){$reasons+=@($errorMarker.kind+' at message '+$errorMarker.index+': '+$errorMarker.message)}
$accepted=$reasons.Count -eq 0
[ordered]@{schema=2;status=$(if($accepted){'PASS'}else{'HOLD'});nativeCloseSucceeded=$accepted;eventsLost=$lost;messageCount=$index;initialized=$initialized;closed=$closed;lastInitializedIndex=$lastInit;lastClosedIndex=$lastClose;lastStopVideoIndex=$lastStop;orderedSameOpeningIfeClose=$orderedIfe;explicitIcpSuccessAvailable=$icpSuccessAvailable;explicitIcpSuccessOrdered=$orderedIcp;explicitIcpSuccessRequired=$icpSuccessAvailable;icpSuccessEvidence=$(if($icpSuccessAvailable){'Conditional explicit close/destroy marker'}else{'No explicit ICP success marker in this trace; none invented'});errors=@($errors|ForEach-Object message);reasons=@($reasons);closeMessages=@($current|Where-Object kind -eq 'IFE_CLOSED'|ForEach-Object message);markers=@($markers);dmaRetirementProven=$false;limits=@('Native close success is additional retirement evidence, not hardware DMA quiescence proof.','Explicit same-opening ICP close failure or firmware SFR abort rejects even after successful IFE close.','ThreadProc exiting, reset ACK warnings and ordinary ICP error-level CAMNOC status are not close success or fatal abort markers.','No context/request identity is present in these textual markers; ordering uses the last observed IFE initialization in this one-opening trace.')}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath $OutputFile -Encoding UTF8
if(-not $accepted){throw ('Native close evidence rejected: '+($reasons -join '; ')+'; retain bindings, no rollback')}
