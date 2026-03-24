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

function readfile ()
{
    for file in $1/*
    do
        if [ -d "$file" ];then
	    readfile $file $2 $3
        elif [ "$file" -nt "$2" ]; then
            echo $file is update
            touch $3;
            return
        fi
    done
}    

echo $1 for check kernel dir
echo $2 for output image
echo $3 for timestamp
if [ -e "$2" ]; then
    readfile $1 $2 $3
    if [ "$3" -nt "$2" ]; then
        echo "need update $2"
        rm -rf $2;
    fi
fi

