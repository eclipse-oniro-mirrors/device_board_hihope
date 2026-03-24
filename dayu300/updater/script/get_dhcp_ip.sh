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
#echo 0 99999999 | tee /proc/sys/net/ipv4/ping_group_range #set ICMP range
ifconfig eth0 up
ifconfig eth0 192.168.1.2     # set defult IP
busybox udhcpc -i eth0 -b -s /etc/dhcp_script   # get IP from dhcp server

