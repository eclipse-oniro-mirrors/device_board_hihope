# SPDX-License-Identifier: GPL-2.0
# Out-of-tree build; driven by build_mcscan.sh (same flow as mcs_net).
ifneq ($(KERNELRELEASE),)
obj-m := mcs_can.o
else
# Standalone clean: "make clean" in this directory, no kernel tree needed.
clean:
	rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order
	rm -rf .tmp_versions
endif
