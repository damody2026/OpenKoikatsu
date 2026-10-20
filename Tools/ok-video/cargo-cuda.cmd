@echo off
rem Build ok-video with CUDA: MSVC 2022 host compiler (nvcc 12.9 does not accept VS 18) + CUDA 12.9, sm_120 (RTX 50).
rem Usage: cargo-cuda.cmd build --release   (any cargo arguments)
setlocal
if not defined CUDA_PATH set "CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9"
if not defined CUDA_COMPUTE_CAP set CUDA_COMPUTE_CAP=120
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=%CUDA_PATH%\bin;%PATH%"
cd /d "%~dp0"
cargo %*
if errorlevel 1 exit /b %errorlevel%
rem Redistributable CUDA runtime next to the executable (no CUDA toolkit or PATH needed to run).
for %%P in (release debug) do if exist "target\%%P\ok-video.exe" for %%D in (cudart64_12.dll cublas64_12.dll cublasLt64_12.dll curand64_10.dll) do if not exist "target\%%P\%%D" copy /y "%CUDA_PATH%\bin\%%D" "target\%%P\" >nul
exit /b 0
