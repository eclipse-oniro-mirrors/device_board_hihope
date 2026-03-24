# Copyright (C) 2025 Phytium Technology Co., Ltd.
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

JOB := $(shell cores=$$(nproc); calc_cores=$$(( (cores * 4 + 4) / 5 )); if [ $$calc_cores -lt 8 ]; then echo 8; else echo $$calc_cores; fi)
PRODUCT_NAME=$(TARGET_PRODUCT)
OHOS_BUILD_HOME := $(OUT_DIR)/../../..
KERNEL_SRC_TMP_PATH := $(OUT_DIR)/kernel/${KERNEL_VERSION}
KERNEL_OBJ_TMP_PATH := $(OUT_DIR)/kernel/OBJ/${KERNEL_VERSION}
ifeq ($(BUILD_TYPE), standard)
    KERNEL_SRC_TMP_PATH := $(OUT_DIR)/kernel/src_tmp/${KERNEL_VERSION}
endif

KERNEL_SRC_PATH := $(OHOS_BUILD_HOME)/kernel/linux/${KERNEL_VERSION}
KERNEL_PATCH_PATH := $(OHOS_BUILD_HOME)/kernel/linux/patches/${KERNEL_VERSION}

ifeq ($(DEVICE_NAME), dayu300)
    PHYTIUM_KERNEL_SRC_PATH := $(OHOS_BUILD_HOME)/device/soc/phytium/D3000M/kernel_source
else
    PHYTIUM_KERNEL_SRC_PATH := $(OHOS_BUILD_HOME)/device/soc/phytium/common/kernel_source/${KERNEL_VERSION}
endif
HCK_PATH := $(PHYTIUM_KERNEL_SRC_PATH)/hck

GPU_KERNEL_PATH := $(OHOS_BUILD_HOME)/device/soc/phytium/common/gpu_model/${GPU_MODEL}/kernel_source/${KERNEL_VERSION}

PREBUILTS_GCC_DIR := $(OHOS_BUILD_HOME)/prebuilts/gcc
CLANG_HOST_TOOLCHAIN := $(OHOS_BUILD_HOME)/prebuilts/clang/ohos/linux-x86_64/llvm/bin
KERNEL_HOSTCC := $(CLANG_HOST_TOOLCHAIN)/clang
KERNEL_PREBUILT_MAKE := make
CLANG_CC := $(CLANG_HOST_TOOLCHAIN)/clang

ifeq ($(KERNEL_ARCH), arm)
    KERNEL_TARGET_TOOLCHAIN := $(PREBUILTS_GCC_DIR)/linux-x86/arm/gcc-linaro-7.5.0-arm-linux-gnueabi/bin
    KERNEL_TARGET_TOOLCHAIN_PREFIX := $(KERNEL_TARGET_TOOLCHAIN)/arm-linux-gnueabi-
else ifeq ($(KERNEL_ARCH), arm64)
    KERNEL_TARGET_TOOLCHAIN := $(PREBUILTS_GCC_DIR)/linux-x86/aarch64/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/bin
    KERNEL_TARGET_TOOLCHAIN_PREFIX := $(KERNEL_TARGET_TOOLCHAIN)/aarch64-linux-gnu-
endif


KERNEL_CROSS_COMPILE :=
KERNEL_CROSS_COMPILE += CC="$(CLANG_CC)"
KERNEL_CROSS_COMPILE += CROSS_COMPILE="$(KERNEL_TARGET_TOOLCHAIN_PREFIX)"

KERNEL_MAKE := \
    PATH="$(CLANG_HOST_TOOLCHAIN):$$PATH" \
    $(KERNEL_PREBUILT_MAKE)

KERNEL_IMAGE_FILE := $(KERNEL_SRC_TMP_PATH)/arch/$(KERNEL_ARCH)/boot/$(KERNEL_IMAGE)
DEFCONFIG_FILE := phytium_standard_defconfig

.PHONY: $(KERNEL_IMAGE_FILE)
$(KERNEL_IMAGE_FILE):
	echo "build kernel... "

ifneq ($(wildcard $(KERNEL_IMAGE_FILE)),)
	echo "Image is already exists, use incremental compilation"
else
	$(hide) rm -rf $(KERNEL_SRC_TMP_PATH);mkdir -p $(KERNEL_SRC_TMP_PATH);cp -arfL $(KERNEL_SRC_PATH)/* $(KERNEL_SRC_TMP_PATH)

ifeq ($(DEVICE_NAME), dayu300)
ifeq ($(KERNEL_VERSION), linux-6.6)
	$(hide) $(OHOS_BUILD_HOME)/device/board/hihope/dayu300/build_kernel/copy_and_patch_kernel_source.sh  $(OHOS_BUILD_HOME) $(KERNEL_SRC_TMP_PATH) $(DEVICE_NAME) ${KERNEL_VERSION}
endif
endif

	$(hide) $(OHOS_BUILD_HOME)/device/board/hihope/dayu300/build_kernel/patch_phytium.sh $(PHYTIUM_KERNEL_SRC_PATH)  $(KERNEL_SRC_TMP_PATH) $(GPU_KERNEL_PATH)

	$(hide) $(OHOS_BUILD_HOME)/device/board/hihope/dayu300/build_kernel/merge_hck.sh ${HCK_PATH} ${KERNEL_SRC_TMP_PATH}


	$(hide) $(OHOS_BUILD_HOME)/drivers/hdf_core/adapter/khdf/linux/patch_hdf.sh $(OHOS_BUILD_HOME) $(KERNEL_SRC_TMP_PATH) $(KERNEL_PATCH_PATH) $(DEVICE_NAME)

ifeq ($(KERNEL_VERSION), linux-6.6)
	$(hide) sed -i 's/<stdarg.h>/<linux\/stdarg.h>/' ${KERNEL_SRC_TMP_PATH}/bounds_checking_function/include/securec.h
endif


	$(hide) cp $(PHYTIUM_KERNEL_SRC_PATH)/${DEFCONFIG_FILE}  $(KERNEL_SRC_TMP_PATH)/arch/$(KERNEL_ARCH)/configs/
	$(hide) $(OHOS_BUILD_HOME)/device/board/hihope/dayu300/build_kernel/build_dts.sh $(OHOS_BUILD_HOME)/device/board/hihope/dayu300/$(DEVICE_NAME)/dts \
	       	$(KERNEL_SRC_TMP_PATH)/arch/$(KERNEL_ARCH)/boot/dts/phytium
endif	#ifneq ($(wildcard $(KERNEL_IMAGE_FILE)),)

	$(hide) $(KERNEL_MAKE) -C $(KERNEL_SRC_TMP_PATH) ARCH=$(KERNEL_ARCH) LLVM=1 LLVM_IAS=1 AR=ar $(KERNEL_CROSS_COMPILE) $(DEFCONFIG_FILE)
	$(hide) $(KERNEL_MAKE) -C $(KERNEL_SRC_TMP_PATH) ARCH=$(KERNEL_ARCH) LLVM=1 LLVM_IAS=1 AR=ar $(KERNEL_CROSS_COMPILE) -j$(JOB)
.PHONY: build-kernel
build-kernel: $(KERNEL_IMAGE_FILE)
