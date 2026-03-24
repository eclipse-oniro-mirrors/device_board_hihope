#!/bin/bash

# Copyright (c) 2025 Phytium Technology Co., Ltd.  All rights reserved.
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

set -e  # Exit immediately on error

# ========================
#   FUNCTION DEFINITIONS
# ========================
unmount_partition() {
    echo -n "⏳ Unmounting partition... "
    if umount "$mount_point" &> /dev/null; then
        echo -e "\e[32mSUCCESS\e[0m (${PARTITIONS} unmounted)"
        return 0
    else
        echo -e "\e[31mFAILED\e[0m"
        echo -e "❌ Error: Could not unmount ${PARTITIONS}"
        echo "Possible reasons:"
        echo "1. Filesystem is busy (check open files)"
        echo "2. Device removal in progress"
        echo "3. Invalid mount point: ${mount_point}"
        return 1
    fi
}

# ========================
#   SCRIPT START
# ========================
cat << "EOF"
╔══════════════════════════════════════════╗
║  GRUB Configuration Switcher             ║
╟──────────────────────────────────────────╢
║  Switches between OpenHarmony and Updater║
╚══════════════════════════════════════════╝
EOF

# ========================
#   1. LOCATE FSTAB FILE
# ========================
echo -e "\n\e[34m[STEP 1/5] Locating system configuration...\e[0m"
if [ -f /vendor/etc/fstab.* ]; then
    fstab_file=$(ls -1 /vendor/etc/fstab.* | head -n1)
    mount_point="/tmp"
    echo -e "✓ Found system file: \e[36m${fstab_file}\e[0m"
elif [ -f /etc/fstab.* ]; then
    fstab_file=$(ls -1 /etc/fstab.* | head -n1)
    mount_point="/mnt"
    echo -e "✓ Found updater file: \e[36m${fstab_file}\e[0m"
else
    echo -e "\e[31m✗ Critical: No fstab files found\e[0m"
    echo "System files required for operation not found."
    exit 1
fi

# ========================
#   2. IDENTIFY BOOT PARTITION
# ========================
echo -e "\n\e[34m[STEP 2/5] Identifying boot partition...\e[0m"
PARTITIONS=$(
  grep -E '^/dev/block/(sd[a-z]+[0-9]*|nvme[0-9]+n[0-9]+p?[0-9]*|mmcblk[0-9]+p?[0-9]*)' "$fstab_file" \
  | sed -E '
      /^#/d;                # 删除注释行
      s#^([^ ]+).*#\1#;     # 提取分区路径（第一列）
      s/[0-9]+p?[0-9]*$//;  # 移除分区末尾数字（如 sda1 → sda）
      /(nvme|mmcblk)/ s#$#p1#;  # NVMe/MMC 设备补全为 p1
      /sd/ s#$#1#;          # SATA 设备补全为 1
  ' \
  | sort -u \
  | sed 's/[[:space:]]//g'  # 删除所有空白字符
)

if [ -z "$PARTITIONS" ]; then
    echo -e "\e[31m✗ Error: No boot partition found in ${fstab_file}\e[0m"
    exit 1
else
    echo -e "✓ Boot partition identified: \e[1;36m${PARTITIONS}\e[0m"
fi

# ========================
#   3. MOUNT BOOT PARTITION
# ========================
echo -e "\n\e[34m[STEP 3/5] Mounting boot partition...\e[0m"
echo -n "Mounting ${PARTITIONS} to ${mount_point}... "
if mount -t vfat ${PARTITIONS} ${mount_point} &> /dev/null; then
    echo -e "\e[32mSUCCESS\e[0m"
else
    echo -e "\e[31mFAILED\e[0m"
    echo "Possible reasons:"
    echo "1. Invalid partition: ${PARTITIONS}"
    echo "2. Already mounted at another location"
    echo "3. Filesystem errors (try fsck)"
    exit 1
fi

# ========================
#   4. MODIFY GRUB CONFIG
# ========================
echo -e "\n\e[34m[STEP 4/5] Modifying GRUB configuration...\e[0m"
grub_cfg="${mount_point}/EFI/BOOT/grub.cfg"   # GRUB config path
# Verify GRUB config exists
if [ ! -f "$grub_cfg" ]; then
    echo -e "\e[31m✗ Critical: GRUB config not found at ${grub_cfg}\e[0m"
    unmount_partition
    exit 1
fi

# Detect and toggle default boot entry
echo -n "Switching default boot entry... "
if grep -q 'default="OpenHarmony"' "$grub_cfg"; then
    sed -i 's/default="OpenHarmony"/default="Updater"/' "$grub_cfg"
    echo -e "\e[32mOpenHarmony → Updater\e[0m"
elif grep -q 'default="Updater"' "$grub_cfg"; then
    sed -i 's/default="Updater"/default="OpenHarmony"/' "$grub_cfg"
    echo -e "\e[32mUpdater → OpenHarmony\e[0m"
else
    echo -e "\e[31mFAILED\e[0m"
    echo "✗ Error: Boot options not found in GRUB config"
    echo "Expected entries: OpenHarmony or Updater"
    unmount_partition
    exit 1
fi

# ========================
#   5. CLEANUP
# ========================
echo -e "\n\e[34m[STEP 5/5] Finalizing operation...\e[0m"
if unmount_partition; then
    echo -e "\n\e[42m\t OPERATION COMPLETED SUCCESSFULLY \t\e[0m\n"
else
    echo -e "\n\e[41m\t OPERATION COMPLETED WITH WARNINGS \t\e[0m\n"
    echo "Warning: Partition unmount failed!"
    echo "Please check: umount ${mount_point}"
fi