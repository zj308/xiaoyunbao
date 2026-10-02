/* 小云宝 ESP32-S3：百炼实时 ASR -> 千问流式对话 -> CosyVoice-TTS -> MAX98357A。 */
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <driver/i2s.h>
#include <mbedtls/base64.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <opus.h>
#include "driver/gpio.h"
// Qwen3-TTS 的 Base64 音频 JSON 帧通常超过 WebSockets 2.7.3 默认的 15 KB。
#define WEBSOCKETS_MAX_DATA_SIZE (64 * 1024)
#include <WebSocketsClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <TJpg_Decoder.h>
#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.local.example.h"
#endif

#define PIN_I2S_BCLK 4
#define PIN_I2S_WS 5
#define PIN_I2S_MIC_DIN 6
#define PIN_I2S_SPK_DOUT 7
#define PIN_TFT_RST 8
#define PIN_TFT_DC 9
#define PIN_TFT_CS 10
#define PIN_SPI_MOSI 11
#define PIN_SPI_SCK 12
#define PIN_SPI_MISO 13
#define PIN_TFT_BL 14
#define PIN_SD_CS 15
// 四个外接实体按键均使用内部上拉，按下时接通 GND；不再使用 BOOT/GPIO0。
// K1=GPIO18 短按唤醒/打断，长按重连 Wi-Fi。
// K2=GPIO21 短按中英切换，长按静音。
// K3=GPIO3  短按亮度-，长按音量-（按住连减）。
// K4=GPIO45 短按亮度+，长按音量+（按住连加）；上电、复位、烧录时不要按住 GPIO45。
#define PIN_BUTTON_K1 18
#define PIN_BUTTON_K2 21
#define PIN_BUTTON_K3 3
#define PIN_BUTTON_K4 45
#define PIN_WAKE_BUTTON PIN_BUTTON_K1
#define BUTTON_DEBOUNCE_MS 25
#define BUTTON_LOCKOUT_MS 100
#define BUTTON_LONG_PRESS_MS 1200
#define BUTTON_REPEAT_MS 280
#define BUTTON_TOAST_MS 1800
#define AUX_BUTTON_DEBOUNCE_MS BUTTON_DEBOUNCE_MS
#define WAKE_BUTTON_LONG_PRESS_MS BUTTON_LONG_PRESS_MS
// GPIO14 直接连接屏幕 BL，通过 LEDC 输出高频 PWM；BL 不连接按键。
#define TFT_BACKLIGHT_PWM_FREQUENCY 40000
#define TFT_BACKLIGHT_PWM_CHANNEL 5
#define TFT_BACKLIGHT_PWM_RESOLUTION 8
#define TFT_BACKLIGHT_INITIAL_PERCENT 70
#define TFT_BACKLIGHT_STEP_PERCENT 10
#define TFT_BACKLIGHT_MINIMUM_PERCENT 10
#define TFT_BACKLIGHT_MAXIMUM_PERCENT 100
#define SPEAKER_VOLUME_INITIAL_PERCENT 30
#define SPEAKER_VOLUME_STEP_PERCENT 10
#define SPEAKER_VOLUME_MINIMUM_PERCENT 10
#define SPEAKER_VOLUME_MAXIMUM_PERCENT 100
#define MIC_SAMPLE_RATE 16000
#define SPEAKER_SAMPLE_RATE 24000
#define RECORD_TIME_MS 10000
#define ASR_TEXT_TAIL_MS 700
#define ASR_TEXT_TAIL_EN_MS 700
#define ASR_SENTENCE_CONFIRM_MS 280
#define ASR_SENTENCE_CONFIRM_EN_MS 650
#define RECORD_TIME_EN_MS 12000
// INMP441 只使用接线选择的左声道，避免在左右声道之间按瞬时噪声跳换。
#define MIC_USE_RIGHT_CHANNEL false
// 先滤除麦克风直流偏置，再用噪声门抑制持续环境底噪。
#define MIC_DC_TRACK_SHIFT 8
#define MIC_NOISE_TRACK_SHIFT 8
#define MIC_NOISE_GATE_MULTIPLIER 2
#define MIC_NOISE_GATE_OFFSET 80
#define MIC_NOISE_GATE_MINIMUM 90
#define MIC_NOISE_GATE_MINIMUM_EN 90
#define MIC_GATE_HOLD_MS 500
#define MIC_GATE_HOLD_EN_MS 700
// 略提高人声幅度，避免轻辅音被云端听成别的词。
#define MIC_PCM_GAIN 2
// 低延迟连续播放版本：整条回复只建立一次 TTS 会话，安全缓冲后立即开播。
#define XIYUNBAO_FIRMWARE_VERSION "2026-09-25-companion-v139"
// 云端到 ESP32 使用 24 kHz 单声道 Opus，每包固定承载 40 ms 音频。
#define RELAY_OPUS_SAMPLE_RATE 24000
#define RELAY_OPUS_CHANNELS 1
#define RELAY_OPUS_FRAME_MS 40
#define RELAY_OPUS_FRAME_SAMPLES (RELAY_OPUS_SAMPLE_RATE * RELAY_OPUS_FRAME_MS / 1000)
#define RELAY_OPUS_HEADER_BYTES 12
#define RELAY_OPUS_PROTOCOL_VERSION 1
// WebSocket 回调只复制压缩包；Opus 在独立大栈任务中解码，避免压坏 loopTask 栈。
#define RELAY_OPUS_PACKET_QUEUE_LENGTH 128
#define RELAY_OPUS_DECODER_STACK_BYTES 32768
#define RELAY_OPUS_ITEM_START 1
#define RELAY_OPUS_ITEM_DATA 2
#define RELAY_OPUS_ITEM_END 3
// 队列使用 PSRAM 保存 TTS PCM；256 个包足以覆盖网络短暂抖动。
#define TTS_AUDIO_QUEUE_LENGTH 256
// 流式长回复首段缓存 1.6 秒，兼顾首声延迟和云端音频突发到达时的连续性。
#define TTS_START_BUFFER_MS 1600
#define TTS_START_BUFFER_BYTES (SPEAKER_SAMPLE_RATE * 2 * TTS_START_BUFFER_MS / 1000)
// 短回复保持较低起播延迟；字符数包含标点，仍按 UTF-8 字符而不是字节判断。
#define TTS_SHORT_REPLY_BUFFER_MS 280
#define TTS_SHORT_REPLY_BUFFER_BYTES (SPEAKER_SAMPLE_RATE * 2 * TTS_SHORT_REPLY_BUFFER_MS / 1000)
#define TTS_SHORT_REPLY_CHARACTER_LIMIT 18
// 播放中真正断粮后先恢复约 0.65 秒储备，再连续播放，避免几十次停一下、播一下。
#define TTS_REBUFFER_MS 650
#define TTS_REBUFFER_BYTES (SPEAKER_SAMPLE_RATE * 2 * TTS_REBUFFER_MS / 1000)
// 千问最多回复五句，环形队列留出额外余量以兼容短句和换行切分。
#define TTS_TEXT_QUEUE_LENGTH 16
#define TTS_INACTIVITY_TIMEOUT_MS 30000
// 留出数字余量，避免云端满幅 PCM 经 MAX98357A 放大后削顶失真。
#define TTS_VOLUME_PERCENT 32
#define TTS_FADE_IN_SAMPLES (SPEAKER_SAMPLE_RATE * 8 / 1000)
#define TTS_FADE_OUT_SAMPLES (SPEAKER_SAMPLE_RATE * 20 / 1000)
// 末尾补充的零电平时长，给 MAX98357A 一个稳定的静音收尾。
#define TTS_TAIL_SILENCE_MS 40
// 末尾写入完成后等待 DMA 排空的时间，必须覆盖全部 DMA 缓冲区。
#define TTS_DMA_DRAIN_WAIT_MS 180
// 测试音使用 440 Hz 标准音高，便于用耳朵判断是否存在失真或杂音。
#define SPEAKER_TEST_TONE_FREQUENCY_HZ 440
// 测试音幅度低于满幅，避免功放或喇叭在测试时削顶。
#define SPEAKER_TEST_TONE_AMPLITUDE 12000
// 每个测试块固定写入 256 个立体声采样帧。
#define SPEAKER_TEST_TONE_FRAMES_PER_BLOCK 256
// 测试音播放 94 个块，实际时长约为 1 秒。
#define SPEAKER_TEST_TONE_BLOCK_COUNT 94
// 测试音也使用 20 ms 淡入淡出，避免测试本身造成尾部爆音。
#define SPEAKER_TEST_TONE_FADE_SAMPLES (SPEAKER_SAMPLE_RATE * 20 / 1000)
// 聊天记录区域尽量铺满屏幕宽度，让一行文字接近屏幕边缘再换行。
#define CHAT_TEXT_LEFT 10
#define CHAT_TEXT_RIGHT 229
// 聊天记录区域从数字人物下方开始，占用整个屏幕下半部分。
#define CHAT_TEXT_TOP 199
// 聊天记录区域下边界，底部 26 像素留给居中的三点聆听指示。
#define CHAT_TEXT_BOTTOM 293
// 每行固定 19 像素，避免滚动时发生文字跳动。
#define CHAT_LINE_HEIGHT 18
// 16 像素中文字体在保留底部指示带后安全显示五行。
#define CHAT_VISIBLE_LINE_COUNT 5
// 粗体会向右重复绘制一个像素，排版时预留余量，避免最右侧笔画被截断。
#define CHAT_BOLD_RIGHT_MARGIN 2
// 最多保存十二条用户或机器人消息，足够按键查看近期聊天记录。
#define CHAT_HISTORY_CAPACITY 12
// 排版时最多暂存九十六行，防止超长回复占用过多内存。
#define CHAT_RENDER_LINE_CAPACITY 96
// 只按固定时间间隔刷新逐字进度；不会高频重绘抢占网络处理。
#define CHAT_SYNC_REFRESH_INTERVAL_MS 120
// ASR 增量文字最短刷新间隔，避免半屏 SPI 重绘阻塞 WebSocket 音频接收。
#define CHAT_USER_PREVIEW_REFRESH_INTERVAL_MS 700
#define UI_HEADER_HEIGHT 44
#define UI_BUBBLE_LEFT 5
#define UI_BUBBLE_TOP 199
#define UI_BUBBLE_RIGHT 235
#define UI_BUBBLE_BOTTOM 293
#define UI_BUBBLE_COLOR 0x2C1C
#define UI_BUBBLE_TEXT_COLOR 0xFFFF
// 英语模式对话区盖住 UI 原图里印好的示例文字，避免和真消息叠在一起。
#define EN_CHAT_LEFT 8
#define EN_CHAT_RIGHT 231
#define EN_CHAT_TOP 154
#define EN_CHAT_BOTTOM 316
#define EN_CHAT_PANEL_COLOR 0x0000
#define EN_USER_BUBBLE_COLOR 0xCE79
#define EN_AI_BUBBLE_COLOR 0xFFFF
#define EN_CHAT_TEXT_COLOR 0x0000
#define EN_CHAT_LINE_HEIGHT 18
#define EN_BUBBLE_PAD_X 6
#define EN_BUBBLE_PAD_Y 5
#define EN_BUBBLE_GAP 6
#define EN_BUBBLE_MAX_WIDTH 216
#define EN_BUBBLE_RADIUS 10
#define UI_GREETING_FRAME_COUNT 7
#define UI_GREETING_FRAME_INTERVAL_MS 420
#define UI_LISTEN_FRAME_COUNT 7
#define UI_LISTEN_FRAME_INTERVAL_MS 280
// 动画文件与屏幕同为 240x320，统一尺寸可避免旧版局部帧被错误解释。
#define UI_GREETING_FRAME_WIDTH 240
#define UI_GREETING_FRAME_HEIGHT 320
// 中文 TTS 在标准语速下约每秒说四个汉字，用于音频未结束前的文字同步估算。
#define TTS_ESTIMATED_CHARACTERS_PER_SECOND 4
// 下一句在预计发声前约 180 ms 完整出现，避免声音先于文字。
#define TTS_SENTENCE_DISPLAY_LEAD_MS 180
// 千问对话历史最多保存十二条消息，也就是六轮用户和助手对话。
#define QWEN_HISTORY_CAPACITY 12
// 千问 SSE 流连续三十秒没有新数据时主动结束，避免设备永久卡在思考状态。
#define QWEN_STREAM_INACTIVITY_TIMEOUT_MS 30000
// 一次唤醒后保持连续对话；每轮播放结束后自动进入下一轮聆听。
#define CONTINUOUS_DIALOG_ENABLED true
// 必须先识别到“小云宝”才进入持续对话。
#define ALWAYS_ON_DIALOG_ENABLED false
// 每轮播放结束后留出短暂间隔，避免喇叭尾音被麦克风立即采集。
#define CONTINUOUS_DIALOG_RESTART_DELAY_MS 0
// 连续对话没有听到内容时，延迟五秒再尝试，避免每两秒创建一次云端任务。
#define CONTINUOUS_DIALOG_EMPTY_RETRY_DELAY_MS 800
// 连续三次空结果后进入较长安静等待，但不退出已经完成的唤醒会话。
#define CONTINUOUS_DIALOG_MAX_EMPTY_RETRIES 3
// 连续对话中没有听清时直接退出，避免设备无人操作时反复连接云端。
#define CONTINUOUS_DIALOG_STOP_ON_EMPTY_RESULT false
// 唤醒后的连续会话超过 30 秒没有收到有效用户语音时自动退出。
#define CONTINUOUS_DIALOG_IDLE_TIMEOUT_MS 30000
#define COMPANION_STANDBY_IDLE_MS 120000UL
#define TFT_STANDBY_BACKLIGHT_PERCENT 15
// 方案 B：待机时先识别唤醒词；识别到“小云宝”后才进入连续对话。
#define VOICE_WAKE_WORD_ENABLED false
// 先使用兼容性最高的纯文本 TTS，避免部分音色在 SSML 双工请求中返回 416。
#define XIYUNBAO_TTS_ENABLE_SSML false
// 唤醒词允许的常见 ASR 变体，兼容“小云宝/小云宝呀/小云宝在吗”。
const char VOICE_WAKE_WORD[] = "小云宝";
// 待机唤醒监听失败后，延迟重新监听，避免网络异常时高速重连。
#define WAKE_LISTEN_RESTART_DELAY_MS 900
// 已取消本地中文 MultiNet 唤醒，避免依赖 model 分区和中文模型包。
#define LOCAL_WAKE_ENABLED false
#define LOCAL_WAKE_COMMAND_ID 1
#define LOCAL_WAKE_DETECTION_THRESHOLD 0.50f
#define LOCAL_WAKE_INPUT_SHIFT 14
#define LOCAL_WAKE_DIAGNOSTIC_INTERVAL_MS 4000
#define LOCAL_WAKE_MODEL_TIMEOUT_MS 5760
const char LOCAL_WAKE_COMMAND_TEXT[] = "小云宝";
// mn7_cn 使用 SentencePiece 词表；各音节自带词首标记，令牌之间不能插入普通空格。
const char LOCAL_WAKE_COMMAND_PHONEMES[] = "xiao yun bao";
const char QWEN_SYSTEM_PROMPT[] =
"你是小云宝，运行在ESP32嵌入式终端的同龄AI伙伴，平等相处，不刻意讨好、不谄媚。\n"
"核心：聊天时分析用户话语，从字里行间推断处境、场景与情绪。即便用户没有直接说自己孤身一人，若文字透露出独自承担、无人分担、独处生活的状态，自然带出关心，提醒好好吃饭、照顾自己、好好生活，像真实好友，共情且克制，不是哄人的工具。\n"
"\n"
"交互规则：\n"
"1. 读懂潜台词，分辨用户是否独自扛压力、独自生活；不生硬提问，悄悄捕捉独处信号，合适时机温柔关心，不强行触发。\n"
"2. 优先识别场景：竞赛备赛、身体不适、学习压力，结合场景回应，拒绝空洞套话。\n"
"3. 语气自然平实，口语化，适配TTS语音播报；最多4句话，120字以内。\n"
"4. 承接整段对话上下文，记住前面聊的内容，不反复自我介绍，不无故跳话题。\n"
"5. 收到视觉情绪线索时，参考对方状态回应；负面情绪先共情，再结合场景给简单可行的小建议。\n"
"6. 用户指出错误时坦然承认，正常继续对话，不回避、不敷衍。\n"
"\n"
"输出格式严格遵守，禁止多余文字：\n"
"首行【emotion:标签】，可选：happy/sad/neutral/shy/surprise/angry/fear\n"
"第二行起正文，最多4句话，120字以内，朋友口吻。\n"
"\n"
"示例1：\n"
"用户：项目调试到深夜，一堆问题要慢慢解决\n"
"【emotion:sad】\n"
"一直熬夜啃工程难题，过程肯定难熬。就算没人陪着，也要记得按时吃饭，好好照顾自己。\n"
"\n"
"示例2：\n"
"用户：身体不舒服，手头任务还得继续赶\n"
"【emotion:sad】\n"
"一边扛着不适一边推进事情，真的不容易。再忙也别忽略身体，一个人也要好好生活。\n";


SPIClass shared_spi(FSPI);
Adafruit_ST7789 display(&shared_spi, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
// U8g2 为 Adafruit_GFX 提供 UTF-8 中文字形绘制能力。
U8G2_FOR_ADAFRUIT_GFX chinese_font;
// 聊天区域先在内存画布中完整合成，再一次性推送到屏幕，避免先擦后画造成闪烁。
GFXcanvas16 *chat_canvas = NULL;
U8G2_FOR_ADAFRUIT_GFX chat_canvas_font;
GFXcanvas16 *english_chat_canvas = NULL;
U8G2_FOR_ADAFRUIT_GFX english_chat_font;
WebSocketsClient asr_socket;
WebSocketsClient tts_socket;
// 小云宝专用中转使用 asr_socket 作为唯一长连接；tts_socket 仅保留声明，
// 以免无关的旧辅助函数大范围改写。正常对话不会再直接连接任何云端地址。
bool relay_connected = false;
bool relay_connecting = false;
// WebSocket 客户端只配置一次；连接断开后由库的自动重连机制继续工作。
bool relay_client_started = false;
bool relay_listen_start_pending = false;
bool relay_audio_started = false;
bool relay_audio_is_opus = false;
OpusDecoder *relay_opus_decoder = NULL;
int16_t relay_opus_pcm_buffer[RELAY_OPUS_FRAME_SAMPLES];
uint16_t relay_opus_expected_sequence = 0;
struct RelayOpusItem
{
    uint8_t type;
    uint8_t *data;
    size_t length;
    uint32_t generation;
};
QueueHandle_t relay_opus_packet_queue = NULL;
TaskHandle_t relay_opus_decoder_task_handle = NULL;
volatile uint32_t relay_opus_generation = 0;
volatile bool relay_opus_decoder_ready = false;
volatile bool relay_opus_decode_error_pending = false;
// 保存转换后发送给百炼的 16 位单声道 PCM 音频。
uint8_t microphone_buffer[3200];
// INMP441 使用左右两个 32 位 I2S 数据槽，先用 32 位数组接收原始采样。
int32_t microphone_raw_buffer[1600];
// 统计本次录音发送的音频总字节数。
unsigned long microphone_bytes_sent = 0;
// 统计本次录音采样的最大绝对音量。
int32_t microphone_max_volume = 0;
// 保存原始 I2S 数据的最大绝对值，用于判断麦克风是否真正输出数据。
int32_t microphone_raw_max_volume = 0;
// 标记是否已经打印过本次录音的原始首帧诊断。
bool microphone_first_frame_printed = false;
// 麦克风数字高通和噪声门状态；只在录音期间更新。
int32_t microphone_dc_estimate = 0;
uint32_t microphone_noise_floor = 0;
uint16_t microphone_gate_hold_samples = 0;
bool wifi_ready = false;
bool network_offline_notice_visible = false;
unsigned long wifi_last_reconnect_attempt = 0;
bool wifi_connection_announced = false;
unsigned long wifi_connection_start_time = 0;
bool wifi_provision_active = false;
String wifi_sta_ssid = "";
String wifi_sta_password = "";
String wifi_ap_ssid = "XiaoYunBao";
bool wifi_reconnect_user_requested = false;
bool screen_ready = false;
bool sd_card_ready = false;
String sd_ui_image_path = "";
bool sd_ui_image_loaded = false;
uint16_t *sd_ui_framebuffer = NULL;
uint16_t *english_ui_framebuffer = NULL;
bool english_ui_loaded = false;
bool english_ui_visible = false;
bool english_announce_active = false;
bool english_announce_pending = false;
bool english_announce_abort = false;
String english_ui_image_path = "";
uint16_t english_chat_panel_color = EN_CHAT_PANEL_COLOR;
uint16_t *chat_background_cache = NULL;
// 招手动画七帧一次性放入 PSRAM，避免每次刷新都从 TF 卡读整屏。
uint16_t *greeting_frame_cache[UI_GREETING_FRAME_COUNT] = {NULL};
bool greeting_frame_available[UI_GREETING_FRAME_COUNT] = {false};
uint8_t greeting_frame_loaded_count = 0;
bool greeting_frames_loaded = false;
// 陪伴模式聆听动画同样缓存七帧，只在中文凝听时播放。
uint16_t *listen_frame_cache[UI_LISTEN_FRAME_COUNT] = {NULL};
bool listen_frame_available[UI_LISTEN_FRAME_COUNT] = {false};
uint8_t listen_frame_loaded_count = 0;
bool listen_frames_loaded = false;
// 兼容旧版单帧读取函数；正常动画路径不再使用此缓冲区。
uint16_t *greeting_frame_buffer = NULL;
int16_t sd_jpeg_draw_x = 0;
int16_t sd_jpeg_draw_y = 0;
bool microphone_ready = false;
bool speaker_ready = false;
bool asr_active = false;
bool recording = false;
bool asr_result_ready = false;
bool listen_commit_pending = false;
bool tts_active = false;
bool tts_finished = false;
bool turn_busy = false;
// 标记当前是否处于一次唤醒后的连续对话会话。
bool continuous_dialog_active = false;
// 待机阶段由 ESP-SR 中文 MultiNet7 在设备本地识别“小云宝”。
volatile bool local_wake_task_active = false;
volatile bool local_wake_stop_requested = false;
volatile bool local_wake_detected = false;
bool local_wake_available = true;
bool local_wake_restart_pending = false;
TaskHandle_t local_wake_task_handle = NULL;
// 待机唤醒监听状态；true 表示当前 ASR 只用于寻找“小云宝”。
bool wake_listening_active = false;
// 防止待机自动重连时反复刷屏打印同一条提示。
bool wake_prompt_printed = false;
bool english_mode_enabled = false;
bool language_switch_pending = false;
bool pending_english_mode = false;
// 屏幕背光百分比及 PWM 是否成功挂接。
uint8_t tft_backlight_percent = TFT_BACKLIGHT_INITIAL_PERCENT;
uint8_t tft_user_backlight_percent = TFT_BACKLIGHT_INITIAL_PERCENT;
bool tft_backlight_pwm_ready = false;
bool companion_standby_active = false;
unsigned long last_user_activity_ms = 0;
uint8_t speaker_volume_percent = SPEAKER_VOLUME_INITIAL_PERCENT;
bool speaker_muted = false;
bool ignore_incoming_reply = false;
bool button_toast_visible = false;
unsigned long button_toast_expire_time = 0;
char button_toast_text[40] = "";
// 四键共用短按/长按状态机：100ms 消抖，600ms 判定长按。
struct ControlButtonState
{
    uint8_t pin;
    bool raw_pressed;
    bool stable_pressed;
    bool long_press_fired;
    unsigned long last_change_time;
    unsigned long pressed_time;
    unsigned long next_repeat_time;
    unsigned long last_event_time;
    volatile bool fall_latched;
    volatile bool irq_armed;
};
ControlButtonState control_buttons[4];
// 标记是否等待本轮 TTS 完成后重新启动 ASR。
bool continuous_dialog_restart_pending = false;
// 保存下一轮 ASR 允许启动的时间。
unsigned long continuous_dialog_restart_time = 0;
unsigned long continuous_dialog_last_voice_time = 0;
unsigned long record_stop_time = 0;
unsigned long record_hard_stop_time = 0;
unsigned long asr_sentence_confirm_time = 0;
bool asr_sentence_end_pending = false;
bool asr_preview_dirty = false;
unsigned long asr_timeout = 0;
unsigned long tts_timeout = 0;
String final_text = "";
String tts_text = "";
// 保存本轮去掉情绪标签后的纯回复，供屏幕显示和聊天历史使用。
String tts_plain_text = "";
// 仅用于送入云端合成的副本；屏幕和聊天历史始终保留模型原文。
String tts_synthesis_text = "";
// 保存尚未显示和播报的后续句子；按句缓存，兼顾快速首句与连续播放。
String tts_pending_reply = "";
bool tts_sentence_sequence_active = false;
// 保存加入 speak 和 break 标签后的 SSML，发送给 CosyVoice。
String tts_ssml_text = "";
// 保存本轮解析出的情绪；任何非法或缺失标签都会回退为 neutral。
String current_emotion = "neutral";
// 保存根据情绪映射得到的本轮 TTS 语速。
float current_tts_rate = 0.95f;
// 保存根据情绪映射得到的本轮 TTS 音高。
float current_tts_pitch = 1.00f;
String asr_task_id = "";
String tts_task_id = "";
bool asr_started = false;
bool asr_finish_sent = false;
// 标记 ASR 是否已经收到 task-finished，用于区分正常断开和异常断开。
bool asr_task_finished = false;
// 主动结束 ASR 时忽略随后到达的断开回调，防止退出后又排入持续监听。
bool asr_disconnect_expected = false;
bool tts_started = false;
bool tts_text_sent = false;
bool tts_session_finish_sent = false;
// task-started 后才能向同一个 CosyVoice 任务追加短句。
bool tts_task_ready = false;
// true 表示千问 SSE 仍可能产生后续句子，不能提前发送 finish-task。
bool tts_streaming_input_active = false;
bool tts_streaming_input_requested = false;
bool tts_text_input_complete = true;
String tts_text_queue[TTS_TEXT_QUEUE_LENGTH];
unsigned int tts_text_queue_head = 0;
unsigned int tts_text_queue_tail = 0;
unsigned int tts_text_queue_count = 0;
unsigned int tts_text_sentence_sent_count = 0;
// 千问 SSE 流水线状态：标签完整解析后才允许首句启动 TTS。
bool qwen_stream_emotion_ready = false;
bool qwen_reply_streamed_to_tts = false;
String qwen_stream_label_buffer = "";
String qwen_stream_sentence_buffer = "";
String qwen_stream_plain_reply = "";
bool i2s_driver_ready = false;
bool tts_error_pending = false;
bool tts_disconnect_expected = false;
// 首次 TLS/WebSocket 握手偶发抖动时快速重试，避免一次 TCP cleanup 丢掉整轮回复。
const unsigned int TTS_CONNECT_RETRY_LIMIT = 2;
unsigned int tts_connect_retry_count = 0;
volatile bool tts_playback_busy = false;
volatile bool tts_playback_abort = false;
volatile bool tts_playback_failed = false;
volatile bool tts_playback_started = false;
// 每轮按回复长度选择起播水位，短回复优先及时，长回复优先连续。
volatile unsigned long tts_start_buffer_target_bytes = TTS_START_BUFFER_BYTES;
// 只有播放任务确认末尾静音已经排空后才置为 true。
volatile bool tts_playback_drained = false;
volatile bool tts_response_done = false;
// 云端提前断开时，保留已经收到的 PCM 并让播放任务自然排空。
volatile bool tts_graceful_drain_pending = false;
// response.done 到来后，记录服务端已经生成的最后一个 PCM 包编号。
volatile unsigned long tts_final_audio_packet_count = 0;
// 统计播放任务真正断粮并进入再缓冲的次数和累计等待时间。
volatile unsigned long tts_queue_underrun_count = 0;
volatile unsigned long tts_queue_underrun_ms = 0;
volatile unsigned long tts_queue_underrun_started_at = 0;
volatile bool tts_playback_rebuffering = false;
// 记录播放过程中队列剩余的最小包数，用于量化云端音频是否供给不足。
volatile unsigned long tts_queue_minimum_packet_count = TTS_AUDIO_QUEUE_LENGTH;
// 分别统计实际送入 I2S 的 PCM 包数和输入字节数，用于核对长回复是否完整播放。
volatile unsigned long tts_playback_packet_count = 0;
volatile unsigned long tts_playback_byte_count = 0;
bool tts_cleanup_done = false;
// 保存本轮 TTS 已经收到的音频包数量。
unsigned long tts_audio_packet_count = 0;
// 保存本轮 TTS 已经收到的音频总字节数。
unsigned long tts_audio_byte_count = 0;
// 播放位置和原始 PCM 统计跨越所有网络包，保证包边界处增益连续。
volatile unsigned long tts_played_sample_count = 0;
volatile unsigned long tts_total_sample_count = 0;
// 记录建连、首包与首声时间，用串口量化回复延迟。
unsigned long tts_request_start_time = 0;
unsigned long tts_first_audio_time = 0;
volatile unsigned long tts_first_playback_time = 0;
volatile uint32_t tts_pcm_peak = 0;
volatile unsigned long tts_pcm_near_clip_count = 0;
// MAX98357A 使用双声道 I2S；左右声道都写入同一个经过处理的 16 位 PCM。
uint8_t speaker_pcm_buffer[8192];
struct TtsAudioChunk
{
    uint8_t *data;
    size_t length;
    unsigned long packet_number;
};
QueueHandle_t tts_audio_queue = NULL;
TaskHandle_t tts_playback_task_handle = NULL;
String tts_error_message = "";
// WebSockets 库会复制额外请求头；保留字符串对象便于调试和复用。
String tts_workspace_header = "";
// 保存上一次成功接受语音触发命令的时间。
unsigned long last_voice_trigger_time = 0;
unsigned long listen_start_sent_time = 0;
uint8_t listen_start_retry_count = 0;
// Wake requests are queued so a transient Wi-Fi outage never blocks button/UI servicing.
bool voice_turn_pending = false;
String voice_turn_pending_source = "";
// 保存语音触发防抖的时间间隔。
const unsigned long VOICE_TRIGGER_DEBOUNCE_MS = 800;
// 一条聊天消息使用基础结构保存文字和说话方，不依赖复杂类。
struct ChatMessage
{
    String text;
    bool from_user;
};
// 保存近期聊天记录，达到容量上限后删除最旧的一条。
ChatMessage chat_history[CHAT_HISTORY_CAPACITY];
// 当前实际保存的消息数量。
int chat_history_count = 0;
// 当前正在识别的用户临时消息下标；句末后会转为正式历史消息。
int chat_live_user_message_index = -1;
// 记录连续对话中连续没有识别到文字的轮数。
unsigned int continuous_dialog_empty_retry_count = 0;
// 记录用户临时文字上一次实际刷新屏幕的时间。
unsigned long chat_user_preview_last_refresh_time = 0;
// 记录上一次打印到串口的非空识别文字，避免重复刷屏。
String last_asr_printed_text = "";
// 聊天记录向上滚动的行数；为零表示跟随最新消息。
int chat_scroll_lines = 0;
// 当前播放中机器人消息在历史数组中的位置。
int chat_speaking_message_index = -1;
// 当前播放中允许显示的机器人 UTF-8 字节数。
size_t chat_speaking_visible_bytes = 0;
// 上一次按语音进度刷新聊天记录的时间。
unsigned long chat_last_sync_refresh_time = 0;
String chat_last_render_signature = "";
bool ui_greeting_animation_active = false;
uint8_t ui_greeting_frame_index = 0;
unsigned long ui_greeting_next_frame_time = 0;
bool ui_listen_animation_active = false;
uint8_t ui_listen_frame_index = 0;
unsigned long ui_listen_next_frame_time = 0;
unsigned long ui_listening_dots_next_time = 0;
uint8_t ui_listening_dots_phase = 0;
bool ui_listening_indicator_active = false;
// 连续会话刚开始聆听时保留上一条回复，收到用户首段语音后才清屏。
bool ui_listening_has_speech = false;
// 为每一行记录所属消息、文字和颜色方向，用于整体上下滚动显示。
struct ChatRenderLine
{
    String text;
    bool from_user;
    int message_index;
};
// 当前屏幕可渲染的完整聊天行缓存。
ChatRenderLine chat_render_lines[CHAT_RENDER_LINE_CAPACITY];
// 本次排版得到的总行数。
int chat_render_line_count = 0;

// 千问历史只保存角色和纯文本，不保存 system 提示词及 SSML 标记。
struct QwenHistoryMessage
{
    String role;
    String content;
};
// 保存最近六轮已经完成的用户和助手消息。
QwenHistoryMessage qwen_history[QWEN_HISTORY_CAPACITY];
// 保存当前实际使用的千问历史消息数量。
int qwen_history_count = 0;

void stop_i2s(void);
bool device_wifi_connected(void);
bool ensure_wifi_connected(unsigned long timeout_ms);
void service_network_offline_notice(void);
void service_wifi_connection(void);
void wifi_load_credentials(void);
bool wifi_credentials_available(void);
void wifi_start_station(void);
void wifi_provision_begin(const char *reason);
void service_wifi_provision(void);
void draw_wifi_provision_screen(void);
void service_relay_preconnect(void);
void mute_speaker_data_pin(void);
void setup_microphone(void);
void service_local_wake(void);
void setup_control_buttons(void);
void setup_tft_backlight_pwm(void);
void set_tft_backlight_percent(int percent);
void set_speaker_volume_percent(int percent);
void service_control_buttons(void);
void show_button_toast(const char *text);
void paint_button_toast(void);
void restore_button_toast_area(void);
void service_button_toast(void);
void handle_k1_short_press(void);
void handle_k1_long_press(void);
void handle_k2_short_press(void);
void handle_k2_long_press(void);
void configure_button_pin(uint8_t pin);
void IRAM_ATTR control_button_isr(void *arg);
void dispatch_button_short_press(int index);
void dispatch_button_long_press(int index);
void barge_in_current_turn(void);
void request_wifi_reconnect(void);
void setup_speaker(void);
void play_speaker_test_tone(void);
void start_tts(const String &text);
void start_reply_sentence_sequence(const String &reply);
bool take_next_reply_sentence(String *sentence);
esp_err_t write_speaker_pcm(const uint8_t *pcm_data, size_t pcm_length, size_t *written_bytes, bool final_chunk);
void report_state(const char *state);
void draw_status(const char *title, const char *detail);
void draw_sd_ui_background(void);
void restore_sd_ui_background(void);
bool find_sd_ui_image(String *path);
void load_english_ui_image(void);
void apply_language_ui(void);
void fill_english_chat_panel(void);
void fill_companion_chat_canvas_background(void);
void blank_english_chat_in_framebuffer(void);
void play_english_ready_local(void);
void service_english_ready_announce(void);
void load_greeting_animation_frames(void);
void load_listen_animation_frames(void);
void start_greeting_animation(void);
void stop_greeting_animation(void);
void service_greeting_animation(void);
void start_listen_animation(void);
void stop_listen_animation(void);
void service_listen_animation(void);
void draw_listening_indicator(void);
void draw_companion_status_text(const char *text);
void draw_greeting_frame(void);
void draw_listen_frame(void);
void clear_listening_dots_area(void);
bool draw_jpeg_file(const char *path);
bool draw_rgb565_file(const char *path);
bool draw_bmp_file(const char *path);
bool draw_bmp_file_legacy(const char *path);
bool find_english_ui_image(String *path);
bool draw_bmp_region(const char *path, int left, int top, int width, int height);
bool sd_jpeg_block(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t *pixels);
void draw_wake_prompt(void);
void draw_network_offline_notice(void);
void draw_ui_header(void);
void set_dialog_user_text(const String &user_text);
void update_dialog_user_preview(const String &user_text);
void set_dialog_reply(const String &reply);
void set_dialog_reply_visible(const String &reply, size_t visible_bytes);
void update_dialog_reply_page(void);
void draw_dialog_reply_page(void);
void clear_dialog_text_area(void);
String dialog_fit_text_from_end(const String &text, int maximum_width);
size_t count_utf8_characters(const String &text);
size_t utf8_bytes_for_character_count(const String &text, size_t character_count);
bool is_sentence_ending_character(const String &character);
size_t reply_sentence_end_bytes(const String &text, size_t start_byte);
void draw_bold_chat_text(int x, int baseline, const String &text);
void draw_bold_chat_text_to_canvas(int x, int baseline, const String &text);
void add_chat_message(const String &text, bool from_user);
void redraw_chat_history(void);
void rebuild_chat_render_lines(void);
void append_chat_render_line(const String &text, bool from_user, int message_index);
String chat_visible_text(const String &text, int message_index);
void update_chat_speech_sync(void);
void scroll_chat_history(int direction);
size_t utf8_character_length(const String &text, size_t byte_index);
void start_voice_turn(void);
void request_voice_turn(const char *source);
void request_voice_turn_forced(const char *source);
void service_pending_voice_turn(void);
void abort_current_listen(const char *reason);
void commit_current_listen(void);
void stop_continuous_dialog(const char *reason);
void note_user_activity(void);
void refresh_tft_backlight_pwm(void);
void service_companion_standby(void);
bool camera_care_is_active(void);
void draw_english_listen_prompt(void);
void send_microphone_audio(void);
void asr_event(WStype_t type, uint8_t *payload, size_t length);
void handle_relay_event(WStype_t type, uint8_t *payload, size_t length);
void relay_send_json(const String &message);
void set_dialog_language_mode(bool english_enabled, bool announce);
void send_english_ready_announce(void);
void prepare_relay_audio_playback(const String &format);
void reset_relay_opus_decoder(void);
bool prepare_relay_opus_decoder(void);
bool decode_and_queue_relay_opus(const uint8_t *packet, size_t packet_length, uint32_t generation);
bool enqueue_relay_opus_packet_copy(const uint8_t *packet, size_t packet_length);
bool enqueue_relay_opus_control(uint8_t type);
void clear_relay_opus_packet_queue(void);
void relay_opus_decoder_task(void *parameter);
void tts_event(WStype_t type, uint8_t *payload, size_t length);
void tts_send_event(const String &event_json);
void reset_tts_text_queue(void);
bool enqueue_tts_text_sentence(const String &sentence);
void pump_tts_text_queue(void);
void finish_tts_text_input(void);
void append_streaming_sentence_to_chat(const String &sentence);
void reset_qwen_stream_pipeline(void);
void consume_qwen_stream_chunk(const String &chunk_text);
void flush_qwen_stream_sentence_buffer(void);
size_t complete_sentence_end_bytes(const String &text);
bool decode_and_queue_tts_audio(const uint8_t *base64_audio, size_t base64_length, size_t *decoded_length);
bool queue_tts_pcm_copy(const uint8_t *pcm_data, size_t pcm_length, size_t *queued_length);
bool enqueue_tts_pcm_owned(uint8_t *pcm_data, size_t pcm_length, size_t *queued_length);
void tts_playback_task(void *parameter);
void finish_tts_playback_output(void);
void clear_tts_audio_queue(void);
void mark_tts_failure(const String &message, bool disconnect_socket);
void finalize_tts_if_ready(void);
void process_asr_result(void);
String remove_wake_word_punctuation(const String &text);
bool is_ignorable_asr_text(const String &text);
void discard_dialog_user_preview(void);
String call_qwen(const String &text);
String build_qwen_request_body(const String &user_text);
String read_qwen_sse_stream(HTTPClient *http);
void append_qwen_history(const String &role, const String &content);
bool is_valid_emotion(const String &emotion);
String strip_emotion_markup(const String &text);
void parse_emotion_reply(const String &raw_reply, String *emotion, String *reply);
void set_tts_parameters_for_emotion(const String &emotion);
String build_ssml_text(const String &reply);
String xml_escape(const String &text);
String create_task_id(const char *prefix);
String json_escape(const String &text);
String json_string(const String &json, const String &key);
String json_last_nonempty_string(const String &json, const String &key);
void camera_emotion_begin(void);
void service_camera_emotion(void);
void camera_on_vision_emotion(const String &emotion, bool ok, bool face, float confidence);
uint32_t text_fingerprint(const char *text);

void refresh_tft_backlight_pwm(void)
{
    if (tft_backlight_pwm_ready == false)
    {
        tft_backlight_pwm_ready = ledcAttachChannel(PIN_TFT_BL,
                                                    TFT_BACKLIGHT_PWM_FREQUENCY,
                                                    TFT_BACKLIGHT_PWM_RESOLUTION,
                                                    TFT_BACKLIGHT_PWM_CHANNEL);
        if (tft_backlight_pwm_ready == false)
        {
            digitalWrite(PIN_TFT_BL, LOW);
            return;
        }
    }
    uint8_t percent = tft_backlight_percent;
    if (companion_standby_active == true)
    {
        percent = TFT_STANDBY_BACKLIGHT_PERCENT;
    }
    if (percent < TFT_BACKLIGHT_MINIMUM_PERCENT)
    {
        percent = TFT_BACKLIGHT_MINIMUM_PERCENT;
    }
    const uint32_t maximum_duty = (1UL << TFT_BACKLIGHT_PWM_RESOLUTION) - 1;
    const uint32_t duty = maximum_duty * percent / 100;
    // 摄像头初始化会复位整组 LEDC，背光通道要重新挂上，否则引脚悬空就会闪。
    ledcWrite(PIN_TFT_BL, duty);
}

void set_tft_backlight_percent(int percent)
{
    if (percent < TFT_BACKLIGHT_MINIMUM_PERCENT)
    {
        percent = TFT_BACKLIGHT_MINIMUM_PERCENT;
    }
    if (percent > TFT_BACKLIGHT_MAXIMUM_PERCENT)
    {
        percent = TFT_BACKLIGHT_MAXIMUM_PERCENT;
    }
    tft_backlight_percent = (uint8_t)percent;
    tft_user_backlight_percent = tft_backlight_percent;
    note_user_activity();
    refresh_tft_backlight_pwm();
    Serial.print("屏幕亮度: " );
    Serial.print(tft_backlight_percent);
    Serial.println("%");
}

void set_speaker_volume_percent(int percent)
{
    if (percent < SPEAKER_VOLUME_MINIMUM_PERCENT)
    {
        percent = SPEAKER_VOLUME_MINIMUM_PERCENT;
    }
    if (percent > SPEAKER_VOLUME_MAXIMUM_PERCENT)
    {
        percent = SPEAKER_VOLUME_MAXIMUM_PERCENT;
    }
    speaker_volume_percent = (uint8_t)percent;
    speaker_muted = false;
    note_user_activity();
    Serial.print("播放音量: ");
    Serial.print(speaker_volume_percent);
    Serial.println("%");
}

void note_user_activity(void)
{
    last_user_activity_ms = millis();
    if (companion_standby_active == true)
    {
        companion_standby_active = false;
        refresh_tft_backlight_pwm();
        Serial.println("退出待机，恢复屏幕亮度");
    }
}

void service_companion_standby(void)
{
    const unsigned long now = millis();
    const bool session_busy =
        wifi_provision_active == true ||
        asr_active == true ||
        recording == true ||
        tts_active == true ||
        turn_busy == true ||
        continuous_dialog_active == true ||
        camera_care_is_active() == true;
    if (session_busy == true)
    {
        last_user_activity_ms = now;
        if (companion_standby_active == true)
        {
            companion_standby_active = false;
            refresh_tft_backlight_pwm();
        }
        return;
    }
    if (last_user_activity_ms == 0)
    {
        last_user_activity_ms = now;
        return;
    }
    if (companion_standby_active == false &&
        (now - last_user_activity_ms) >= COMPANION_STANDBY_IDLE_MS)
    {
        companion_standby_active = true;
        refresh_tft_backlight_pwm();
        Serial.println("长时间未唤醒，进入低功耗待机，约五分钟抓一张画面");
    }
}

void setup_tft_backlight_pwm(void)
{
    pinMode(PIN_TFT_BL, OUTPUT);
    // 在屏幕初始化和第一帧写入完成前始终关闭背光，隐藏 ST7789 显存中的
    // 随机上电内容，避免用户看到屏幕下方花屏。
    digitalWrite(PIN_TFT_BL, LOW);
    // ESP32 Arduino Core 3.x 按引脚自动分配 LEDC 通道。
    // 摄像头时钟占用 LEDC 通道 2。背光固定用通道 5，避免初始化摄像头时把背光频率改乱。
    tft_backlight_pwm_ready = ledcAttachChannel(PIN_TFT_BL,
                                                 TFT_BACKLIGHT_PWM_FREQUENCY,
                                                 TFT_BACKLIGHT_PWM_RESOLUTION,
                                                 TFT_BACKLIGHT_PWM_CHANNEL);
    if (tft_backlight_pwm_ready == false)
    {
        // 暂时保持关闭；第一帧准备完成后再以常亮方式打开。
        digitalWrite(PIN_TFT_BL, LOW);
        Serial.println("屏幕背光 PWM 初始化失败，首帧完成后使用常亮模式");
        return;
    }
    // ledcAttach 后立即写 0，防止通道默认占空比短暂点亮背光。
    ledcWrite(PIN_TFT_BL, 0);
}

void configure_button_pin(uint8_t pin)
{
    gpio_reset_pin((gpio_num_t)pin);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
    gpio_pullup_en((gpio_num_t)pin);
    gpio_pulldown_dis((gpio_num_t)pin);
    pinMode(pin, INPUT_PULLUP);
}

void IRAM_ATTR control_button_isr(void *arg)
{
    const int index = (int)(intptr_t)arg;
    if (index < 0 || index > 3)
    {
        return;
    }
    if (control_buttons[index].irq_armed == true)
    {
        control_buttons[index].irq_armed = false;
        control_buttons[index].fall_latched = true;
    }
}

void setup_control_buttons(void)
{
    const uint8_t pins[4] = {PIN_BUTTON_K1, PIN_BUTTON_K2, PIN_BUTTON_K3, PIN_BUTTON_K4};
    const unsigned long now = millis();
    for (int index = 0; index < 4; index++)
    {
        configure_button_pin(pins[index]);
        const bool pressed = digitalRead(pins[index]) == LOW;
        control_buttons[index].pin = pins[index];
        control_buttons[index].raw_pressed = pressed;
        control_buttons[index].stable_pressed = pressed;
        control_buttons[index].long_press_fired = false;
        control_buttons[index].last_change_time = now;
        control_buttons[index].pressed_time = pressed ? now : 0;
        control_buttons[index].next_repeat_time = 0;
        control_buttons[index].last_event_time = 0;
        control_buttons[index].fall_latched = false;
        control_buttons[index].irq_armed = !pressed;
        attachInterruptArg(digitalPinToInterrupt(pins[index]), control_button_isr,
                           (void *)(intptr_t)index, FALLING);
    }
    Serial.println("四按键已初始化：K1唤醒/重连  K2中英/静音  K3亮度-/音量-  K4亮度+/音量+");
    Serial.println("GPIO: K1=18  K2=21  K3=3  K4=45  全部INPUT_PULLUP，按下为低电平");
    Serial.println("注意：WiFi重连是 K1/GPIO18 长按，不是 K2。K2/GPIO21 短按才切换中英。");
    Serial.print("K2/GPIO21 当前电平: ");
    Serial.println(digitalRead(PIN_BUTTON_K2) == LOW ? "按下(LOW)" : "松开(HIGH)");
}

void send_english_ready_announce(void)
{
    if (english_mode_enabled == false || english_announce_abort == true)
    {
        english_announce_pending = false;
        english_announce_active = false;
        return;
    }
    english_announce_pending = true;
    english_announce_active = false;
    current_emotion = "happy";
    set_tts_parameters_for_emotion(current_emotion);
    abort_current_listen("ENGLISH_READY_ANNOUNCE");
    turn_busy = false;
    Serial.println("英语开场白改为后台播放，K2 可立即切回中文");
}

void set_dialog_language_mode(bool english_enabled, bool announce)
{
    english_mode_enabled = english_enabled;
    english_announce_active = false;
    if (english_enabled == false)
    {
        english_announce_pending = false;
        english_announce_abort = true;
    }
    else
    {
        english_announce_abort = false;
        english_announce_pending = false;
    }
    // 切换语言时结束当前连续会话，下一次短按从全新的语言会话开始。
    if (continuous_dialog_active == true)
    {
        stop_continuous_dialog("LANGUAGE_SWITCH");
    }
    chat_history_count = 0;
    chat_live_user_message_index = -1;
    chat_speaking_message_index = -1;
    chat_speaking_visible_bytes = 0;
    chat_scroll_lines = 0;
    chat_last_render_signature = "";
    chat_render_line_count = 0;
    abort_current_listen("LANGUAGE_SWITCH");
    voice_turn_pending = false;
    voice_turn_pending_source = "";
    listen_start_sent_time = 0;
    listen_start_retry_count = 0;
    asr_task_finished = false;
    asr_disconnect_expected = false;
    // Keep stale relay events suppressed until mode.changed confirms cleanup.
    ignore_incoming_reply = true;
    apply_language_ui();
    if (relay_connected == true)
    {
        relay_send_json(english_mode_enabled
            ? "{\"type\":\"mode.set\",\"language\":\"en\"}"
            : "{\"type\":\"mode.set\",\"language\":\"zh\"}");
        relay_send_json(english_mode_enabled
            ? "{\"type\":\"listen.prepare\",\"language\":\"en\"}"
            : "{\"type\":\"listen.prepare\",\"language\":\"zh\"}");
    }
    if (announce == true && english_mode_enabled == true)
    {
        show_button_toast("已切换English模式");
        Serial.println("已切换到英语模式，播放本地同款音色开场白");
        send_english_ready_announce();
        return;
    }
    if (announce == true)
    {
        Serial.println("已切换到中文模式；短按唤醒后开始中文交流");
        draw_status("小云宝", "短按唤醒");
        show_button_toast("已切换中文模式");
    }
    if (english_announce_pending == false && tts_active == false && tts_playback_busy == false)
    {
        setup_microphone();
    }
}

void show_brightness_toast(void)
{
    char line[24];
    snprintf(line, sizeof(line), "亮度:%u%%", (unsigned int)tft_backlight_percent);
    show_button_toast(line);
}

void show_volume_toast(void)
{
    char line[24];
    if (speaker_muted == true)
    {
        snprintf(line, sizeof(line), "已静音");
    }
    else
    {
        snprintf(line, sizeof(line), "音量:%u%%", (unsigned int)speaker_volume_percent);
    }
    show_button_toast(line);
}

void handle_k1_short_press(void)
{
    note_user_activity();
    if (wifi_provision_active == true)
    {
        show_button_toast("配网中");
        return;
    }
    const bool speaking = turn_busy == true || tts_active == true ||
        tts_playback_started == true || tts_playback_busy == true;
    if (speaking == true)
    {
        show_button_toast("已打断");
        barge_in_current_turn();
        return;
    }
    if (asr_active == true)
    {
        show_button_toast("收音结束");
        commit_current_listen();
        return;
    }
    show_button_toast("已唤醒");
    request_voice_turn("K1_SHORT");
}

void handle_k1_long_press(void)
{
    if (wifi_provision_active == true)
    {
        show_button_toast("配网中");
        return;
    }
    request_wifi_reconnect();
}

void handle_k2_short_press(void)
{
    Serial.println("K2 短按：切换中英");
    if (wifi_provision_active == true)
    {
        show_button_toast("配网中");
        return;
    }
    pending_english_mode = !english_mode_enabled;
    language_switch_pending = true;
    english_announce_abort = true;
    english_announce_pending = false;
    english_announce_active = false;
    ignore_incoming_reply = true;
    tts_playback_abort = true;
    tts_active = false;
    tts_playback_started = false;
    turn_busy = false;
    relay_audio_started = false;
    tts_finished = true;
    tts_response_done = true;
    tts_cleanup_done = true;
    reset_relay_opus_decoder();
    clear_tts_audio_queue();
    stop_i2s();
    abort_current_listen("LANGUAGE_SWITCH");
    show_button_toast(pending_english_mode ? "切换English" : "切换中文");
}

void handle_k2_long_press(void)
{
    speaker_muted = !speaker_muted;
    note_user_activity();
    show_volume_toast();
    Serial.println(speaker_muted == true ? "K2 长按：已静音" : "K2 长按：已取消静音");
}

void dispatch_button_short_press(int index)
{
    if (index == 0)
    {
        handle_k1_short_press();
    }
    else if (index == 1)
    {
        handle_k2_short_press();
    }
    else if (index == 2)
    {
        set_tft_backlight_percent((int)tft_backlight_percent - TFT_BACKLIGHT_STEP_PERCENT);
        show_brightness_toast();
    }
    else
    {
        set_tft_backlight_percent((int)tft_backlight_percent + TFT_BACKLIGHT_STEP_PERCENT);
        show_brightness_toast();
    }
}

void dispatch_button_long_press(int index)
{
    if (index == 0)
    {
        handle_k1_long_press();
    }
    else if (index == 1)
    {
        handle_k2_long_press();
    }
    else if (index == 2)
    {
        set_speaker_volume_percent((int)speaker_volume_percent - SPEAKER_VOLUME_STEP_PERCENT);
        show_volume_toast();
    }
    else
    {
        set_speaker_volume_percent((int)speaker_volume_percent + SPEAKER_VOLUME_STEP_PERCENT);
        show_volume_toast();
    }
}

void service_one_control_button(int index)
{
    ControlButtonState *button = &control_buttons[index];
    const unsigned long now = millis();
    bool fall_latched = false;
    noInterrupts();
    if (button->fall_latched == true)
    {
        button->fall_latched = false;
        fall_latched = true;
    }
    interrupts();
    if (fall_latched == true)
    {
        if (digitalRead(button->pin) == LOW)
        {
            button->raw_pressed = true;
            button->stable_pressed = true;
            button->pressed_time = now;
            button->long_press_fired = false;
            button->next_repeat_time = 0;
            button->last_change_time = now;
            if (index == 1)
            {
                Serial.println("K2/GPIO21 下降沿已锁存");
            }
        }
        else if (now - button->last_event_time >= BUTTON_LOCKOUT_MS)
        {
            // 动画或 SPI 阻塞期间已经点完松开，补一次短按。
            button->raw_pressed = false;
            button->stable_pressed = false;
            button->irq_armed = true;
            button->last_event_time = now;
            if (button->long_press_fired == false)
            {
                dispatch_button_short_press(index);
            }
            return;
        }
        else
        {
            button->irq_armed = true;
        }
    }
    const bool raw_pressed = digitalRead(button->pin) == LOW;
    if (raw_pressed != button->raw_pressed)
    {
        button->raw_pressed = raw_pressed;
        button->last_change_time = now;
    }
    if (raw_pressed != button->stable_pressed &&
        now - button->last_change_time >= BUTTON_DEBOUNCE_MS)
    {
        button->stable_pressed = raw_pressed;
        if (button->stable_pressed == true)
        {
            button->pressed_time = now;
            button->long_press_fired = false;
            button->next_repeat_time = 0;
        }
        else
        {
            button->irq_armed = true;
            if (button->long_press_fired == false &&
                now - button->last_event_time >= BUTTON_LOCKOUT_MS)
            {
                button->last_event_time = now;
                dispatch_button_short_press(index);
            }
        }
    }
    if (button->stable_pressed == true &&
        button->long_press_fired == false &&
        now - button->pressed_time >= BUTTON_LONG_PRESS_MS)
    {
        button->long_press_fired = true;
        button->next_repeat_time = now + BUTTON_REPEAT_MS;
        button->last_event_time = now;
        dispatch_button_long_press(index);
    }
    if (button->stable_pressed == true &&
        button->long_press_fired == true &&
        (index == 2 || index == 3) &&
        (long)(now - button->next_repeat_time) >= 0)
    {
        button->next_repeat_time = now + BUTTON_REPEAT_MS;
        if (index == 3)
        {
            set_speaker_volume_percent((int)speaker_volume_percent + SPEAKER_VOLUME_STEP_PERCENT);
        }
        else
        {
            set_speaker_volume_percent((int)speaker_volume_percent - SPEAKER_VOLUME_STEP_PERCENT);
        }
        show_volume_toast();
    }
}

void service_control_buttons(void)
{
    for (int index = 0; index < 4; index++)
    {
        service_one_control_button(index);
    }
}

void service_relay_preconnect(void)
{
    // 上电不等待 Wi-Fi；联网后由主循环在后台建立唯一的中转长连接。
    if (wifi_ready == false || relay_client_started == true)
    {
        return;
    }
    asr_socket.onEvent(handle_relay_event);
    asr_socket.setReconnectInterval(2000);
    asr_socket.enableHeartbeat(15000, 8000, 2);
    asr_socket.begin(XIYUNBAO_RELAY_HOST, XIYUNBAO_RELAY_PORT, XIYUNBAO_RELAY_PATH);
    relay_connecting = true;
    relay_client_started = true;
    Serial.println("正在后台预连接电脑中转服务");
}

void setup(void)
{
    // 必须放在 setup() 最前面，尽早压低背光；屏幕第一帧准备好之前不可点亮。
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, LOW);
    Serial.begin(115200);
    // 记录开始等待 USB CDC 串口的时间。
    unsigned long serial_wait_start = millis();
    // 等待电脑建立 USB CDC 串口连接，最长等待五秒。
    while (!Serial && millis() - serial_wait_start < 300)
    {
        // 短暂让出处理器，等待 USB 串口完成枚举。
        delay(10);
    }
    // 等待串口稳定，避免第一行文字丢失。
    Serial.println("小云宝 ESP32-S3 N16R8 启动");
    Serial.print("PSRAM KB 总量/空闲: ");
    Serial.print(ESP.getPsramSize() / 1024);
    Serial.print("/");
    Serial.println(ESP.getFreePsram() / 1024);
    Serial.print("固件版本: ");
    Serial.println(XIYUNBAO_FIRMWARE_VERSION);
    camera_emotion_begin();
    // 屏幕和共用 SPI 总线最先初始化。即使 Wi-Fi 或 TF 卡异常，
    // 通电后也能立即看到启动提示，不会长时间保持黑屏。
    shared_spi.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SD_CS);
    pinMode(PIN_TFT_CS, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
    setup_tft_backlight_pwm();
    display.init(240, 320);
    // 整屏转 180 度：排线原来在上的一头现在在下，画面仍按原来的上下显示。
    display.setRotation(0);
    display.fillScreen(ST77XX_BLACK);
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);
    chinese_font.begin(display);
    chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
    chinese_font.setFontMode(1);
    screen_ready = true;
    last_user_activity_ms = millis();
    tft_user_backlight_percent = tft_backlight_percent;
    Serial.print("屏幕硬件初始化耗时 ms: " );
    Serial.println(millis());
    tts_audio_queue = xQueueCreate(TTS_AUDIO_QUEUE_LENGTH, sizeof(TtsAudioChunk));
    if (tts_audio_queue == NULL)
    {
        Serial.println("TTS 音频队列创建失败");
    }
    else
    {
        BaseType_t task_result = xTaskCreatePinnedToCore(
            tts_playback_task,
            "tts_playback",
            6144,
            NULL,
            3,
            &tts_playback_task_handle,
            0);
        if (task_result != pdPASS)
        {
            Serial.println("TTS 播放任务创建失败");
            vQueueDelete(tts_audio_queue);
            tts_audio_queue = NULL;
            tts_playback_task_handle = NULL;
        }
        else
        {
            Serial.print("TTS 独立播放任务已启动，安全缓冲毫秒: ");
            Serial.println(TTS_START_BUFFER_MS);
        }
    }
    relay_opus_packet_queue = xQueueCreate(RELAY_OPUS_PACKET_QUEUE_LENGTH, sizeof(RelayOpusItem));
    if (relay_opus_packet_queue == NULL)
    {
        Serial.println("Opus 压缩包队列创建失败");
    }
    else
    {
        BaseType_t opus_task_result = xTaskCreatePinnedToCore(
            relay_opus_decoder_task,
            "opus_decoder",
            RELAY_OPUS_DECODER_STACK_BYTES,
            NULL,
            2,
            &relay_opus_decoder_task_handle,
            1);
        if (opus_task_result != pdPASS)
        {
            Serial.println("Opus 独立解码任务创建失败");
            vQueueDelete(relay_opus_packet_queue);
            relay_opus_packet_queue = NULL;
            relay_opus_decoder_task_handle = NULL;
        }
        else
        {
            Serial.println("Opus 独立解码任务已启动，栈空间: 32 KB");
        }
    }
    wifi_load_credentials();
    wifi_start_station();
    Serial.println("WiFi 使用手机配网或已保存网络，不阻塞屏幕启动");
    // 优先用 10 MHz 挂载，失败时再降到 4 MHz 和 400 kHz。SD.begin() 内部
    // 会按规范完成低速识别，传入值是挂载成功后的总线运行频率。
    bool sd_initialized = false;
    uint32_t sd_active_clock = 0;
    const uint32_t sd_clock_rates[] = {10000000, 4000000, 400000};
    for (size_t sd_attempt = 0; sd_attempt < 2 && sd_initialized == false; sd_attempt++)
    {
        for (size_t sd_speed_index = 0; sd_speed_index < 3 && sd_initialized == false; sd_speed_index++)
        {
            digitalWrite(PIN_TFT_CS, HIGH);
            digitalWrite(PIN_SD_CS, HIGH);
            sd_initialized = SD.begin(PIN_SD_CS, shared_spi, sd_clock_rates[sd_speed_index]);
            if (sd_initialized == true)
            {
                sd_active_clock = sd_clock_rates[sd_speed_index];
            }
            else
            {
                SD.end();
                delay(80);
            }
        }
    }
    if (sd_initialized)
    {
        sd_card_ready = true;
        Serial.print("TF 卡容量 MB=");
        Serial.println((uint32_t)(SD.cardSize() / (1024 * 1024)));
        Serial.print("TF 卡 SPI 频率 Hz=");
        Serial.println(sd_active_clock);
        // 优先显示用户放入 TF 卡的启动界面，代替旧版文字启动提示。
        const unsigned long startup_ui_start = millis();
        if (draw_rgb565_file("/ui/startup.rgb565") == true)
        {
            Serial.print("启动 UI 已显示，耗时 ms: " );
            Serial.println(millis() - startup_ui_start);
        }
        else
        {
            Serial.println("启动 UI 未找到或读取失败: /ui/startup.rgb565");
        }
        SD.mkdir("/ui");
        SD.mkdir("/ui/cute");
        SD.mkdir("/ui/sleep");
        SD.mkdir("/egg");
    }
    else
    {
        Serial.println("TF 卡挂载失败，请检查供电、CS=15 和 FAT32 格式");
    }
    // 此时启动图已经完整写入显存（失败时显存是初始化后的纯黑），现在才
    // 打开背光，因此用户第一眼不会看到随机花屏。
    if (tft_backlight_pwm_ready == true)
    {
        set_tft_backlight_percent(TFT_BACKLIGHT_INITIAL_PERCENT);
    }
    else
    {
        digitalWrite(PIN_TFT_BL, HIGH);
        tft_backlight_percent = TFT_BACKLIGHT_INITIAL_PERCENT;
        Serial.println("屏幕背光已开启: 常亮模式");
    }
    // 208x144 RGB565 画布约占 60 KB；优先保证聊天区无闪烁整帧更新。
    chat_canvas = new GFXcanvas16(UI_BUBBLE_RIGHT - UI_BUBBLE_LEFT + 1,
                                  CHAT_TEXT_BOTTOM - CHAT_TEXT_TOP + 1);
    if (chat_canvas != NULL && chat_canvas->getBuffer() != NULL)
    {
        chat_canvas_font.begin(*chat_canvas);
        chat_canvas_font.setFont(u8g2_font_wqy16_t_gb2312);
        chat_canvas_font.setFontMode(1);
        Serial.println("聊天离屏画布初始化完成，启用无闪烁整帧刷新");
    }
    else
    {
        Serial.println("聊天离屏画布内存不足，退回直接绘制模式");
    }
    english_chat_canvas = new GFXcanvas16(EN_CHAT_RIGHT - EN_CHAT_LEFT + 1,
                                          EN_CHAT_BOTTOM - EN_CHAT_TOP + 1);
    if (english_chat_canvas != NULL && english_chat_canvas->getBuffer() != NULL)
    {
        english_chat_font.begin(*english_chat_canvas);
        english_chat_font.setFont(u8g2_font_wqy16_t_gb2312);
        english_chat_font.setFontMode(1);
        Serial.println("英语对话离屏画布初始化完成，启用无闪烁整帧刷新");
    }
    else
    {
        Serial.println("英语对话离屏画布内存不足，英语模式将尽量少重绘");
    }
    // 启动页保持显示时先把招手动画缓存到 PSRAM；资源准备完成后再切换
    // 到聊天主界面，避免启动图刚出现就被立即覆盖。
    load_greeting_animation_frames();
    load_listen_animation_frames();
    const unsigned long ui_load_start = millis();
    draw_sd_ui_background();
    load_english_ui_image();
    setup_microphone();
    setup_control_buttons();
    Serial.print("千问模型: ");
    Serial.println(XIYUNBAO_QWEN_MODEL);
    Serial.print("ASR 模型: ");
    Serial.println(XIYUNBAO_ASR_MODEL);
    Serial.print("TTS 音色: ");
    Serial.println(XIYUNBAO_COSYVOICE_VOICE_ID);
    report_state("IDLE");
    if (device_wifi_connected() == false)
    {
        draw_network_offline_notice();
    }
    else
    {
        draw_status("小云宝", "请唤醒我开始交流");
    }
    Serial.print("UI 显示完成耗时 ms: " );
    Serial.println(millis() - ui_load_start);
    // 所有启动资源已经就绪，现在开始显示招手动画。
    start_greeting_animation();
    // 此时若 Wi-Fi 已连接就立即预连接；配网中则只服务热点页面。
    if (wifi_provision_active == false)
    {
        service_wifi_connection();
        service_relay_preconnect();
    }
}

void loop(void)
{
    service_wifi_provision();
    if (wifi_provision_active == false)
    {
        service_wifi_connection();
        service_network_offline_notice();
        service_relay_preconnect();
    }
    service_pending_voice_turn();
    // 中转连接跨越录音、思考和播放全过程，主循环始终驱动它。
    asr_socket.loop();
    if (relay_opus_decode_error_pending == true)
    {
        relay_opus_decode_error_pending = false;
        relay_audio_started = false;
        reset_relay_opus_decoder();
        if (tts_active == true || turn_busy == true)
        {
            mark_tts_failure("Opus 解码或音频入队失败", false);
        }
    }
    // 保存上一次发送串口心跳的时间。
    static unsigned long serial_heartbeat_time = 0;
    // 每隔三十秒发送一次简短心跳，避免串口持续刷屏干扰调试。
    if (millis() - serial_heartbeat_time >= 30000)
    {
        // 记录本次发送心跳的时间。
        serial_heartbeat_time = millis();
        // 在串口监视器中提示当前程序仍在运行，不重复要求用户唤醒。
        Serial.println("小云宝运行中");
    }
    service_local_wake();
    // 先处理实体按键；摄像头已改后台任务，不再在这里做 JPEG 压缩。
    service_control_buttons();
    service_companion_standby();
    service_english_ready_announce();
    service_camera_emotion();
    // 动画前后再扫一次按键，避免整帧 SPI 期间漏掉唤醒/打断。
    service_control_buttons();
    service_button_toast();
    if (language_switch_pending == true && turn_busy == false && asr_active == false &&
        tts_active == false && tts_playback_started == false && tts_playback_busy == false)
    {
        language_switch_pending = false;
        set_dialog_language_mode(pending_english_mode, true);
    }
    if (continuous_dialog_active == true &&
        tts_active == false &&
        turn_busy == false &&
        asr_active == false &&
        tts_playback_started == false &&
        tts_playback_busy == false &&
        continuous_dialog_last_voice_time != 0 &&
        millis() - continuous_dialog_last_voice_time >= CONTINUOUS_DIALOG_IDLE_TIMEOUT_MS)
    {
        // 30 秒静音退出需要同时终止正在进行的云端 ASR，不能只清除会话标志。
        bool had_asr_connection = asr_active;
        asr_result_ready = false;
        recording = false;
        asr_active = false;
        asr_preview_dirty = false;
        asr_sentence_end_pending = false;
        discard_dialog_user_preview();
        if (had_asr_connection == true)
        {
            asr_disconnect_expected = true;
            asr_socket.disconnect();
        }
        stop_continuous_dialog("30S_NO_VOICE");
        report_state("IDLE");
        draw_status("小云宝", "请唤醒我开始交流");
    }
    // 动画放在按键和会话状态处理之后，整帧刷新不会阻塞按键消抖。
    service_listen_animation();
    service_greeting_animation();
    service_control_buttons();
    // 按实际 TTS PCM 播放进度逐字刷新机器人文字，实现声音与文字同步。
    update_dialog_reply_page();
    while (Serial.available() > 0)
    {
        char command = (char)Serial.read();
        if (command == 'r' || command == 'R')
        {
            request_voice_turn("USB_SERIAL");
        }
        else if (command == 'p' || command == 'P')
        {
            play_speaker_test_tone();
        }
        else if (command == 't' || command == 'T')
        {
            if (relay_connected == true && turn_busy == false && tts_active == false)
            {
                Serial.println("发送电脑中转语音测试");
                relay_send_json("{\"type\":\"say\",\"text\":\"爸爸你好，我是小云宝。现在测试电脑中转音频，声音应该连续清楚。\",\"emotion\":\"happy\"}");
            }
            else
            {
                Serial.println("中转语音测试未启动：连接未就绪或设备忙碌");
            }
        }
        else if (command == 'm' || command == 'M')
        {
            // 多句连续测试：整条只合成一次，文字按句先出现，语音不中断。
            current_emotion = "happy";
            set_tts_parameters_for_emotion(current_emotion);
            start_reply_sentence_sequence("爸爸好，我是小云宝。第一句话会先完整显示，再开始播放。播放结束后，下一句话才会出现并发声。");
        }
        else if (command == 'u' || command == 'U')
        {
            // 电脑串口输入 u 等价于实体上键，查看更早聊天记录。
            scroll_chat_history(1);
        }
        else if (command == 'd' || command == 'D')
        {
            // 电脑串口输入 d 等价于实体下键，查看更新聊天记录。
            scroll_chat_history(-1);
        }
        else if (command == 'x' || command == 'X')
        {
            // 电脑串口输入 x 结束本次连续对话；当前播放中的语音不会被硬切断。
            stop_continuous_dialog("USB_SERIAL");
        }
        else if (command == 'w' || command == 'W')
        {
            wifi_provision_begin("USB_SERIAL");
        }
        else if (command == 'b' || command == 'B')
        {
            Serial.print("按键电平 K1/18=");
            Serial.print(digitalRead(PIN_BUTTON_K1) == LOW ? "按下" : "松开");
            Serial.print("  K2/21=");
            Serial.print(digitalRead(PIN_BUTTON_K2) == LOW ? "按下" : "松开");
            Serial.print("  K3/3=");
            Serial.print(digitalRead(PIN_BUTTON_K3) == LOW ? "按下" : "松开");
            Serial.print("  K4/45=");
            Serial.println(digitalRead(PIN_BUTTON_K4) == LOW ? "按下" : "松开");
        }
    }
    if (asr_active == true &&
        asr_started == false &&
        relay_connected == true &&
        english_announce_active == false &&
        listen_start_sent_time != 0 &&
        listen_start_retry_count < 3 &&
        (long)(millis() - listen_start_sent_time) >= 1500)
    {
        listen_start_retry_count = (uint8_t)(listen_start_retry_count + 1);
        listen_start_sent_time = millis();
        Serial.print("未收到 listen.started，重试开始收音 ");
        Serial.println(listen_start_retry_count);
        relay_send_json(english_mode_enabled == true
            ? "{\"type\":\"listen.start\",\"language\":\"en\"}"
            : "{\"type\":\"listen.start\",\"language\":\"zh\"}");
    }
    if (asr_active == true &&
        asr_started == false &&
        relay_connected == true &&
        listen_start_retry_count >= 3 &&
        listen_start_sent_time != 0 &&
        (long)(millis() - listen_start_sent_time) >= 2000)
    {
        Serial.println("收音没有启动，重新打开麦克风");
        listen_start_retry_count = 0;
        listen_start_sent_time = millis();
        setup_microphone();
        recording = true;
        relay_send_json(english_mode_enabled == true
            ? "{\"type\":\"listen.start\",\"language\":\"en\"}"
            : "{\"type\":\"listen.start\",\"language\":\"zh\"}");
    }
    if (asr_active == true)
    {
        if (recording == true)
        {
            send_microphone_audio();
            unsigned long current_time = millis();
            bool sentence_confirmed = asr_sentence_end_pending == true &&
                                      (long)(current_time - asr_sentence_confirm_time) >= 0;
            bool recording_deadline_reached = (long)(current_time - record_stop_time) >= 0;
            bool hard_deadline_reached = (long)(current_time - record_hard_stop_time) >= 0;
            if (asr_started == true &&
                asr_finish_sent == false &&
                (sentence_confirmed == true || recording_deadline_reached == true || hard_deadline_reached == true))
            {
                relay_send_json("{\"type\":\"listen.stop\"}");
                asr_finish_sent = true;
                recording = false;
                ui_listening_indicator_active = false;
                asr_sentence_end_pending = false;
                report_state("THINKING");
                draw_status("思考中", "正在识别语音");
                Serial.println(sentence_confirmed ? "句末确认窗口结束，提交完整语句" : "录音时间到，提交当前语句");
            }
        }
        if (millis() >= asr_timeout)
        {
            const bool was_commit = listen_commit_pending == true;
            const bool was_wake_listen = wake_listening_active == true;
            const bool had_started = asr_started == true;
            Serial.println("ASR 监听超时，清理当前会话");
            abort_current_listen("ASR_TIMEOUT");
            if (had_started == false && relay_connected == true)
            {
                // A missing listen.started usually means a stale relay session.
                // Reconnect it once so the next request starts with a clean task.
                asr_socket.disconnect();
            }
            if (was_commit)
            {
                stop_continuous_dialog("K1_COMMIT_TIMEOUT");
                draw_wake_prompt();
                Serial.println("结束收音后识别超时，已回到待机");
            }
            else if (was_wake_listen)
            {
                report_state("IDLE");
                draw_status("小云宝", "请唤醒我开始交流");
                Serial.println("唤醒监听超时，稍后重新监听");
            }
            else
            {
                // 持续模式下单次云端超时只重试本轮，不能让设备永久停止收音。
                report_state("ERROR");
                draw_status("识别超时", "正在恢复监听");
                if (continuous_dialog_active == true)
                {
                    continuous_dialog_restart_pending = true;
                    continuous_dialog_restart_time = millis() + CONTINUOUS_DIALOG_EMPTY_RETRY_DELAY_MS;
                    Serial.println("ASR 超时，800 毫秒后自动恢复监听");
                }
            }
        }
    }
    // WebSocket 回调只更新文字；TFT 重绘统一在主循环节流执行，避免阻塞音频和网络回调。
    if (asr_preview_dirty == true && continuous_dialog_active == true && wake_listening_active == false &&
        millis() - chat_user_preview_last_refresh_time >= CHAT_USER_PREVIEW_REFRESH_INTERVAL_MS)
    {
        asr_preview_dirty = false;
        update_dialog_user_preview(final_text);
        chat_user_preview_last_refresh_time = millis();
    }
    // 中转服务收到最终识别后会直接启动千问和 TTS，不再让 ESP32 重复请求云端。
    if (tts_active == true)
    {
        if (tts_playback_failed == true && tts_error_pending == false)
        {
            mark_tts_failure("I2S 播放任务失败", true);
        }
        if (tts_finished == false && (long)(millis() - tts_timeout) >= 0)
        {
            Serial.print("TTS 连续 30 秒无活动，音频包数: ");
            Serial.print((unsigned long)tts_audio_packet_count);
            Serial.print(", 音频字节数: ");
            Serial.println((unsigned long)tts_audio_byte_count);
            mark_tts_failure("连续 30 秒未收到 TTS 数据", true);
        }
    }
    finalize_tts_if_ready();
    // 播放任务完成后再次检查收尾状态，确保连续对话一定排入下一轮监听。
    if (tts_finished == true &&
        tts_playback_drained == true &&
        tts_playback_busy == false &&
        tts_cleanup_done == false)
    {
        finalize_tts_if_ready();
    }
    // 待机阶段禁止创建百炼 ASR 连接。后续只有本地“小云宝”识别成功，
    // 或明确的本地按键/串口触发，才允许进入 start_voice_turn()。
    // 上一轮回答播放并收尾完成后，自动开启连续对话的下一轮聆听。
    if (continuous_dialog_restart_pending == true &&
        continuous_dialog_active == true &&
        asr_disconnect_expected == false &&
        turn_busy == false &&
        asr_active == false &&
        tts_active == false &&
        (long)(millis() - continuous_dialog_restart_time) >= 0)
    {
        // 先清除待启动标记，避免主循环在连接建立前重复启动 ASR。
        continuous_dialog_restart_pending = false;
        // 自动续听不经过实体按键防抖，也不会重新创建会话。
        Serial.println("连续对话：自动进入下一轮聆听");
        start_voice_turn();
    }
    delay(2);
}

// Phone Wi-Fi provisioning and saved-credential station connect
#include "wifi_provision.h"
// WiFi and wake-word services implementation
#include "wifi_wakeup.h"
// INMP441 microphone and MAX98357A audio implementation
#include "audio_io.h"
// TFT, SD background and drawing implementation
#include "display_ui.h"
// Chat history and sentence-synchronized UI implementation
#include "chat_ui.h"
// Computer relay and ASR communication implementation
#include "dialog_relay.h"
#include "camera_emotion.h"
// Sentence queue, TTS streaming and playback implementation
#include "tts_pipeline.h"
// Qwen request, SSE parsing and emotion mapping implementation
#include "qwen_client.h"
