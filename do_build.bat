@echo on
echo Setting environment...
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.5.1
set IDF_PYTHON_ENV_PATH=C:\Espressif\python_env\idf5.5_py3.11_env
set PATH=C:\Espressif\tools\idf-git\2.44.0\cmd;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;%PATH%
set MSYSTEM=
cd /d C:\DayZ_servertracker
echo Running build and flash...
C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe C:\Espressif\frameworks\esp-idf-v5.5.1\tools\idf.py -p COM5 flash
echo Flash finished with exit code %ERRORLEVEL%
