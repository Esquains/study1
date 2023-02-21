import os
import subprocess
import sys
import pathlib


cuda_version = os.environ['CUDA_VERSION']
tmp_win_dir = os.environ['TMP_DIR_WIN']
build_type = os.environ['BUILD_TYPE']


if cuda_version == "cpu":

    subprocess.run('echo skip magma installation for cpu builds', shell=True)
    sys.exit(0)


# remove dot in cuda_version, fox example 11.1 to 111


if os.environ['USE_CUDA'] != "1":

    sys.exit(0)


if '.' not in cuda_version:

    subprocess.run('echo CUDA version ' + cuda_version +
        'format isn\'t correct, which doesn\'t contain \'.\'', shell=True)

    sys.exit(1)

os.system(str(pathlib.Path(__file__).parent.resolve()) + '\\magma_vars.bat')
cuda_suffix = 'cuda' + cuda_version.replace('.', '')

if 'REBUILD' not in os.environ:

    try:

        if 'BUILD_ENVIRONMENT' not in os.environ:

            subprocess.run('curl --retry 3 -k https://s3.amazonaws.com/ossci-windows/magma_2.5.4_' +
                cuda_suffix + '_' + build_type + '.7z --output ' + tmp_win_dir + '\\magma_2.5.4_' +
                    cuda_suffix + '_' + build_type + '.7z', shell=True, check=True)

        else:

            subprocess.run('aws s3 cp s3://ossci-windows/magma_2.5.4_' +
                cuda_suffix + '_' + build_type + '.7z ' + tmp_win_dir + '\\magma_2.5.4_'
                    + cuda_suffix + '_' + build_type + '.7z --quiet', shell=True, check=True)

        subprocess.run('7z x -aoa ' + tmp_win_dir + '\\magma_2.5.4_' +
            cuda_suffix + '_' + build_type + '.7z -o' + tmp_win_dir + '\\magma', shell=True, check=True)

    except Exception as e:

        subprocess.run('echo install magma failed', shell=True)
        subprocess.run('echo ' + str(e), shell=True)
        sys.exit()
