#!/bin/bash
# Copyright (C) 2025 Phytium Technology Co., Ltd.
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

#$1 - kernel build script work dir
#$2 - kernel build script stage dir
#$3 - GN target output dir
export KERNEL_WORK_DIR=${1}
export OUT_KERNEL_OBJ=${2}
export OHOS_OUT_PATH=${3}
export BUILD_TYPE=${4}
export TARGET_CPU=${5}
export PRODUCT_PATH=${6}
export DEVICE_NAME=${7}
export KERNEL_VERSION=${8}
export OHOS_ROOT_PATH=${9}
export GPU_MODEL=${10}

if [[ ${DEVICE_NAME} == "dayu300" ]];then
    export KERNEL_PATCH=${OHOS_ROOT_PATH}/device/soc/phytium/D3000M/kernel_source/phytium.patch
    export KERNEL_CONFIG=${OHOS_ROOT_PATH}/device/soc/phytium/D3000M/kernel_source/phytium_standard_defconfig
else
    export KERNEL_PATCH=${OHOS_ROOT_PATH}/device/soc/phytium/common/kernel_source/${KERNEL_VERSION}/phytium.patch
    export KERNEL_CONFIG=${OHOS_ROOT_PATH}/device/soc/phytium/common/kernel_source/${KERNEL_VERSION}/phytium_standard_defconfig
fi
export BOOT_IMAGE_PATH=${OHOS_OUT_PATH}/boot
export KERNEL_OUTPUT_PATH=${OUT_KERNEL_OBJ}/kernel/src_tmp/${KERNEL_VERSION}/arch/arm64/boot
export DEVICE_BOARD_PATH=${OHOS_ROOT_PATH}/device/board/hihope

echo build_kernel
if [ -f "${KERNEL_PATCH}" -a -f "${KERNEL_CONFIG}" ]; then
    pushd ${KERNEL_WORK_DIR}
    ./kernel_module_build.sh ${OUT_KERNEL_OBJ} ${BUILD_TYPE} ${TARGET_CPU} ${PRODUCT_PATH} ${DEVICE_NAME} ${KERNEL_VERSION} ${GPU_MODEL}
    mkdir -p ${OHOS_OUT_PATH}
    rm -rf ${OHOS_OUT_PATH}/../../../kernel.timestamp

###create boot folder, cp kernel Image dtb to boot folder
    if [ -d ${BOOT_IMAGE_PATH} ];then
        echo "${BOOT_IMAGE_PATH} existed!"
        cd ${BOOT_IMAGE_PATH}
        rm -rf *
        cd -
    else
        mkdir -p ${BOOT_IMAGE_PATH}
    fi

    cp ${KERNEL_OUTPUT_PATH}/Image ${BOOT_IMAGE_PATH}

    if [ -f ${KERNEL_OUTPUT_PATH}/dts/phytium/*.dtb ]; then
        cp ${KERNEL_OUTPUT_PATH}/dts/phytium/*.dtb ${BOOT_IMAGE_PATH}
    fi

    mkdir -p ${BOOT_IMAGE_PATH}/EFI
    cp ${DEVICE_BOARD_PATH}/${DEVICE_NAME}/loader/EFI/* ${BOOT_IMAGE_PATH}/EFI -rf
    cp ${DEVICE_BOARD_PATH}/${DEVICE_NAME}/tools/generate_image/*  ${OHOS_OUT_PATH} -rf
    popd

else
    echo "No kernel patch found, skip building Kernel"
fi

