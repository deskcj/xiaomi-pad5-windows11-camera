$ErrorActionPreference='Stop'
$b=Split-Path $PSScriptRoot
$sdk='H:\Vlad\EWDK\Program Files\Microsoft Visual Studio\2022\BuildTools'
$assembly=[Reflection.Assembly]::LoadFrom((Join-Path $b 'bin\Microsoft.Diagnostics.Tracing.TraceEvent.dll'))
$rows=@()
foreach($reference in $assembly.GetReferencedAssemblies()){
 if($reference.Name -eq 'netstandard'){continue}
 $target=Join-Path $b ('bin\'+$reference.Name+'.dll')
 if((Test-Path -LiteralPath $target) -and ([Reflection.AssemblyName]::GetAssemblyName($target)).Version -eq $reference.Version){continue}
 $found=$null
 foreach($file in @(& rg --files -g ($reference.Name+'.dll') $sdk)){
  try{if(([Reflection.AssemblyName]::GetAssemblyName($file)).Version -eq $reference.Version){$found=$file;break}}catch{}
 }
 if($found){Copy-Item -LiteralPath $found -Destination $target;$rows+=@([ordered]@{name=$reference.Name;version=$reference.Version.ToString();source=$found;status='COPIED_EXACT'})}
 else{$rows+=@([ordered]@{name=$reference.Name;version=$reference.Version.ToString();status='NO_EXACT_LOCAL_FILE'})}
}
$rows|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $b 'research\reader-dependencies079.json') -Encoding UTF8
$rows|Format-Table name,version,status
