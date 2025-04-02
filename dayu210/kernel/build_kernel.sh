#!/bin/bash

# Copyright (c) 2021-2023 HiHope Open Source Organization .
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
ROOT_DIR=${5}
export PRODUCT_PATH=${4}
export DEVICE_COMPANY=${6}
export DEVICE_NAME=dayu210
export PRODUCT_COMPANY=${8}

KERNEL_SRC_TMP_PATH=${ROOT_DIR}/out/kernel/src_tmp/linux-5.10
KERNEL_OBJ_TMP_PATH=${ROOT_DIR}/out/kernel/OBJ/linux-5.10
KERNEL_SOURCE=${ROOT_DIR}/kernel/linux/linux-5.10
KERNEL_PATCH_PATH=${ROOT_DIR}/device/board/hihope/dayu210/kernel/kernel_patch/linux-5.10
KERNEL_PATCH=${ROOT_DIR}/device/board/hihope/dayu210/kernel/kernel_patch/linux-5.10/dayu210_patch/kernel.patch
HDF_PATCH=${ROOT_DIR}/device/board/hihope/dayu210/kernel/kernel_patch/linux-5.10/dayu210_patch/hdf.patch
KERNEL_CONFIG_FILE=${ROOT_DIR}/device/board/hihope/dayu210/kernel/kernel_config/linux-5.10/arch/arm64/rk3588_standard_defconfig

rm -rf ${KERNEL_SRC_TMP_PATH}
mkdir -p ${KERNEL_SRC_TMP_PATH}

rm -rf ${KERNEL_OBJ_TMP_PATH}
mkdir -p ${KERNEL_OBJ_TMP_PATH}
#export KBUILD_OUTPUT=${KERNEL_OBJ_TMP_PATH}
export KBUILD_OUTPUT=${KERNEL_SRC_TMP_PATH}

echo "cp kernel source"
cp -arf ${KERNEL_SOURCE}/* ${KERNEL_SRC_TMP_PATH}/

cd ${KERNEL_SRC_TMP_PATH}

#HDF patch
echo "HDF patch"
bash ${ROOT_DIR}/drivers/hdf_core/adapter/khdf/linux/patch_hdf.sh ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${KERNEL_PATCH_PATH} ${DEVICE_NAME}

#kernel patch
echo "kernel patch"
patch -p1 < ${KERNEL_PATCH}

cp -rf ${3}/kernel/logo* ${KERNEL_SRC_TMP_PATH}/
cp -rf ${3}/kernel/make*.sh ${KERNEL_SRC_TMP_PATH}/
#config
cp -rf ${KERNEL_CONFIG_FILE} ${KERNEL_SRC_TMP_PATH}/arch/arm64/configs/rockchip_linux_defconfig

ln -s ${ROOT_DIR}/device/soc/rockchip/rk3588/kernel ${KERNEL_SRC_TMP_PATH}/vendor

if [ ! -d "${ROOT_DIR}/device/soc/rockchip/rk3588/kernel/drivers/net/wireless/rockchip_wlan" ];then
    ln -s ${ROOT_DIR}/device/soc/rockchip/common/kernel/drivers/net/wireless/rockchip_wlan ${KERNEL_SRC_TMP_PATH}/vendor/drivers/net/wireless/rockchip_wlan
fi

if [ ! -d "${ROOT_DIR}/device/soc/rockchip/rk3588/kernel/drivers/gpu/arm" ];then
    ln -s ${ROOT_DIR}/device/soc/rockchip/common/kernel/drivers/gpu/arm ${KERNEL_SRC_TMP_PATH}/vendor/drivers/gpu/arm
fi

cp -rf ${3}/kernel/logo* ${KERNEL_SRC_TMP_PATH}/vendor/

if [ "enable_ramdisk" == "${9}" ]; then
    ./make-ohos.sh BQ3588C1 enable_ramdisk
else
    ./make-ohos.sh BQ3588C1 disable_ramdisk
fi

mkdir -p ${2}

cp ${KERNEL_SRC_TMP_PATH}/resource.img ${2}/resource.img
cp ${3}/loader/parameter.txt ${2}/parameter.txt
cp ${3}/loader/MiniLoaderAll.bin ${2}/MiniLoaderAll.bin
cp ${3}/loader/uboot.img ${2}/uboot.img
cp ${3}/loader/config.cfg ${2}/config.cfg
popd

../kernel/src_tmp/linux-5.10/make-boot.sh ..
