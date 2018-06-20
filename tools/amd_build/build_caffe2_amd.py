#!/usr/bin/env python

import os
import glob
import subprocess


caffe2_root = "../../caffe2"
hipify_tool = "../tools/amd_build/hipify-perl"

os.chdir(caffe2_root)

for folder in ["operators", "sgd", "image", "transforms", "video"]:
    for extension in ["/*.cu", "/*.cuh", "/*gpu.cc"]:
        target = folder + extension
        if glob.glob(target):
            os.system(hipify_tool + ' --inplace ' + target)
        os.chdir(folder)
        if glob.glob('*hip.cc'):
            os.system("mv -n *hip.cc hip")
        os.chdir(caffe2_root)

