#!/bin/bash

# Copyright (c) 2021-2026 Phytium Technology Co., Ltd.
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

shopt -s nullglob dotglob

merge_directories() {
    local src_dir=$1
    local dest_dir=$2
    local type=$3

    while IFS= read -r -d '' item; do

        local base_item=$(basename "$item")
        local dest_item="$dest_dir/$base_item"

        if [ -f "$item" ]; then
            if [ ! -f "$dest_item" ]; then
                cp "$item" "$dest_item"
            elif [[ "$base_item" == "Makefile" ]]; then
                while IFS= read -r line || [[ -n "$line" ]]; do
                    if [[ ! "$line"  =~ ^obj.*+=.*/$ ]]; then
                        echo "$line" >> $dest_item
                    fi
                done < "$item"
            elif [[ "$base_item" == "Kconfig" ]]; then
                while IFS= read -r line || [[ -n "$line" ]]; do
                    if [[ ! "$line" =~ ^source.* ]]; then
                        echo >> $dest_item
                        echo "$line" >> $dest_item
                    fi
                done < "$item"
            fi
        elif [ -d "$item" ]; then
            if [ ! -d "$dest_item" ]; then
                mkdir -p "$dest_item"
                cp -r ${item}/*  ${dest_item}/
                find "$item" -name "Kconfig" | while read -r kfile; do
                    if [[ $type == "drivers" ]]; then
                        echo "source \"drivers/${kfile#*/drivers/}\"" >> ${dest_dir}/Kconfig
                    elif [[ $type == "sound" ]]; then
                        echo "source \"sound/${kfile#*/sound/}\"" >> ${dest_dir}/Kconfig
                    fi
                done
                echo "obj-y += $base_item/" >> ${dest_dir}/Makefile
            else
                merge_directories "$item" "$dest_item" "$type"
            fi
        fi
    done < <(find "$src_dir" -mindepth 1 -maxdepth 1 -print0)
}

HCK="${1}"
OH_KERNEL="${2}"
DRIVER=drivers
SOUND=sound
KERNEL=kernel

echo "begin merge hck..."
merge_directories "$HCK/${DRIVER}" "$OH_KERNEL/${DRIVER}" ${DRIVER}
merge_directories "$HCK/${SOUND}" "$OH_KERNEL/${SOUND}"  ${SOUND}
merge_directories "$HCK/${KERNEL}" "$OH_KERNEL/${KERNEL}"  ${KERNEL}
echo "merge hck success"
