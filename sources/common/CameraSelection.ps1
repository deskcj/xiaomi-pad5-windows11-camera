Set-StrictMode -Version Latest
function Get-CameraRecoveryAction([object]$Previous,[object]$CurrentBootIdentity,[string]$CurrentBoot) {
 if(-not $Previous){return 'NONE'}
 if(-not ($Previous.pendingPid -or $Previous.unsafeToContinue -or -not $Previous.captureClosed -or -not $Previous.fullMftDecoded -or $Previous.rebootRequired)){return 'NONE'}
 $newBoot=$false
 if($Previous.PSObject.Properties['bootIdentity']){$newBoot=Test-CameraNewBoot $Previous.bootIdentity $CurrentBootIdentity}
 elseif($Previous.PSObject.Properties['boot']){$newBoot=[math]::Abs(([datetimeoffset]$CurrentBoot-[datetimeoffset]$Previous.boot).TotalSeconds) -gt 2}
 if($newBoot){return 'NEW_BOOT'}
 return 'RESTART_REQUIRED'
}
function Get-ProbeRetirement([string]$Text) {
 $states=[regex]::Matches($Text,'(?m)^STATE256 captureCreated=([01]) initializeAttempted=([01]) readerCreated=([01]) startAttempted=([01]) readerStarted=([01])\r?$')
 $retired=[regex]::Matches($Text,'(?m)^RETIRE256 neverOpened=([01]) readerStopped=([01]) readerClosed=([01]) captureClosed=([01])\r?$')
 $failed=$Text -match '(?m)^CLOSE256 \S+ hr=(?!00000000)[0-9a-fA-F]{8}\r?$'
 $closed=$false;$neverOpened=$false
 if($states.Count -eq 1 -and $retired.Count -eq 1 -and -not $failed){
  $s=$states[0].Groups;$t=$retired[0].Groups
  $created=$s[1].Value -eq '1';$initialized=$s[2].Value -eq '1';$reader=$s[3].Value -eq '1';$start=$s[4].Value -eq '1';$started=$s[5].Value -eq '1'
  $consistent=($created -eq $initialized) -and (-not $reader -or $created) -and (-not $start -or $reader) -and (-not $started -or $start)
  $neverOpened=-not $created -and -not $initialized -and -not $reader -and -not $start -and -not $started -and $t[1].Value -eq '1'
  $neverOpened=$neverOpened -and $Text -notmatch '(?m)^STEP256 (initialize|reader_)' -and $Text -notmatch '(?m)^CLOSE256 '
  $marks=(-not $start -or $Text -match '(?m)^CLOSE256 reader_stop hr=00000000\r?$') -and (-not $reader -or $Text -match '(?m)^CLOSE256 reader_close hr=00000000\r?$') -and (-not $created -or $Text -match '(?m)^CLOSE256 capture_close hr=00000000\r?$')
  $closed=$consistent -and $t[2].Value -eq '1' -and $t[3].Value -eq '1' -and $t[4].Value -eq '1' -and $marks -and (($created -and $t[1].Value -eq '0') -or $neverOpened)
 }elseif($states.Count -eq 0 -and $retired.Count -eq 0 -and -not $failed){
  # Compatibility for historical recorded probes; missing markers never prove safety.
  $closed=$Text -match '(?m)^CLOSE256 reader_stop hr=00000000\r?$' -and $Text -match '(?m)^CLOSE256 reader_close hr=00000000\r?$' -and $Text -match '(?m)^CLOSE256 capture_close hr=00000000\r?$'
 }
 [pscustomobject]@{closed=[bool]$closed;neverOpened=[bool]($closed -and $neverOpened);lifecyclePresent=($states.Count -gt 0 -or $retired.Count -gt 0)}
}
function Read-CameraInventory([string]$Path) {
 $inventory=Get-Content -LiteralPath $Path -Raw -Encoding UTF8|ConvertFrom-Json
 if($inventory.schema -ne 1 -or $inventory.revision -ne 'R22' -or $inventory.enumerationComplete -ne $true -or $inventory.cameraOpened -ne $false){throw 'Camera enumeration did not prove completion without opening a camera'}
 $seen=@{}
 foreach($device in @($inventory.devices)){
  if(-not $device.id -or $device.id -match '["\r\n]' -or $seen.ContainsKey($device.id)){throw 'Camera enumeration contains invalid or duplicate interface identities'}
  $seen[$device.id]=$true
 }
 return $inventory
}
function Select-NativeCamera([object]$Inventory,[string]$AvsInstance,[ValidateSet('front','rear')][string]$Side,[switch]$AllowAbsent) {
 $interfacePrefix='\\?\'+$AvsInstance.Replace('\','#')+'#'
 $native=@($Inventory.devices|Where-Object {$_.enabled -eq $true -and ($_.instanceId -ieq $AvsInstance -or $_.id.StartsWith($interfacePrefix,[StringComparison]::OrdinalIgnoreCase))})
 $frontGuid='{4FAEAFD4-041B-4E46-85FD-400473891182}';$rearGuid='{5584F823-3830-4CFD-947F-78DE17A8B14C}'
 $chosen=@()
 foreach($device in $native){
  if($device.instanceId -and $device.instanceId -ine $AvsInstance){throw 'Camera interface and device instance identities disagree'}
  $front=$device.panel -eq 1 -or $device.id.EndsWith('\'+$frontGuid,[StringComparison]::OrdinalIgnoreCase)
  $rear=$device.panel -eq 2 -or $device.id.EndsWith('\'+$rearGuid,[StringComparison]::OrdinalIgnoreCase)
  if($front -and $rear){throw 'Native camera panel and sensor interface identities disagree'}
  if(-not $front -and -not $rear){throw 'Unrecognized native camera sensor identity; retain drivers and inspect camera enumeration'}
  if(($Side -eq 'front' -and $front) -or ($Side -eq 'rear' -and $rear)){$chosen+=@($device)}
 }
 if($chosen.Count -eq 0 -and $AllowAbsent){return $null}
 if($chosen.Count -ne 1){throw ('Expected one native '+$Side+' camera interface, found '+$chosen.Count+'. See *-enumeration.json; friendly names are not used to select native cameras.')}
 return $chosen[0]
}
function Get-CameraInventory([string]$Label) {
 $toolRoot=if($cache){$cache}else{$root};$path=Join-Path $run ($Label+'-enumeration.json')
 $null=Run-Child ($Label+'-enumerate') (Join-Path $toolRoot 'Tools\CameraProbe256.exe') @('--enumerate-json',('"'+$path+'"')) 30000
 return Read-CameraInventory $path
}
