#!/usr/bin/env python
"""Build script for ESP32 that bypasses MSYSTEM environment issues."""
import subprocess
import sys
import os

# Filter out MSYSTEM-related env vars that block ESP-IDF
new_env = {}
for k, v in os.environ.items():
    if 'MSYS' in k or 'MINGW' in k or 'MSYSTEM' in k:
        continue
    new_env[k] = v

# Add ESP-IDF tools to PATH
paths_to_add = [
    'C:\\Espressif\\tools\\cmake\\3.30.2\\bin',
    'C:\\Espressif\\tools\\ninja\\1.12.1',
    'C:\\Espressif\\tools\\xtensa-esp-elf\\esp-14.2.0_20241119\\xtensa-esp-elf\\bin',
    'C:\\Espressif\\python_env\\idf5.5_py3.11_env\\Scripts',
    'C:\\Espressif\\frameworks\\esp-idf-v5.5.1\\tools',
]
new_env['PATH'] = ';'.join(paths_to_add) + ';' + new_env.get('PATH', '')
new_env['IDF_PATH'] = 'C:\\Espressif\\frameworks\\esp-idf-v5.5.1'
new_env['IDF_PYTHON_ENV_PATH'] = 'C:\\Espressif\\python_env\\idf5.5_py3.11_env'

python_exe = 'C:\\Espressif\\python_env\\idf5.5_py3.11_env\\Scripts\\python.exe'
idf_py = 'C:\\Espressif\\frameworks\\esp-idf-v5.5.1\\tools\\idf.py'

result = subprocess.run(
    [python_exe, idf_py, 'build'],
    env=new_env,
    cwd='C:\\DayZ_servertracker'
)
sys.exit(result.returncode)
