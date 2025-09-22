@echo off
setlocal enabledelayedexpansion

set "with_changes="
set "no_changes="
set "others="

for /d %%i in (*) do (
    set "is_repo=0"
    set "has_changes=0"
    if exist "%%i\.git\" (
        pushd "%%i"
        git rev-parse --is-inside-work-tree >nul 2>&1
        if not errorlevel 1 (
            set "is_repo=1"
            for /f %%a in ('git status --porcelain 2^>nul ^| find /c /v ""') do (
                if %%a gtr 0 set "has_changes=1"
            )
        )
        popd
    )
    if !is_repo! equ 1 (
        if !has_changes! equ 1 (
            set "with_changes=!with_changes! %%i"
        ) else (
            set "no_changes=!no_changes! %%i"
        )
    ) else (
        set "others=!others! %%i"
    )
)

echo ### Depos with changes ###
for %%k in (!with_changes!) do echo %%k 
echo.
echo ### Depos with no changes ###
for %%k in (!no_changes!) do echo %%k 
echo.
echo ### Other Folders ###
for %%k in (!others!) do echo %%k

pause

