# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2026 Huawei Device Co., Ltd.
# Out-of-tree build; driven by build_mcsuart.sh (same flow as mcs_can).
ifneq ($(KERNELRELEASE),)
obj-m := mcs_uart.o
else
# Standalone clean: "make clean" in this directory, no kernel tree needed.
clean:
	rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order
	rm -rf .tmp_versions
endif
