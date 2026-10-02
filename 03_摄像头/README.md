# OV2640 独立测试工程

工程文件：`ov2640_standalone_test.ino`（文件名必须与文件夹同名，Arduino IDE 才能作为项目打开）

## Arduino IDE 设置

- 开发板：与原项目相同的 `ESP32S3 Dev Module`
- Flash：16MB
- PSRAM：`OPI PSRAM`
- USB CDC：`Hardware CDC and JTAG`
- 串口：115200

## 测试范围

程序只验证 OV2640 是否能初始化、读取传感器 ID 和连续抓取 QVGA JPEG；不连接 Wi-Fi、服务器、屏幕、TF 卡、麦克风或扬声器。

## 默认接线

```text
D0..D7 = GPIO16,17,38,39,40,41,42,43
PCLK   = GPIO44
VS     = GPIO47
HREF   = GPIO46（GPIO48 是开发板板载 RGB 灯数据脚，不能接 HREF）
SDA    = GPIO1
SCL    = GPIO2
XCLK   = 默认不接（`CAM_XCLK=-1`，假定 V1.5 板载晶振）；若确认有 MCLK 引出，再改为 GPIO17 并接 MCLK
RST    = 3.3V（模块 RESET，低有效）
PWDN   = GND（模块 PWDN，高有效）
3V3/GND 接开发板 3.3V/GND
```

按你照片中 BYDZ 18 针排针的实际顺序，逐针连接如下（从照片上排最左开始，再接下排最左）：

| 模块标注 | ESP32-S3 | 说明 |
|---|---:|---|
| GND | GND | 地 |
| SCL | GPIO2 | SCCB 时钟 |
| SDA | GPIO1 | SCCB 数据 |
| D0 | GPIO16 | 8 位数据最低位 |
| D2 | GPIO38 | 数据线 |
| D4 | GPIO40 | 数据线 |
| D6 | GPIO42 | 数据线 |
| PCLK | GPIO44 | 像素时钟输出 |
| PWDN | GND | 高有效，未使用时拉低 |
| 3V3 | 3.3V | 只接 3.3V |
| VS | GPIO47 | VSYNC |
| HREF | GPIO46 | 行同步；GPIO46 为输入脚，独立测试使用 |
| RST | 3.3V | RESETB 低有效，未使用时拉高 |
| D1 | GPIO17 | 8 位数据 |
| D3 | GPIO39 | 8 位数据 |
| D5 | GPIO41 | 8 位数据 |
| D7 | GPIO43 | 8 位数据最高位 |
| FLASH | 不接 | 先不控制补光灯 |

照片上排针标签只显示 D0、D2、D4、D6，下排显示 D1、D3、D5、D7；这是 8 位并口的交错排针，不是缺少 D1/D3 等信号。

你提供的 24 针传感器图中，`#13 MCLK` 是输入时钟，`#6 RESET`、`#8 PWDN` 为控制脚；BYDZ 转接板把传感器的 8 位输出（D9:D2）标成 D0~D7。转接板照片没有引出 MCLK，默认按板载晶振版本测试；请检查背面是否有晶振。**不能把 `PCLK` 当作 `MCLK`。** 若背面没有晶振且 V1.5 也没有 MCLK 引脚，就不能用这块转接板直接采集，需要适配板或卖家提供时钟引出方式。

本测试使用 GPIO1、2、16、17、38~44、46、47；GPIO48 是开发板板载 RGB 灯数据脚，已明确保留不用。独立测试时仍建议断开所有原项目外设。

## 使用方法

1. 独立测试时断开原小云宝的 TFT、TF 卡、INMP441、MAX98357A 和两个亮度按键；本测试使用摄像头 GPIO：1、2、16、17、38、39、40、41、42、43、44、47、48。
2. 打开本目录中的 `.ino` 文件并上传。
3. 打开串口监视器，波特率选择 `115200`。
4. 每 2 秒自动抓一帧；输入 `c` 手动抓帧，输入 `p` 切换 QQVGA/RGB565 原始模式，输入 `t` 采样 PCLK/VS/HREF，输入 `r` 重新初始化。先不要反复输入 `r`，因为驱动重新初始化失败是次生现象。

成功示例：

```text
摄像头传感器 ID: PID=0x26 ...
摄像头状态: READY
抓帧成功 #1: 320x240, xxxx 字节, 格式=4
```

失败时先检查 3.3V、GND、MCLK/XCLK、SCCB 的 SDA/SCL，以及 D0~D7 是否按顺序接线。资料建议 SCCB 上拉约 4.7K；多数转接板已自带上拉，若没有，SDA/SCL 各加 4.7K 到 3.3V。
