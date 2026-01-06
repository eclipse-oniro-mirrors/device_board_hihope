#!/bin/bash
# Copyright (c) 2023 Phytium Technology Co., Ltd.  All rights reserved.
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
DTS_SRC_DIR=${1}
DTS_TARGET_DIR=${2}

function make_empty_Makefile ()
{
    mkdir -p ${DTS_TARGET_DIR}
    echo -e "all:\n\t@echo No dts file" > "${DTS_TARGET_DIR}/Makefile"
}

if [ ! -d $DTS_SRC_DIR ]; then
    mkdir -p ${DTS_SRC_DIR}
    make_empty_Makefile
    echo "there isn't path ${DTS_SRC_DIR}"
    #exit 0
fi

if [ ! -e ${DTS_TARGET_DIR} ]; then
    mkdir -p ${DTS_TARGET_DIR}
fi

dts_files=$(find ${DTS_SRC_DIR} -type f -name "*.dts")
if [ -n "$dts_files" ]; then
    cp ${DTS_SRC_DIR}/*.dts* ${DTS_TARGET_DIR}
    for dts_file in $dts_files; do
        dts_name=$(basename "$dts_file" .dts)
        echo "dtb-\$(CONFIG_ARCH_PHYTIUM) += ${dts_name}.dtb" >> "${DTS_TARGET_DIR}/Makefile"
    done
else
    make_empty_Makefile
    echo "there aren't dts files in ${DTS_SRC_DIR}"
fi
