:: Place all build related files in a specific directory.
:: Whenever you'd like to clean the build and restart it from scratch, you can
:: delete this directory without worrying about deleting important files.
mkdir build-msvc15
cd build-msvc15

:: Call cmake to generate the all configured Visual Studio solution
:: If Houdini is not found, you can specify its location using e.g. -DHoudini_DIR=C:/Program Files/Side Effects Software/Houdini 17.5.258/toolkit/cmake
cmake .. -G "Visual Studio 15 2017 Win64" -DHoudini_DIR="E:/Program Files/Side Effects Software/Houdini 17.5.258/toolkit/cmake"

@echo off
:: Check that it run all right
if errorlevel 1 (
	echo [91mUnsuccessful[0m
) else (
	echo [92mSuccessful[0m
)
pause
