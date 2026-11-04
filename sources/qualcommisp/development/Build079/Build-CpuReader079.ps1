$ErrorActionPreference='Stop'
$b=Split-Path $PSScriptRoot
$private='H:\Vlad\EWDK\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\PrivateAssemblies'
$trace=Join-Path $private 'Microsoft.Diagnostics.Tracing.TraceEvent.dll'
New-Item -ItemType Directory -Path (Join-Path $b 'bin') -Force|Out-Null
$netstandard=Join-Path ([Runtime.InteropServices.RuntimeEnvironment]::GetRuntimeDirectory()) 'netstandard.dll'
$asm=[Reflection.Assembly]::LoadFrom($trace)
$refs=@($trace,$netstandard,'System.Core.dll','System.Web.Extensions.dll')
foreach($ref in $asm.GetReferencedAssemblies()){$file=Join-Path $private ($ref.Name+'.dll');if(Test-Path -LiteralPath $file){$refs+=@($file)}}
Add-Type -Path (Join-Path $b 'src\ReadCpu079.cs') -ReferencedAssemblies $refs -OutputAssembly (Join-Path $b 'bin\ReadCpu079.exe') -OutputType ConsoleApplication
Copy-Item -LiteralPath $trace -Destination (Join-Path $b 'bin')
foreach($ref in $asm.GetReferencedAssemblies()){
 $file=Join-Path $private ($ref.Name+'.dll');if(Test-Path -LiteralPath $file){Copy-Item -LiteralPath $file -Destination (Join-Path $b 'bin')}
}
'PASS079_OFFLINE_CPU_READER_BUILT'
