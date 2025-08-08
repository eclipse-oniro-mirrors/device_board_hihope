#!/bin/bash

# Copyright (c) 2021 HiHope Open Source Organization .
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -e

pushd ${1}
ROOT_DIR=${3}

UBOOT_SRC_TMP_PATH=${ROOT_DIR}/out/uboot/src_tmp

UBOOT_SOURCE=${ROOT_DIR}/uboot/rk3588

rm -rf ${UBOOT_SRC_TMP_PATH}
mkdir -p ${UBOOT_SRC_TMP_PATH}


cp -arf ${UBOOT_SOURCE}/* ${UBOOT_SRC_TMP_PATH}/

cd ${UBOOT_SRC_TMP_PATH}

cp -rf ${4}/uboot/make.sh ${UBOOT_SRC_TMP_PATH}/

./make.sh rk3588-edge

mkdir -p ${2}

cp ${UBOOT_SRC_TMP_PATH}/uboot.img ${2}/uboot.img

popd
