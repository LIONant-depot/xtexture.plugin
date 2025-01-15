@echo OFF
setlocal enabledelayedexpansion
cd %cd%
set XPATH="%cd%"

rem --------------------------------------------------------------------------------------------------------
rem Set the color of the terminal to blue with yellow text
rem --------------------------------------------------------------------------------------------------------
COLOR 8E
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore Cyan Welcome Ready for building texture compiler
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
echo.

powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White FINDING VISUAL STUDIO / MSBuild
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
cd /d %XPATH%

for /f "usebackq tokens=*" %%i in (`.\..\dependencies\xresource_pipeline_v2\dependencies\xcore\bin\vswhere -version 16.0 -sort -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    SET MSBUILD=%%i
)

for /f "usebackq tokens=1* delims=: " %%i in (`.\..\dependencies\xresource_pipeline_v2\dependencies\xcore\bin\vswhere -version 16.0 -sort -requires Microsoft.VisualStudio.Workload.NativeDesktop`) do (
    if /i "%%i"=="installationPath" set VSPATH=%%j
)

IF EXIST "%MSBUILD%" ( 
    echo VISUAL STUDIO VERSION: "%MSBUILD%"
    echo INSTALLATION PATH: "%VSPATH%"
    GOTO :READY_COMPILATION
    )
powershell write-host -fore Red Failed to find VS2019 MSBuild!!! 
GOTO :ERROR

:READY_COMPILATION
powershell write-host -fore Cyan Debug: Compiling...
"%MSBUILD%" "%CD%\xtexture_compiler.vs2022\xtexture_compiler.vcxproj" /p:configuration=Debug /p:Platform="x64" /verbosity:minimal 
if %ERRORLEVEL% GEQ 1 goto :ERROR

powershell write-host -fore Cyan Release: Compiling...
"%MSBUILD%" "%CD%\xtexture_compiler.vs2022\xtexture_compiler.vcxproj" /p:configuration=Release /p:Platform="x64" /verbosity:minimal 
if %ERRORLEVEL% GEQ 1 goto :ERROR

:DONE
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White TEXTURE COMPILER COMPILATION - SUCCESSFULLY DONE!!
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
goto :PAUSE

:ERROR
powershell write-host -fore Red ------------------------------------------------------------------------------------------------------
powershell write-host -fore Red TEXTURE COMPILER COMPILATION - FAILED!!
powershell write-host -fore Red ------------------------------------------------------------------------------------------------------

:PAUSE
rem if no one give us any parameters then we will pause it at the end, else we are assuming that another batch file called us
if %1.==. pause