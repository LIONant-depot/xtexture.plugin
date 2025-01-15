@echo OFF
setlocal enabledelayedexpansion
set XTEXTURE_COMPILER_PATH=%cd%

rem --------------------------------------------------------------------------------------------------------
rem Set the color of the terminal to blue with yellow text
rem --------------------------------------------------------------------------------------------------------
COLOR 8E
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore Cyan Welcome I am your XTEXTURE PLUGIN dependency updater bot, let me get to work...
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
echo.

:DOWNLOAD_DEPENDENCIES
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White XTEXTURE PLUGIN - DOWNLOADING DEPENDENCIES
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
echo.

rem ------------------------------------------------------------
rem XRESOURCE_PIPELINE
rem ------------------------------------------------------------
:XRESOURCE_PIPELINE
rmdir "../dependencies/xresource_pipeline_v2" /S /Q
git clone https://github.com/LIONant-depot/xresource_pipeline_v2.git "../dependencies/xresource_pipeline_v2"
if %ERRORLEVEL% GEQ 1 goto :ERROR
cd ../dependencies/xresource_pipeline_v2/build
if %ERRORLEVEL% GEQ 1 goto :ERROR
call updateDependencies.bat "return"
if %ERRORLEVEL% GEQ 1 goto :ERROR
cd /d %XTEXTURE_COMPILER_PATH%
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem ------------------------------------------------------------
rem XBMP_TOOLS
rem ------------------------------------------------------------
:XBMP_TOOLS
rmdir "../dependencies/xbmp_tools" /S /Q
git clone --recurse-submodules -j8 https://github.com/LIONant-depot/xbmp_tools.git "../dependencies/xbmp_tools"
if %ERRORLEVEL% GEQ 1 goto :ERROR
cd ../dependencies/xbmp_tools/build
if %ERRORLEVEL% GEQ 1 goto :ERROR
call updateDependencies.bat "return"
if %ERRORLEVEL% GEQ 1 goto :ERROR
cd /d %XTEXTURE_COMPILER_PATH%
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem ------------------------------------------------------------
rem CRUNCH
rem ------------------------------------------------------------
:CRUNCH
rmdir "../dependencies/crunch" /S /Q
git clone --recurse-submodules -j8 https://github.com/BinomialLLC/crunch.git "../dependencies/crunch"
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem ------------------------------------------------------------
rem Compressonator
rem ------------------------------------------------------------
:CRUNCH
rmdir "../dependencies/compressonator" /S /Q
git clone https://github.com/GPUOpen-Tools/compressonator.git "../dependencies/compressonator"
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem ------------------------------------------------------------
rem BASIS UNIVERSAL
rem ------------------------------------------------------------
:BASIS_UNIVERSAL
rmdir "../dependencies/basis_universal" /S /Q
git clone --recurse-submodules -j8 https://github.com/BinomialLLC/basis_universal.git "../dependencies/basis_universal"
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem ------------------------------------------------------------
rem DONE
rem ------------------------------------------------------------
:DONE
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White XTEXTURE PLUGIN - DONE!!
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
goto :PAUSE

:ERROR
powershell write-host -fore Red ------------------------------------------------------------------------------------------------------
powershell write-host -fore Red XTEXTURE PLUGIN - ERROR!!
powershell write-host -fore Red ------------------------------------------------------------------------------------------------------

:PAUSE
rem if no one give us any parameters then we will pause it at the end, else we are assuming that another batch file called us
if %1.==. pause