$ErrorActionPreference = "Continue"

# Clear MSys environment variables
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:MSYSTEM_CARCH -ErrorAction SilentlyContinue
Remove-Item Env:MSYSTEM_CHOST -ErrorAction SilentlyContinue
Remove-Item Env:MSYSTEM_PREFIX -ErrorAction SilentlyContinue
Remove-Item Env:MINGW_CHOST -ErrorAction SilentlyContinue
Remove-Item Env:MINGW_PREFIX -ErrorAction SilentlyContinue

$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5.1"
$env:IDF_TOOLS_PATH = "C:\Espressif"
$env:PATH = "C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;C:\Espressif\python_env\idf5.5_py3.11_env\Scripts;C:\Espressif\tools\idf-git\2.44.0\cmd;$env:PATH"

$python = "C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
$idf = "C:\Espressif\frameworks\esp-idf-v5.5.1\tools\idf.py"

Set-Location "C:\DayZ_servertracker"

Write-Host "=== Building firmware ===" -ForegroundColor Green
& $python $idf build
if ($LASTEXITCODE -ne 0) { Write-Host "Error: build failed with code $LASTEXITCODE" -ForegroundColor Red; exit 1 }

Write-Host "`n=== SUCCESS! Firmware built ===" -ForegroundColor Green
Write-Host "Ready to flash. Run: idf.py -p COMx flash" -ForegroundColor Cyan
