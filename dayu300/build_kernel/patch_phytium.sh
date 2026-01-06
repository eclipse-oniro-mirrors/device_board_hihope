#!/bin/bash
# Copyright (c) 2024 Phytium Technology Co., Ltd.
# All rights reserved.
#
# This file contains confidential and proprietary information of
# OSWare Technology Co., Ltd
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

PHYTIUM_PATCH_PATH=$1
KERNEL_BUILD_ROOT=$2
GPU_KERNEL_PATCH_PATH=$3

function patch_phytium()
{
    cd $KERNEL_BUILD_ROOT

    for patch in ${1}/*.patch; do
        if [ -f "${patch}" ]; then
            echo "Applying ${patch}..."
            patch -p1 < ${patch}
        else
            echo "No patch file found in ${1} skipping..."
        fi
    done

    cd -
}

function main()
{
    patch_phytium ${PHYTIUM_PATCH_PATH}
    patch_phytium ${GPU_KERNEL_PATCH_PATH}
}

main
