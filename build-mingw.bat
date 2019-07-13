:: Place all build related files in a specific directory.
:: Whenever you'd like to clean the build and restart it from scratch, you can
:: delete this directory without worrying about deleting important files.
mkdir build-mingw
cd build-mingw

:: Call cmake to generate the MinGW solution
:: If Houdini is not found, you can specify its location using e.g. -DHoudini_DIR=C:/Program Files/Side Effects Software/Houdini 17.5.258/toolkit/cmake
cmake .. -G "MinGW Makefiles" -DHoudini_DIR="E:/Program Files/Side Effects Software/Houdini 17.5.258/toolkit/cmake"

@echo off
:: Check that it run all right
if errorlevel 1 (
	echo [91mUnsuccessful[0m
) else (
	echo [92mSuccessful[0m
	echo "You can now run 'mingw32-make' in directory 'build-mingw'"
)
pause
