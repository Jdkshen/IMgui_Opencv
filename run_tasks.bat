@echo off
setlocal
cd /d "%~dp0"

set TEST_EXE=x64\Release\RegressionTests.exe
if exist "%TEST_EXE%" (
    "%TEST_EXE%"
    exit /b %ERRORLEVEL%
)

echo RegressionTests.exe was not found.
echo Build Test\RegressionTests.vcxproj first, then run this script again.
exit /b 1
