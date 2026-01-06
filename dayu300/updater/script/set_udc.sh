# Copyright (c) 2025 Phytium Technology Co., Ltd.
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

#!/bin/bash

TENGLONG_E="000701"
TENGLONG_E_UDC="31800000.usb2"
PD2508="000B01"
PD2508_UDC="PHYT0064:00"


CPU_TYPE_PATH="/sys/devices/soc0/cpu_type"



if [ ! -f "$CPU_TYPE_PATH" ]; then
    echo "can't find cpu type path"
    exit 1
fi

cpu_type=$(cat "$CPU_TYPE_PATH")

echo "cpu type is '$cpu_type'"

case $cpu_type in
    $TENGLONG_E)
        param set sys.usb.controller $TENGLONG_E_UDC
        param set sys.usb.configfs 1
        ;;
    $PD2508)
        param set sys.usb.controller $PD2508_UDC
        param set sys.usb.configfs 1
        ;;
    *)
esac
