#!/bin/bash
# Copyright (c) 2024 Phytium Technology Co., Ltd. 
#
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

export OUT_DIR=$1
export BUILD_TYPE=$2
export KERNEL_ARCH=$3
export PRODUCT_PATH=vendor/hihope/$5
export DEVICE_NAME=$5
export KERNEL_VERSION=$6
export GPU_MODEL=${7}
LINUX_KERNEL_OUT=${OUT_DIR}/kernel/src_tmp/${KERNEL_VERSION}

# it needs adaptation for more device target
kernel_image=""
if [ "$KERNEL_ARCH" == "arm" ];then
    kernel_image="uImage"
elif [ "$KERNEL_ARCH" == "arm64" ];then
    kernel_image="Image"
fi

export KERNEL_IMAGE=${kernel_image}
LINUX_KERNEL_IMAGE_FILE=${LINUX_KERNEL_OUT}/arch/${KERNEL_ARCH}/boot/${kernel_image}

make -f kernel.mk

if [ -f "${LINUX_KERNEL_IMAGE_FILE}" ];then
    echo "Image: ${LINUX_KERNEL_IMAGE_FILE} build success"
else
    echo "Image: ${LINUX_KERNEL_IMAGE_FILE} build failed!!!"
    exit 1
fi

exit 0
