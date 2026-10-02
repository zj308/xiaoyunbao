# Arduino IDE 编译烧录速查

打开 `02_主程序/SmartCompanionS3/SmartCompanionS3.ino`。IDE 使用 Arduino 1.8.19 或 2.x，ESP32 core 3.3.11。**开发板必须选 ESP32S3 Dev Module**，不要选 Ozobot DRV Kit。选项：OPI PSRAM，QIO 80 MHz，16 MB Flash，3 MB APP/9.9 MB FATFS，240 MHz，Hardware CDC and JTAG，USB CDC On Boot Enabled，921600。`secrets.local.h` 已在工程内，直接编译即可。安装 Adafruit GFX/ST7789、U8g2、U8g2_for_Adafruit_GFX 及工程依赖。选择正确 COM 口后上传。无法连接时按住 BOOT，点上传，出现 Connecting 后松开，再按 EN。源码必须打开 `.ino`，`.bin` 只能用 esptool 烧录。若出现 `read ECONNRESET`，先改对开发板，关掉 IDE 再编译一次。

烧录后打开 115200 串口，确认版本为 v97、TF 卡容量、UI 显示完成、WebSocket 握手和 TTS 音色。上传 UI 时把 `SmartCompanionS3/data/ui` 内容复制到 TF 卡根目录 `/ui`。
