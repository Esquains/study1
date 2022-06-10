call %SCRIPT_HELPERS_DIR%\setup_pytorch_env.bat

git submodule update --init --recursive --jobs 0 third_party/pybind11
cd test\custom_operator

:: Build the custom operator library.
mkdir build
pushd build

echo "Executing CMake for custom_operator test..."

:: Note: Caffe2 does not support MSVC + CUDA + Debug mode (has to be Release mode)
cmake -DCMAKE_PREFIX_PATH=%TMP_DIR_WIN%\build\torch -DCMAKE_BUILD_TYPE=Release -GNinja ..
if ERRORLEVEL 1 exit /b 1

echo "Executing Ninja for custom_operator test..."

ninja -v
if ERRORLEVEL 1 exit /b 1

echo "Ninja succeeded for custom_operator test."

popd

:: Run tests Python-side and export a script module.
python test_custom_ops.py -v
if ERRORLEVEL 1 exit /b 1

:: TODO: fix and re-enable this test
:: See https://github.com/pytorch/pytorch/issues/25155
:: python test_custom_classes.py -v
:: if ERRORLEVEL 1 exit /b 1

python model.py --export-script-module="build/model.pt"
if ERRORLEVEL 1 exit /b 1

:: Run tests C++-side and load the exported script module.
cd build
set PATH=C:\Program Files\NVIDIA Corporation\NvToolsExt\bin\x64;%TMP_DIR_WIN%\build\torch\lib;%PATH%
test_custom_ops.exe model.pt
if ERRORLEVEL 1 exit /b 1
