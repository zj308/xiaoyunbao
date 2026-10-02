# 先看这里

正式 Arduino 工程在 `C:\XiaoYunBao\Arduino\SmartCompanionS3`，本交接包内的对应副本在 `02_主程序/SmartCompanionS3`。当前源码基线是 v98 手机配网；已编译镜像仍是 v97，换网功能需重新编译烧录。TTS 音色固定为 `qwen-audio-3.0-tts-flash-longtongxuxian`。

完整说明请先阅读上级目录的 `00_项目交接总文档.md`，再按需要查看本目录的迁移、GPIO、电源和服务器说明。工程里已带可编译的 `secrets.local.h`（中转 `47.95.194.77` 和原云端配置）；Wi-Fi 不用改文件，用手机配网。打开 `02_主程序/SmartCompanionS3/SmartCompanionS3.ino` 直接编译烧录即可。设备热点 `XiaoYunBao-XXXX`，密码 `xiaoyunbao`，浏览器打开 `192.168.4.1`。K2/GPIO21 短按切换中英；Wi-Fi 重连是 K1/GPIO18 长按，或串口输入 `w`。
