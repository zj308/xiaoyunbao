// TFT, SD background and drawing
// Shared state and declarations are kept in the main sketch.

void draw_status(const char *title, const char *detail)
{
    if (screen_ready == false)
    {
        return;
    }
    // 串口保留完整状态；待机时屏幕只显示固定唤醒提示，不显示环境语音识别结果。
    digitalWrite(PIN_SD_CS, HIGH);
    Serial.print("屏幕状态: ");
    Serial.print(title);
    Serial.print(" - ");
    Serial.println(detail);
    if (wifi_provision_active == true)
    {
        draw_wifi_provision_screen();
        return;
    }
    if (title != NULL && strstr(title, "网络") != NULL)
    {
        draw_network_offline_notice();
        chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
        return;
    }
    if (english_mode_enabled == true)
    {
        return;
    }
    if (continuous_dialog_active == false)
    {
        draw_wake_prompt();
        chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
        return;
    }
    // 正在逐字打印回复时，状态刷新不能把气泡盖掉或把上一轮全文刷回来。
    if (chat_speaking_message_index >= 0 &&
        chat_speaking_visible_bytes > 0 &&
        tts_active == true)
    {
        chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
        return;
    }
    if (title != NULL &&
        (strstr(title, "没听清") != NULL ||
         strstr(title, "识别失败") != NULL ||
         strstr(title, "识别超时") != NULL))
    {
        draw_companion_status_text("请再说一遍没有听见");
        chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
        return;
    }
    if (ui_listening_indicator_active == true)
    {
        draw_listening_indicator();
    }
    else
    {
        // 思考中也保持聆听提示，等回复真正露出文字再画气泡。
        draw_companion_status_text("正在倾听");
    }
    chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
}

void draw_ui_header(void)
{
    if (screen_ready == false || english_mode_enabled == true)
    {
        return;
    }
    // 原图标题发糊，这里盖住顶部后只画一次，避免和动画帧里的字叠成两个。
    display.fillRect(0, 0, 240, UI_HEADER_HEIGHT, ST77XX_BLACK);
    chinese_font.setFontMode(0);
    chinese_font.setBackgroundColor(ST77XX_BLACK);
    chinese_font.setForegroundColor(ST77XX_WHITE);
    chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
    const char *title = "云伴";
    int title_x = (240 - chinese_font.getUTF8Width(title)) / 2;
    chinese_font.drawUTF8(title_x, 28, title);
    chinese_font.setFontMode(1);
    if (button_toast_visible == true)
    {
        paint_button_toast();
    }
}

void draw_wake_prompt(void)
{
    if (screen_ready == false)
    {
        return;
    }
    if (wifi_provision_active == true)
    {
        draw_wifi_provision_screen();
        return;
    }
    if (english_mode_enabled == true)
    {
        if (english_ui_visible == false)
        {
            apply_language_ui();
        }
        return;
    }
    stop_listen_animation();
    draw_ui_header();
    clear_dialog_text_area();
    clear_listening_dots_area();
    if (ui_greeting_animation_active == false)
    {
        start_greeting_animation();
    }
    if (device_wifi_connected() == false)
    {
        draw_network_offline_notice();
        return;
    }
    display.fillRoundRect(UI_BUBBLE_LEFT, UI_BUBBLE_TOP, UI_BUBBLE_RIGHT - UI_BUBBLE_LEFT + 1,
                          42, 12, UI_BUBBLE_COLOR);
    chinese_font.setFont(u8g2_font_wqy12_t_gb2312);
    chinese_font.setForegroundColor(UI_BUBBLE_TEXT_COLOR);
    // 使用气泡蓝色作为字库背景，避免透明字模式把黑色背景带到笔画边缘。
    chinese_font.setFontMode(0);
    chinese_font.setBackgroundColor(UI_BUBBLE_COLOR);
    const char *prompt = "请唤醒我开始交流";
    int width = chinese_font.getUTF8Width(prompt);
    int x = UI_BUBBLE_LEFT + ((UI_BUBBLE_RIGHT - UI_BUBBLE_LEFT + 1) - width) / 2;
    if (x < UI_BUBBLE_LEFT + 3) x = UI_BUBBLE_LEFT + 3;
    chinese_font.drawUTF8(x, UI_BUBBLE_TOP + 26, prompt);
    chinese_font.setFontMode(1);
}

static void paint_centered_utf8(U8G2_FOR_ADAFRUIT_GFX &font, int left, int width, int baseline, const char *text)
{
    int text_width = font.getUTF8Width(text);
    int x = left + (width - text_width) / 2;
    if (x < left + 3)
    {
        x = left + 3;
    }
    font.drawUTF8(x, baseline, text);
}

void draw_network_offline_notice(void)
{
    if (screen_ready == false || wifi_provision_active == true)
    {
        return;
    }
    network_offline_notice_visible = true;
    digitalWrite(PIN_SD_CS, HIGH);
    stop_listen_animation();
    chat_last_render_signature = "network-offline";
    if (english_mode_enabled == true)
    {
        if (english_ui_visible == false)
        {
            if (english_ui_loaded == true && english_ui_framebuffer != NULL)
            {
                display.drawRGBBitmap(0, 0, english_ui_framebuffer, 240, 320);
            }
            else
            {
                display.fillScreen(ST77XX_BLACK);
                fill_english_chat_panel();
            }
            english_ui_visible = true;
        }
        const char *prompt = "No network";
        const int panel_width = EN_CHAT_RIGHT - EN_CHAT_LEFT + 1;
        const int panel_height = EN_CHAT_BOTTOM - EN_CHAT_TOP + 1;
        if (english_chat_canvas != NULL && english_chat_canvas->getBuffer() != NULL)
        {
            english_chat_canvas->fillScreen(english_chat_panel_color);
            english_chat_font.setFont(u8g2_font_wqy16_t_gb2312);
            english_chat_font.setFontMode(1);
            english_chat_font.setBackgroundColor(english_chat_panel_color);
            english_chat_font.setForegroundColor(0xF800);
            paint_centered_utf8(english_chat_font, 0, panel_width, 36, prompt);
            display.drawRGBBitmap(EN_CHAT_LEFT, EN_CHAT_TOP,
                                  english_chat_canvas->getBuffer(), panel_width, panel_height);
        }
        else
        {
            fill_english_chat_panel();
            chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
            chinese_font.setFontMode(1);
            chinese_font.setBackgroundColor(english_chat_panel_color);
            chinese_font.setForegroundColor(0xF800);
            paint_centered_utf8(chinese_font, 0, 240, EN_CHAT_TOP + 36, prompt);
        }
        return;
    }
    draw_ui_header();
    const int chat_left = UI_BUBBLE_LEFT;
    const int chat_width = UI_BUBBLE_RIGHT - UI_BUBBLE_LEFT + 1;
    const int bubble_height = 42;
    const char *prompt = "无网络连接";
    const bool use_canvas = chat_canvas != NULL && chat_canvas->getBuffer() != NULL;
    if (use_canvas == true)
    {
        fill_companion_chat_canvas_background();
        chat_canvas->fillRoundRect(0, 0, chat_width, bubble_height, 12, UI_BUBBLE_COLOR);
        chat_canvas_font.setFontMode(0);
        chat_canvas_font.setBackgroundColor(UI_BUBBLE_COLOR);
        chat_canvas_font.setForegroundColor(UI_BUBBLE_TEXT_COLOR);
        chat_canvas_font.setFont(u8g2_font_wqy16_t_gb2312);
        paint_centered_utf8(chat_canvas_font, 0, chat_width, 26, prompt);
        chat_canvas_font.setFontMode(1);
        display.drawRGBBitmap(chat_left, CHAT_TEXT_TOP, chat_canvas->getBuffer(),
                              chat_width, CHAT_TEXT_BOTTOM - CHAT_TEXT_TOP + 1);
    }
    else
    {
        clear_dialog_text_area();
        display.fillRoundRect(UI_BUBBLE_LEFT, UI_BUBBLE_TOP, chat_width, bubble_height, 12, UI_BUBBLE_COLOR);
        chinese_font.setFontMode(0);
        chinese_font.setBackgroundColor(UI_BUBBLE_COLOR);
        chinese_font.setForegroundColor(UI_BUBBLE_TEXT_COLOR);
        chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
        paint_centered_utf8(chinese_font, UI_BUBBLE_LEFT, chat_width, UI_BUBBLE_TOP + 26, prompt);
        chinese_font.setFontMode(1);
    }
    clear_listening_dots_area();
}

size_t utf8_character_length(const String &text, size_t byte_index)
{
    // 越过字符串末尾时没有可读取字符。
    if (byte_index >= text.length())
    {
        return 0;
    }
    // 读取 UTF-8 字符的首字节。
    uint8_t first_byte = (uint8_t)text[byte_index];
    // ASCII 字符只占一个字节。
    if ((first_byte & 0x80) == 0)
    {
        return 1;
    }
    // 常用中文 UTF-8 字符占三个字节。
    if ((first_byte & 0xF0) == 0xE0)
    {
        return 3;
    }
    // 四字节 UTF-8 字符通常是表情；字体不支持时仍作为一个整体跳过，不拆坏编码。
    if ((first_byte & 0xF8) == 0xF0)
    {
        return 4;
    }
    // 其余两字节 UTF-8 字符按两个字节处理。
    if ((first_byte & 0xE0) == 0xC0)
    {
        return 2;
    }
    // 遇到非法首字节时只跳过一个字节，防止循环卡住。
    return 1;
}

size_t count_utf8_characters(const String &text)
{
    // 按 UTF-8 字符边界统计字符数，中文三个字节只能计为一个字符。
    size_t byte_index = 0;
    size_t character_count = 0;
    while (byte_index < text.length())
    {
        size_t character_bytes = utf8_character_length(text, byte_index);
        if (character_bytes == 0)
        {
            break;
        }
        byte_index = byte_index + character_bytes;
        character_count = character_count + 1;
    }
    return character_count;
}

size_t utf8_bytes_for_character_count(const String &text, size_t character_count)
{
    size_t byte_index = 0;
    size_t current_character = 0;
    while (byte_index < text.length() && current_character < character_count)
    {
        size_t character_bytes = utf8_character_length(text, byte_index);
        if (character_bytes == 0 || byte_index + character_bytes > text.length())
        {
            break;
        }
        byte_index = byte_index + character_bytes;
        current_character = current_character + 1;
    }
    return byte_index;
}

bool is_sentence_ending_character(const String &character)
{
    return character == "。" || character == "！" || character == "？" ||
           character == "!" || character == "?" || character == "；" || character == ";";
}

size_t reply_sentence_end_bytes(const String &text, size_t start_byte)
{
    if (start_byte >= text.length())
    {
        return text.length();
    }
    size_t read_index = start_byte;
    while (read_index < text.length())
    {
        size_t character_bytes = utf8_character_length(text, read_index);
        if (character_bytes == 0 || read_index + character_bytes > text.length())
        {
            break;
        }
        String one_character = text.substring(read_index, read_index + character_bytes);
        read_index += character_bytes;
        if (is_sentence_ending_character(one_character) == true || one_character == "\n")
        {
            return read_index;
        }
    }
    return text.length();
}

void draw_bold_chat_text(int x, int baseline, const String &text)
{
    // WQY16 没有单独的粗体字库；横向相差一个像素叠画，增强笔画且保留完整中文字形。
    chinese_font.drawUTF8(x, baseline, text.c_str());
    chinese_font.drawUTF8(x + 1, baseline, text.c_str());
}

void draw_bold_chat_text_to_canvas(int x, int baseline, const String &text)
{
    chat_canvas_font.drawUTF8(x, baseline, text.c_str());
    chat_canvas_font.drawUTF8(x + 1, baseline, text.c_str());
}

void set_dialog_reply(const String &reply)
{
    set_dialog_reply_visible(reply, 0);
}

void set_dialog_reply_visible(const String &reply, size_t visible_bytes)
{
    // 机器人消息只创建一次；整条语音播放期间按句子边界扩大可见范围。
    add_chat_message(reply, false);
    chat_speaking_message_index = chat_history_count - 1;
    chat_speaking_visible_bytes = visible_bytes < reply.length() ? visible_bytes : reply.length();
    chat_last_sync_refresh_time = 0;
    // 新消息到达时自动回到聊天记录底部。
    chat_scroll_lines = 0;
    // 上一轮排空标记不能带到新回复，否则会先整段画出再被播放进度擦掉。
    tts_playback_drained = false;
    // 还没露出第一个字时保持聆听提示，不要把上一轮气泡或空气泡刷出来。
    if (chat_speaking_visible_bytes == 0)
    {
        return;
    }
    redraw_chat_history();
}

void set_dialog_user_text(const String &user_text)
{
    if (user_text.length() == 0)
    {
        chat_live_user_message_index = -1;
        chat_scroll_lines = 0;
        return;
    }
    if (chat_live_user_message_index >= 0 && chat_live_user_message_index < chat_history_count)
    {
        chat_history[chat_live_user_message_index].text = user_text;
        chat_history[chat_live_user_message_index].from_user = true;
    }
    else
    {
        add_chat_message(user_text, true);
        chat_live_user_message_index = chat_history_count - 1;
    }
    chat_live_user_message_index = -1;
    chat_scroll_lines = 0;
    if (english_mode_enabled == true)
    {
        redraw_chat_history();
    }
}

void update_dialog_user_preview(const String &user_text)
{
    if (user_text.length() == 0)
    {
        return;
    }
    if (english_mode_enabled == false)
    {
        return;
    }
    if (chat_live_user_message_index >= 0 && chat_live_user_message_index < chat_history_count)
    {
        chat_history[chat_live_user_message_index].text = user_text;
    }
    else
    {
        add_chat_message(user_text, true);
        chat_live_user_message_index = chat_history_count - 1;
    }
    chat_scroll_lines = 0;
    redraw_chat_history();
}

bool find_sd_ui_image(String *path)
{
    if (sd_card_ready == false || path == NULL)
    {
        return false;
    }
    // 优先读取预转换的 240x320 RGB565，避免 ESP32 开机时现场缩放
    // 480x640 BMP；不存在时仍兼容旧 BMP。
    File preferred_rgb565 = SD.open("/ui/ui1.1.rgb565", FILE_READ);
    if (preferred_rgb565)
    {
        preferred_rgb565.close();
        *path = "/ui/ui1.1.rgb565";
        Serial.println("选择快速 UI: /ui/ui1.1.rgb565");
        return true;
    }
    File preferred = SD.open("/ui/ui1.1.bmp", FILE_READ);
    if (preferred)
    {
        preferred.close();
        *path = "/ui/ui1.1.bmp";
        Serial.println("选择指定 UI: /ui/ui1.1.bmp");
        return true;
    }
    const char *directory_paths[] = {"/", "/ui", "/ui/cute", "/ui/sleep"};
    String fallback_path = "";
    String newest_bmp_path = "";
    time_t newest_bmp_time = 0;
    for (size_t directory_index = 0; directory_index < 4; directory_index++)
    {
        File directory = SD.open(directory_paths[directory_index]);
        if (!directory || !directory.isDirectory())
        {
            continue;
        }
        File entry = directory.openNextFile();
        while (entry)
        {
            String name = entry.name();
            String lower_name = name;
            lower_name.toLowerCase();
            bool supported = lower_name.endsWith(".jpg") ||
                             lower_name.endsWith(".jpeg") ||
                             lower_name.endsWith(".bmp") ||
                             lower_name.endsWith(".rgb565") ||
                             lower_name.endsWith(".raw");
            if (!entry.isDirectory() && supported == true)
            {
                if (!name.startsWith("/"))
                {
                    String prefix = String(directory_paths[directory_index]);
                    if (!prefix.endsWith("/"))
                    {
                        prefix += "/";
                    }
                    name = prefix + name;
                }
                Serial.print("SD UI 文件: " );
                Serial.print(name);
                Serial.print(" bytes=" );
                Serial.println((unsigned long)entry.size());
                if (lower_name.endsWith(".bmp"))
                {
                    time_t modified = entry.getLastWrite();
                    if (newest_bmp_path.length() == 0 || modified >= newest_bmp_time)
                    {
                        newest_bmp_path = name;
                        newest_bmp_time = modified;
                    }
                }
                if (fallback_path.length() == 0)
                {
                    fallback_path = name;
                }
            }
            entry.close();
            entry = directory.openNextFile();
        }
        directory.close();
    }
    if (newest_bmp_path.length() > 0)
    {
        *path = newest_bmp_path;
        Serial.print("选择最新 BMP UI: " );
        Serial.println(*path);
        return true;
    }
    if (fallback_path.length() > 0)
    {
        *path = fallback_path;
        return true;
    }
    return false;
}

static bool sd_file_exists(const char *path)
{
    File file = SD.open(path, FILE_READ);
    if (!file)
    {
        return false;
    }
    file.close();
    return true;
}

static bool english_ui_name_match(const String &lower_name)
{
    return lower_name.indexOf("enlishiui") >= 0 ||
           lower_name.indexOf("enlishi") >= 0 ||
           lower_name.indexOf("enlish") >= 0 ||
           lower_name.indexOf("english") >= 0;
}

static bool english_ui_name_supported(const String &lower_name)
{
    return lower_name.endsWith(".jpg") || lower_name.endsWith(".jpeg") ||
           lower_name.endsWith(".bmp") || lower_name.endsWith(".rgb565") ||
           lower_name.endsWith(".raw") || lower_name.endsWith(".png");
}

static String english_ui_full_path(const String &name, const char *directory)
{
    if (name.startsWith("/"))
    {
        return name;
    }
    String path = directory;
    if (path.endsWith("/") == false)
    {
        path += "/";
    }
    path += name;
    return path;
}

bool find_english_ui_image(String *path)
{
    if (sd_card_ready == false || path == NULL)
    {
        return false;
    }
    const char *candidates[] = {
        "/ui/enlishi/enlish.rgb565", "/ui/enlishi/enlish.bmp",
        "/ui/enlishiui.jpg", "/ui/enlishiui.jpeg", "/ui/enlishiui.bmp", "/ui/enlishiui.rgb565",
        "/ui/enlish.jpg", "/ui/enlish.jpeg", "/ui/enlish.bmp", "/ui/enlish.rgb565",
        "/ui/english.jpg", "/ui/english.jpeg", "/ui/english.bmp", "/ui/english.rgb565"
    };
    for (size_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]); index++)
    {
        if (sd_file_exists(candidates[index]) == true)
        {
            *path = candidates[index];
            Serial.print("选择英语 UI: ");
            Serial.println(*path);
            return true;
        }
    }
    String preferred = "";
    const char *directories[] = {"/ui/enlishi", "/ui", "/"};
    for (size_t dir_index = 0; dir_index < 2; dir_index++)
    {
        File directory = SD.open(directories[dir_index]);
        if (!directory || !directory.isDirectory())
        {
            continue;
        }
        Serial.print("TF 目录 ");
        Serial.print(directories[dir_index]);
        Serial.println(" 文件:");
        File entry = directory.openNextFile();
        while (entry)
        {
            String name = entry.name();
            Serial.print("  ");
            Serial.println(name);
            String lower_name = name;
            lower_name.toLowerCase();
            if (!entry.isDirectory() &&
                english_ui_name_match(lower_name) == true &&
                english_ui_name_supported(lower_name) == true)
            {
                String found = english_ui_full_path(name, directories[dir_index]);
                if (preferred.length() == 0 || lower_name.indexOf("enlishiui") >= 0)
                {
                    preferred = found;
                }
            }
            entry.close();
            entry = directory.openNextFile();
        }
        directory.close();
    }
    if (preferred.length() > 0)
    {
        *path = preferred;
        Serial.print("选择英语 UI: ");
        Serial.println(*path);
        return true;
    }
    Serial.println("未在 TF 卡 /ui 或根目录找到 enlishiui/enlish/english 图片");
    return false;
}

bool load_ui_file_into_buffer(const char *path, uint16_t *dest)
{
    if (path == NULL || dest == NULL)
    {
        return false;
    }
    String lower_path = path;
    lower_path.toLowerCase();
    if (lower_path.endsWith(".rgb565") || lower_path.endsWith(".raw"))
    {
        File file = SD.open(path, FILE_READ);
        const size_t expected_bytes = 240 * 320 * sizeof(uint16_t);
        if (!file || file.size() != expected_bytes)
        {
            if (file)
            {
                file.close();
            }
            return false;
        }
        size_t total_read = 0;
        uint8_t *bytes = (uint8_t *)dest;
        while (total_read < expected_bytes)
        {
            size_t read_now = file.read(bytes + total_read, expected_bytes - total_read);
            if (read_now == 0)
            {
                file.close();
                return false;
            }
            total_read += read_now;
        }
        file.close();
        return true;
    }
    uint16_t *saved = sd_ui_framebuffer;
    sd_ui_framebuffer = dest;
    bool decoded = false;
    if (lower_path.endsWith(".bmp"))
    {
        decoded = draw_bmp_file_legacy(path);
        if (decoded == false)
        {
            decoded = draw_bmp_file(path);
            if (decoded == true)
            {
                if (saved != NULL)
                {
                    memcpy(dest, sd_ui_framebuffer, 240 * 320 * sizeof(uint16_t));
                }
                if (saved != NULL && sd_ui_image_loaded == true)
                {
                    display.drawRGBBitmap(0, 0, saved, 240, 320);
                }
            }
        }
    }
    else if (lower_path.endsWith(".jpg") || lower_path.endsWith(".jpeg"))
    {
        decoded = draw_jpeg_file(path);
    }
    sd_ui_framebuffer = saved;
    return decoded;
}

void load_english_ui_image(void)
{
    if (english_ui_loaded == true || sd_card_ready == false)
    {
        return;
    }
    if (find_english_ui_image(&english_ui_image_path) == false)
    {
        Serial.println("未找到英语 UI（/ui/enlish 或 /ui/english），英语模式将盖住中文界面下半区");
        return;
    }
    english_ui_framebuffer = (uint16_t *)heap_caps_malloc(240 * 320 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (english_ui_framebuffer == NULL)
    {
        english_ui_framebuffer = (uint16_t *)malloc(240 * 320 * sizeof(uint16_t));
    }
    if (english_ui_framebuffer == NULL)
    {
        Serial.println("英语 UI 内存不足");
        return;
    }
    if (load_ui_file_into_buffer(english_ui_image_path.c_str(), english_ui_framebuffer) == true)
    {
        english_ui_loaded = true;
        english_chat_panel_color = 0x0000;
        blank_english_chat_in_framebuffer();
        Serial.print("英语 UI 已缓存: ");
        Serial.println(english_ui_image_path);
        return;
    }
    free(english_ui_framebuffer);
    english_ui_framebuffer = NULL;
    Serial.print("英语 UI 读取失败: ");
    Serial.println(english_ui_image_path);
}

void blank_english_chat_in_framebuffer(void)
{
    if (english_ui_framebuffer == NULL)
    {
        return;
    }
    const int width = EN_CHAT_RIGHT - EN_CHAT_LEFT + 1;
    for (int y = EN_CHAT_TOP; y <= EN_CHAT_BOTTOM; y++)
    {
        uint16_t *row = english_ui_framebuffer + (size_t)y * 240 + EN_CHAT_LEFT;
        for (int x = 0; x < width; x++)
        {
            row[x] = english_chat_panel_color;
        }
    }
}

void draw_english_listen_prompt(void)
{
    if (screen_ready == false || english_mode_enabled == false)
    {
        return;
    }
    if (chat_history_count > 0)
    {
        return;
    }
    if (device_wifi_connected() == false)
    {
        draw_network_offline_notice();
        return;
    }
    digitalWrite(PIN_SD_CS, HIGH);
    const char *prompt = "Please speak";
    const int panel_width = EN_CHAT_RIGHT - EN_CHAT_LEFT + 1;
    const int panel_height = EN_CHAT_BOTTOM - EN_CHAT_TOP + 1;
    if (english_chat_canvas != NULL && english_chat_canvas->getBuffer() != NULL)
    {
        english_chat_canvas->fillScreen(english_chat_panel_color);
        english_chat_font.setFont(u8g2_font_wqy16_t_gb2312);
        english_chat_font.setFontMode(1);
        english_chat_font.setBackgroundColor(english_chat_panel_color);
        english_chat_font.setForegroundColor(0xC618);
        int width = english_chat_font.getUTF8Width(prompt);
        int x = (panel_width - width) / 2;
        if (x < 0)
        {
            x = 0;
        }
        english_chat_font.drawUTF8(x, 36, prompt);
        display.drawRGBBitmap(EN_CHAT_LEFT, EN_CHAT_TOP,
                              english_chat_canvas->getBuffer(), panel_width, panel_height);
    }
    else
    {
        fill_english_chat_panel();
        chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
        chinese_font.setFontMode(1);
        chinese_font.setBackgroundColor(english_chat_panel_color);
        chinese_font.setForegroundColor(0xC618);
        int width = chinese_font.getUTF8Width(prompt);
        int x = (240 - width) / 2;
        chinese_font.drawUTF8(x, EN_CHAT_TOP + 36, prompt);
    }
    chat_last_render_signature = "listen-prompt";
}

void fill_english_chat_panel(void)
{
    if (screen_ready == false)
    {
        return;
    }
    digitalWrite(PIN_SD_CS, HIGH);
    display.fillRect(EN_CHAT_LEFT, EN_CHAT_TOP,
                     EN_CHAT_RIGHT - EN_CHAT_LEFT + 1,
                     EN_CHAT_BOTTOM - EN_CHAT_TOP + 1,
                     english_chat_panel_color);
}

void apply_language_ui(void)
{
    if (screen_ready == false)
    {
        return;
    }
    digitalWrite(PIN_SD_CS, HIGH);
    chat_last_render_signature = "";
    if (english_mode_enabled == true)
    {
        stop_greeting_animation();
        stop_listen_animation();
        if (english_ui_visible == false)
        {
            if (english_ui_loaded == true && english_ui_framebuffer != NULL)
            {
                display.drawRGBBitmap(0, 0, english_ui_framebuffer, 240, 320);
            }
            else if (sd_ui_image_loaded == true)
            {
                restore_sd_ui_background();
                fill_english_chat_panel();
            }
            else
            {
                display.fillScreen(ST77XX_BLACK);
            }
            english_ui_visible = true;
        }
        if (chat_history_count == 0)
        {
            draw_english_listen_prompt();
        }
        return;
    }
    english_ui_visible = false;
    if (sd_ui_image_loaded == true)
    {
        restore_sd_ui_background();
    }
    start_greeting_animation();
    draw_ui_header();
}

uint16_t read_bmp_u16(File &file)
{
    uint8_t bytes[2];
    if (file.read(bytes, sizeof(bytes)) != sizeof(bytes))
    {
        return 0;
    }
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

uint32_t read_bmp_u32(File &file)
{
    uint8_t bytes[4];
    if (file.read(bytes, sizeof(bytes)) != sizeof(bytes))
    {
        return 0;
    }
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

bool draw_bmp_file_legacy(const char *path)
{
    if (path == NULL || sd_card_ready == false || screen_ready == false)
    {
        return false;
    }
    File file = SD.open(path, FILE_READ);
    if (!file)
    {
        return false;
    }
    if (read_bmp_u16(file) != 0x4D42)
    {
        file.close();
        return false;
    }
    read_bmp_u32(file);
    read_bmp_u16(file);
    read_bmp_u16(file);
    uint32_t pixel_offset = read_bmp_u32(file);
    uint32_t dib_size = read_bmp_u32(file);
    if (dib_size < 40)
    {
        file.close();
        return false;
    }
    int32_t width = (int32_t)read_bmp_u32(file);
    int32_t height = (int32_t)read_bmp_u32(file);
    uint16_t planes = read_bmp_u16(file);
    uint16_t bits_per_pixel = read_bmp_u16(file);
    uint32_t compression = read_bmp_u32(file);
    read_bmp_u32(file);
    read_bmp_u32(file);
    read_bmp_u32(file);
    read_bmp_u32(file);
    read_bmp_u32(file);
    if (planes != 1 || (bits_per_pixel != 24 && bits_per_pixel != 16) || compression != 0 ||
        width <= 0 || height == 0)
    {
        file.close();
        return false;
    }
    bool top_down = height < 0;
    int32_t absolute_height = top_down ? -height : height;
    if (width != 240 || absolute_height != 320)
    {
        Serial.print("SD UI BMP 尺寸为 ");
        Serial.print(width);
        Serial.print("x");
        Serial.print(absolute_height);
        Serial.println("，需要 240x320");
        file.close();
        return false;
    }
    uint32_t row_bytes = bits_per_pixel == 24
        ? ((uint32_t)width * 3 + 3) & ~3UL
        : ((uint32_t)width * 2 + 3) & ~3UL;
    uint8_t *row = (uint8_t *)malloc(row_bytes);
    uint16_t *pixels = (uint16_t *)malloc((size_t)width * sizeof(uint16_t));
    if (row == NULL || pixels == NULL)
    {
        free(row);
        free(pixels);
        file.close();
        return false;
    }
    for (int32_t output_y = 0; output_y < absolute_height; output_y++)
    {
        int32_t source_y = top_down ? output_y : absolute_height - 1 - output_y;
        file.seek(pixel_offset + (uint32_t)source_y * row_bytes);
        if (file.read(row, row_bytes) != row_bytes)
        {
            free(row);
            free(pixels);
            file.close();
            return false;
        }
        for (int32_t x = 0; x < width; x++)
        {
            if (bits_per_pixel == 24)
            {
                uint8_t blue = row[x * 3];
                uint8_t green = row[x * 3 + 1];
                uint8_t red = row[x * 3 + 2];
                pixels[x] = ((uint16_t)(red & 0xF8) << 8) |
                            ((uint16_t)(green & 0xFC) << 3) |
                            (blue >> 3);
            }
            else
            {
                // BI_RGB 的 16 位 BMP 默认使用 RGB555，转换为屏幕需要的 RGB565。
                uint16_t rgb555 = (uint16_t)row[x * 2] | ((uint16_t)row[x * 2 + 1] << 8);
                uint16_t red = (rgb555 >> 10) & 0x1F;
                uint16_t green5 = (rgb555 >> 5) & 0x1F;
                uint16_t blue = rgb555 & 0x1F;
                uint16_t green6 = (green5 << 1) | (green5 >> 4);
                pixels[x] = (red << 11) | (green6 << 5) | blue;
            }
        }
        memcpy(sd_ui_framebuffer + (size_t)output_y * width, pixels, (size_t)width * sizeof(uint16_t));
    }
    digitalWrite(PIN_SD_CS, HIGH);
    free(row);
    free(pixels);
    file.close();
    return true;
}

bool draw_bmp_region(const char *path, int left, int top, int region_width, int region_height)
{
    if (path == NULL || sd_card_ready == false || screen_ready == false ||
        left < 0 || top < 0 || region_width <= 0 || region_height <= 0 ||
        left + region_width > 240 || top + region_height > 320)
    {
        return false;
    }
    File file = SD.open(path, FILE_READ);
    if (!file || read_bmp_u16(file) != 0x4D42)
    {
        if (file)
        {
            file.close();
        }
        return false;
    }
    read_bmp_u32(file);
    read_bmp_u16(file);
    read_bmp_u16(file);
    uint32_t pixel_offset = read_bmp_u32(file);
    uint32_t dib_size = read_bmp_u32(file);
    int32_t bmp_width = (int32_t)read_bmp_u32(file);
    int32_t bmp_height = (int32_t)read_bmp_u32(file);
    uint16_t planes = read_bmp_u16(file);
    uint16_t bits_per_pixel = read_bmp_u16(file);
    uint32_t compression = read_bmp_u32(file);
    bool top_down = bmp_height < 0;
    int32_t absolute_height = top_down ? -bmp_height : bmp_height;
    // 该函数也用于清除聊天区，不能在每次局部重绘时重复打印 BMP 元数据。
    if (dib_size < 40 || planes != 1 || bmp_width <= 0 || absolute_height <= 0 ||
        (bits_per_pixel != 16 && bits_per_pixel != 24 && bits_per_pixel != 32) || compression != 0)
    {
        Serial.println("SD UI BMP 不兼容：仅支持未压缩的 16/24/32 位 BMP");
        Serial.print(bmp_width);
        Serial.print("x");
        Serial.print(absolute_height);
        Serial.print(" /");
        Serial.println(bits_per_pixel);
        file.close();
        return false;
    }
    uint32_t bytes_per_pixel = bits_per_pixel / 8;
    uint32_t source_row_bytes = ((uint32_t)bmp_width * bytes_per_pixel + 3) & ~3UL;
    uint8_t *source_row = (uint8_t *)malloc(source_row_bytes);
    if (source_row == NULL)
    {
        Serial.println("BMP 行缓冲内存不足");
        file.close();
        return false;
    }
    uint16_t display_row[240];
    for (int output_y = top; output_y < top + region_height; output_y++)
    {
        int32_t scaled_y = (int32_t)((int64_t)output_y * absolute_height / 320);
        if (scaled_y >= absolute_height) scaled_y = absolute_height - 1;
        int32_t source_y = top_down ? scaled_y : absolute_height - 1 - scaled_y;
        if (!file.seek(pixel_offset + (uint32_t)source_y * source_row_bytes) ||
            file.read(source_row, source_row_bytes) != source_row_bytes)
        {
            free(source_row);
            file.close();
            return false;
        }
        for (int x = left; x < left + region_width; x++)
        {
            int32_t source_x = (int32_t)((int64_t)x * bmp_width / 240);
            if (source_x >= bmp_width) source_x = bmp_width - 1;
            if (bits_per_pixel == 24)
            {
                uint8_t blue = source_row[source_x * 3];
                uint8_t green = source_row[source_x * 3 + 1];
                uint8_t red = source_row[source_x * 3 + 2];
                display_row[x] = ((uint16_t)(red & 0xF8) << 8) |
                                 ((uint16_t)(green & 0xFC) << 3) |
                                 (blue >> 3);
            }
            else if (bits_per_pixel == 16)
            {
                uint16_t rgb555 = (uint16_t)source_row[source_x * 2] |
                                  ((uint16_t)source_row[source_x * 2 + 1] << 8);
                uint16_t red = (rgb555 >> 10) & 0x1F;
                uint16_t green5 = (rgb555 >> 5) & 0x1F;
                uint16_t blue = rgb555 & 0x1F;
                uint16_t green6 = (green5 << 1) | (green5 >> 4);
                display_row[x] = (red << 11) | (green6 << 5) | blue;
            }
            else
            {
                uint8_t blue = source_row[source_x * 4];
                uint8_t green = source_row[source_x * 4 + 1];
                uint8_t red = source_row[source_x * 4 + 2];
                display_row[x] = ((uint16_t)(red & 0xF8) << 8) |
                                 ((uint16_t)(green & 0xFC) << 3) |
                                 (blue >> 3);
            }
        }
        digitalWrite(PIN_SD_CS, HIGH);
        if (sd_ui_framebuffer != NULL && left == 0 && region_width == 240)
        {
            memcpy(sd_ui_framebuffer + (size_t)output_y * 240, display_row, 240 * sizeof(uint16_t));
        }
        display.drawRGBBitmap(left, output_y, display_row + left, region_width, 1);
    }
    free(source_row);
    file.close();
    return true;
}

bool draw_bmp_file(const char *path)
{
    return draw_bmp_region(path, 0, 0, 240, 320);
}

bool sd_jpeg_block(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t *pixels)
{
    if (sd_ui_framebuffer == NULL || pixels == NULL)
    {
        return false;
    }
    for (uint16_t row = 0; row < height; row++)
    {
        int target_y = y + row;
        if (target_y < 0 || target_y >= 320)
        {
            continue;
        }
        for (uint16_t column = 0; column < width; column++)
        {
            int target_x = x + column;
            if (target_x < 0 || target_x >= 240)
            {
                continue;
            }
            sd_ui_framebuffer[(size_t)target_y * 240 + target_x] = pixels[(size_t)row * width + column];
        }
    }
    return true;
}

bool draw_jpeg_file(const char *path)
{
    if (path == NULL || sd_card_ready == false || sd_ui_framebuffer == NULL)
    {
        return false;
    }
    uint16_t source_width = 0;
    uint16_t source_height = 0;
    if (TJpgDec.getSdJpgSize(&source_width, &source_height, path) != JDR_OK ||
        source_width == 0 || source_height == 0)
    {
        Serial.println("无法读取 JPG 尺寸");
        return false;
    }
    uint8_t scale = 1;
    const uint8_t scales[] = {8, 4, 2, 1};
    for (size_t index = 0; index < sizeof(scales); index++)
    {
        uint8_t candidate = scales[index];
        if (source_width / candidate >= 240 && source_height / candidate >= 320)
        {
            scale = candidate;
            break;
        }
    }
    int scaled_width = source_width / scale;
    int scaled_height = source_height / scale;
    sd_jpeg_draw_x = (240 - scaled_width) / 2;
    sd_jpeg_draw_y = (320 - scaled_height) / 2;
    memset(sd_ui_framebuffer, 0, 240 * 320 * sizeof(uint16_t));
    TJpgDec.setSwapBytes(false);
    TJpgDec.setJpgScale(scale);
    TJpgDec.setCallback(sd_jpeg_block);
    Serial.print("SD UI JPG 原图/缩放: " );
    Serial.print(source_width);
    Serial.print("x");
    Serial.print(source_height);
    Serial.print(" / " );
    Serial.print(scale);
    Serial.print("，绘制偏移: " );
    Serial.print(sd_jpeg_draw_x);
    Serial.print(",");
    Serial.println(sd_jpeg_draw_y);
    return TJpgDec.drawSdJpg(sd_jpeg_draw_x, sd_jpeg_draw_y, path) == JDR_OK;
}

bool draw_rgb565_file(const char *path)
{
    if (path == NULL || sd_card_ready == false || screen_ready == false)
    {
        return false;
    }
    digitalWrite(PIN_TFT_CS, HIGH);
    digitalWrite(PIN_SD_CS, HIGH);
    File file = SD.open(path, FILE_READ);
    const size_t expected_bytes = 240 * 320 * sizeof(uint16_t);
    if (!file || file.size() != expected_bytes)
    {
        if (file)
        {
            Serial.print("RGB565 文件字节数错误: " );
            Serial.println((unsigned long)file.size());
            file.close();
        }
        return false;
    }
    // TFT 与 TF 共用 SPI。先把整张图读到 PSRAM 并关闭文件，再一次性
    // 发送到屏幕，避免逐行在 SD/TFT 之间切换总线造成几十秒延迟。
    uint16_t *frame = sd_ui_framebuffer;
    bool temporary_frame = false;
    if (frame == NULL)
    {
        frame = (uint16_t *)heap_caps_malloc(expected_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (frame == NULL)
        {
            frame = (uint16_t *)malloc(expected_bytes);
        }
        temporary_frame = true;
    }
    if (frame == NULL)
    {
        file.close();
        Serial.println("RGB565 整帧缓冲区分配失败");
        return false;
    }
    uint8_t *bytes = (uint8_t *)frame;
    size_t total_read = 0;
    while (total_read < expected_bytes)
    {
        size_t read_now = file.read(bytes + total_read, expected_bytes - total_read);
        if (read_now == 0)
        {
            file.close();
            if (temporary_frame)
            {
                heap_caps_free(frame);
            }
            return false;
        }
        total_read += read_now;
    }
    file.close();
    digitalWrite(PIN_SD_CS, HIGH);
    display.drawRGBBitmap(0, 0, frame, 240, 320);
    if (temporary_frame)
    {
        heap_caps_free(frame);
    }
    return true;
}

bool draw_rgb565_region(const char *path, int left, int top, int width, int height)
{
    if (path == NULL || sd_card_ready == false || screen_ready == false ||
        left < 0 || top < 0 || width <= 0 || height <= 0 ||
        left + width > 240 || top + height > 320)
    {
        return false;
    }
    File file = SD.open(path, FILE_READ);
    const size_t row_bytes = 240 * sizeof(uint16_t);
    if (!file || file.size() != row_bytes * 320)
    {
        if (file)
        {
            file.close();
        }
        return false;
    }
    uint16_t row[240];
    for (int y = top; y < top + height; y++)
    {
        if (!file.seek((uint32_t)y * row_bytes) ||
            file.read((uint8_t *)row, row_bytes) != row_bytes)
        {
            file.close();
            return false;
        }
        digitalWrite(PIN_SD_CS, HIGH);
        display.drawRGBBitmap(left, y, row + left, width, 1);
    }
    file.close();
    return true;
}

void restore_sd_ui_background(void)
{
    if (sd_ui_image_loaded == false || screen_ready == false)
    {
        return;
    }
    if (sd_ui_framebuffer != NULL)
    {
        digitalWrite(PIN_SD_CS, HIGH);
        for (int y = 0; y < 320; y++)
        {
            display.drawRGBBitmap(0, y, sd_ui_framebuffer + (size_t)y * 240, 240, 1);
        }
        return;
    }
    String lower_path = sd_ui_image_path;
    lower_path.toLowerCase();
    if (lower_path.endsWith(".rgb565") || lower_path.endsWith(".raw"))
    {
        draw_rgb565_file(sd_ui_image_path.c_str());
        return;
    }
    if (lower_path.endsWith(".bmp"))
    {
        draw_bmp_file(sd_ui_image_path.c_str());
        return;
    }
}

void draw_sd_ui_background(void)
{
    if (sd_ui_image_loaded == true)
    {
        return;
    }
    if (find_sd_ui_image(&sd_ui_image_path) == false)
    {
        Serial.println("SD 卡未找到 JPG/BMP/RGB565 UI 图片，继续使用文字界面");
        return;
    }
    String lower_path = sd_ui_image_path;
    lower_path.toLowerCase();
    bool decoded = false;
    sd_ui_framebuffer = (uint16_t *)heap_caps_malloc(240 * 320 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (sd_ui_framebuffer == NULL)
    {
        sd_ui_framebuffer = (uint16_t *)malloc(240 * 320 * sizeof(uint16_t));
    }
    if (lower_path.endsWith(".bmp"))
    {
        decoded = draw_bmp_file(sd_ui_image_path.c_str());
    }
    else if (lower_path.endsWith(".rgb565") || lower_path.endsWith(".raw"))
    {
        decoded = draw_rgb565_file(sd_ui_image_path.c_str());
    }
    if (sd_ui_framebuffer != NULL &&
        (lower_path.endsWith(".jpg") || lower_path.endsWith(".jpeg")))
    {
        decoded = draw_jpeg_file(sd_ui_image_path.c_str());
    }
    sd_ui_image_loaded = decoded;
    if (sd_ui_image_loaded == true)
    {
        restore_sd_ui_background();
        const int chat_width = UI_BUBBLE_RIGHT - UI_BUBBLE_LEFT + 1;
        const int chat_height = CHAT_TEXT_BOTTOM - CHAT_TEXT_TOP + 1;
        const int chat_left = UI_BUBBLE_LEFT;
        if (sd_ui_framebuffer != NULL)
        {
            chat_background_cache = (uint16_t *)heap_caps_malloc((size_t)chat_width * chat_height * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (chat_background_cache == NULL)
            {
                chat_background_cache = (uint16_t *)malloc((size_t)chat_width * chat_height * sizeof(uint16_t));
            }
            if (chat_background_cache != NULL)
            {
                for (int row = 0; row < chat_height; row++)
                {
                    memcpy(chat_background_cache + (size_t)row * chat_width,
                           sd_ui_framebuffer + (size_t)(CHAT_TEXT_TOP + row) * 240 + chat_left,
                           (size_t)chat_width * sizeof(uint16_t));
                }
                Serial.println("聊天背景已缓存到内存，刷新不再读取 TF 卡");
            }
        }
        Serial.print("SD UI 图片已显示: ");
        Serial.println(sd_ui_image_path);
    }
    else
    {
        free(sd_ui_framebuffer);
        sd_ui_framebuffer = NULL;
        Serial.print("SD UI 图片无法显示（支持 JPG、240x320 BMP、RGB565）: " );
        Serial.println(sd_ui_image_path);
    }
}

bool draw_greeting_rgb565_frame(const char *path)
{
    if (path == NULL || sd_card_ready == false || screen_ready == false)
    {
        return false;
    }
    // TFT 与 TF 卡共用 SPI。先完整读入内存并关闭文件，再刷新屏幕，避免
    // “文件保持打开 + 屏幕逐行传输”导致 SD 卡在中途失步。
    if (greeting_frame_buffer == NULL)
    {
        greeting_frame_buffer = (uint16_t *)heap_caps_malloc(
            240U * 320U * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (greeting_frame_buffer == NULL)
        {
            greeting_frame_buffer = (uint16_t *)malloc(240U * 320U * sizeof(uint16_t));
        }
    }
    if (greeting_frame_buffer == NULL)
    {
        Serial.println("招手动画缓冲区分配失败");
        return false;
    }
    digitalWrite(PIN_TFT_CS, HIGH);
    digitalWrite(PIN_SD_CS, HIGH);
    File file = SD.open(path, FILE_READ);
    const size_t row_bytes = 240 * sizeof(uint16_t);
    if (!file)
    {
        Serial.print("招手动画文件打开失败: ");
        Serial.println(path);
        return false;
    }
    if (file.size() != row_bytes * 320)
    {
        Serial.print("招手动画文件大小错误: ");
        Serial.print(path);
        Serial.print("，实际=");
        Serial.println((unsigned long)file.size());
        file.close();
        return false;
    }
    uint8_t *frame_bytes = (uint8_t *)greeting_frame_buffer;
    size_t total_read = 0;
    const size_t frame_bytes_total = row_bytes * 320U;
    while (total_read < frame_bytes_total)
    {
        digitalWrite(PIN_TFT_CS, HIGH);
        size_t read_now = file.read(frame_bytes + total_read, frame_bytes_total - total_read);
        if (read_now == 0)
        {
            Serial.print("招手动画读取中断: ");
            Serial.print(path);
            Serial.print("，已读字节=");
            Serial.println((unsigned long)total_read);
            file.close();
            return false;
        }
        total_read += read_now;
    }
    file.close();
    // SD 文件已关闭，下面的整帧 SPI 传输不会再与读卡交错。
    digitalWrite(PIN_SD_CS, HIGH);
    display.drawRGBBitmap(0, 0, greeting_frame_buffer, 240, 320);
    return true;
}

bool is_supported_greeting_frame_size(size_t bytes)
{
    return bytes == 240U * 320U * sizeof(uint16_t) ||
           bytes == 208U * 222U * sizeof(uint16_t);
}

bool read_greeting_frame_file(const char *path, uint16_t *buffer)
{
    if (path == NULL || buffer == NULL || sd_card_ready == false)
    {
        return false;
    }
    digitalWrite(PIN_TFT_CS, HIGH);
    digitalWrite(PIN_SD_CS, HIGH);
    File file = SD.open(path, FILE_READ);
    if (!file || !is_supported_greeting_frame_size(file.size()))
    {
        if (file)
        {
            file.close();
        }
        return false;
    }
    const size_t file_bytes = file.size();
    const uint16_t frame_width = file_bytes == 208U * 222U * sizeof(uint16_t) ? 208 : 240;
    const uint16_t frame_height = file_bytes == 208U * 222U * sizeof(uint16_t) ? 222 : 320;
    const uint16_t offset_x = (240U - frame_width) / 2U;
    const uint16_t offset_y = frame_height == 320 ? 0 : 38;
    // 旧版局部帧只覆盖数字人物区域，其余区域沿用当前 UI 背景，避免黑边和残影。
    if (sd_ui_framebuffer != NULL)
    {
        memcpy(buffer, sd_ui_framebuffer, 240U * 320U * sizeof(uint16_t));
    }
    else
    {
        memset(buffer, 0, 240U * 320U * sizeof(uint16_t));
    }
    // 当前 TF 卡中的 240x320 帧直接整帧读入 PSRAM，避免 320 次小读取
    // 导致缓存缓慢或中途失败。旧版 208x222 局部帧仍走兼容的逐行路径。
    if (frame_width == 240 && frame_height == 320)
    {
        uint8_t *frame_bytes = (uint8_t *)buffer;
        size_t total_read = 0;
        while (total_read < file_bytes)
        {
            size_t read_now = file.read(frame_bytes + total_read, file_bytes - total_read);
            if (read_now == 0)
            {
                file.close();
                return false;
            }
            total_read += read_now;
        }
        file.close();
        digitalWrite(PIN_SD_CS, HIGH);
        return true;
    }
    uint16_t row_buffer[240];
    for (uint16_t row = 0; row < frame_height; row++)
    {
        const size_t row_bytes = (size_t)frame_width * sizeof(uint16_t);
        size_t read_now = file.read((uint8_t *)row_buffer, row_bytes);
        if (read_now != row_bytes)
        {
            file.close();
            return false;
        }
        memcpy(buffer + (size_t)(offset_y + row) * 240U + offset_x,
               row_buffer, row_bytes);
    }
    file.close();
    digitalWrite(PIN_SD_CS, HIGH);
    return true;
}

bool find_listen_frame_file(uint8_t frame_index, String *path)
{
    if (path == NULL || frame_index >= UI_LISTEN_FRAME_COUNT)
    {
        return false;
    }
    const unsigned int number = (unsigned int)frame_index + 1;
    char candidates[10][64];
    snprintf(candidates[0], sizeof(candidates[0]), "/ui/anim/listen%02u.rgb565", number);
    snprintf(candidates[1], sizeof(candidates[1]), "/ui/anim/listen%u.rgb565", number);
    snprintf(candidates[2], sizeof(candidates[2]), "/ui/anim/listen_%02u.rgb565", number);
    snprintf(candidates[3], sizeof(candidates[3]), "/ui/listen/listen%02u.rgb565", number);
    snprintf(candidates[4], sizeof(candidates[4]), "/ui/listen/%02u.rgb565", number);
    snprintf(candidates[5], sizeof(candidates[5]), "/ui/listen/%u.rgb565", number);
    snprintf(candidates[6], sizeof(candidates[6]), "/anim/listen%02u.rgb565", number);
    snprintf(candidates[7], sizeof(candidates[7]), "/ui/listen%02u.rgb565", number);
    snprintf(candidates[8], sizeof(candidates[8]), "/ui/anim/listening%02u.rgb565", number);
    snprintf(candidates[9], sizeof(candidates[9]), "/ui/anim/listen%02u.raw", number);
    for (size_t candidate_index = 0; candidate_index < 10; candidate_index++)
    {
        File file = SD.open(candidates[candidate_index], FILE_READ);
        if (file)
        {
            bool valid_size = is_supported_greeting_frame_size(file.size());
            file.close();
            if (valid_size)
            {
                *path = candidates[candidate_index];
                return true;
            }
        }
    }
    return false;
}

void load_listen_animation_frames(void)
{
    listen_frames_loaded = false;
    listen_frame_loaded_count = 0;
    for (uint8_t index = 0; index < UI_LISTEN_FRAME_COUNT; index++)
    {
        listen_frame_available[index] = false;
        if (listen_frame_cache[index] != NULL)
        {
            heap_caps_free(listen_frame_cache[index]);
            listen_frame_cache[index] = NULL;
        }
        if (sd_card_ready == false)
        {
            continue;
        }
        String path;
        if (find_listen_frame_file(index, &path) == false)
        {
            continue;
        }
        uint16_t *buffer = (uint16_t *)heap_caps_malloc(
            240U * 320U * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buffer == NULL)
        {
            Serial.println("聆听动画 PSRAM 缓冲区分配失败");
            break;
        }
        if (read_greeting_frame_file(path.c_str(), buffer) == false)
        {
            heap_caps_free(buffer);
            continue;
        }
        listen_frame_cache[index] = buffer;
        listen_frame_available[index] = true;
        listen_frame_loaded_count++;
        Serial.print("聆听动画已缓存: ");
        Serial.println(path);
    }
    listen_frames_loaded = listen_frame_loaded_count > 0;
    Serial.print("聆听动画缓存帧数: ");
    Serial.print(listen_frame_loaded_count);
    Serial.print("/");
    Serial.println(UI_LISTEN_FRAME_COUNT);
}

void draw_listen_frame(void)
{
    if (listen_frames_loaded == false || screen_ready == false ||
        english_mode_enabled == true || wifi_provision_active == true)
    {
        return;
    }
    uint8_t selected_index = ui_listen_frame_index;
    for (uint8_t offset = 0; offset < UI_LISTEN_FRAME_COUNT; offset++)
    {
        uint8_t candidate = (uint8_t)((ui_listen_frame_index + offset) % UI_LISTEN_FRAME_COUNT);
        if (listen_frame_available[candidate] == true)
        {
            selected_index = candidate;
            break;
        }
    }
    if (listen_frame_available[selected_index] == false)
    {
        return;
    }
    const int animation_top = UI_HEADER_HEIGHT;
    const int animation_bottom = UI_BUBBLE_TOP - 1;
    const int animation_height = animation_bottom - animation_top + 1;
    digitalWrite(PIN_SD_CS, HIGH);
    display.drawRGBBitmap(0, animation_top,
                          listen_frame_cache[selected_index] + (size_t)animation_top * 240,
                          240, animation_height);
}

void start_listen_animation(void)
{
    if (wifi_provision_active == true || english_mode_enabled == true ||
        listen_frames_loaded == false)
    {
        return;
    }
    stop_greeting_animation();
    if (ui_listen_animation_active == true)
    {
        return;
    }
    ui_listen_animation_active = true;
    ui_listen_frame_index = 0;
    ui_listen_next_frame_time = millis() + UI_LISTEN_FRAME_INTERVAL_MS;
    draw_listen_frame();
}

void stop_listen_animation(void)
{
    ui_listen_animation_active = false;
}

void service_listen_animation(void)
{
    if (wifi_provision_active == true || english_mode_enabled == true ||
        screen_ready == false || listen_frames_loaded == false)
    {
        if (ui_listen_animation_active == true)
        {
            stop_listen_animation();
        }
        return;
    }
    if (ui_listening_indicator_active == false || tts_active == true || turn_busy == true)
    {
        if (ui_listen_animation_active == true)
        {
            stop_listen_animation();
        }
        return;
    }
    if (ui_listen_animation_active == false)
    {
        start_listen_animation();
        return;
    }
    const unsigned long now = millis();
    if ((long)(now - ui_listen_next_frame_time) < 0)
    {
        return;
    }
    ui_listen_next_frame_time = now + UI_LISTEN_FRAME_INTERVAL_MS;
    ui_listen_frame_index = (ui_listen_frame_index + 1) % UI_LISTEN_FRAME_COUNT;
    draw_listen_frame();
}

bool find_greeting_frame_file(uint8_t frame_index, String *path)
{
    if (path == NULL || frame_index >= UI_GREETING_FRAME_COUNT)
    {
        return false;
    }
    const unsigned int number = (unsigned int)frame_index + 1;
    char candidates[8][64];
    snprintf(candidates[0], sizeof(candidates[0]), "/ui/anim/greeting%02u.rgb565", number);
    snprintf(candidates[1], sizeof(candidates[1]), "/ui/anim/greeting%u.rgb565", number);
    snprintf(candidates[2], sizeof(candidates[2]), "/ui/anim/greeting_%02u.rgb565", number);
    snprintf(candidates[3], sizeof(candidates[3]), "/anim/greeting%02u.rgb565", number);
    snprintf(candidates[4], sizeof(candidates[4]), "/ui/greeting%02u.rgb565", number);
    snprintf(candidates[5], sizeof(candidates[5]), "/ui/anim/greeting%02u.raw", number);
    snprintf(candidates[6], sizeof(candidates[6]), "/anim/greeting%02u.raw", number);
    snprintf(candidates[7], sizeof(candidates[7]), "/ui/greeting%02u.raw", number);
    for (size_t candidate_index = 0; candidate_index < 8; candidate_index++)
    {
        File file = SD.open(candidates[candidate_index], FILE_READ);
        if (file)
        {
            bool valid_size = is_supported_greeting_frame_size(file.size());
            file.close();
            if (valid_size)
            {
                *path = candidates[candidate_index];
                return true;
            }
        }
    }
    return false;
}

void load_greeting_animation_frames(void)
{
    greeting_frames_loaded = false;
    greeting_frame_loaded_count = 0;
    for (uint8_t index = 0; index < UI_GREETING_FRAME_COUNT; index++)
    {
        greeting_frame_available[index] = false;
        if (greeting_frame_cache[index] != NULL)
        {
            heap_caps_free(greeting_frame_cache[index]);
            greeting_frame_cache[index] = NULL;
        }
        if (sd_card_ready == false)
        {
            continue;
        }
        String path;
        if (find_greeting_frame_file(index, &path) == false)
        {
            continue;
        }
        uint16_t *buffer = (uint16_t *)heap_caps_malloc(
            240U * 320U * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buffer == NULL)
        {
            Serial.println("招手动画 PSRAM 缓冲区分配失败");
            break;
        }
        if (read_greeting_frame_file(path.c_str(), buffer) == false)
        {
            heap_caps_free(buffer);
            continue;
        }
        greeting_frame_cache[index] = buffer;
        greeting_frame_available[index] = true;
        greeting_frame_loaded_count++;
        Serial.print("招手动画已缓存: ");
        Serial.println(path);
    }
    greeting_frames_loaded = greeting_frame_loaded_count > 0;
    Serial.print("招手动画缓存帧数: ");
    Serial.print(greeting_frame_loaded_count);
    Serial.print("/");
    Serial.println(UI_GREETING_FRAME_COUNT);
}

void draw_greeting_frame(void)
{
    if (greeting_frames_loaded == false || screen_ready == false ||
        english_mode_enabled == true || wifi_provision_active == true)
    {
        return;
    }
    uint8_t selected_index = ui_greeting_frame_index;
    for (uint8_t offset = 0; offset < UI_GREETING_FRAME_COUNT; offset++)
    {
        uint8_t candidate = (uint8_t)((ui_greeting_frame_index + offset) % UI_GREETING_FRAME_COUNT);
        if (greeting_frame_available[candidate] == true)
        {
            selected_index = candidate;
            break;
        }
    }
    if (greeting_frame_available[selected_index] == false)
    {
        return;
    }
    // 只推送数字人物区域。旧版整屏推送后再重画气泡，会造成气泡和文字随动画闪烁。
    // 标题、气泡、文字和底部指示带由各自状态函数独立维护，不在动画帧中触碰。
    const int animation_top = UI_HEADER_HEIGHT;
    const int animation_bottom = UI_BUBBLE_TOP - 1;
    const int animation_height = animation_bottom - animation_top + 1;
    digitalWrite(PIN_SD_CS, HIGH);
    display.drawRGBBitmap(0, animation_top,
                          greeting_frame_cache[selected_index] + (size_t)animation_top * 240,
                          240, animation_height);
}

void clear_listening_dots_area(void)
{
    if (screen_ready == false)
    {
        return;
    }
    // 聊天区不再覆盖底部指示带；每帧先恢复原图，避免半径变化留下白色残影。
    digitalWrite(PIN_SD_CS, HIGH);
    const int top = CHAT_TEXT_BOTTOM + 1;
    const int height = 320 - top;
    if (sd_ui_image_loaded == true && sd_ui_framebuffer != NULL && height > 0)
    {
        display.drawRGBBitmap(0, top, sd_ui_framebuffer + (size_t)top * 240, 240, height);
    }
    else if (height > 0)
    {
        display.fillRect(0, top, 240, height, 0x0000);
    }
}

void start_greeting_animation(void)
{
    if (wifi_provision_active == true || english_mode_enabled == true ||
        ui_listening_indicator_active == true)
    {
        return;
    }
    stop_listen_animation();
    ui_greeting_animation_active = greeting_frames_loaded;
    ui_greeting_frame_index = 0;
    ui_greeting_next_frame_time = 0;
    if (ui_greeting_animation_active == true)
    {
        draw_greeting_frame();
    }
}

void stop_greeting_animation(void)
{
    ui_greeting_animation_active = false;
}

void service_greeting_animation(void)
{
    if (wifi_provision_active == true || english_mode_enabled == true ||
        ui_listen_animation_active == true ||
        ui_listening_indicator_active == true ||
        screen_ready == false || tts_active == true || turn_busy == true ||
        asr_active == true)
    {
        return;
    }
    if (ui_greeting_animation_active == false)
    {
        start_greeting_animation();
        return;
    }
    const unsigned long now = millis();
    if ((long)(now - ui_greeting_next_frame_time) < 0)
    {
        return;
    }
    ui_greeting_next_frame_time = now + UI_GREETING_FRAME_INTERVAL_MS;
    ui_greeting_frame_index = (ui_greeting_frame_index + 1) % UI_GREETING_FRAME_COUNT;
    draw_greeting_frame();
}

void fill_companion_chat_canvas_background(void)
{
    if (chat_canvas == NULL || chat_canvas->getBuffer() == NULL)
    {
        return;
    }
    const int chat_width = UI_BUBBLE_RIGHT - UI_BUBBLE_LEFT + 1;
    const int chat_height = CHAT_TEXT_BOTTOM - CHAT_TEXT_TOP + 1;
    uint16_t *canvas_pixels = chat_canvas->getBuffer();
    if (chat_background_cache != NULL)
    {
        memcpy(canvas_pixels, chat_background_cache,
               (size_t)chat_width * chat_height * sizeof(uint16_t));
        return;
    }
    if (sd_ui_framebuffer != NULL)
    {
        for (int row = 0; row < chat_height; row++)
        {
            memcpy(canvas_pixels + (size_t)row * chat_width,
                   sd_ui_framebuffer + (size_t)(CHAT_TEXT_TOP + row) * 240 + UI_BUBBLE_LEFT,
                   (size_t)chat_width * sizeof(uint16_t));
        }
        return;
    }
    chat_canvas->fillScreen(0x0000);
}

void draw_companion_status_text(const char *text)
{
    if (screen_ready == false || english_mode_enabled == true || text == NULL)
    {
        return;
    }
    if (ui_listen_animation_active == false)
    {
        stop_greeting_animation();
        draw_ui_header();
    }
    String signature = String("companion-status:") + text;
    if (signature == chat_last_render_signature)
    {
        return;
    }
    chat_last_render_signature = signature;
    const int chat_left = UI_BUBBLE_LEFT;
    const int chat_width = UI_BUBBLE_RIGHT - UI_BUBBLE_LEFT + 1;
    const int chat_height = CHAT_TEXT_BOTTOM - CHAT_TEXT_TOP + 1;
    const int bubble_height = 42;
    const bool use_canvas = chat_canvas != NULL && chat_canvas->getBuffer() != NULL;
    if (use_canvas == true)
    {
        fill_companion_chat_canvas_background();
        chat_canvas->fillRoundRect(0, 0, chat_width, bubble_height, 12, UI_BUBBLE_COLOR);
        chat_canvas_font.setFontMode(0);
        chat_canvas_font.setBackgroundColor(UI_BUBBLE_COLOR);
        chat_canvas_font.setForegroundColor(UI_BUBBLE_TEXT_COLOR);
        chat_canvas_font.setFont(u8g2_font_wqy16_t_gb2312);
        int text_width = chat_canvas_font.getUTF8Width(text);
        if (text_width > chat_width - 12)
        {
            chat_canvas_font.setFont(u8g2_font_wqy12_t_gb2312);
            text_width = chat_canvas_font.getUTF8Width(text);
        }
        int x = (chat_width - text_width) / 2;
        if (x < 6)
        {
            x = 6;
        }
        chat_canvas_font.drawUTF8(x, 26, text);
        chat_canvas_font.setFontMode(1);
        digitalWrite(PIN_SD_CS, HIGH);
        display.drawRGBBitmap(chat_left, CHAT_TEXT_TOP, chat_canvas->getBuffer(), chat_width, chat_height);
    }
    else
    {
        clear_dialog_text_area();
        display.fillRoundRect(UI_BUBBLE_LEFT, UI_BUBBLE_TOP, chat_width, bubble_height, 12, UI_BUBBLE_COLOR);
        chinese_font.setFontMode(0);
        chinese_font.setBackgroundColor(UI_BUBBLE_COLOR);
        chinese_font.setForegroundColor(UI_BUBBLE_TEXT_COLOR);
        chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
        int text_width = chinese_font.getUTF8Width(text);
        if (text_width > chat_width - 12)
        {
            chinese_font.setFont(u8g2_font_wqy12_t_gb2312);
            text_width = chinese_font.getUTF8Width(text);
        }
        int x = UI_BUBBLE_LEFT + (chat_width - text_width) / 2;
        if (x < UI_BUBBLE_LEFT + 6)
        {
            x = UI_BUBBLE_LEFT + 6;
        }
        chinese_font.drawUTF8(x, UI_BUBBLE_TOP + 26, text);
        chinese_font.setFontMode(1);
    }
    clear_listening_dots_area();
}

void draw_listening_indicator(void)
{
    if (screen_ready == false || english_mode_enabled == true)
    {
        return;
    }
    start_listen_animation();
    draw_companion_status_text("正在倾听");
}

void paint_button_toast(void)
{
    if (screen_ready == false || button_toast_text[0] == '\0')
    {
        return;
    }
    digitalWrite(PIN_SD_CS, HIGH);
    display.fillRoundRect(10, 6, 220, 32, 8, 0x10A2);
    chinese_font.setFontMode(0);
    chinese_font.setBackgroundColor(0x10A2);
    chinese_font.setForegroundColor(ST77XX_WHITE);
    chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
    int width = chinese_font.getUTF8Width(button_toast_text);
    if (width > 208)
    {
        chinese_font.setFont(u8g2_font_wqy12_t_gb2312);
        width = chinese_font.getUTF8Width(button_toast_text);
    }
    int x = (240 - width) / 2;
    if (x < 16)
    {
        x = 16;
    }
    chinese_font.drawUTF8(x, 28, button_toast_text);
    chinese_font.setFontMode(1);
}

void restore_button_toast_area(void)
{
    if (screen_ready == false)
    {
        return;
    }
    digitalWrite(PIN_SD_CS, HIGH);
    if (english_mode_enabled == true && english_ui_framebuffer != NULL)
    {
        display.drawRGBBitmap(0, 0, english_ui_framebuffer, 240, UI_HEADER_HEIGHT);
        return;
    }
    if (english_mode_enabled == true)
    {
        display.fillRect(0, 0, 240, UI_HEADER_HEIGHT, ST77XX_BLACK);
        return;
    }
    draw_ui_header();
}

void show_button_toast(const char *text)
{
    if (text == NULL)
    {
        return;
    }
    size_t copy_index = 0;
    while (text[copy_index] != '\0' && copy_index < sizeof(button_toast_text) - 1)
    {
        button_toast_text[copy_index] = text[copy_index];
        copy_index++;
    }
    button_toast_text[copy_index] = '\0';
    button_toast_visible = true;
    button_toast_expire_time = millis() + BUTTON_TOAST_MS;
    paint_button_toast();
}

void service_button_toast(void)
{
    if (button_toast_visible == false)
    {
        return;
    }
    if ((long)(millis() - button_toast_expire_time) < 0)
    {
        return;
    }
    button_toast_visible = false;
    button_toast_text[0] = '\0';
    restore_button_toast_area();
}

