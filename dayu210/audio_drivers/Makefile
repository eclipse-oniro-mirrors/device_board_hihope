#
# Copyright (C) 2022-2023 HiHope Open Source Organization .
#
# This software is licensed under the terms of the GNU General Public
# License version 2, as published by the Free Software Foundation, and
# may be copied, distributed, and modified under those terms.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
#

KHDF_AUDIO_KHDF_ROOT_DIR = drivers/hdf/khdf
KHDF_FRAMEWORK_ROOT_DIR = drivers/hdf/framework
KHDF_AUDIO_RK3588_INC_DIR = ../../../../device/board/hihope/dayu210/audio_drivers

obj-$(CONFIG_DRIVERS_HDF_AUDIO_RK3588) += \
        accessory/es8323/src/es8323_adapter.o \
        accessory/es8323/src/es8323_impl.o \
        accessory/es8323/src/es8323_linux_driver.o \
        dai/src/rk3588_dai_linux_driver.o \
        dai/src/rk3588_dai_adapter.o \
        dai/src/rk3588_dai_ops.o \
        soc/src/rk3588_dma_adapter.o \
        soc/src/rk3588_dma_ops.o

ccflags-$(CONFIG_DRIVERS_HDF_AUDIO_RK3588) += \
        -I$(srctree)/$(KHDF_AUDIO_KHDF_ROOT_DIR)/osal/include \
        -I$(srctree)/$(KHDF_FRAMEWORK_ROOT_DIR)/include/core \
        -I$(srctree)/$(KHDF_FRAMEWORK_ROOT_DIR)/include/utils \
        -I$(srctree)/$(KHDF_FRAMEWORK_ROOT_DIR)/include/osal \
        -I$(srctree)/drivers/hdf/inner_api/osal/shared \
        -I$(srctree)/drivers/hdf/inner_api/utils \
        -I$(srctree)/drivers/hdf/inner_api/core \
        -I$(srctree)/drivers/hdf/inner_api/host/shared \
        -I$(srctree)/$(KHDF_FRAMEWORK_ROOT_DIR)/include/platform \
        -I$(srctree)/$(KHDF_FRAMEWORK_ROOT_DIR)/include/config \
        -I$(srctree)/$(KHDF_FRAMEWORK_ROOT_DIR)/include/audio \
        -I$(srctree)/$(KHDF_FRAMEWORK_ROOT_DIR)/ability/sbuf/include \
        -I$(srctree)/$(KHDF_FRAMEWORK_ROOT_DIR)/core/common/include/host \
        -I$(srctree)/$(KHDF_FRAMEWORK_ROOT_DIR)/core/host/include \
        -I$(srctree)/$(KHDF_FRAMEWORK_ROOT_DIR)/model/audio/core/include \
        -I$(srctree)/$(KHDF_FRAMEWORK_ROOT_DIR)/model/audio/sapm/include \
        -I$(srctree)/$(KHDF_FRAMEWORK_ROOT_DIR)/model/audio/dispatch/include \
        -I$(srctree)/$(KHDF_FRAMEWORK_ROOT_DIR)/model/audio/common/include \
        -I$(srctree)/bounds_checking_function/include \
        -I$(srctree)/sound/soc/rockchip \
        -I$(srctree)/$(KHDF_AUDIO_RK3588_INC_DIR)/soc/include \
        -I$(srctree)/$(KHDF_AUDIO_RK3588_INC_DIR)/dai/include \
        -I$(srctree)/$(KHDF_AUDIO_RK3588_INC_DIR)/dsp/include \
        -I$(srctree)/$(KHDF_AUDIO_RK3588_INC_DIR)/accessory/es8323/include \
        -I$(srctree)/$(KHDF_AUDIO_RK3588_INC_DIR)/include
