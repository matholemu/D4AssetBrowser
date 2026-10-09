@echo off
setlocal enabledelayedexpansion
title D4AssetBrowser - Push Wiki
cd /d "%~dp0"

:: ============================================================================
::  Push Wiki.bat - commit and push the GitHub wiki. Double-click it.
::
::  The wiki is a SEPARATE repository from the code. GitHub serves it from
::  D4AssetBrowser.wiki.git, which lives beside this project folder:
::
::      G:\G Claude Projects\AssetBrowser\Diablo4AssetBrowser Native   <- here
::      G:\G Claude Projects\AssetBrowser\D4AssetBrowser.wiki          <- the wiki
::
::  This file lives HERE and reaches across, deliberately: a .bat sitting in
::  the wiki folder would be committed and pushed to the wiki itself.
::
::  You can skip the prompt by passing the message:
::      "Push Wiki.bat"  correct the ShaderMap group id
::
::  THREE THINGS THIS WILL NOT DO, each for a reason that has already cost a
::  session:
::
::    * It never runs "git init". If .git is missing, the folder is not the
::      wiki clone and the fix is to clone it, not to make a new empty history
::      in place and push that over the real one.
::    * It never force-pushes. The wiki can be edited in the browser, so the
::      remote may legitimately be ahead; this rebases onto it and stops if
::      that needs a human.
::    * It never touches the code repository. Wrong repo, wrong history.
::
::  Wiki branch is "master" (GitHub's wikis still are, whatever the code repo
::  uses), read from the clone rather than assumed.
:: ============================================================================

set "WIKIDIR=%~dp0..\D4AssetBrowser.wiki"
set "WEBURL=https://github.com/trappuss/D4AssetBrowser/wiki"

:: THE PAGER. git sends anything longer than a screen to "less", which takes the
:: console over and waits for keys nobody expects to press - the script looks
:: frozen. Per command rather than globally, so your own git config is left alone.
set "GIT=git --no-pager"

echo ============================================================
echo   D4AssetBrowser  -  Wiki
echo   %WEBURL%
echo ============================================================
echo.

:: ---------------------------------------------------------------- preflight
where git >nul 2>&1 || (
    echo  ^>^> git is not on PATH. Install Git for Windows, then re-run this.
    goto :die
)

if not exist "%WIKIDIR%\" (
    echo  ^>^> The wiki folder is not where this expects it:
    echo     %WIKIDIR%
    echo.
    echo     Clone it beside this project folder:
    echo       git clone https://github.com/trappuss/D4AssetBrowser.wiki.git
    goto :die
)

pushd "%WIKIDIR%" || goto :die

if not exist ".git\" (
    echo  ^>^> "%WIKIDIR%" has no .git folder, so it is not the wiki clone.
    echo     NOT running "git init" - that would create a fresh empty history
    echo     and the first push would overwrite the real wiki. Clone it instead:
    echo       git clone https://github.com/trappuss/D4AssetBrowser.wiki.git
    popd
    goto :die
)

:: A stale lock file is the signature of git having been interrupted - most often
:: by being run across a file bridge or network share, which this project does not
:: do for exactly this reason. Report it rather than deleting it blindly: if a real
:: git process is running, removing its lock corrupts the index.
if exist ".git\index.lock" (
    echo  ^>^> .git\index.lock exists in the wiki clone.
    echo     If no git command is running, delete it and re-run:
    echo       del "%WIKIDIR%\.git\index.lock"
    popd
    goto :die
)

for /f "delims=" %%B in ('%GIT% rev-parse --abbrev-ref HEAD 2^>nul') do set "BRANCH=%%B"
if not defined BRANCH set "BRANCH=master"

for /f "delims=" %%U in ('%GIT% config --get remote.origin.url 2^>nul') do set "RURL=%%U"
echo   folder   %WIKIDIR%
echo   remote   !RURL!
echo   branch   !BRANCH!
echo.

:: Guard against being pointed at the code repo by a bad path edit. The wiki remote
:: always ends in ".wiki.git"; the code remote does not.
echo !RURL! | find /i ".wiki.git" >nul
if errorlevel 1 (
    echo  ^>^> That remote is not a wiki remote ^(expected one ending .wiki.git^).
    echo     Refusing to push - this would write to the wrong repository.
    popd
    goto :die
)

:: ---------------------------------------------------------------- what changed
echo ------------------------------------------------------------
%GIT% status --short
echo ------------------------------------------------------------
echo.

%GIT% diff --quiet
set "UNSTAGED=!errorlevel!"
%GIT% diff --cached --quiet
set "STAGED=!errorlevel!"
for /f "delims=" %%N in ('%GIT% ls-files --others --exclude-standard 2^>nul') do set "UNTRACKED=1"

if "!UNSTAGED!"=="0" if "!STAGED!"=="0" if not defined UNTRACKED (
    echo   Nothing to commit - the wiki folder matches its last commit.
    echo   Checking whether anything is waiting to be pushed...
    echo.
    %GIT% log --oneline origin/!BRANCH!..!BRANCH! 2>nul
    %GIT% rev-list --count origin/!BRANCH!..!BRANCH! >"%TEMP%\d4wikiahead.txt" 2>nul
    set /p AHEAD=<"%TEMP%\d4wikiahead.txt"
    del "%TEMP%\d4wikiahead.txt" >nul 2>&1
    if "!AHEAD!"=="0" (
        echo   Nothing to push either. Up to date.
        popd
        goto :done
    )
    echo   !AHEAD! commit^(s^) already committed but not pushed. Pushing those.
    goto :push
)

:: ---------------------------------------------------------------- message
set "MSG=%*"
if not defined MSG (
    echo   Commit message ^(blank cancels^):
    set /p "MSG=  > "
)
if not defined MSG (
    echo.
    echo   Cancelled - nothing was committed.
    popd
    goto :done
)

echo.
%GIT% add -A
if errorlevel 1 (
    echo  ^>^> git add failed.
    popd
    goto :die
)

%GIT% commit -m "!MSG!"
if errorlevel 1 (
    echo  ^>^> git commit failed ^(see above^).
    popd
    goto :die
)

:: ---------------------------------------------------------------- push
:push
echo.
echo   Fetching and rebasing onto origin/!BRANCH! first - the wiki can also be
echo   edited in the browser, so the remote may legitimately be ahead.
%GIT% fetch origin
if errorlevel 1 (
    echo  ^>^> git fetch failed. Check the network and that you are signed in.
    popd
    goto :die
)

%GIT% pull --rebase origin !BRANCH!
if errorlevel 1 (
    echo.
    echo  ^>^> The rebase stopped and needs a human - usually the same page was
    echo     edited here and in the browser. Resolve it in "%WIKIDIR%" with:
    echo        git status
    echo        ^<fix the file^>
    echo        git add ^<file^>  ^&^&  git rebase --continue
    echo     Nothing has been pushed. Not forcing anything.
    popd
    goto :die
)

%GIT% push origin !BRANCH!
if errorlevel 1 (
    echo.
    echo  ^>^> git push failed ^(see above^). NOT retrying with --force.
    popd
    goto :die
)

echo.
echo   Pushed. Give GitHub a few seconds, then:
echo     %WEBURL%
popd

:done
echo.
pause
exit /b 0

:die
echo.
echo   Stopped. Nothing was pushed.
echo.
pause
exit /b 1
