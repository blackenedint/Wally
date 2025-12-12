@ECHO OFF
SETLOCAL enabledelayedexpansion
REM unset any global ones.
SET VCPKG_ROOT=

SET APPROOT=%~dp0
SET APPDIR=release

SET "INSTALLROOT=%APPROOT%source\thirdparty"

SET "VCPKG_EXE=%APPROOT%vcpkg\vcpkg.exe"


SET "DEPENDENCIES=%APPROOT%dependencies"

REM get vcpkg distribution
if not exist vcpkg git clone https://github.com/Microsoft/vcpkg.git || exit /b 1

@REM ensure vcpkg is always up to date!!
@rem okay this is causing a stupid error about not being able to merge, even though it used to work.
if exist vcpkg (
    pushd vcpkg 
    git pull || exit /b 1
    popd 
    )
	
REM build vcpkg
if not exist vcpkg\vcpkg.exe call vcpkg\bootstrap-vcpkg.bat -disableMetrics || exit /b 2

SET "VCPKG_OVERLAY_TRIPLETS=%DEPENDENCIES%\triplets"
SET TRIPLET_64BIT=x64-windows-static-md
@ECHO.
@ECHO.
@ECHO ====================================================================================
@ECHO BUILDING LIBRARIES (%TRIPLET_64BIT%)
@ECHO ====================================================================================
TITLE (%TRIPLET_64BIT%) BUILD
PUSHD %DEPENDENCIES%
%VCPKG_EXE% install --triplet %TRIPLET_64BIT% --x-install-root=%INSTALLROOT%  --x-manifest-root="%DEPENDENCIES%" || exit /b 3
SET VCPKG_OVERLAY_TRIPLETS=
POPD
