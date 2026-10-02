# 小云宝

智能对话加英语对话终端。跑在 ESP32-S3 上：麦克风听你说话，屏幕显示对话，喇叭播报回答。默认为中文智能对话；按语言键切换到英语对话。

## 仓库内容

| 目录 | 内容 |
| --- | --- |
| `02_主程序/SmartCompanionS3` | 设备固件 |
| `03_摄像头` | 摄像头测试程序 |
| `04_服务器/relay-server` | 中转服务。设备把声音发到这里，这里再调用识别、对话和语音合成 |

## 密钥

API Key 和服务器口令不在仓库里。

1. 复制 `02_主程序/SmartCompanionS3/secrets.local.example.h` 为 `secrets.local.h`，填入你自己的百炼 API Key、中转地址和口令。
2. 中转服务优先读取环境变量 `XIYUNBAO_DASHSCOPE_API_KEY`。

## 编译烧录

用 Arduino IDE 打开 `02_主程序/SmartCompanionS3/SmartCompanionS3.ino`。

- 开发板：ESP32S3 Dev Module
- PSRAM：OPI PSRAM
- Flash Size：16MB
- Partition Scheme：16M Flash（3MB APP / 9.9MB FATFS）
- USB CDC On Boot：Enabled

烧录前拔掉摄像头。也可以在工程目录执行：

```text
arduino-cli compile --profile xiaoyunbao 02_主程序/SmartCompanionS3
```

## 使用

长按 K1 配网。手机连接热点 `XiaoYunBao-XXXX`，密码 `xiaoyunbao`，浏览器打开 `192.168.4.1` 保存 Wi-Fi。

短按 K1 开始智能对话。短按 K2 在中文智能对话和英语对话之间切换。

## 许可证

MIT，见 `LICENSE`。
