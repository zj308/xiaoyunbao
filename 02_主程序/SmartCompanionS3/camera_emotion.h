// Confirmed OV2640 path: Y1 crystal, XCLK unused, UART0 released from 43/44.
// Capture/encode runs on a FreeRTOS task so loop()/buttons/animation stay responsive.
#include "esp_camera.h"
#include "img_converters.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#define CAM_D0 16
#define CAM_D1 17
#define CAM_D2 38
#define CAM_D3 39
#define CAM_D4 40
#define CAM_D5 41
#define CAM_D6 42
#define CAM_D7 43
#define CAM_XCLK -1
#define CAM_PCLK 44
#define CAM_VSYNC 47
#define CAM_HREF 46
#define CAM_SIOD 1
#define CAM_SIOC 2
#define CAM_UPLOAD_MIN_MS 80
#define CAM_ACTIVE_CARE_MS 500
#define CAM_DIALOG_INTERVAL_MS 1000
#define CAM_STANDBY_INTERVAL_MS 300000UL
#define CAM_JPEG_MAX_BYTES (48 * 1024)
#define CAM_JPEG_QUALITY 16
#define CAM_CAPTURE_FAILURE_LIMIT 8
#define CAM_INIT_RETRY_MIN_MS 5000
#define CAM_INIT_RETRY_MAX_MS 30000
#define CAM_DIAGNOSTIC_INTERVAL_MS 30000
#define CAM_CARE_LINE_MS 6500

static volatile bool camera_ready = false;
static volatile bool camera_waiting = false;
static volatile bool camera_enabled = true;
static bool camera_task_started = false;
static volatile unsigned long camera_next_probe_ms = 0;
static volatile unsigned long camera_settle_until_ms = 0;
static volatile bool camera_ack_seen = false;
static volatile bool camera_init_request = false;
static volatile bool camera_initializing = false;
static unsigned int camera_init_failure_count = 0;
static unsigned int camera_capture_failure_streak = 0;
static volatile unsigned long camera_capture_total = 0;
static volatile unsigned long camera_capture_success = 0;
static volatile unsigned long camera_upload_success = 0;
static unsigned long camera_last_diagnostic_ms = 0;
static unsigned long camera_next_upload_ms = 0;
static volatile pixformat_t camera_active_format = PIXFORMAT_JPEG;

static uint8_t *camera_jpeg_buf = NULL;
static volatile size_t camera_jpeg_len = 0;
static volatile bool camera_jpeg_ready = false;
static portMUX_TYPE camera_jpeg_mux = portMUX_INITIALIZER_UNLOCKED;
static bool camera_care_round_active = false;

static void camera_release_uart0(void)
{
    Serial0.end();
    uart_driver_delete(UART_NUM_0);
    gpio_reset_pin((gpio_num_t)CAM_D7);
    gpio_reset_pin((gpio_num_t)CAM_PCLK);
}

static void camera_sccb_sda(int v)
{
    if (v) pinMode(CAM_SIOD, INPUT_PULLUP);
    else { pinMode(CAM_SIOD, OUTPUT); digitalWrite(CAM_SIOD, LOW); }
}

static void camera_sccb_scl(int v)
{
    if (v) pinMode(CAM_SIOC, INPUT_PULLUP);
    else { pinMode(CAM_SIOC, OUTPUT); digitalWrite(CAM_SIOC, LOW); }
}

static void camera_sccb_wait(void) { delayMicroseconds(20); }

static void camera_sccb_start(void)
{
    camera_sccb_sda(1); camera_sccb_scl(1); camera_sccb_wait();
    camera_sccb_sda(0); camera_sccb_wait();
    camera_sccb_scl(0); camera_sccb_wait();
}

static void camera_sccb_stop(void)
{
    camera_sccb_sda(0); camera_sccb_scl(1); camera_sccb_wait();
    camera_sccb_sda(1); camera_sccb_wait();
}

static bool camera_sccb_wr(uint8_t data)
{
    for (int i = 7; i >= 0; --i)
    {
        camera_sccb_sda((data >> i) & 1);
        camera_sccb_wait();
        camera_sccb_scl(1); camera_sccb_wait();
        camera_sccb_scl(0); camera_sccb_wait();
    }
    pinMode(CAM_SIOD, INPUT_PULLUP);
    camera_sccb_wait();
    camera_sccb_scl(1); camera_sccb_wait();
    bool ack = digitalRead(CAM_SIOD) == LOW;
    camera_sccb_scl(0); camera_sccb_wait();
    return ack;
}

static bool camera_sccb_ack(void)
{
    pinMode(CAM_SIOD, INPUT_PULLUP);
    pinMode(CAM_SIOC, INPUT_PULLUP);
    camera_sccb_start();
    bool ack = camera_sccb_wr(0x60);
    camera_sccb_stop();
    pinMode(CAM_SIOD, INPUT_PULLUP);
    pinMode(CAM_SIOC, INPUT_PULLUP);
    return ack;
}

static bool camera_ui_busy(void)
{
    return wifi_provision_active == true ||
           english_announce_active == true ||
           english_announce_pending == true ||
           recording == true;
}

static bool camera_overlay_blocked(void)
{
    return english_mode_enabled == true ||
           asr_active == true ||
           tts_active == true ||
           turn_busy == true ||
           tts_playback_started == true ||
           tts_playback_busy == true ||
           ui_listening_indicator_active == true ||
           ui_listen_animation_active == true ||
           network_offline_notice_visible == true;
}

static bool camera_buttons_busy(void)
{
    for (int index = 0; index < 4; index++)
    {
        if (control_buttons[index].stable_pressed == true ||
            control_buttons[index].fall_latched == true)
        {
            return true;
        }
    }
    return false;
}

static unsigned long camera_current_interval_ms(void)
{
    if (companion_standby_active == true && camera_care_round_active == false)
    {
        return CAM_STANDBY_INTERVAL_MS;
    }
    if (continuous_dialog_active == true || asr_active == true ||
        tts_active == true || turn_busy == true)
    {
        return CAM_DIALOG_INTERVAL_MS;
    }
    return CAM_ACTIVE_CARE_MS;
}

static bool camera_dvp_alive(void)
{
    // 只读取电平，不再对已经归 LCD_CAM 所有的 PCLK/VSYNC/HREF 调用 pinMode。
    int previous_pclk = digitalRead(CAM_PCLK);
    int previous_vsync = digitalRead(CAM_VSYNC);
    unsigned long pclk_rises = 0;
    unsigned long vsync_changes = 0;
    unsigned long href_high = 0;
    const unsigned long started_us = micros();
    while ((unsigned long)(micros() - started_us) < 120000UL)
    {
        int pclk = digitalRead(CAM_PCLK);
        if (pclk == HIGH && previous_pclk == LOW) pclk_rises++;
        previous_pclk = pclk;
        int vsync = digitalRead(CAM_VSYNC);
        if (vsync != previous_vsync) vsync_changes++;
        previous_vsync = vsync;
        if (digitalRead(CAM_HREF) == HIGH) href_high++;
    }
    Serial.printf("摄像头 DVP: P=%lu V=%lu H=%lu\n",
                  pclk_rises, vsync_changes, href_high);
    return pclk_rises > 0 && vsync_changes > 0 && href_high > 0;
}

static bool camera_init_mode(pixformat_t pixel_format,
                             framesize_t frame_size,
                             camera_fb_location_t frame_location,
                             int pll_hz,
                             int fb_count)
{
    tft_backlight_pwm_ready = false;
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_2;
    config.ledc_timer = LEDC_TIMER_2;
    config.pin_d0 = CAM_D0;
    config.pin_d1 = CAM_D1;
    config.pin_d2 = CAM_D2;
    config.pin_d3 = CAM_D3;
    config.pin_d4 = CAM_D4;
    config.pin_d5 = CAM_D5;
    config.pin_d6 = CAM_D6;
    config.pin_d7 = CAM_D7;
    config.pin_xclk = CAM_XCLK;
    config.pin_pclk = CAM_PCLK;
    config.pin_vsync = CAM_VSYNC;
    config.pin_href = CAM_HREF;
    config.pin_sccb_sda = CAM_SIOD;
    config.pin_sccb_scl = CAM_SIOC;
    config.pin_pwdn = -1;
    config.pin_reset = -1;
    config.xclk_freq_hz = pll_hz;
    config.pixel_format = pixel_format;
    config.frame_size = frame_size;
    config.jpeg_quality = CAM_JPEG_QUALITY;
    config.fb_count = fb_count;
    config.grab_mode = (fb_count > 1) ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = frame_location;

    Serial.printf("主程序摄像头尝试 format=%d size=%d pll=%dMHz fb=%d\n",
                  (int)pixel_format, (int)frame_size, pll_hz / 1000000, fb_count);
    esp_err_t err = esp_camera_init(&config);
    Serial.printf("主程序摄像头初始化 %s\n", esp_err_to_name(err));
    if (err != ESP_OK)
    {
        esp_camera_deinit();
        refresh_tft_backlight_pwm();
        return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == NULL)
    {
        esp_camera_deinit();
        refresh_tft_backlight_pwm();
        return false;
    }
    sensor->set_framesize(sensor, frame_size);
    if (pixel_format == PIXFORMAT_JPEG)
    {
        sensor->set_quality(sensor, CAM_JPEG_QUALITY);
    }
    // 与已通过的稳定预览方向一致；视觉模型不依赖串口命令切换方向。
    sensor->set_vflip(sensor, 1);
    sensor->set_hmirror(sensor, 0);
    vTaskDelay(pdMS_TO_TICKS(300));

    if (camera_dvp_alive() == false)
    {
        Serial.println("摄像头初始化后无 DVP 时序，尝试下一模式");
        esp_camera_deinit();
        refresh_tft_backlight_pwm();
        return false;
    }

    camera_fb_t *warm = esp_camera_fb_get();
    if (warm == NULL)
    {
        Serial.println("摄像头预热帧失败，尝试下一模式");
        esp_camera_deinit();
        refresh_tft_backlight_pwm();
        return false;
    }
    Serial.printf("摄像头预热帧成功 %ux%u %uB format=%d\n",
                  warm->width, warm->height, (unsigned int)warm->len,
                  (int)warm->format);
    esp_camera_fb_return(warm);
    camera_active_format = pixel_format;
    refresh_tft_backlight_pwm();
    return true;
}

static bool camera_init_sensor(void)
{
    camera_release_uart0();
    const int jpeg_pll_frequencies[] = {12000000, 24000000, 20000000};
    for (int pll_hz : jpeg_pll_frequencies)
    {
        if (camera_init_mode(PIXFORMAT_JPEG, FRAMESIZE_QVGA,
                             CAMERA_FB_IN_PSRAM, pll_hz, 2))
        {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        if (camera_init_mode(PIXFORMAT_JPEG, FRAMESIZE_QVGA,
                             CAMERA_FB_IN_PSRAM, pll_hz, 1))
        {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    Serial.println("JPEG模式均未通过，回退RGB565 QQVGA 12MHz");
    return camera_init_mode(PIXFORMAT_RGB565, FRAMESIZE_QQVGA,
                            CAMERA_FB_IN_DRAM, 12000000, 1);
}

static uint8_t *camera_alloc_psram(size_t bytes)
{
    uint8_t *ptr = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL)
    {
        ptr = (uint8_t *)malloc(bytes);
    }
    return ptr;
}

static bool camera_queue_upload(const uint8_t *data, size_t len)
{
    if (data == NULL || camera_jpeg_buf == NULL || len < 32 || len > CAM_JPEG_MAX_BYTES)
    {
        return false;
    }
    bool can_upload = false;
    portENTER_CRITICAL(&camera_jpeg_mux);
    can_upload = (camera_jpeg_ready == false);
    portEXIT_CRITICAL(&camera_jpeg_mux);
    if (can_upload == false)
    {
        return false;
    }
    memcpy(camera_jpeg_buf, data, len);
    portENTER_CRITICAL(&camera_jpeg_mux);
    camera_jpeg_len = len;
    camera_jpeg_ready = true;
    portEXIT_CRITICAL(&camera_jpeg_mux);
    return true;
}

static bool camera_capture_into_buffer(void)
{
    bool pending = false;
    portENTER_CRITICAL(&camera_jpeg_mux);
    pending = camera_jpeg_ready;
    portEXIT_CRITICAL(&camera_jpeg_mux);
    if (pending == true)
    {
        return true;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    camera_capture_total++;
    if (fb == NULL)
    {
        return false;
    }

    bool queued = false;
    if (fb->format == PIXFORMAT_JPEG)
    {
        queued = camera_queue_upload(fb->buf, fb->len);
    }
    else
    {
        uint8_t *jpg = NULL;
        size_t jpg_len = 0;
        if (fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format,
                    CAM_JPEG_QUALITY, &jpg, &jpg_len) &&
            jpg != NULL)
        {
            queued = camera_queue_upload(jpg, jpg_len);
        }
        if (jpg != NULL)
        {
            free(jpg);
        }
    }
    esp_camera_fb_return(fb);
    if (queued == true)
    {
        camera_capture_success++;
        camera_next_upload_ms = millis() + CAM_UPLOAD_MIN_MS;
        return true;
    }
    return true;
}

static unsigned long camera_init_retry_delay_ms(void)
{
    unsigned long delay_ms = CAM_INIT_RETRY_MIN_MS;
    for (unsigned int index = 1;
         index < camera_init_failure_count && delay_ms < CAM_INIT_RETRY_MAX_MS;
         index++)
    {
        delay_ms *= 2;
    }
    if (delay_ms > CAM_INIT_RETRY_MAX_MS) delay_ms = CAM_INIT_RETRY_MAX_MS;
    return delay_ms;
}

static void camera_capture_task(void *parameter)
{
    (void)parameter;
    unsigned long next_capture = 0;
    for (;;)
    {
        if (camera_enabled == false)
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (camera_init_request == true && camera_ready == false)
        {
            camera_initializing = true;
            camera_init_request = false;
            bool ok = camera_init_sensor();
            camera_ready = ok;
            camera_waiting = !ok;
            camera_ack_seen = false;
            camera_initializing = false;
            Serial.println(ok ? "摄像头就绪，后台上传画面给网页" : "摄像头初始化失败，稍后重试");
            if (ok == true)
            {
                camera_init_failure_count = 0;
                camera_capture_failure_streak = 0;
                camera_next_upload_ms = millis() + 400;
                next_capture = millis() + 200;
            }
            else
            {
                camera_init_failure_count++;
                camera_waiting = true;
                const unsigned long retry_delay = camera_init_retry_delay_ms();
                camera_next_probe_ms = millis() + retry_delay;
                Serial.printf("摄像头初始化退避 %lu ms\n", retry_delay);
            }
            continue;
        }
        if (camera_ready == false)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        const unsigned long now = millis();
        if ((long)(now - next_capture) < 0)
        {
            unsigned long wait_ms = next_capture - now;
            if (wait_ms > 200) wait_ms = 200;
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
            continue;
        }
        if (camera_ui_busy() == true)
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        const unsigned long interval_ms = camera_current_interval_ms();
        next_capture = now + interval_ms;
        if (camera_capture_into_buffer() == true)
        {
            camera_capture_failure_streak = 0;
        }
        else
        {
            camera_capture_failure_streak++;
            Serial.printf("摄像头抓帧失败，连续次数=%u\n",
                          camera_capture_failure_streak);
            if (camera_capture_failure_streak >= CAM_CAPTURE_FAILURE_LIMIT)
            {
                Serial.println("摄像头连续抓帧失败，停止采集并在5秒后有限重试");
                esp_camera_deinit();
                refresh_tft_backlight_pwm();
                camera_ready = false;
                camera_waiting = true;
                camera_ack_seen = false;
                camera_capture_failure_streak = 0;
                camera_next_probe_ms = millis() + CAM_INIT_RETRY_MIN_MS;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

#define CAM_CARE_MIN_CONFIDENCE 0.22f
#define CAM_CARE_SWITCH_COUNT 3
#define CAM_CARE_MISS_ABORT 8

static const char *const CAM_CARE_ANGRY[] = {
    "怎么了？",
    "遇到让你生气的事了吗",
    "先停一下，我在这儿",
    "不用一个人憋着",
    "深呼吸，气会慢慢过去",
    "委屈可以跟我说",
    "谁惹你不高兴了",
    "别急着发脾气，我听着",
    "想骂两句也可以按键说",
    "我站你这边",
};
static const char *const CAM_CARE_FEAR[] = {
    "怎么了？",
    "看起来有点紧张",
    "遇到难过的事情了吗",
    "我陪着你，不着急",
    "先放松肩膀，慢慢来",
    "害怕的话不用硬撑",
    "我在，你不是一个人",
    "愿意的话按键跟我说",
    "先把心里那块石头放下",
    "我们一起把事情捋一捋",
};
static const char *const CAM_CARE_SURPRISE[] = {
    "怎么了？",
    "发生什么了吗",
    "吓到你了？",
    "突然出现什么状况了",
    "我听着，你慢慢说",
    "先缓一缓再告诉我",
    "按键就能跟我聊",
    "是好事还是吓了一跳",
};
static const char *const CAM_CARE_HAPPY[] = {
    "看到你在笑",
    "遇到开心的事了吗",
    "这笑容真好看",
    "今天心情不错呀",
    "想分享就按键跟我说",
    "把高兴的事也留给我听",
    "继续这样轻松就好",
    "要不要跟我说说喜事",
};

static String camera_care_emotion = "";
static String camera_pending_emotion = "";
static uint8_t camera_pending_streak = 0;
static uint8_t camera_care_misses = 0;
static int camera_care_line = -1;
static unsigned long camera_care_next_ms = 0;

static const char *const *camera_care_lines(const String &emotion, int *count)
{
    if (emotion == "angry")
    {
        *count = (int)(sizeof(CAM_CARE_ANGRY) / sizeof(CAM_CARE_ANGRY[0]));
        return CAM_CARE_ANGRY;
    }
    if (emotion == "fear")
    {
        *count = (int)(sizeof(CAM_CARE_FEAR) / sizeof(CAM_CARE_FEAR[0]));
        return CAM_CARE_FEAR;
    }
    if (emotion == "surprise")
    {
        *count = (int)(sizeof(CAM_CARE_SURPRISE) / sizeof(CAM_CARE_SURPRISE[0]));
        return CAM_CARE_SURPRISE;
    }
    if (emotion == "happy")
    {
        *count = (int)(sizeof(CAM_CARE_HAPPY) / sizeof(CAM_CARE_HAPPY[0]));
        return CAM_CARE_HAPPY;
    }
    *count = 0;
    return NULL;
}

static void camera_care_reset(bool restore_wake)
{
    camera_care_emotion = "";
    camera_pending_emotion = "";
    camera_pending_streak = 0;
    camera_care_misses = 0;
    camera_care_line = -1;
    camera_care_round_active = false;
    camera_care_next_ms = 0;
    if (restore_wake == true)
    {
        draw_wake_prompt();
    }
}

static void camera_care_start_round(const String &emotion)
{
    int count = 0;
    const char *const *lines = camera_care_lines(emotion, &count);
    if (lines == NULL || count <= 0)
    {
        return;
    }
    camera_care_emotion = emotion;
    camera_care_line = 0;
    camera_care_round_active = true;
    camera_care_next_ms = millis() + CAM_CARE_LINE_MS;
    camera_pending_emotion = "";
    camera_pending_streak = 0;
    camera_care_misses = 0;
    Serial.printf("屏幕关怀 round=%s line=0 %s\n", emotion.c_str(), lines[0]);
    note_user_activity();
    draw_companion_status_text(lines[0]);
}

static void camera_care_advance_if_due(void)
{
    if (camera_care_round_active == false)
    {
        return;
    }
    const unsigned long now = millis();
    if ((long)(now - camera_care_next_ms) < 0)
    {
        return;
    }

    int count = 0;
    const char *const *lines = camera_care_lines(camera_care_emotion, &count);
    if (lines == NULL || count <= 0)
    {
        camera_care_round_active = false;
        return;
    }

    if (camera_care_line + 1 < count)
    {
        camera_care_line++;
        camera_care_next_ms = now + CAM_CARE_LINE_MS;
        Serial.printf("屏幕关怀 round=%s line=%d %s\n",
                      camera_care_emotion.c_str(), camera_care_line, lines[camera_care_line]);
        draw_companion_status_text(lines[camera_care_line]);
        return;
    }

    camera_care_round_active = false;
}

static void camera_note_pending(const String &emotion)
{
    if (emotion == camera_pending_emotion)
    {
        if (camera_pending_streak < 255) camera_pending_streak++;
    }
    else
    {
        camera_pending_emotion = emotion;
        camera_pending_streak = 1;
    }
}

void camera_on_vision_emotion(const String &emotion, bool ok, bool face, float confidence)
{
    if (camera_overlay_blocked() == true)
    {
        return;
    }

    camera_care_advance_if_due();

    const bool accepted = (ok == true &&
                           face == true &&
                           confidence >= CAM_CARE_MIN_CONFIDENCE &&
                           emotion.length() > 0 &&
                           emotion != "neutral");

    // 一轮没走完：继续当前情绪的台词，不因为单次误判换一套。
    if (camera_care_round_active == true)
    {
        if (accepted == true && emotion == camera_care_emotion)
        {
            camera_care_misses = 0;
            camera_pending_emotion = "";
            camera_pending_streak = 0;
        }
        else if (accepted == true)
        {
            camera_note_pending(emotion);
        }
        else
        {
            camera_care_misses++;
            if (camera_care_misses >= CAM_CARE_MISS_ABORT)
            {
                camera_care_reset(true);
            }
        }
        return;
    }

    if (accepted == false)
    {
        camera_pending_streak = 0;
        if (camera_care_line >= 0)
        {
            camera_care_misses++;
            if (camera_care_misses >= 3)
            {
                camera_care_reset(true);
            }
        }
        return;
    }

    camera_care_misses = 0;

    // 还没开始，或上一轮刚走完且还是同一情绪：马上开一轮。
    if (camera_care_emotion.length() == 0 || emotion == camera_care_emotion)
    {
        camera_pending_emotion = "";
        camera_pending_streak = 0;
        camera_care_start_round(emotion);
        return;
    }

    // 上一轮走完后，新情绪要连续出现几次才换，避免误判。
    camera_note_pending(emotion);
    if (camera_pending_streak >= CAM_CARE_SWITCH_COUNT)
    {
        camera_care_start_round(emotion);
    }
}

void camera_emotion_begin(void)
{
    camera_release_uart0();
    if (camera_jpeg_buf == NULL)
    {
        camera_jpeg_buf = camera_alloc_psram(CAM_JPEG_MAX_BYTES);
    }
    camera_waiting = true;
    camera_ack_seen = false;
    camera_initializing = false;
    camera_init_request = false;
    camera_settle_until_ms = 0;
    camera_next_probe_ms = millis() + 1500;
    camera_last_diagnostic_ms = millis();
    Serial.println("摄像头：空闲关怀，对话中低频抓拍，待机约五分钟一张");
}

void service_camera_emotion(void)
{
    if (camera_enabled == false || wifi_provision_active == true) return;

    if (camera_task_started == false && camera_jpeg_buf != NULL)
    {
        BaseType_t ok = xTaskCreatePinnedToCore(
            camera_capture_task,
            "cam_capture",
            16384,
            NULL,
            2,
            NULL,
            1);
        camera_task_started = (ok == pdPASS);
        Serial.println(camera_task_started ? "摄像头后台任务已启动" : "摄像头后台任务启动失败");
    }

    const unsigned long now = millis();

    // Fast path: only ship an already-encoded JPEG. Keep ready=true throughout
    // sendBIN so the producer cannot overwrite the shared buffer while sending.
    if (camera_jpeg_ready == true &&
        relay_connected == true &&
        camera_ui_busy() == false &&
        camera_buttons_busy() == false)
    {
        size_t len = 0;
        portENTER_CRITICAL(&camera_jpeg_mux);
        len = camera_jpeg_len;
        portEXIT_CRITICAL(&camera_jpeg_mux);
        if (len >= 32 && camera_jpeg_buf != NULL)
        {
            relay_send_json("{\"type\":\"vision.frame\",\"mode\":\"stream\"}");
            bool sent = asr_socket.sendBIN(camera_jpeg_buf, len);
            if (sent == true)
            {
                camera_upload_success++;
            }
        }
        portENTER_CRITICAL(&camera_jpeg_mux);
        camera_jpeg_ready = false;
        camera_jpeg_len = 0;
        portEXIT_CRITICAL(&camera_jpeg_mux);
    }

    if (now - camera_last_diagnostic_ms >= CAM_DIAGNOSTIC_INTERVAL_MS)
    {
        camera_last_diagnostic_ms = now;
        Serial.printf("摄像头统计 ready=%d fmt=%d capture=%lu/%lu upload=%lu\n",
                      camera_ready ? 1 : 0, (int)camera_active_format,
                      camera_capture_success, camera_capture_total,
                      camera_upload_success);
    }

    if (camera_ready == true) return;
    if (camera_waiting == false) return;
    if (camera_initializing == true || camera_init_request == true) return;

    if (camera_ack_seen == true)
    {
        if ((long)(now - camera_settle_until_ms) < 0) return;
        if (camera_ui_busy() == true) return;
        camera_ack_seen = false;
        camera_init_request = true;
        return;
    }

    if ((long)(now - camera_next_probe_ms) < 0) return;
    camera_next_probe_ms = now + 2000;
    if (camera_sccb_ack() == false) return;
    camera_ack_seen = true;
    camera_settle_until_ms = now + 1500;
    Serial.println("摄像头已检测到，等待供电稳定后初始化");
}

bool camera_care_is_active(void)
{
    return camera_care_round_active == true;
}
