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
ROOT_DIR=${5}
export PRODUCT_PATH=${4}
export DEVICE_COMPANY=${6}
export DEVICE_NAME=${7}
export PRODUCT_COMPANY=${8}
ENABLE_LTO_O0=${9}

KERNEL_SRC_TMP_PATH=${ROOT_DIR}/out/kernel/src_tmp/linux-5.10
KERNEL_OBJ_TMP_PATH=${ROOT_DIR}/out/kernel/OBJ/linux-5.10
KERNEL_SOURCE=${ROOT_DIR}/kernel/linux/linux-5.10
KERNEL_PATCH_PATH=${ROOT_DIR}/kernel/linux/patches/linux-5.10
KERNEL_PATCH=${ROOT_DIR}/kernel/linux/patches/linux-5.10/rk3568_patch/kernel.patch
KERNEL_CONFIG_FILE=${ROOT_DIR}/kernel/linux/config/linux-5.10/arch/arm64/configs/rk3568_standard_defconfig
NEWIP_PATCH_FILE=${ROOT_DIR}/foundation/communication/sfc/newip/apply_newip.sh

rm -rf ${KERNEL_SRC_TMP_PATH}
mkdir -p ${KERNEL_SRC_TMP_PATH}

rm -rf ${KERNEL_OBJ_TMP_PATH}
mkdir -p ${KERNEL_OBJ_TMP_PATH}
export KBUILD_OUTPUT=${KERNEL_OBJ_TMP_PATH}

cp -arf ${KERNEL_SOURCE}/* ${KERNEL_SRC_TMP_PATH}/

cd ${KERNEL_SRC_TMP_PATH}

#HDF patch
bash ${ROOT_DIR}/drivers/hdf_core/adapter/khdf/linux/patch_hdf.sh ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${KERNEL_PATCH_PATH} ${DEVICE_NAME}

#kernel patch
patch -p1 < ${KERNEL_PATCH}

#newip
if [ -f $NEWIP_PATCH_FILE ]; then
bash $NEWIP_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} linux-5.10
fi

cp -rf ${3}/kernel/logo* ${KERNEL_SRC_TMP_PATH}/

#config
cp -rf ${KERNEL_CONFIG_FILE} ${KERNEL_SRC_TMP_PATH}/arch/arm64/configs/rockchip_linux_defconfig

ramdisk_arg="disable_ramdisk"
make_ohos_env="GPUDRIVER=mali"
for i in "$@" 
do
	case $i in
		enable_ramdisk)
			ramdisk_arg=enable_ramdisk
			;;
		enable_mesa3d)
			make_ohos_env="GPUDRIVER=mesa3d"
			python ${ROOT_DIR}/third_party/mesa3d/ohos/modifyDtsi.py ${KERNEL_SRC_TMP_PATH}/arch/arm64/boot/dts/rockchip/rk3568.dtsi
			;;
	esac
done
eval $make_ohos_env ./make-ohos.sh TB-RK3568X0 $ramdisk_arg ${ENABLE_LTO_O0}

mkdir -p ${2}

if [ "enable_ramdisk" != "${10}" ]; then
	cp ${KERNEL_OBJ_TMP_PATH}/boot_linux.img ${2}/boot_linux.img
fi
cp ${KERNEL_OBJ_TMP_PATH}/resource.img ${2}/resource.img
cp ${3}/loader/parameter.txt ${2}/parameter.txt
cp ${3}/loader/MiniLoaderAll.bin ${2}/MiniLoaderAll.bin
cp ${3}/loader/uboot.img ${2}/uboot.img
cp ${3}/loader/config.cfg ${2}/config.cfg
popd

../kernel/src_tmp/linux-5.10/make-boot.sh ..

