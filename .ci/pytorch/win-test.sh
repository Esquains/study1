#!/bin/bash
set -ex

SCRIPT_PARENT_DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
# shellcheck source=./common.sh
source "$SCRIPT_PARENT_DIR/common.sh"

export TMP_DIR="${PWD}/build/win_tmp"
TMP_DIR_WIN=$(cygpath -w "${TMP_DIR}")
export TMP_DIR_WIN
export PROJECT_DIR="${PWD}"
PROJECT_DIR_WIN=$(cygpath -w "${PROJECT_DIR}")
export PROJECT_DIR_WIN
export TEST_DIR="${PWD}/test"
TEST_DIR_WIN=$(cygpath -w "${TEST_DIR}")
export TEST_DIR_WIN
export PYTORCH_FINAL_PACKAGE_DIR="${PYTORCH_FINAL_PACKAGE_DIR:-/c/w/build-results}"
PYTORCH_FINAL_PACKAGE_DIR_WIN=$(cygpath -w "${PYTORCH_FINAL_PACKAGE_DIR}")
export PYTORCH_FINAL_PACKAGE_DIR_WIN

mkdir -p "$TMP_DIR"/build/torch


# This directory is used only to hold "pytorch_env_restore.bat", called via "setup_pytorch_env.py"
CI_SCRIPTS_DIR=$TMP_DIR/ci_scripts
mkdir -p "$CI_SCRIPTS_DIR"

if [ -n "$(ls "$CI_SCRIPTS_DIR"/*)" ]; then
    rm "$CI_SCRIPTS_DIR"/*
fi

export SCRIPT_HELPERS_DIR=$SCRIPT_PARENT_DIR/win-test-helpers

if [[ "$TEST_CONFIG" = "force_on_cpu" ]]; then
  # run the full test suite for force_on_cpu test
  export USE_CUDA=0
fi

if [[ "$BUILD_ENVIRONMENT" == *cuda* ]]; then
  # Used so that only cuda/rocm specific versions of tests are generated
  # mainly used so that we're not spending extra cycles testing cpu
  # devices on expensive gpu machines
  export PYTORCH_TESTING_DEVICE_ONLY_FOR="cuda"
fi

run_tests() {
    # Run nvidia-smi if available
    for path in '/c/Program Files/NVIDIA Corporation/NVSMI/nvidia-smi.exe' /c/Windows/System32/nvidia-smi.exe; do
        if [[ -x "$path" ]]; then
            "$path" || echo "true";
            break
        fi
    done

    if [[ "${TEST_CONFIG}" == *functorch* ]]; then
        python "$SCRIPT_HELPERS_DIR"/install_test_functorch.py
    elif [[ $NUM_TEST_SHARDS -eq 1 ]]; then
        python "$SCRIPT_HELPERS_DIR"/test_python_shard.py
        python "$SCRIPT_HELPERS_DIR"/test_custom_script_ops.py
        python "$SCRIPT_HELPERS_DIR"/test_custom_backend.py
        python "$SCRIPT_HELPERS_DIR"/test_libtorch.py
    else
        python "$SCRIPT_HELPERS_DIR"/test_python_shard.py
        if [[ "${SHARD_NUMBER}" == 1 && $NUM_TEST_SHARDS -gt 1 ]]; then
            python "$SCRIPT_HELPERS_DIR"/test_libtorch.py
            if [[ "${USE_CUDA}" == "1" ]]; then
              python "$SCRIPT_HELPERS_DIR"/test_python_jit_legacy.py
            fi
        elif [[ "${SHARD_NUMBER}" == 2 && $NUM_TEST_SHARDS -gt 1 ]]; then
            python "$SCRIPT_HELPERS_DIR"/test_custom_backend.py
            python "$SCRIPT_HELPERS_DIR"/test_custom_script_ops.py
        fi
    fi
}

run_tests
assert_git_not_dirty
echo "TEST PASSED"
