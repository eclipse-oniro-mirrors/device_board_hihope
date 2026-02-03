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

#manual:
#    this script is used to generate image of phytium openharmony product.
#tools needed: 
#    before use this script, make sure 3 tools intalled firstly. 
#    tools: parted, mke2fs
#    if not, install them with command sudo apt-get install
#usage:
#    how to generate image,  only 3 steps.
#        step1. make sure  boot.img or boot_efi.img, system.img, vendor.img, userdata.img have been maked.
#        step2. if you want to include uboot, copy uboot img to same folder
#        step3. execute this script, if with uboot img, add the path of uboot img as a parameter.
#    for example: $ ./generate_image.sh
#                 $ ./generate_image.sh <path of uboot img>
#    how to burn image, It supports two ways.
#        in linux: sudo dd if=imagename of=/dev/sdX bs=1M
#        in windows:burn it with tool Win32DiskImager.exe, this tool can be downloaded from web very easily.
#company:
#    phytium
#verion:
#    V0.1.0 original
#    V0.1.1 fix mistake that node /dev/loopx was occupied by others.
#    v1.0.1 rm losetup tool, rm sudo requirement, add mke2fs ext4 userdata.img
############################################################################################################################

set -o errexit
##############################################################################################################################
#set color defination in echo info
green="\e[32;1m"
red="\e[31;1m"
normal="\e[0m"
##############################################################################################################################
# Local functions

#function: get_file_size_decimal_mib
#check image file exists or not, and get image file size in decimal MiB
function get_file_size_decimal_mib(){
    local file="$1"
    local size=$(stat -c%s "$file" 2>/dev/null)

    if [ -z "$size" ]; then
        echo "-1"
    else
        echo "$size" | awk '{printf "%d\n", (int($1 / (1000*1000)) + 1)}'
    fi
}

#function: convert_1000mib_to_1024mib
#convert unit from decimal MiB to binary MiB
function convert_1000mib_to_1024mib(){
  #0.95367431640625 = (1000*1000)/(1024*1024)
  local tmp=0.95367431640625 # convert：1000*1000 MiB -> 1024*1024 MiB
  local out=$(echo "scale=0; ($1*$tmp + 0.5) / 1" | bc)
  echo "$out"
}

#fuction: make a ext4 image
function make_ext4_image()
{
    src=$1
    dis=$2
    size=$3
    block_size=$4
    block_num=$((${size} * 1024 * 1024 / ${block_size}))

    mke2fs -F -b ${block_size} -d ${src} -i 8192 -t ext4 ${dis} ${block_num}
    return $?
}

#fuction :check image file is ext4 or not
function is_ext4_image()
{
  # Check if the file exists
  if [ ! -f "$1" ]; then
    echo "-1"
  fi

  # Use blkid to check the filesystem type of the image
  local fstype=$(blkid -o value -s TYPE "$1")

  echo "$fstype"
}
##############################################################################################################################
#1.Analyze input parameters
UBOOT_NAME="no_uboot"
if [ -f $1 ]; then
    UBOOT_NAME=$1
fi

#2.Check the image file, obtain the file size in decimal MiB
#check uboot image
LABLE_RESERVE_SIZE=$(get_file_size_decimal_mib $UBOOT_NAME)
if [ ${LABLE_RESERVE_SIZE} == -1 ]; then
    UBOOT_NAME="no_uboot"
    LABLE_RESERVE_SIZE=1
    IMAGE_NAME_END="no_uboot"
else
    IMAGE_NAME_END="with_uboot"
fi

IMAGE_NAME_EX="openharmony_img"
IMAGE_NAME="${IMAGE_NAME_EX}_${IMAGE_NAME_END}.img"
echo -e "${green}start generate $IMAGE_NAME with gpt mode !!!${normal}"

#check boot image
BOOT_IMG="boot.img"
if [ -e "boot.img" ]; then
    BOOT_IMG="boot.img"
else
    BOOT_IMG="boot_efi.img"
fi
BOOT_PARTION_SIZE=$(get_file_size_decimal_mib "$BOOT_IMG")

#check system and vendor image
SYSTEM_PARTION_SIZE=$(get_file_size_decimal_mib "system.img")
VENDOR_PARTION_SIZE=$(get_file_size_decimal_mib "vendor.img")
if [ ${BOOT_PARTION_SIZE} == -1 ] || [ ${SYSTEM_PARTION_SIZE} == -1 ] || [ ${VENDOR_PARTION_SIZE} == -1 ]; then
    echo -e "${red}check boot.img system.img vendor.img fail !!!${normal}"
    exit
fi

#check userdata image
userdata_fstype=$(is_ext4_image "userdata.img")
if [ ${userdata_fstype} == "ext4" ]; then
    USERDATA_PARTION_SIZE=$(get_file_size_decimal_mib "userdata.img")
else
    echo -e "${green}userdata.img is not exsit or its fstype is not ext4, make a new one with size 1GB.${normal}"
    USERDATA_PARTION_SIZE=1024
    mkdir data
    make_ext4_image "./data" "userdata.img" ${USERDATA_PARTION_SIZE} 4096
    rm data -rf
fi

#3.Calculate the start and end addresses of partition image burning and image size
START_BOOT_ADDR=$LABLE_RESERVE_SIZE
let END_BOOT_ADDR=$LABLE_RESERVE_SIZE+$BOOT_PARTION_SIZE
START_SYSTEM_ADDR=$END_BOOT_ADDR
let END_SYSTEM_ADDR=$START_SYSTEM_ADDR+$SYSTEM_PARTION_SIZE
START_VENDOR_ADDR=$END_SYSTEM_ADDR
let END_VENDOR_ADDR=$START_VENDOR_ADDR+$VENDOR_PARTION_SIZE
START_USERDATA_ADDR=$END_VENDOR_ADDR
let END_USERDATA_ADDR=$START_USERDATA_ADDR+$USERDATA_PARTION_SIZE
let TOTAL_IMAGE_SIZE=$LABLE_RESERVE_SIZE+$BOOT_PARTION_SIZE+$SYSTEM_PARTION_SIZE+$VENDOR_PARTION_SIZE+$USERDATA_PARTION_SIZE

#4.creating empty image
echo -e "${green}1.start creating empty image, please wait......${normal}"
dd if=/dev/zero of=$IMAGE_NAME bs=1MB count=$TOTAL_IMAGE_SIZE

#5.part image with gpt mode
echo -e "${green}2.parting image with gpt mode......${normal}"
parted $IMAGE_NAME --script -- mklabel gpt
parted $IMAGE_NAME --script -- mkpart boot fat32 ${START_BOOT_ADDR}M ${END_BOOT_ADDR}M-1
parted $IMAGE_NAME --script -- mkpart system ext4 ${START_SYSTEM_ADDR}M ${END_SYSTEM_ADDR}M-1
parted $IMAGE_NAME --script -- mkpart vendor ext4 ${START_VENDOR_ADDR}M ${END_VENDOR_ADDR}M-1
parted $IMAGE_NAME --script -- mkpart userdata ext4 ${START_USERDATA_ADDR}M ${END_USERDATA_ADDR}M-1

#6.dd partition images
echo -e "${green}3.start dd partition images......${normal}"
DD_START_BOOT_ADDR=$(convert_1000mib_to_1024mib "$START_BOOT_ADDR")
DD_START_SYSTEM_ADDR=$(convert_1000mib_to_1024mib "$START_SYSTEM_ADDR")
DD_START_VENDOR_ADDR=$(convert_1000mib_to_1024mib "$START_VENDOR_ADDR")
DD_START_USRERDATA_ADDR=$(convert_1000mib_to_1024mib "$START_USERDATA_ADDR")
dd if=${BOOT_IMG} of=${IMAGE_NAME} seek=$DD_START_BOOT_ADDR  bs=1M conv=notrunc
dd if=system.img of=${IMAGE_NAME} seek=$DD_START_SYSTEM_ADDR bs=1M conv=notrunc
dd if=vendor.img of=${IMAGE_NAME} seek=$DD_START_VENDOR_ADDR bs=1M conv=notrunc
dd if=userdata.img of=${IMAGE_NAME} seek=$DD_START_USRERDATA_ADDR bs=1M conv=notrunc

#7.save partion table and burn uboot image
if [ -f $UBOOT_NAME ];then
    echo -e "${green}5.start to burn uboot img......${normal}"
    dd if=${IMAGE_NAME} of=table.bin bs=1 skip=440 count=17410
    dd if=$UBOOT_NAME of=${IMAGE_NAME} conv=notrunc
    dd if=table.bin of=${IMAGE_NAME} bs=1 seek=440 conv=notrunc
    rm table.bin -f
fi

echo -e "${green}generate $IMAGE_NAME successfully!!!!!! ${normal}"
