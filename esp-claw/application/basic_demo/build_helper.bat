@echo off
chcp 65001 >nul 2>&1
set PYTHONIOENCODING=utf-8
set IDF_PATH=e:\ESPIDF\551\v5.5.1\esp-idf
set IDF_TOOLS_PATH=e:\ESPIDF\551\Tools_551
set IDF_PYTHON_ENV_PATH=E:\ESPIDF\551\Tools_551\python_env\idf5.5_py3.11_env
set MSYSTEM=
set PATH=E:\ESPIDF\551\Tools_551\tools\cmake\3.30.2\bin;E:\ESPIDF\551\Tools_551\tools\ninja\1.12.1;E:\ESPIDF\551\Tools_551\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;%PATH%
cd /d E:\Projects\ESPCLAW\esp-claw\application\basic_demo
E:\ESPIDF\551\Tools_551\python_env\idf5.5_py3.11_env\Scripts\python.exe %IDF_PATH%\tools\idf.py build 2>&1
echo BUILD_EXIT_CODE=%ERRORLEVEL%
