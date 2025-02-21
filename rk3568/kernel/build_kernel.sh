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
export DEVICE_NAME=${7}
export PRODUCT_COMPANY=${8}
KERNEL_FORM=${9}
KERNEL_PROD=${10}
ENABLE_LTO_O0=${11}

KERNEL_SRC_TMP_PATH=${ROOT_DIR}/out/kernel/src_tmp/linux-5.10
KERNEL_OBJ_TMP_PATH=${ROOT_DIR}/out/kernel/OBJ/linux-5.10
KERNEL_SOURCE=${ROOT_DIR}/kernel/linux/linux-5.10
KERNEL_PATCH_PATH=${ROOT_DIR}/kernel/linux/patches/linux-5.10
KERNEL_PATCH=${ROOT_DIR}/kernel/linux/patches/linux-5.10/rk3568_patch/kernel.patch
BUILD_SCRIPT_PATH=${3}
NEWIP_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/newip/apply_newip.sh
TZDRIVER_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/tzdriver/apply_tzdriver.sh
XPM_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/xpm/apply_xpm.sh
CED_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/container_escape_detection/apply_ced.sh
HIDEADDR_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/memory_security/apply_hideaddr.sh
QOS_AUTH_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/qos_auth/apply_qos_auth.sh
UNIFIED_COLLECTION_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/ucollection/apply_ucollection.sh
CODE_SIGN_PATCH_FILE=${ROOT_DIR}/kernel/linux/common_modules/code_sign/apply_code_sign.sh

HARMONY_CONFIG_PATH=${ROOT_DIR}/kernel/linux/config/linux-5.10
DEVICE_CONFIG_PATH=${ROOT_DIR}/kernel/linux/config/linux-5.10/${DEVICE_NAME}
DEFCONFIG_BASE_FILE=${HARMONY_CONFIG_PATH}/base_defconfig
DEFCONFIG_TYPE_FILE=${HARMONY_CONFIG_PATH}/type/standard_defconfig
DEFCONFIG_FORM_FILE=${HARMONY_CONFIG_PATH}/form/${KERNEL_FORM}_defconfig
DEFCONFIG_ARCH_FILE=${DEVICE_CONFIG_PATH}/arch/arm64_defconfig
DEFCONFIG_PROC_FILE=${DEVICE_CONFIG_PATH}/product/${KERNEL_PROD}_defconfig

RAMDISK_ARG="disable_ramdisk"
MAKE_OHOS_ENV="GPUDRIVER=mali"
export KBUILD_OUTPUT=${KERNEL_OBJ_TMP_PATH}

source ${BUILD_SCRIPT_PATH}/kernel/kernel_source_checker.sh

for i in "$@"
do
    case $i in
        enable_ramdisk)
            RAMDISK_ARG=enable_ramdisk
            ;;
        enable_mesa3d)
            MAKE_OHOS_ENV="GPUDRIVER=mesa3d"
            ;;
    esac
done

function copy_and_patch_kernel_source()
{
    rm -rf ${KERNEL_SRC_TMP_PATH}
    mkdir -p ${KERNEL_SRC_TMP_PATH}

    rm -rf ${KERNEL_OBJ_TMP_PATH}
    mkdir -p ${KERNEL_OBJ_TMP_PATH}

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

    #tzdriver
    if [ -f $TZDRIVER_PATCH_FILE ]; then
        bash $TZDRIVER_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} linux-5.10
    fi

    #xpm
    if [ -f $XPM_PATCH_FILE ]; then
        bash $XPM_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} linux-5.10
    fi

    #ced
    if [ -f $CED_PATCH_FILE ]; then
        bash $CED_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} linux-5.10
    fi

    #qos_auth
    if [ -f $QOS_AUTH_PATCH_FILE ]; then
        bash $QOS_AUTH_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} linux-5.10
    fi

    #hideaddr
    if [ -f $HIDEADDR_PATCH_FILE ]; then
        bash $HIDEADDR_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} linux-5.10
    fi

    #ucollection
    if [ -f $UNIFIED_COLLECTION_PATCH_FILE ]; then
        bash $UNIFIED_COLLECTION_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} linux-5.10
    fi

    #code_sign
    if [ -f $CODE_SIGN_PATCH_FILE ]; then
	bash $CODE_SIGN_PATCH_FILE ${ROOT_DIR} ${KERNEL_SRC_TMP_PATH} ${DEVICE_NAME} linux-5.10
    fi

    cp -rf ${BUILD_SCRIPT_PATH}/kernel/logo* ${KERNEL_SRC_TMP_PATH}/

    #config
    if [ ! -f "$DEFCONFIG_FORM_FILE" ]; then
        DEFCONFIG_FORM_FILE=
        echo "warning no form config file $(DEFCONFIG_FORM_FILE)"
    fi
    if [ ! -f "$DEFCONFIG_PROC_FILE" ]; then
        DEFCONFIG_PROC_FILE=
        echo "warning no prod config file $(DEFCONFIG_PROC_FILE)"
    fi
    bash ${ROOT_DIR}/kernel/linux/linux-5.10/scripts/kconfig/merge_config.sh -O ${KERNEL_SRC_TMP_PATH}/arch/arm64/configs/ -m ${DEFCONFIG_TYPE_FILE} ${DEFCONFIG_FORM_FILE} ${DEFCONFIG_ARCH_FILE} ${DEFCONFIG_PROC_FILE} ${DEFCONFIG_BASE_FILE}
    mv ${KERNEL_SRC_TMP_PATH}/arch/arm64/configs/.config ${KERNEL_SRC_TMP_PATH}/arch/arm64/configs/rockchip_linux_defconfig

    #selinux config patch
    for arg in "$@"; do
        if [ "$arg" = "is_release" ]; then
            echo "close selinux kernel config CONFIG_SECURITY_SELINUX_DEVELOP in release version"
            ${KERNEL_SOURCE}/scripts/config --file ${KERNEL_SRC_TMP_PATH}/arch/arm64/configs/rockchip_linux_defconfig -d SECURITY_SELINUX_DEVELOP
        fi
    done

    if [ $MAKE_OHOS_ENV == "GPUDRIVER=mesa3d" ]; then
        python ${ROOT_DIR}/third_party/mesa3d/ohos/modifyDtsi.py ${KERNEL_SRC_TMP_PATH}/arch/arm64/boot/dts/rockchip/rk3568.dtsi
    fi
}

set +e
is_kernel_change ${ROOT_DIR}
KERNEL_SOURCE_CHANGED=$?
set -e
if [ ${KERNEL_SOURCE_CHANGED}  -ne 0 ]; then
    echo "kernel or it's deps changed, start source update."
    copy_and_patch_kernel_source
else
    echo "no changes to kernel, skip source copy."
fi

cd ${KERNEL_SRC_TMP_PATH}

eval $MAKE_OHOS_ENV ./make-ohos.sh TB-RK3568X0 $RAMDISK_ARG ${ENABLE_LTO_O0}

mkdir -p ${2}

if [ "enable_ramdisk" != "${12}" ]; then
    cp ${KERNEL_OBJ_TMP_PATH}/boot_linux.img ${2}/boot_linux.img
fi
cp ${KERNEL_OBJ_TMP_PATH}/resource.img ${2}/resource.img
cp ${3}/loader/MiniLoaderAll.bin ${2}/MiniLoaderAll.bin
cp ${3}/loader/uboot.img ${2}/uboot.img

if [ "enable_absystem" == "${14}" ]; then
    cp ${3}/loader/parameter_ab.txt ${2}/parameter_ab.txt
    cp ${3}/loader/config_ab.cfg ${2}/config_ab.cfg
else
    cp ${3}/loader/parameter.txt ${2}/parameter.txt
    cp ${3}/loader/config.cfg ${2}/config.cfg
fi

popd

../kernel/src_tmp/linux-5.10/make-boot.sh ..

if [ ${KERNEL_SOURCE_CHANGED} -ne 0 ]; then
    cp ${ROOT_DIR}/out/kernel/checkpoint/last_build.info ${ROOT_DIR}/out/kernel/checkpoint/last_build.backup
    cp ${ROOT_DIR}/out/kernel/checkpoint/current_build.info ${ROOT_DIR}/out/kernel/checkpoint/last_build.info
    echo "kernel compile finish, save build info."
else
    echo "kernel compile finish."
fi
