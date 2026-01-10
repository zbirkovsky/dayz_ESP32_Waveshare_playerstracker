$env:MSYSTEM = $null
$env:MSYSTEM_CHOST = $null
$env:MINGW_PREFIX = $null
$env:MSYSTEM_CARCH = $null
$env:MSYSTEM_PREFIX = $null
$env:MINGW_PACKAGE_PREFIX = $null
$env:IDF_PATH = 'C:\Users\zbirk\esp\v5.5.1\esp-idf'
$env:IDF_TOOLS_PATH = 'C:\Espressif'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.11_env'
$env:PATH = 'C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;C:\Espressif\python_env\idf5.5_py3.11_env\Scripts;C:\Windows\System32;C:\Windows'
Set-Location 'C:\DayZ_servertracker'
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' 'C:\Users\zbirk\esp\v5.5.1\esp-idf\tools\idf.py' build flash
