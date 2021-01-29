#!/bin/bash

# shellcheck disable=SC2034
source "$(dirname "${BASH_SOURCE[0]}")/macos-common.sh"

git submodule sync --recursive
git submodule update --init --recursive
export CMAKE_PREFIX_PATH=${WORKSPACE_DIR}/miniconda3/

# Build PyTorch
if [ -z "${IN_CI}" ]; then
  export DEVELOPER_DIR=/Applications/Xcode9.app/Contents/Developer
fi

function write_sccache_stub() {
  printf "#!/bin/sh\nif [ \$(ps auxc \$(ps auxc -o ppid \$\$ | grep \$\$ | rev | cut -d' ' -f1 | rev) | tr '\\\\n' ' ' | rev | cut -d' ' -f2 | rev) != sccache ]; then\n  exec sccache $(which $1) \"\$@\"\nelse\n  exec $(which $1) \"\$@\"\nfi" > "${WORKSPACE_DIR}/$1"
  chmod a+x "${WORKSPACE_DIR}/$1"
}

if which sccache > /dev/null; then
  write_sccache_stub clang++
  write_sccache_stub clang

  export PATH="${WORKSPACE_DIR}:$PATH"
fi

USE_DISTRIBUTED=1 python setup.py install

assert_git_not_dirty

# Upload torch binaries when the build job is finished
if [ -z "${IN_CI}" ]; then
  7z a ${IMAGE_COMMIT_TAG}.7z ${WORKSPACE_DIR}/miniconda3/lib/python3.6/site-packages/torch*
  aws s3 cp ${IMAGE_COMMIT_TAG}.7z s3://ossci-macos-build/pytorch/${IMAGE_COMMIT_TAG}.7z --acl public-read
fi
