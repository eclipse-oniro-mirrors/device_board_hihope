# device_board_hihope_dayu300

## 简介

### dayu300开发板简介
润和软件OpenHarmony 智能硬件大禹系列 采用飞腾腾锐d3000m芯片，支持OpenHarmony 标准系统及各类SDK，核心板板载AI智能芯片，性能强劲、接口丰富，
，dayu300，板载已测试的外设如下清单所示：
|外设|支持状况|
|-------|-----------|
|USB Host HID|YES|
|USB OTG HDC|无硬件接口，未测试|
|SD控制器|YES|
|以太网接口测试|YES|
|WiFi|无硬件，未测试|
|CAN|YES|
|UART|YES|
|I2C|YES|
|GPIO|YES|



## 目录

```

device/board/hihope/dayu300
		│   ├── audio_alsa
		│   ├── BUILD.gn
		│   ├── build_kernel
		│   ├── cfg
		│   ├── device.gni
		│   ├── distributedhardware
		│   ├── dts
		│   ├── hap
		│   ├── libc
		│   ├── LICENSE
		│   ├── loader
		│   ├── ohos.build
		│   ├── README.md
		│   ├── tools
		│   ├── updater
		│   └── wifi


## 相关仓

* [vendor/hihope]（..dayu300）
* [device/soc/phytium](../device_soc_phytium)
