@setlocal

REM set CUVER_NODOT=%~1
set CUVER=%CUVER_NODOT:~0,-1%.%CUVER_NODOT:~-1,1%

REM set CONFIG=Debug

set CONFIG_LOWERCASE=%CONFIG:D=d%
set CONFIG_LOWERCASE=%CONFIG:R=r%
set CONFIG_LOWERCASE=%CONFIG:M=m%

mkdir magma_cuda%CUVER_NODOT%
cd magma_cuda%CUVER_NODOT%

REM set "PATH=C:\Program Files\CMake\bin\;C:\Program Files\7-Zip\;C:\curl-7.57.0-win64-mingw\bin\;%PATH%"

set "PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v%CUVER%\bin;C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v%CUVER%\libnvvp;%PATH%"
set CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v%CUVER%
set NVTOOLSEXT_PATH=C:\Program Files\NVIDIA Corporation\NvToolsExt

:: First install MKL, which provides BLAS and LAPACK API
:: Download and install from: 
:: curl https://s3.amazonaws.com/ossci-windows/w_mkl_2018.2.185.exe -k -O
:: .\w_mkl_2018.2.185.exe
:: Follow the installer steps and install MKL to default path

pushd %CD%
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x86_amd64
REM call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat" x86_amd64
::call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\bin\x86_amd64\vcvarsx86_amd64.bat"
popd

if not exist magma (
  git clone https://github.com/peterjc123/magma.git magma
) else (
  rmdir /S /Q magma\build
  rmdir /S /Q magma\install
)

IF "%CUVER_NODOT%" == "80" (
  set "CUDAHOSTCXX=C:/Program Files (x86)/Microsoft Visual Studio 14.0/VC/bin/x86_amd64/cl.exe"
) ELSE (
  set CUDAHOSTCXX=
)

set MKLROOT=

cd magma
mkdir build && cd build

:: curl -k https://s3.amazonaws.com/ossci-windows/ninja_1.8.2.exe --output ninja.exe

IF "%CUVER_NODOT%" == "80" (
  set GPU_TARGET=sm_35 sm_50 sm_52 sm_37 sm_53 sm_60 sm_61
) ELSE (
  set GPU_TARGET=All
)

cmake .. -DGPU_TARGET="%GPU_TARGET%" ^
             -DUSE_FORTRAN=0 ^
             -DCMAKE_CXX_FLAGS="/FS /Zf" ^
             -DCMAKE_BUILD_TYPE=%CONFIG% ^
             -DCMAKE_GENERATOR=Ninja ^
             -DCMAKE_INSTALL_PREFIX=..\install\
if errorlevel 1 exit /b 1
                                
set CC=cl.exe
set CXX=cl.exe
cmake --build . --target install --config %CONFIG% -- -j%NUMBER_OF_PROCESSORS%
if errorlevel 1 exit /b 1

cd ..\..\..

:: Create 
7z a magma_2.5.2_cuda%CUVER_NODOT%_%CONFIG_LOWERCASE%.7z %cd%\magma_cuda%CUVER_NODOT%\magma\install\*

rmdir /S /Q magma_cuda%CUVER_NODOT%\
@endlocal
