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
BOOT_LINUX=${1}/rk3588/kernel/src_tmp/linux-5.10
OUT_IMAGE=${1}/rk3588/packages/phone/images/
IMAGE_SIZE=64  # 64M
IMAGE_BLOCKS=4096

BUILD_PATH=boot_linux
EXTLINUX_PATH=${BUILD_PATH}/extlinux
EXTLINUX_CONF=${EXTLINUX_PATH}/extlinux.conf
TOYBRICK_DTB=toybrick.dtb

function make_boot_image()
{
        blocks=${IMAGE_BLOCKS}
        block_size=$((${IMAGE_SIZE} * 1024 * 1024 / ${blocks}))
        echo "blocks = ${blocks}  block_size ${block_size}"
        if [ "`uname -m`" == "aarch64" ]; then
                echo y | sudo mke2fs -b ${block_size} -d boot_linux -i 8192 -t ext2 boot_linux.img ${blocks}
        else
                genext2fs -B ${blocks} -b ${block_size} -d boot_linux -i 8192 -U boot_linux.img
        fi

        return $?
}

cd ${BOOT_LINUX}
make_boot_image
cd -
cp ${BOOT_LINUX}/boot_linux.img ${OUT_IMAGE}
