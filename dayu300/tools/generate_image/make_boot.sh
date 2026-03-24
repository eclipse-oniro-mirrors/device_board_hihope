#!/bin/bash
# Copyright (c) 2024 Phytium Technology Co., Ltd.
# All rights reserved.
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

#set color defination in echo info
green="\e[32;1m"
red="\e[31;1m"
normal="\e[0m"

OUT_PATH=${1}
FORMATE=efi
IMG_NAME=${OUT_PATH}/boot.img
if [ $# -eq 2 ] &&  [ "uboot" = $2 ]; then
    FORMATE=uboot
    IMG_NAME=${OUT_PATH}/boot_uboot.img
fi
BOOT_PATH=${OUT_PATH}/boot/

IMAGE_SIZE=128  # 128M
IMAGE_BLOCKS=4096
FILE_CHECK="fail"

echo  -e "${green}>>>>> making ${FORMATE} boot image...${normal}"
echo  -e "${green}src path:${BOOT_PATH}, dis image name:${IMG_NAME}, size:${IMAGE_SIZE}M, block size:${IMAGE_BLOCKS}${normal}"

if [ -e ${OUT_PATH}/ramdisk.img ]; then
    cp ${OUT_PATH}/ramdisk.img ${BOOT_PATH}
fi

if [ -e ${OUT_PATH}/updater.img ]; then
    cp ${OUT_PATH}/updater.img ${BOOT_PATH}
fi

function make_uboot_boot_image()
{
    src=$1
    dis=$2
    size=$3
    block_size=$4
    block_num=$((${size} * 1024 * 1024 / ${block_size}))

    if [ "`uname -m`" == "aarch64" ]; then
        echo y | sudo mke2fs -b ${block_size} -d ${src} -i 8192 -t ext2 ${dis} ${block_num}
    else
        genext2fs -B ${block_size} -b ${block_num} -d ${src} -i 8192 -U ${dis}
    fi

    return $?
}

function make_efi_boot_image()
{
    src=$1
    dis=$2
    size=$3"M"

    VOLUME_LABEL=boot
    # create void file
    truncate -s ${size} ${dis}

    # format the file as vfat
    mkfs.vfat -n $VOLUME_LABEL -F 32 ${dis}

    # create mount point
    if [ -d "vfat_mount_point" ];then
        if mountpoint -q vfat_mount_point; then
            sudo umount vfat_mount_point
        fi
	rm vfat_mount_point/* -rf
    else
        mkdir -p vfat_mount_point
    fi

    # mount void file to mount point
    sudo mount -o loop ${dis} vfat_mount_point
    #sudo mount  ${dis} vfat_mount_point

    # copy file to mount point
    sudo cp ${src}/* vfat_mount_point/ -r

    # umount mount point
    sudo umount vfat_mount_point
    rm vfat_mount_point -rf

    return $?
}

function make_efi_boot_image_mtools()
{
    src=$1
    dis=$2
    size=$3"M"

    VOLUME_LABEL=boot

    # create void img file
    truncate -s ${size} ${dis}

    # format img by mformat from mtools
    mformat -i ${dis} -v ${VOLUME_LABEL} -F ::

    # copy files to img by mcopy from mtools
    # -/ : recursion, like cp -r
    mcopy -/ -i ${dis} $src/* ::/

    return $?
}

function check_boot_files()
{
    src=$1
    formate=$2
    check_Image="Image "
    check_ramdisk="ramdisk.img "
    check_efi="EFI "
    #check_dtb="dtb "
    check_dtb=""
    check_updater="updater.img "

    for FILE in `ls ${src}`
    do
        if [[ $FILE = "Image" ]]; then
            check_Image=""
        elif [[ $FILE = "ramdisk.img" ]]; then
            check_ramdisk=""
        elif [[ $FILE =~ ".dtb" ]]; then
            check_dtb=""
        elif  [[ $FILE = "EFI" ]]; then
            check_efi=""
        elif [[ $FILE = "updater.img" ]]; then
            check_updater=""
        fi
    done

    if [ -z $check_Image ]&&[ -z $check_ramdisk ]&&[ -z $check_dtb ]&&[ -z $check_efi ]&&[ -z $check_updater ]; then
        FILE_CHECK="sucess"
    else
        echo -e "${red}check files in ${BOOT_PATH} failed,${check_Image}${check_ramdisk}${check_dtb}${check_efi}${check_updater}is needed.${normal}"
        echo "ls ${src}:["`ls ${src}`"]"
    fi
}

check_boot_files ${BOOT_PATH} ${FORMATE}
if [ ${FILE_CHECK} == "fail" ]; then
    echo -e "${red}<<<<< make ${FORMATE} boot image failed.${normal}"
else
    if [ "efi" = ${FORMATE} ];then
        #make_efi_boot_image ${BOOT_PATH} ${IMG_NAME} ${IMAGE_SIZE}

        ## make efi boot img by mtools, no need sudo
        ## sudo apt-get install mtools
        make_efi_boot_image_mtools ${BOOT_PATH} ${IMG_NAME} ${IMAGE_SIZE}
    else
        make_uboot_boot_image ${BOOT_PATH} ${IMG_NAME} ${IMAGE_SIZE} ${IMAGE_BLOCKS}
    fi
    ret=$?
    if [ $? == 0 ]; then
        echo -e "${green}<<<<< make ${FORMATE} boot image success.${normal}"
    else
        echo -e "${red}<<<<< make ${FORMATE} boot image failed, error code:$ret.${normal}"
    fi
fi
