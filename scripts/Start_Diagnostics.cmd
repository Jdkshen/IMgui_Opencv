@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "MISSING="
for %%F in (
    Windows_imgui.exe
    DirectML.dll
    ncnn.dll
    onnxruntime.dll
    onnxruntime_providers_shared.dll
    opencv_world500.dll
    concrt140.dll
    msvcp140.dll
    msvcp140_1.dll
    msvcp140_atomic_wait.dll
    vcruntime140.dll
    vcruntime140_1.dll
    vcomp140.dll
) do (
    if not exist "%%F" (
        echo [MISSING] %%F
        set "MISSING=1"
    )
)

if defined MISSING (
    echo.
    echo Runtime files are incomplete. Extract the whole ZIP again; do not run the EXE inside the ZIP.
    echo If antivirus quarantined a DLL, restore the file or rebuild the package from a trusted source.
    pause
    exit /b 2
)

echo Runtime dependency check passed.
if not exist "MvCameraControl.dll" (
    echo [INFO] Hikrobot MVS runtime is not bundled. Install the x64 MVS Runtime before using an MVS camera.
)
echo Startup log: %LOCALAPPDATA%\IMgui_Opencv\startup.log
echo Crash dumps: %LOCALAPPDATA%\IMgui_Opencv\*.dmp
echo.

start "" /wait "%~dp0Windows_imgui.exe"
set "APP_EXIT=%ERRORLEVEL%"
if not "%APP_EXIT%"=="0" (
    echo.
    echo Windows_imgui.exe exited with code %APP_EXIT%.
    echo Open the startup log shown above and send it with any .dmp file for diagnosis.
    pause
)
exit /b %APP_EXIT%
