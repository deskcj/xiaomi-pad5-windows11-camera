$ErrorActionPreference='Stop'
$b=Split-Path $PSScriptRoot
$netstandard=Join-Path ([Runtime.InteropServices.RuntimeEnvironment]::GetRuntimeDirectory()) 'netstandard.dll'
$refs=@($netstandard,'System.Core.dll','System.Web.Extensions.dll')+@(Get-ChildItem -LiteralPath (Join-Path $b 'bin') -Filter '*.dll'|ForEach-Object FullName)
$exe=Join-Path $b 'bin\ReadCpu079c.exe'
Add-Type -Path (Join-Path $b 'src\ReadCpu079c.cs') -ReferencedAssemblies $refs -OutputAssembly $exe -OutputType ConsoleApplication
$doc=[Xml.XmlDocument]::new();$doc.LoadXml('<configuration><runtime><assemblyBinding xmlns="urn:schemas-microsoft-com:asm.v1"/></runtime></configuration>')
$bindings=$doc.DocumentElement.FirstChild.FirstChild
foreach($dll in Get-ChildItem -LiteralPath (Join-Path $b 'bin') -Filter '*.dll'){
 $name=[Reflection.AssemblyName]::GetAssemblyName($dll.FullName);$token=($name.GetPublicKeyToken()|ForEach-Object {$_.ToString('x2')}) -join ''
 $dep=$doc.CreateElement('dependentAssembly',$bindings.NamespaceURI);$id=$doc.CreateElement('assemblyIdentity',$bindings.NamespaceURI);$id.SetAttribute('name',$name.Name);$id.SetAttribute('publicKeyToken',$token);$id.SetAttribute('culture','neutral');[void]$dep.AppendChild($id)
 $redirect=$doc.CreateElement('bindingRedirect',$bindings.NamespaceURI);$redirect.SetAttribute('oldVersion','0.0.0.0-'+$name.Version);$redirect.SetAttribute('newVersion',$name.Version.ToString());[void]$dep.AppendChild($redirect);[void]$bindings.AppendChild($dep)
}
$doc.Save($exe+'.config')
'PASS079C_OFFLINE_CPU_READER_AND_LOCAL_BINDINGS_BUILT'
