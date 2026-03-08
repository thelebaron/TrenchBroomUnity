@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "BUILD_DIR=%BUILD_DIR%"
if not defined BUILD_DIR set "BUILD_DIR=build-windows"

set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake"

call :detectQt
if errorlevel 1 goto :error

call :detectVisualStudio
if errorlevel 1 goto :error

echo Running tests for TrenchBroom on Windows
echo   Build dir: %BUILD_DIR%
echo   Qt dir: %QT_ROOT_DIR%
echo   Visual Studio: %VS_ROOT%
echo   Generator: %CMAKE_GENERATOR%
echo   Toolset: %CMAKE_TOOLSET%
echo.

"%CMAKE_EXE%" --version
if errorlevel 1 goto :error

"%CMAKE_EXE%" -S . -B "%BUILD_DIR%" --fresh -G "%CMAKE_GENERATOR%" -T "%CMAKE_TOOLSET%" -A x64 -DCMAKE_PREFIX_PATH="%QT_ROOT_DIR%" -DTB_ENABLE_PCH=0 -DTB_ENABLE_CCACHE=0
if errorlevel 1 goto :error

set "PATH=%QT_ROOT_DIR%\bin;%PATH%"
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release --target common-test vm-test kdl-test upd-test
if errorlevel 1 goto :error

call :runTest "%BUILD_DIR%\common\test\common-test.exe"
call :runTest "%BUILD_DIR%\lib\vm\test\vm-test.exe"
call :runTest "%BUILD_DIR%\lib\kdl\test\kdl-test.exe"
call :runTest "%BUILD_DIR%\lib\upd\test\upd-test.exe"

echo.
echo Tests succeeded.
goto :end

:runTest
set "TEST_PATH=%~1"
if not exist "%TEST_PATH%" (
  echo Test executable not found: %TEST_PATH%
  goto :error
)

"%TEST_PATH%" --reporter compact --success
if errorlevel 1 goto :error
exit /b 0

:detectQt
if defined QT_ROOT_DIR (
  if exist "%QT_ROOT_DIR%\bin\qmake.exe" exit /b 0
  echo QT_ROOT_DIR is set but invalid: %QT_ROOT_DIR%
  exit /b 1
)

for /f "delims=" %%I in ('dir /b /ad /o-n "C:\Qt" 2^>nul') do (
  if exist "C:\Qt\%%I\msvc2022_64\bin\qmake.exe" (
    set "QT_ROOT_DIR=C:\Qt\%%I\msvc2022_64"
    exit /b 0
  )
)

echo Could not find a Qt MSVC kit under C:\Qt.
echo Set QT_ROOT_DIR to something like C:\Qt\6.10.2\msvc2022_64 and try again.
exit /b 1

:detectVisualStudio
if defined VS_ROOT (
  if exist "%VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" goto :setGeneratorFromRoot
  echo VS_ROOT is set but invalid: %VS_ROOT%
  exit /b 1
)

for %%V in (18 17) do (
  for %%E in (Community Professional Enterprise BuildTools) do (
    if not defined VS_ROOT if exist "C:\Program Files\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat" (
      set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\%%V\%%E"
      set "VS_VERSION=%%V"
    )
  )
)

if not defined VS_ROOT (
  echo Could not find a supported Visual Studio installation.
  echo Install Visual Studio 2026 or 2022 with Desktop development with C++.
  exit /b 1
)

:setGeneratorFromRoot
if not defined VS_VERSION (
  echo %VS_ROOT% | findstr /C:"\18\" >nul && set "VS_VERSION=18"
  if not defined VS_VERSION echo %VS_ROOT% | findstr /C:"\17\" >nul && set "VS_VERSION=17"
)

if "%VS_VERSION%"=="18" (
  set "CMAKE_GENERATOR=Visual Studio 18 2026"
  set "CMAKE_TOOLSET=v143"
  exit /b 0
)

if "%VS_VERSION%"=="17" (
  set "CMAKE_GENERATOR=Visual Studio 17 2022"
  set "CMAKE_TOOLSET=v143"
  exit /b 0
)

echo Unsupported Visual Studio installation: %VS_ROOT%
exit /b 1

:error
echo.
echo Running tests failed.
set "EXIT_CODE=1"
goto :done

:end
set "EXIT_CODE=0"

:done
if not defined TB_NO_PAUSE pause
exit /b %EXIT_CODE%
