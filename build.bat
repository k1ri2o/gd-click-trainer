@echo off
rem Builds the mod and installs it into Geometry Dash (via the Geode CLI profile).
setlocal
if not defined GEODE_SDK set GEODE_SDK=C:\Users\TS\Documents\Geode\sdk
if not defined GEODE_BINDINGS_REPO_PATH set GEODE_BINDINGS_REPO_PATH=C:\Users\TS\Documents\Geode\bindings
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
if not exist build\build.ninja (
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl || exit /b 1
)
cmake --build build --target ClickTrainer_PACKAGE || exit /b 1
