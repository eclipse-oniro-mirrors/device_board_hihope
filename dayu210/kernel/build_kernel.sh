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
KERNEL_VERSION=linux-5.10
export PRODUCT_PATH=${4}
export DEVICE_COMPANY=${6}
export DEVICE_NAME=dayu210
export PRODUCT_COMPANY=${8}

KERNEL_SRC_TMP_PATH=${ROOT_DIR}/out/rk3588/kernel/src_tmp/linux-5.10
KERNEL_OBJ_TMP_PATH=${ROOT_DIR}/out/rk3588/kernel/OBJ/linux-5.10
KERNEL_SOURCE=${ROOT_DIR}/kernel/linux/linux-5.10
KERNEL_PATCH_PATH=${ROOT_DIR}/device/board/hihope/dayu210/kernel/kernel_patch/linux-5.10
KERNEL_PATCH=${ROOT_DIR}/device/board/hihope/dayu210/kernel/kernel_patch/linux-5.10/dayu210_patch/kernel.patch
HDF_PATCH=${ROOT_DIR}/device/board/hihope/dayu210/kernel/kernel_patch/linux-5.10/dayu210_patch/hdf.patch
KERNEL_CONFIG_FILE=${ROOT_DIR}/device/board/hihope/dayu210/kernel/kernel_config/linux-5.10/arch/arm64/rk3588_standard_defconfig

NEWIP_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/newip/apply_newip.sh
TZDRIVER_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/tzdriver/apply_tzdriver.sh
XPM_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/xpm/apply_xpm.sh
CED_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/container_escape_detection/apply_ced.sh
HIDEADDR_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/memory_security/apply_hideaddr.sh
QOS_AUTH_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/qos_auth/apply_qos_auth.sh
UNIFIED_COLLECTION_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/ucollection/apply_ucollection.sh
CODE_SIGN_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/code_sign/apply_code_sign.sh

HARMONY_CONFIG_PATH=${ROOT_DIR}/kernel/linux/config/${KERNEL_VERSION}
DEVICE_CONFIG_PATH=${ROOT_DIR}/kernel/linux/config/${KERNEL_VERSION}/${DEVICE_NAME}
DEFCONFIG_BASE_FILE=${HARMONY_CONFIG_PATH}/base_defconfig
DEFCONFIG_TYPE_FILE=${HARMONY_CONFIG_PATH}/type/standard_defconfig
DEFCONFIG_FORM_FILE=${HARMONY_CONFIG_PATH}/form/${KERNEL_FORM}_defconfig
DEFCONFIG_ARCH_FILE=${DEVICE_CONFIG_PATH}/arch/arm64_defconfig
DEFCONFIG_PROC_FILE=${DEVICE_CONFIG_PATH}/product/${KERNEL_PROD}_defconfig


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

#xpm
if [ -f $XPM_PATCH_FILE ]; then
    bash $XPM_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} ${KERNEL_VERSION}
fi

#ced
if [ -f $CED_PATCH_FILE ]; then
    bash $CED_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} ${KERNEL_VERSION}
fi
#code_sign
if [ -f $CODE_SIGN_PATCH_FILE ]; then
    bash $CODE_SIGN_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} ${KERNEL_VERSION}
fi

bash ${ROOT_DIR}/kernel/linux/${KERNEL_VERSION}/scripts/kconfig/merge_config.sh -O ${KERNEL_SRC_TMP_PATH}/arch/arm64/configs/ -m ${KERNEL_CONFIG_FILE} ${DEFCONFIG_BASE_FILE}
mv ${KERNEL_SRC_TMP_PATH}/arch/arm64/configs/.config ${KERNEL_SRC_TMP_PATH}/arch/arm64/configs/rockchip_linux_defconfig
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

kernel/src_tmp/linux-5.10/make-boot.sh ..
