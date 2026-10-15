# SPDX-License-Identifier: GPL-2.0
# Out-of-tree build; driven by build_mcsnet.sh (same flow as mcs_km).
ifneq ($(KERNELRELEASE),)
obj-m := mcs_net.o
else
# Standalone clean: "make clean" in this directory, no kernel tree needed.
# (kbuild's own clean, "make -C $SRC O=$OBJ M=$PWD clean", also works but
# requires the built kernel tree.)
clean:
	rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order
	rm -rf .tmp_versions
endif
