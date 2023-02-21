import os
from os.path import exists
import subprocess
import sys
import contextlib


shard_number = os.environ['SHARD_NUMBER']


try:

    subprocess.run('python ' + os.environ['SCRIPT_HELPERS_DIR'] + '\\setup_pytorch_env.py', shell=True, check=True)

except Exception as e:

    subprocess.run('echo setup pytorch env failed', shell=True)
    subprocess.run('echo ' + str(e), shell=True)
    sys.exit()



gflags_exe = "C:\\Program Files (x86)\\Windows Kits\\10\\Debuggers\\x64\\gflags.exe"
os.environ['GFLAGS_EXE'] = gflags_exe


if shard_number == "1" and exists(gflags_exe):

    subprocess.run('echo Some smoke tests', shell=True, cwd='test')

    try:
        subprocess.run(gflags_exe + ' /i python.exe +sls', shell=True, check=True, cwd='test')

        subprocess.run('conda run -n test_env' + ' python ' + os.environ['SCRIPT_HELPERS_DIR'] + '\\run_python_nn_smoketests.py', shell=True, check=True, cwd='test')

        subprocess.run(gflags_exe + ' /i python.exe -sls', shell=True, check=True, cwd='test')

    except Exception as e:

        subprocess.run('echo shard dmoke test failed', shell=True)
        subprocess.run('echo ' + str(e), shell=True)
        sys.exit()


subprocess.run('echo Copying over test times file', shell=True)
subprocess.run('copy /Y ' + str(os.environ['PYTORCH_FINAL_PACKAGE_DIR_WIN']) +
    '\\.pytorch-test-times.json ' + str(os.environ['PROJECT_DIR_WIN']), shell=True, cwd='test')


subprocess.run('echo Run nn tests', shell=True)

try:
    subprocess.run('conda install -n test_env python run_test.py --exclude-jit-executor ' +
        '--exclude-distributed-tests --shard ' + shard_number + ' ' + str(os.environ['NUM_TEST_SHARDS']) +
            ' --verbose', shell=True, check=True, cwd='test')

except Exception as e:

    subprocess.run('echo shard nn tests failed', shell=True)
    subprocess.run('echo ' + str(e), shell=True)
    sys.exit()
