@echo off
setlocal
cd /d "%~dp0"

where pyw >nul 2>nul
if %errorlevel%==0 (
    start "PLC Simulator" pyw -3 "%~dp0plc_simulator.py"
    exit /b 0
)

where pythonw >nul 2>nul
if %errorlevel%==0 (
    start "PLC Simulator" pythonw "%~dp0plc_simulator.py"
    exit /b 0
)

echo 未找到 Python 3。请安装 Python 3，并勾选 Add Python to PATH。
pause
exit /b 1
