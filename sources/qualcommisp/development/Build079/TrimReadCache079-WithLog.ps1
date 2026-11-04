$ErrorActionPreference='Stop'
if(@(Get-Process -Name 'WindowsCamera','QispCapture*','QispFocus*','TorchPulse*','DbgEngWitness*','cdb','tracerpt','devcon','pnputil','wpr' -ErrorAction SilentlyContinue).Count){throw 'Owner/recorder still active; no cache flush'}
& 'C:\Users\Xiaomi\AppData\Local\Python\pythoncore-3.15-64\python.exe' 'H:\Vlad\Codex\QualcommISP\Build079\tools\Trim-ReadCache079.py' *> 'H:\Vlad\Codex\QualcommISP\Build079\research\cache-trim079.log'
