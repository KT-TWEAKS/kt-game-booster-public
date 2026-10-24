@echo off

setlocal enabledelayedexpansion

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="
set "MSBUILD="

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
    if not defined VSROOT for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%i"
    if defined VSROOT (
        set "VCVARS=!VSROOT!\VC\Auxiliary\Build\vcvars64.bat"
        if exist "!VSROOT!\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD=!VSROOT!\MSBuild\Current\Bin\MSBuild.exe"
    )
)

if not defined VCVARS set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined MSBUILD set "MSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"

if not exist "%VCVARS%" (
    echo [erro] vcvars64.bat nao encontrado.
    exit /b 1
)
if not exist "%MSBUILD%" (
    echo [erro] MSBuild.exe nao encontrado.
    exit /b 1
)

call "%VCVARS%" >nul
cd /d "%~dp0"

echo.
echo [1/3] Compilando ktengine.dll (engine C++ x64 in-proc)...
cl /MT /O2 /W3 /EHsc /LD /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 ^
    src\util.cpp src\config.cpp src\detect.cpp src\procopt.cpp ^
    src\threadopt.cpp src\powersys.cpp src\cpulim.cpp ^
    src\etw.cpp src\driver.cpp src\platform.cpp src\snapshot.cpp src\booster.cpp src\winsvcmon.cpp ^
    src\metrics.cpp src\fpsmon.cpp src\gpumon.cpp ^
    src\ktcore.cpp ^
    /Fe:ktengine.dll ^
    /link /SUBSYSTEM:WINDOWS /LARGEADDRESSAWARE /DEF:src\ktengine.def ^
    advapi32.lib user32.lib gdi32.lib shell32.lib ole32.lib ^
    psapi.lib winmm.lib powrprof.lib dwmapi.lib ntdll.lib pdh.lib
if errorlevel 1 (
    echo [erro] engine build falhou.
    del /f /q *.obj *.exp 2>nul
    exit /b 1
)
del /f /q *.obj *.exp 2>nul

echo.
echo [2/3] Compilando KTGameBooster.exe (UI WPF x64)...
"%MSBUILD%" ui\KTGameBoosterUI\KTGameBoosterUI.csproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m /v:m
if errorlevel 1 (
    echo [erro] UI build falhou.
    exit /b 1
)

echo.
echo [3/3] Empacotando release\...
if not exist release mkdir release
if not exist release\Assets mkdir release\Assets
copy /y ktengine.dll "release\ktengine.dll" >nul
copy /y ui\KTGameBoosterUI\bin\Release\KTGameBooster.exe "release\KTGameBooster.exe" >nul
copy /y ui\KTGameBoosterUI\bin\Release\KTGameBooster.exe.config "release\" >nul 2>nul
copy /y config\gamemode.toml "release\gamemode.toml" >nul
if exist profiles xcopy /y /e /q profiles "release\profiles\" >nul
copy /y ui\KTGameBoosterUI\Assets\ktgam.ico "release\Assets\ktgam.ico" >nul
copy /y ui\KTGameBoosterUI\Assets\ktgam.ico "release\ktgam.ico" >nul
copy /y README.md "release\README.md" >nul

echo.
echo ============================================================================
echo BUILD SUCCESS - x64 unified single-EXE
echo ============================================================================
echo   release\KTGameBooster.exe   ~1.0 MB  (motor, config e perfis embutidos)
echo   release\ktengine.dll        ~570 KB  (staging local; no EXE e extraido)
echo   release\gamemode.toml              (staging local; embutido no EXE)
echo   release\profiles\*.toml           (staging local; embutidos no EXE)
echo   dist\KTGameBooster-v1.0-win-x64.exe  (via package-release.ps1)
echo ============================================================================

endlocal
exit /b 0
