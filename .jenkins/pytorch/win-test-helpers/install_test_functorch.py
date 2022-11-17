import os
import subprocess
import sys
import contextlib


@contextlib.contextmanager
def pushd(new_dir):
    previous_dir = os.getcwd()
    os.chdir(new_dir)
    try:
        yield
    finally:
        os.chdir(previous_dir)


try:

    subprocess.call(str(os.environ['SCRIPT_HELPERS_DIR']) + '\setup_pytorch_env.py', shell=True)

except Exception as e:

    subprocess.run(['echo', 'setup pytorch env failed'])
    subprocess.run(['echo', e])
    sys.exit()


subprocess.run(['echo', 'Installing test dependencies'])

try:
    subprocess.run(['pip', 'install', 'networkx'])

except:
    sys.exit()


subprocess.run(['echo', 'Test functorch'])

try:

    with pushd('test'):
        subprocess.run(['python', 'run_test.py', '--functorch', '--shard',\
         str(os.environ['SHARD_NUMBER']), str(os.environ['NUM_TEST_SHARDS']), '--verbose'])

except:
    sys.exit(1)


sys.exit(0)
