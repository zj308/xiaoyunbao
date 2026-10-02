# 小云宝

桌面上的校园陪伴。屏幕里是云朵小人「云伴」：平时看表情、在画面上给出关怀；按一下唤醒键后用中文聊天；再按语言键可以换成英语口语练习。

当前固件版本写在 `02_主程序/SmartCompanionS3/SmartCompanionS3.ino` 的 `XIYUNBAO_FIRMWARE_VERSION`。

成品怎么用、元件和接线、程序怎么连在一起，见 `01_快速开始/小云宝完整使用手册.pdf`。

## 仓库里有什么

| 目录 | 内容 |
| --- | --- |
| `02_主程序/SmartCompanionS3` | ESP32-S3 固件 |
| `04_服务器/relay-server` | 中转服务。设备把声音和照片发到这里，这里再调用识别、对话和语音合成 |
| `01_快速开始` | 使用手册、引脚、电源和编译说明 |
| `05_硬件与EDA` | 自画 PCB 工程 |

## 不会放进仓库的内容

百炼 API Key、中转口令、服务器登录密码只留在你自己的电脑上。

| 本机文件 | 仓库里的示例 |
| --- | --- |
| `02_主程序/SmartCompanionS3/secrets.local.h` | `secrets.local.example.h` |
| `04_服务器/relay-server/vision_monitor.local.json` | `vision_monitor.example.json` |

编译出来的 `build/` 和 `.bin` 也不上传。固件编进去之后会带上 `secrets.local.h` 里的地址和口令。

## 自己做一台

### 1. 硬件

主控是 ESP32-S3 N16R8。屏幕 ST7789，摄像头 OV2640，麦克风 INMP441，功放 MAX98357A，存储用 TF 卡。侧面开关、充电和升压接在自画 PCB 上，外面有外壳。引脚表在 `01_快速开始/02_当前引脚与接口.md`，以固件里的宏为准。

### 2. 填写密钥

1. 把 `secrets.local.example.h` 复制成同目录的 `secrets.local.h`。
2. 填入你自己的百炼 API Key、Workspace、中转地址和口令。
3. 中转服务读取环境变量 `XIYUNBAO_DASHSCOPE_API_KEY`，没有环境变量时再读这份 `secrets.local.h`。

`secrets.local.h` 已经被 `.gitignore` 排除。

### 3. 编译并烧录固件

用 Arduino IDE 打开 `02_主程序/SmartCompanionS3/SmartCompanionS3.ino`。

- 开发板：ESP32S3 Dev Module
- PSRAM：OPI PSRAM
- Flash Size：16MB
- Partition Scheme：16M Flash（3MB APP / 9.9MB FATFS）
- USB CDC On Boot：Enabled

烧录前拔掉摄像头，它会挡住开发板的 Type-C。侧面开关可以关掉，只靠 USB 供电。

也可以在工程目录用 Arduino CLI：

```text
arduino-cli compile --profile xiaoyunbao 02_主程序/SmartCompanionS3
```

`sketch.yaml` 里的 profile 已经写好上述板型选项。

### 4. 运行中转

在 `04_服务器/relay-server` 安装依赖后启动服务。设备通过 Wi-Fi 连到 `secrets.local.h` 里的主机和端口。一条 WebSocket 上传 16 kHz 录音和摄像头 JPEG，接收识别文字和 24 kHz Opus 语音。

识别、对话和语音合成在中转后面的阿里云百炼完成。模型名写在 `secrets.local.h`：

- 识别：`qwen-audio-3.0-asr-flash-streaming`
- 对话：`qwen-turbo`
- 语音：`qwen-audio-3.0-tts-flash`

### 5. 配网

固件烧好后，长按 K1。手机连接热点 `XiaoYunBao-XXXX`，密码 `xiaoyunbao`，浏览器打开 `192.168.4.1`，保存要使用的 Wi-Fi。

## 许可证

代码以 MIT 许可证发布，见 `LICENSE`。
