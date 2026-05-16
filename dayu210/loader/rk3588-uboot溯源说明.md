OpenHarmony 中device_board_hihope仓dayu210/loader/uboot.img 溯源和编译说明
====================================================

1. 获取uboot源码，请参考以下网址：
https://gitee.com/hihope-rk3588/uboot_rk3588

2. 构建指导
------------------

```
# 获取代码
git clone https://gitee.com/hihope-rk3588/uboot_rk3588.git

OpenHarmaony源码根目录创建uboot目录
ge_nan@ubuntu:~/work/hos_mayun.1.4$ mkdir uboot/
移动并改名获取到的uboot-rk3588源码
ge_nan@ubuntu:~/work/hos_mayun.1.4/uboot$ mv ../uboot-rk3588 rk3588

# 编译rk3588-uboot
修改device/board/hihope/dayu210/BUILD.gn 增加编译项，如下
group("dayu210_group") {
  deps = [
    "cfg:init_configs",
    "distributedhardware:distributedhardware",
    "kernel:kernel",
    "uboot:uboot",  /*新增加编译条目*/
    "updater:updater_files",
    "//device/soc/rockchip/rk3588/hardware:hardware_group",
  ]

编译整个工程或单独编译该模块：
./build.sh --product-name dayu210

out目录下获取新编译的uboot.img
out/uboot/src_tmp/uboot.img
```



----------------


