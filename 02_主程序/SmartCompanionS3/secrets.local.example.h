#pragma once
// 复制为 secrets.local.h 后填写。不要把真实密钥提交到公共仓库。
// Wi-Fi 名称和密码不再需要写死。上电后用手机连接设备热点 XiaoYunBao-XXXX
// （密码 xiaoyunbao），打开 http://192.168.4.1 选择家里 Wi-Fi 或填写热点。
// 下面两项可留空；仅在尚未手机配网时作为一次性回退。
#define XIYUNBAO_WIFI_SSID ""
#define XIYUNBAO_WIFI_PASSWORD ""
#define XIYUNBAO_DASHSCOPE_API_KEY "你的阿里云百炼API Key"
#define XIYUNBAO_WORKSPACE_ID "你的Workspace ID"
#define XIYUNBAO_COSYVOICE_VOICE_ID "qwen-audio-3.0-tts-flash-longtongxuxian"
#define XIYUNBAO_ASR_HOST "你的ASR中转地址"
#define XIYUNBAO_ASR_PATH "/asr"
#define XIYUNBAO_TTS_HOST "你的TTS中转地址"
#define XIYUNBAO_TTS_PATH "/tts"
#define XIYUNBAO_QWEN_MODEL "qwen-turbo"
#define XIYUNBAO_ASR_MODEL "qwen-audio-3.0-asr-flash-streaming"
#define XIYUNBAO_TTS_MODEL "qwen-audio-3.0-tts-flash"
#define XIYUNBAO_RELAY_HOST "电脑局域网IP"
#define XIYUNBAO_RELAY_PORT 8000
#define XIYUNBAO_RELAY_PATH "/xiyunbao/v1/?secret=换成你自己的口令"
#define XIYUNBAO_RELAY_SECRET "换成你自己的口令"
