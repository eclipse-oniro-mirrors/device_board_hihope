# NearLink_DK_WS63星闪开发板


## 介绍
NearLink_DK_WS63采用海思星闪WS63的解决方案，具备对802.11b/g/n/ax无线通信协议的支持；支持SLE1.0协议及SLE网关功能；可基于OpenHarmony轻量系统开发物联网场景功能，是物联网智能终端领域的理想选择。
NearLink_DK_WS63具有以下特点：
稳定、可靠的通信能力
支持复杂环境下 TPC、自动速率、弱干扰免疫等可靠性通信算法
灵活的组网能力
完善的网络支持
支持 IPv4/IPv6 网络功能
支持 DHCPv4/DHCPv6 Client/Server
支持 DNS Client 功能
支持 mDNS 功能
支持 CoAP/MQTT/HTTP/JSON 基础组件
强大的安全引擎
硬件实现 AES128/256 加解密算法
硬件实现 HASH-SHA256、HMAC_SHA256 算法
硬件实现 RSA、ECC 签名校验算法
硬件实现真随机数生成，满足 FIPS140-2 随机测试标准
硬件支持 TLS/DTLS 加速
硬件支持国密算法 SM2、SM3、SM4
内部集成 EFUSE，支持安全存储、安全启动、硬件 ID
内部集成 MPU 特性，支持内存隔离特性
开放的操作系统
开放操作系统 OpenHarmony，提供开放、高效、安全的系统开发、运行环境
丰富的低功耗、小内存、高稳定性、高实时性机制
灵活的协议支撑和扩展能力
二次开发接口
多层级开发接口：操作系统适配接口和系统诊断接口、链路层接口、网络层接口

图 1-1 NearLink_DK_WS63 星闪开发板
![开发板正面.jpg](doc/figures/%E5%BC%80%E5%8F%91%E6%9D%BF%E6%AD%A3%E9%9D%A2.jpg)

图2 NearLink_DK_WS63 星闪开发板背面外观图
![开发板背面.png](doc/figures/%E5%BC%80%E5%8F%91%E6%9D%BF%E8%83%8C%E9%9D%A2.png)




## 规格
Wi-Fi

1×1 2.4GHz 频段（ch1～ch14）
PHY支持 IEEE 802.11b/g/n/ax MAC 支持 IEEE 802.11d/e/i/k/v/w
支持 802.11n 20MHz/40MHz 频宽，支持 802.11ax 20MHz频宽
支持最大速率：150Mbps@HT40 MCS7， 114.7Mbps@HE20 MCS9
内置 PA 和 LNA，集成 TX/RX Switch、Balun 等
支持 STA 和 AP 形态，作为 AP 时最大支持 6 个 STA 接入
支持 A-MPDU、A-MSDU
支持 Block-ACK
支持 QoS，满足不同业务服务质量需求
支持 WPA/WPA2/WPA3 personal、WPS2.0
支持 RF 自校准方案
支持 STBC 和 LDPC


星闪

星闪低功耗接入技术 Sparklink Low Energy（SLE）
支持 SLE 1.0
支持 SLE 1MHz/2MHz/4MHz，最大空口速率 12Mbps
支持 Polar 信道编码
支持 SLE 网关


CPU 子系统

高性能 32bit 微处理器，最大工作频率 240MHz
内嵌 SRAM 606KB、ROM 300KB
内嵌 4MB Flash


外围接口

1 个 SPI 接口、1 个 QSPI 接口、2 个 I2C 接口、1 个 I2S 接口、3 个 UART 接口、19 个 GPIO 接口、6 路 ADC 输入、8 路 PWM（注：上述接口通过复用实现）
外部晶体时钟频率 24MHz、40MHz

Software

Wi-Fi 模式 STA, Soft-AP and sniffer modes
安全机制  WPS / WEP / WPA / WPA2 / WPA3
加密类型  UART Download
软件开发  SDK
网络协议  IPv4, TCP/UDP/HTTP/FTP/MQTT


## 固件烧录

（1）获取源码

```
repo init -u https://gitee.com/openharmony/manifest.git -b master --no-repo-verify
repo sync -c
repo forall -c 'git lfs pull'
```

（2）构建工具下载 ./build/prebuilts_download.sh

（3）安装编译工具 sudo apt install cmake

（4）安装编译工具 python3 -m pip install --user build/hb

（5）设置编译工具链环境变量,当前源码所在路径可用pwd命令查看

```
vim ~/.bashrc
  export PATH=/{当前源码所在路径}/device/soc/hisilicon/ws63v100/sdk/tools/bin/compiler/riscv/cc_riscv32_musl_b090/cc_riscv32_musl/bin:$PATH
  export PATH=~/.local/bin:$PATH
  source ~/.bashrc
```
执行 riscv32-linux-musl-gcc -v检查编译工具链是否配置成功

（6）编译
在源码目录下执行hb set,选择mini,选择对应开发板 选定`nearlink_dk_3863`之后执行命令


执行hb build -f编译,等待编译成功


如需进行XTS功能测试，则添加如下参数，选定`nearlink_dk_3863_xts`之后执行命令

```
hb build -f --gn-args="build_xts=true"
```


(7) 连接烧录


下载BurnTool烧录工具并解压

打开烧录工具，选择对应的串口,打开烧录工具,点开Option选项,选择对应的芯片,WS63E与WS63属于同一款系列，芯片选择WS63即可。

选择烧录文件,示例路径为：\\wsl.localhost\Ubuntu-22.04\home\yoiannis\OpenHarmony\sdk_temp\out\nearlink_dk_3863\nearlink_dk_3863\ws63-liteos-app


勾选Auto Burn和Auto disconnect选项,点击connect连接,单按开发板RST按键开始烧录。


烧录完成之后按下开发板reset按键



打开串口工具,波特率选择115200,上电后可以看到相关的串口打印。

