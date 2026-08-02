@echo off
REM Build BOTH KenshiCoop configurations and stage them as ready-to-copy mod
REM folders under builds\:
REM   builds\release\KenshiCoop\   - Release DLL (no scenario runner, quietest
REM                                  logs; what both players run day to day)
REM   builds\debug\KenshiCoop\     - Harness DLL (scenario runner + heavier
REM                                  diagnostics; swap in when a session needs
REM                                  deep logs, then swap back)
REM Each folder is a complete mod: DLL + RE_Kenshi.json + KenshiCoop.mod +
REM coop_config.json template. Install = copy the KenshiCoop folder over
REM <Kenshi>\mods\KenshiCoop\.
setlocal

set "REPO=%~dp0.."
pushd "%REPO%" >nul
set "REPO=%CD%"
popd >nul

call "%~dp0build_plugin.cmd" Release
if errorlevel 1 (
    echo ERROR: Release build failed.
    exit /b 1
)
call "%~dp0build_plugin.cmd" Harness
if errorlevel 1 (
    echo ERROR: Harness build failed.
    exit /b 1
)

set "SRCJSON=%REPO%\dist\mods\KenshiCoop\RE_Kenshi.json"
set "SRCMOD=%REPO%\dist\mods\KenshiCoop\KenshiCoop.mod"
set "SRCCFG=%REPO%\dist\mod-kit\KenshiCoop\coop_config.json"

for %%V in (release debug) do (
    if not exist "%REPO%\builds\%%V\KenshiCoop" mkdir "%REPO%\builds\%%V\KenshiCoop"
)
copy /Y "%REPO%\src\plugin\x64\Release\KenshiCoop.dll" "%REPO%\builds\release\KenshiCoop\KenshiCoop.dll" >nul || exit /b 1
copy /Y "%REPO%\src\plugin\x64\Harness\KenshiCoop.dll" "%REPO%\builds\debug\KenshiCoop\KenshiCoop.dll" >nul || exit /b 1
for %%V in (release debug) do (
    copy /Y "%SRCJSON%" "%REPO%\builds\%%V\KenshiCoop\RE_Kenshi.json" >nul
    copy /Y "%SRCMOD%"  "%REPO%\builds\%%V\KenshiCoop\KenshiCoop.mod" >nul
    if exist "%SRCCFG%" if not exist "%REPO%\builds\%%V\KenshiCoop\coop_config.json" (
        copy /Y "%SRCCFG%" "%REPO%\builds\%%V\KenshiCoop\coop_config.json" >nul
    )
)

echo.
echo Staged:
echo   %REPO%\builds\release\KenshiCoop  (play build)
echo   %REPO%\builds\debug\KenshiCoop    (diagnostics build)
endlocal
