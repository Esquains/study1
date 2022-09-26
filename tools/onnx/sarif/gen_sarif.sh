#!/bin/bash
ROOT=$(pwd)/../../..
SARIF_DIR=torch/onnx/_internal/diagnostics/infra/sarif_om

# SARIF version
SARIF_VERSION=2.1.0
SARIF_SCHEMA_LINK=https://docs.oasis-open.org/sarif/sarif/v2.1.0/cs01/schemas/sarif-schema-2.1.0.json

# download SARIF schema
tmp_dir=$(mktemp -d)
sarif_schema_file_path=$tmp_dir/sarif-schema-$SARIF_VERSION.json
wget -O $sarif_schema_file_path $SARIF_SCHEMA_LINK

# TODO: A private branch of jschema_to_python was used to enable
#       the generation to dataclasses and support annotation.
python -m jschema_to_python \
    --schema-path $sarif_schema_file_path \
    --module-name torch.onnx._internal.diagnostics.infra.sarif_om \
    --output-directory $ROOT/$SARIF_DIR \
    --root-class-name SarifLog \
    --hints-file-path code-gen-hints.json \
    --force \
    --library dataclasses \
    -vv

# generate SARIF version file
echo "SARIF_VERSION = \""$SARIF_VERSION"\"" > $ROOT/$SARIF_DIR/version.py
echo "SARIF_SCHEMA_LINK = \""$SARIF_SCHEMA_LINK"\"" >> $ROOT/$SARIF_DIR/version.py

# hack to have linter not complain about generated code.
cd $ROOT
for f in $(find $SARIF_DIR -name '*.py'); do
    echo "# flake8: noqa" >> $f
done

lintrunner $SARIF_DIR/** -a
