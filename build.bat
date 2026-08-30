@echo off
rem ============================================================
rem  Desktop Sticker one-click build (Release x64)
rem  Usage: double-click, or run: build.bat [test]
rem  Optional arg "test" runs unit tests after build.
rem ============================================================
setlocal

set "MSBUILD=D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
set "SLN=%~dp0Desktop Sticker\Desktop Sticker.sln"
set "OUT=%~dp0Desktop Sticker\bin\x64\Release"
set "TESTS=%OUT%\Tests\DesktopSticker.Tests.exe"

if not exist "%MSBUILD%" (
    echo [ERROR] MSBuild not found: "%MSBUILD%"
    pause
    exit /b 1
)
if not exist "%SLN%" (
    echo [ERROR] Solution not found: "%SLN%"
    pause
    exit /b 1
)

echo [0/2] Stopping running Desktop Sticker (if any) ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\close_app.ps1"

echo [1/2] Building Release x64 ...
"%MSBUILD%" "%SLN%" -t:Restore -p:Configuration=Release -p:Platform=x64 -m:1 -v:m -nologo
"%MSBUILD%" "%SLN%" -p:Configuration=Release -p:Platform=x64 -m:1 -v:m -nologo
if errorlevel 1 (
    echo.
    echo [FAILED] Build errors, see log above.
    pause
    exit /b 1
)
echo [DONE] Output: "%OUT%\Desktop Sticker\"

if /i "%~1"=="test" (
    echo.
    echo [TEST] Running unit tests ...
    copy /y "%OUT%\DesktopSticker.Features.dll" "%OUT%\Tests\" >nul
    "%TESTS%"
)

endlocal
