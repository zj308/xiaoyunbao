/*
 * OV2640 standalone electrical/capture test.
 * This sketch does not use the XiaoYunbao display, SD, audio, or relay code.
 * Board target: the existing ESP32-S3 N16R8 development board.
 */
#include "esp_camera.h"
#include <Wire.h>

// Unified PCB wiring. These pins are reserved for the camera in the main
// XiaoYunbao project too, so the display/audio/TF peripherals must use the
// alternate map documented in the handoff package.
#define CAM_D0 16
#define CAM_D1 17
#define CAM_D2 38
#define CAM_D3 39
#define CAM_D4 40
#define CAM_D5 41
#define CAM_D6 42
#define CAM_D7 43
// The V1.5 board has the Y1 oscillator fitted and no MCLK on the 18-pin
// header, so the ESP32 does not generate an XCLK output for this test.
#define CAM_XCLK -1
#define CAM_PCLK 44
#define CAM_VSYNC 47
// GPIO48 is the development board's onboard RGB LED data pin. Do not use it
// for camera HREF: the pixel-line signal would make the LED glow white.
// GPIO46 is input-only and is suitable for this camera input in standalone
// testing; leave the BOOT/RESET controls untouched during power-up.
#define CAM_HREF 46
#define CAM_SIOD 1
#define CAM_SIOC 2
// Hardware notes: PWDN is active high, RESETB is active low. Wire PWDN to GND
// and RST to 3V3 for this test; no ESP32 GPIO control is required.
#define CAM_PWDN -1
#define CAM_RESET -1

static bool camera_ready = false;
static unsigned long capture_count = 0;
static pixformat_t active_format = PIXFORMAT_JPEG;
static framesize_t active_framesize = FRAMESIZE_QVGA;
static const uint8_t CAMERA_INIT_RETRIES = 3;
static void prepare_sccb_bus();
static int active_siod = CAM_SIOD;
static int active_sioc = CAM_SIOC;

static bool read_sccb_register(uint8_t address, uint8_t reg, uint8_t *value)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)address, 1) != 1) return false;
    *value = (uint8_t)Wire.read();
    return true;
}

static bool probe_sccb_pair(int sda_pin, int scl_pin)
{
    pinMode(sda_pin, INPUT_PULLUP);
    pinMode(scl_pin, INPUT_PULLUP);
    Wire.begin(sda_pin, scl_pin, 100000);
    delay(10);
    Wire.beginTransmission(0x30);
    uint8_t ack = Wire.endTransmission();
    Serial.printf("SCCB测试 SDA=%d SCL=%d，地址0x30 ACK=%s\n",
                  sda_pin, scl_pin, ack == 0 ? "是" : "否");
    uint8_t pid = 0, ver = 0;
    bool pid_ok = read_sccb_register(0x30, 0x0A, &pid);
    bool ver_ok = read_sccb_register(0x30, 0x0B, &ver);
    Serial.printf("SCCB直接读 PID=%s0x%02X VER=%s0x%02X\n",
                  pid_ok ? "" : "失败/", pid,
                  ver_ok ? "" : "失败/", ver);
    Wire.end();
    prepare_sccb_bus();
    return ack == 0 && pid_ok && pid == 0x26;
}

static void probe_sccb_directly()
{
    // OV2640 uses the 7-bit SCCB address 0x30 (0x60 write / 0x61 read).
    if (probe_sccb_pair(CAM_SIOD, CAM_SIOC)) {
        active_siod = CAM_SIOD;
        active_sioc = CAM_SIOC;
        return;
    }
    // Some third-party 18-pin boards have the silk labels swapped. Detect
    // that case without requiring a wiring change.
    if (probe_sccb_pair(CAM_SIOC, CAM_SIOD)) {
        active_siod = CAM_SIOC;
        active_sioc = CAM_SIOD;
        Serial.printf("检测到 SDA/SCL 方向与默认相反，改用 SDA=%d SCL=%d\n",
                      active_siod, active_sioc);
        return;
    }
    active_siod = CAM_SIOD;
    active_sioc = CAM_SIOC;
}

static void prepare_sccb_bus()
{
    // SCCB/I2C is open-drain. The camera board must provide pull-ups; the
    // internal pull-ups only keep the bus idle long enough to diagnose a
    // missing external resistor on a breadboard.
    pinMode(CAM_SIOD, INPUT_PULLUP);
    pinMode(CAM_SIOC, INPUT_PULLUP);
    delay(5);
    Serial.printf("SCCB空闲电平(SDA/SCL)=%d/%d（应为1/1；为0表示短路或无上拉）\n",
                  digitalRead(CAM_SIOD), digitalRead(CAM_SIOC));
}

static void sample_camera_signals()
{
    // esp_camera_init() owns these GPIO-matrix routes. Do not call pinMode()
    // here: on some Arduino-ESP32 builds that can detach the camera input
    // signals and make every subsequent frame acquisition time out.
    int last_pclk = digitalRead(CAM_PCLK);
    unsigned long pclk_rise = 0;
    unsigned long vsync_change = 0;
    unsigned long href_high = 0;
    int last_vsync = digitalRead(CAM_VSYNC);
    unsigned long start = micros();
    while ((micros() - start) < 100000) {
        int pclk = digitalRead(CAM_PCLK);
        if (pclk && !last_pclk) pclk_rise++;
        last_pclk = pclk;
        int vsync = digitalRead(CAM_VSYNC);
        if (vsync != last_vsync) vsync_change++;
        last_vsync = vsync;
        if (digitalRead(CAM_HREF)) href_high++;
    }
    Serial.printf("信号诊断(100ms): PCLK上升沿=%lu VS变化=%lu HREF高采样=%lu 电平(PCLK/VS/HREF)=%d/%d/%d\n",
                  pclk_rise, vsync_change, href_high,
                  digitalRead(CAM_PCLK), digitalRead(CAM_VSYNC), digitalRead(CAM_HREF));
}

bool init_camera(pixformat_t format = PIXFORMAT_JPEG, framesize_t size = FRAMESIZE_QVGA)
{
    Serial.printf("GPIO: D0..D7=%d,%d,%d,%d,%d,%d,%d,%d PCLK=%d VS=%d HREF=%d SDA=%d SCL=%d XCLK=%d\n",
                  CAM_D0, CAM_D1, CAM_D2, CAM_D3, CAM_D4, CAM_D5, CAM_D6,
                  CAM_D7, CAM_PCLK, CAM_VSYNC, CAM_HREF, active_siod, active_sioc,
                  CAM_XCLK);
    prepare_sccb_bus();
    esp_err_t error = ESP_FAIL;
    for (uint8_t attempt = 1; attempt <= CAMERA_INIT_RETRIES; ++attempt)
    {
        camera_config_t config = {};
        config.ledc_channel = LEDC_CHANNEL_0;
        config.ledc_timer = LEDC_TIMER_0;
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
        config.pin_sccb_sda = active_siod;
        config.pin_sccb_scl = active_sioc;
        config.pin_pwdn = CAM_PWDN;
        config.pin_reset = CAM_RESET;
        config.xclk_freq_hz = 20000000;
        config.pixel_format = format;
        config.frame_size = size;
        config.jpeg_quality = 12;
        // A single buffer is more tolerant of an externally-clocked sensor.
        config.fb_count = 1;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        config.fb_location = CAMERA_FB_IN_PSRAM;

        Serial.printf("摄像头初始化尝试 %u/%u\n", attempt, CAMERA_INIT_RETRIES);
        error = esp_camera_init(&config);
        if (error == ESP_OK) break;
        Serial.printf("本次初始化失败: 0x%08X，释放驱动后等待重试\n", error);
        esp_camera_deinit();
        delay(300);
        prepare_sccb_bus();
    }
    if (error != ESP_OK)
    {
        Serial.printf("摄像头初始化失败（已重试%u次）: 0x%08X\n",
                      CAMERA_INIT_RETRIES, error);
        return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == nullptr)
    {
        Serial.println("摄像头初始化成功但无法读取传感器对象");
        return false;
    }
    Serial.printf("摄像头传感器 ID: PID=0x%02X VER=0x%02X MIDL=0x%02X MIDH=0x%02X\n",
                  sensor->id.PID, sensor->id.VER, sensor->id.MIDL, sensor->id.MIDH);
    sensor->set_framesize(sensor, size);
    if (format == PIXFORMAT_JPEG) sensor->set_quality(sensor, 12);
    active_format = format;
    active_framesize = size;
    sample_camera_signals();
    // The first frame after sensor reset is often incomplete. Discard a few
    // buffers before accepting a user-visible capture.
    delay(150);
    for (int i = 0; i < 2; ++i) {
        camera_fb_t *warmup = esp_camera_fb_get();
        if (warmup) esp_camera_fb_return(warmup);
        delay(20);
    }
    return true;
}

void capture_once()
{
    if (!camera_ready)
    {
        Serial.println("摄像头尚未初始化");
        return;
    }
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr)
    {
        Serial.printf("抓帧失败（格式=%d，分辨率=%d）\n", (int)active_format, (int)active_framesize);
        return;
    }
    capture_count++;
    Serial.printf("抓帧成功 #%lu: %ux%u, %u 字节, 格式=%d\n",
                  capture_count, frame->width, frame->height,
                  (unsigned)frame->len, (int)frame->format);
    esp_camera_fb_return(frame);
}

void setup()
{
    Serial.begin(115200);
    delay(1200);
    Serial.println();
    Serial.println("OV2640 独立测试程序");
    Serial.println("诊断版本: 2026-09-10-sccb-probe");
    Serial.println("只测试初始化和 JPEG 抓帧，不连接视觉识别服务");
    Serial.println("默认命令：c=抓一帧，p=切换RGB565测试，t=采样并口信号，r=重新初始化");
    Serial.println("供电必须为 3.3V，确认模块 XCLK 引脚后再接线");
    probe_sccb_directly();
    camera_ready = init_camera();
    Serial.println(camera_ready ? "摄像头状态: READY" : "摄像头状态: FAILED");
}

void loop()
{
    if (Serial.available())
    {
        char command = (char)Serial.read();
        if (command == 'c' || command == 'C')
        {
            capture_once();
        }
        else if (command == 't' || command == 'T')
        {
            sample_camera_signals();
        }
        else if (command == 'p' || command == 'P')
        {
            if (camera_ready) esp_camera_deinit();
            camera_ready = init_camera(PIXFORMAT_RGB565, FRAMESIZE_QQVGA);
            Serial.println(camera_ready ? "RGB565测试模式已启用" : "RGB565测试模式初始化失败");
            if (camera_ready) capture_once();
        }
        else if (command == 'r' || command == 'R')
        {
            if (camera_ready) esp_camera_deinit();
            camera_ready = init_camera();
            Serial.println(camera_ready ? "重新初始化成功" : "重新初始化失败");
        }
    }
    static unsigned long next_capture = 0;
    if (camera_ready && millis() >= next_capture)
    {
        next_capture = millis() + 2000;
        capture_once();
    }
    delay(10);
}
