import sys
sys.argv.append("--jit_config=legacy")
from test_jit_fuser import *

if __name__ == '__main__':
    run_tests()
