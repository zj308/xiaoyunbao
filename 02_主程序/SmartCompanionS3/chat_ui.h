// Chat history and sentence-synchronized UI
// Shared state and declarations are kept in the main sketch.

void discard_dialog_user_preview(void)
{
    if (chat_live_user_message_index >= 0 &&
        chat_live_user_message_index == chat_history_count - 1)
    {
        chat_history_count = chat_history_count - 1;
    }
    chat_live_user_message_index = -1;
    chat_scroll_lines = 0;
    redraw_chat_history();
}

void clear_dialog_text_area(void)
{
    if (english_mode_enabled == true)
    {
        return;
    }
    if (sd_ui_image_loaded == true)
    {
        digitalWrite(PIN_SD_CS, HIGH);
        const int left = UI_BUBBLE_LEFT;
        const int width = UI_BUBBLE_RIGHT - UI_BUBBLE_LEFT + 1;
        if (chat_background_cache != NULL)
        {
            display.drawRGBBitmap(left, CHAT_TEXT_TOP, chat_background_cache, width, CHAT_TEXT_BOTTOM - CHAT_TEXT_TOP + 1);
            return;
        }
        String lower_path = sd_ui_image_path;
        lower_path.toLowerCase();
        if (lower_path.endsWith(".rgb565") || lower_path.endsWith(".raw"))
        {
            draw_rgb565_region(sd_ui_image_path.c_str(), left, CHAT_TEXT_TOP,
                               width, CHAT_TEXT_BOTTOM - CHAT_TEXT_TOP + 1);
            return;
        }
        if (lower_path.endsWith(".bmp"))
        {
            draw_bmp_region(sd_ui_image_path.c_str(), left, CHAT_TEXT_TOP,
                            width, CHAT_TEXT_BOTTOM - CHAT_TEXT_TOP + 1);
            return;
        }
        if (sd_ui_framebuffer == NULL)
        {
            return;
        }
        for (int y = CHAT_TEXT_TOP; y <= CHAT_TEXT_BOTTOM; y++)
        {
            display.drawRGBBitmap(left, y, sd_ui_framebuffer + (size_t)y * 240 + left, width, 1);
        }
        return;
    }
    // 没有 SD UI 背景时继续使用原有暖白色聊天区域。
    display.fillRect(UI_BUBBLE_LEFT, CHAT_TEXT_TOP, UI_BUBBLE_RIGHT - UI_BUBBLE_LEFT + 1,
                     CHAT_TEXT_BOTTOM - CHAT_TEXT_TOP + 1, 0x0000);
}

String dialog_fit_text_from_end(const String &text, int maximum_width)
{
    // 空文字无需裁剪，直接返回。
    if (text.length() == 0)
    {
        return "";
    }
    // 从文本结尾反向删除完整 UTF-8 字符，保留最新的一段用户话语。
    size_t start_index = 0;
    while (start_index < text.length())
    {
        String candidate = text.substring(start_index);
        if (chinese_font.getUTF8Width(candidate.c_str()) <= maximum_width)
        {
            return candidate;
        }
        size_t character_bytes = utf8_character_length(text, start_index);
        start_index = start_index + character_bytes;
    }
    // 极端情况下至少返回最后一个完整字符。
    return text.substring(text.length() - 1);
}

void add_chat_message(const String &text, bool from_user)
{
    // 空消息不写入历史，防止识别失败时产生空白行。
    if (text.length() == 0)
    {
        return;
    }
    // 历史已满时整体左移，始终保留最近十二条消息。
    if (chat_history_count >= CHAT_HISTORY_CAPACITY)
    {
        for (int index = 1; index < CHAT_HISTORY_CAPACITY; index++)
        {
            chat_history[index - 1] = chat_history[index];
        }
        chat_history_count = CHAT_HISTORY_CAPACITY - 1;
        // 历史移动后，同步修正正在逐字显示的消息下标。
        if (chat_speaking_message_index > 0)
        {
            chat_speaking_message_index = chat_speaking_message_index - 1;
        }
        else if (chat_speaking_message_index == 0)
        {
            // 正在朗读的消息已被容量淘汰，不能留下无效的负下标。
            chat_speaking_message_index = -1;
            chat_speaking_visible_bytes = 0;
        }
    }
    // 保存文字和说话方信息。
    chat_history[chat_history_count].text = text;
    chat_history[chat_history_count].from_user = from_user;
    chat_history_count = chat_history_count + 1;
}

String chat_visible_text(const String &text, int message_index)
{
    // 非当前播放的消息完整显示。
    if (message_index != chat_speaking_message_index)
    {
        return text;
    }
    // 当前机器人消息只显示已经与声音同步的完整 UTF-8 字符。
    if (chat_speaking_visible_bytes >= text.length())
    {
        return text;
    }
    return text.substring(0, chat_speaking_visible_bytes);
}

void append_chat_render_line(const String &text, bool from_user, int message_index)
{
    if (text.length() == 0)
    {
        return;
    }
    // 排版缓存满时淘汰最旧的一行，保证刚生成的回复永远能进入屏幕窗口。
    if (chat_render_line_count >= CHAT_RENDER_LINE_CAPACITY)
    {
        for (int index = 1; index < CHAT_RENDER_LINE_CAPACITY; index++)
        {
            chat_render_lines[index - 1] = chat_render_lines[index];
        }
        chat_render_line_count = CHAT_RENDER_LINE_CAPACITY - 1;
    }
    chat_render_lines[chat_render_line_count].text = text;
    chat_render_lines[chat_render_line_count].from_user = from_user;
    chat_render_lines[chat_render_line_count].message_index = message_index;
    chat_render_line_count = chat_render_line_count + 1;
}

static const int ENGLISH_SPACE_WIDTH = 5;

int measure_chat_text_width(const String &text)
{
    if (english_mode_enabled == false)
    {
        return chinese_font.getUTF8Width(text.c_str());
    }
    int width = 0;
    size_t read_index = 0;
    while (read_index < text.length())
    {
        size_t character_bytes = utf8_character_length(text, read_index);
        if (character_bytes == 0)
        {
            break;
        }
        if (character_bytes == 1 && text.charAt(read_index) == ' ')
        {
            width = width + ENGLISH_SPACE_WIDTH;
        }
        else
        {
            String one_character = text.substring(read_index, read_index + character_bytes);
            width = width + chinese_font.getUTF8Width(one_character.c_str());
        }
        read_index = read_index + character_bytes;
    }
    return width;
}

void draw_chat_utf8(U8G2_FOR_ADAFRUIT_GFX &font, int x, int y, const String &text)
{
    if (english_mode_enabled == false)
    {
        font.drawUTF8(x, y, text.c_str());
        return;
    }
    size_t read_index = 0;
    while (read_index < text.length())
    {
        size_t character_bytes = utf8_character_length(text, read_index);
        if (character_bytes == 0)
        {
            break;
        }
        if (character_bytes == 1 && text.charAt(read_index) == ' ')
        {
            x = x + ENGLISH_SPACE_WIDTH;
        }
        else
        {
            String one_character = text.substring(read_index, read_index + character_bytes);
            font.drawUTF8(x, y, one_character.c_str());
            x = x + font.getUTF8Width(one_character.c_str());
        }
        read_index = read_index + character_bytes;
    }
}

int chat_line_wrap_width(void)
{
    if (english_mode_enabled == true)
    {
        return EN_BUBBLE_MAX_WIDTH - EN_BUBBLE_PAD_X * 2 - CHAT_BOLD_RIGHT_MARGIN;
    }
    return CHAT_TEXT_RIGHT - CHAT_TEXT_LEFT - CHAT_BOLD_RIGHT_MARGIN;
}

void rebuild_chat_render_lines(void)
{
    // 每次重绘前都从聊天历史重新排版，滚动时不需要修改原始消息。
    chat_render_line_count = 0;
    chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
    const int wrap_width = chat_line_wrap_width();
    for (int message_index = 0; message_index < chat_history_count; message_index++)
    {
        if (english_mode_enabled == false && chat_history[message_index].from_user == true)
        {
            continue;
        }
        String visible_text = chat_visible_text(chat_history[message_index].text, message_index);
        // 当前回复尚未播放出第一个字符时，不显示空白机器人行。
        if (visible_text.length() == 0)
        {
            continue;
        }
        String current_line = "";
        size_t read_index = 0;
        while (read_index < visible_text.length())
        {
            size_t character_bytes = utf8_character_length(visible_text, read_index);
            String one_character = visible_text.substring(read_index, read_index + character_bytes);
            // 显式换行也结束当前行。
            if (one_character == "\n")
            {
                if (current_line.length() > 0)
                {
                    append_chat_render_line(current_line, chat_history[message_index].from_user, message_index);
                }
                current_line = "";
                read_index = read_index + character_bytes;
                continue;
            }
            String candidate_line = current_line + one_character;
            int candidate_width = measure_chat_text_width(candidate_line);
            if (candidate_width <= wrap_width)
            {
                current_line = candidate_line;
                read_index = read_index + character_bytes;
                continue;
            }
            if (english_mode_enabled == true)
            {
                int break_at = current_line.lastIndexOf(' ');
                if (break_at > 0)
                {
                    String committed = current_line.substring(0, break_at);
                    committed.trim();
                    String remainder = current_line.substring(break_at + 1);
                    if (committed.length() > 0)
                    {
                        append_chat_render_line(committed, chat_history[message_index].from_user, message_index);
                    }
                    current_line = remainder;
                    continue;
                }
            }
            // 当前行已满，写入行缓存后从新行继续处理当前字符。
            if (current_line.length() > 0)
            {
                append_chat_render_line(current_line, chat_history[message_index].from_user, message_index);
                current_line = "";
                continue;
            }
            // 单个异常宽字符也必须前进，防止排版死循环。
            read_index = read_index + character_bytes;
        }
        // 写入消息末尾尚未提交的最后一行。
        if (current_line.length() > 0)
        {
            append_chat_render_line(current_line, chat_history[message_index].from_user, message_index);
        }
        // 不插入整行空白，避免旧回复占满窗口后把当前问题挤出可见区域。
    }
}

void redraw_english_chat(void)
{
    if (screen_ready == false)
    {
        return;
    }
    digitalWrite(PIN_SD_CS, HIGH);
    rebuild_chat_render_lines();
    struct EnglishBlock
    {
        int first_line;
        int line_count;
        bool from_user;
        int height;
    };
    EnglishBlock blocks[CHAT_HISTORY_CAPACITY];
    int block_count = 0;
    for (int line_index = 0; line_index < chat_render_line_count; line_index++)
    {
        if (block_count == 0 ||
            blocks[block_count - 1].from_user != chat_render_lines[line_index].from_user ||
            chat_render_lines[line_index].message_index !=
                chat_render_lines[blocks[block_count - 1].first_line].message_index)
        {
            if (block_count >= CHAT_HISTORY_CAPACITY)
            {
                break;
            }
            blocks[block_count].first_line = line_index;
            blocks[block_count].line_count = 1;
            blocks[block_count].from_user = chat_render_lines[line_index].from_user;
            block_count++;
        }
        else
        {
            blocks[block_count - 1].line_count++;
        }
    }
    int total_height = 0;
    for (int index = 0; index < block_count; index++)
    {
        blocks[index].height = EN_BUBBLE_PAD_Y * 2 +
                               blocks[index].line_count * EN_CHAT_LINE_HEIGHT +
                               EN_BUBBLE_GAP;
        total_height += blocks[index].height;
    }
    const int panel_height = EN_CHAT_BOTTOM - EN_CHAT_TOP + 1;
    int origin = EN_CHAT_BOTTOM - total_height;
    if (origin < EN_CHAT_TOP)
    {
        origin = EN_CHAT_BOTTOM - total_height;
    }
    origin += chat_scroll_lines * (EN_CHAT_LINE_HEIGHT + EN_BUBBLE_GAP);
    String signature = String(english_mode_enabled) + "/" + String(chat_scroll_lines) + "/";
    for (int index = 0; index < chat_render_line_count; index++)
    {
        signature += chat_render_lines[index].from_user ? "U:" : "A:";
        signature += String(chat_render_lines[index].message_index) + ":";
        signature += chat_render_lines[index].text;
        signature += "|";
    }
    if (signature == chat_last_render_signature)
    {
        return;
    }
    chat_last_render_signature = signature;
    if (block_count == 0)
    {
        draw_english_listen_prompt();
        return;
    }
    const int panel_width = EN_CHAT_RIGHT - EN_CHAT_LEFT + 1;
    const bool use_canvas = english_chat_canvas != NULL && english_chat_canvas->getBuffer() != NULL;
    if (use_canvas == true)
    {
        english_chat_canvas->fillScreen(english_chat_panel_color);
        english_chat_font.setFont(u8g2_font_wqy16_t_gb2312);
        english_chat_font.setFontMode(0);
    }
    else
    {
        fill_english_chat_panel();
        chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
        chinese_font.setFontMode(0);
    }
    int draw_y = origin;
    for (int block_index = 0; block_index < block_count; block_index++)
    {
        const EnglishBlock &block = blocks[block_index];
        const int bubble_height = EN_BUBBLE_PAD_Y * 2 + block.line_count * EN_CHAT_LINE_HEIGHT;
        int max_text_width = 0;
        for (int line = 0; line < block.line_count; line++)
        {
            const char *line_text = chat_render_lines[block.first_line + line].text.c_str();
            int width = measure_chat_text_width(line_text);
            if (width > max_text_width)
            {
                max_text_width = width;
            }
        }
        int bubble_width = max_text_width + EN_BUBBLE_PAD_X * 2 + CHAT_BOLD_RIGHT_MARGIN;
        if (bubble_width < 28)
        {
            bubble_width = 28;
        }
        if (bubble_width > EN_BUBBLE_MAX_WIDTH)
        {
            bubble_width = EN_BUBBLE_MAX_WIDTH;
        }
        int bubble_x = block.from_user == true
            ? EN_CHAT_RIGHT - bubble_width - 4
            : EN_CHAT_LEFT + 4;
        int bubble_y = draw_y;
        draw_y += block.height;
        if (bubble_y + bubble_height <= EN_CHAT_TOP || bubble_y >= EN_CHAT_BOTTOM)
        {
            continue;
        }
        uint16_t bubble_color = block.from_user == true ? EN_USER_BUBBLE_COLOR : EN_AI_BUBBLE_COLOR;
        if (use_canvas == true)
        {
            const int canvas_x = bubble_x - EN_CHAT_LEFT;
            const int canvas_y = bubble_y - EN_CHAT_TOP;
            english_chat_canvas->fillRoundRect(canvas_x, canvas_y, bubble_width, bubble_height,
                                               EN_BUBBLE_RADIUS, bubble_color);
            english_chat_font.setBackgroundColor(bubble_color);
            english_chat_font.setForegroundColor(EN_CHAT_TEXT_COLOR);
            for (int line = 0; line < block.line_count; line++)
            {
                int baseline = canvas_y + EN_BUBBLE_PAD_Y + 14 + line * EN_CHAT_LINE_HEIGHT;
                if (baseline < 14 || baseline > panel_height - 2)
                {
                    continue;
                }
                draw_chat_utf8(english_chat_font, canvas_x + EN_BUBBLE_PAD_X, baseline,
                               chat_render_lines[block.first_line + line].text);
            }
        }
        else
        {
            display.fillRoundRect(bubble_x, bubble_y, bubble_width, bubble_height,
                                  EN_BUBBLE_RADIUS, bubble_color);
            chinese_font.setBackgroundColor(bubble_color);
            chinese_font.setForegroundColor(EN_CHAT_TEXT_COLOR);
            for (int line = 0; line < block.line_count; line++)
            {
                int baseline = bubble_y + EN_BUBBLE_PAD_Y + 14 + line * EN_CHAT_LINE_HEIGHT;
                if (baseline < EN_CHAT_TOP + 14 || baseline > EN_CHAT_BOTTOM - 2)
                {
                    continue;
                }
                draw_chat_utf8(chinese_font, bubble_x + EN_BUBBLE_PAD_X, baseline,
                               chat_render_lines[block.first_line + line].text);
            }
        }
    }
    if (use_canvas == true)
    {
        digitalWrite(PIN_SD_CS, HIGH);
        display.drawRGBBitmap(EN_CHAT_LEFT, EN_CHAT_TOP,
                              english_chat_canvas->getBuffer(), panel_width, panel_height);
    }
}

void redraw_chat_history(void)
{
    // 屏幕没有初始化时不进行 SPI 绘制。
    if (screen_ready == false)
    {
        return;
    }
    if (english_mode_enabled == true)
    {
        redraw_english_chat();
        return;
    }
    // 共享 SPI 总线时保证 TF 卡不被选中。
    digitalWrite(PIN_SD_CS, HIGH);
    // 聊天重绘也刷新顶部标题，避免缩放后的 UI 位图让“云伴/陪伴模式”发糊。
    draw_ui_header();
    // 先刷新全部文字行，再决定当前窗口显示哪七行。
    rebuild_chat_render_lines();
    int maximum_scroll = chat_render_line_count - CHAT_VISIBLE_LINE_COUNT;
    if (maximum_scroll < 0)
    {
        maximum_scroll = 0;
    }
    if (chat_scroll_lines < 0)
    {
        chat_scroll_lines = 0;
    }
    if (chat_scroll_lines > maximum_scroll)
    {
        chat_scroll_lines = maximum_scroll;
    }
    // 手动翻阅历史时使用连续窗口；自动跟随时只排本轮问答。
    int first_visible_line = chat_render_line_count - CHAT_VISIBLE_LINE_COUNT - chat_scroll_lines;
    if (first_visible_line < 0)
    {
        first_visible_line = 0;
    }
    int visible_source_lines[CHAT_VISIBLE_LINE_COUNT];
    int visible_source_count = 0;
    if (chat_scroll_lines == 0)
    {
        // 本轮按“用户文字、空一行、机器人回复”组成完整行流；铺满后整体逐行上移。
        int latest_user_message = -1;
        for (int message_index = chat_history_count - 1; message_index >= 0; message_index--)
        {
            if (chat_history[message_index].from_user == true)
            {
                latest_user_message = message_index;
                break;
            }
        }
        if (latest_user_message >= 0)
        {
            int turn_source_lines[CHAT_RENDER_LINE_CAPACITY + 1];
            int turn_source_count = 0;
            for (int line_index = 0; line_index < chat_render_line_count; line_index++)
            {
                if (chat_render_lines[line_index].message_index == latest_user_message)
                {
                    if (turn_source_count < CHAT_RENDER_LINE_CAPACITY + 1)
                    {
                        turn_source_lines[turn_source_count++] = line_index;
                    }
                }
            }
            // -1 是不绘字的占位行，让本轮用户文字与回复固定间隔一行。
            if (turn_source_count > 0 && turn_source_count < CHAT_RENDER_LINE_CAPACITY + 1)
            {
                turn_source_lines[turn_source_count++] = -1;
            }
            for (int line_index = 0; line_index < chat_render_line_count; line_index++)
            {
                if (chat_render_lines[line_index].message_index > latest_user_message &&
                    turn_source_count < CHAT_RENDER_LINE_CAPACITY + 1)
                {
                    turn_source_lines[turn_source_count++] = line_index;
                }
            }
            int first_turn_line = turn_source_count - CHAT_VISIBLE_LINE_COUNT;
            if (first_turn_line < 0)
            {
                first_turn_line = 0;
            }
            for (int index = first_turn_line; index < turn_source_count && visible_source_count < CHAT_VISIBLE_LINE_COUNT; index++)
            {
                visible_source_lines[visible_source_count++] = turn_source_lines[index];
            }
        }
    }
    if (visible_source_count == 0)
    {
        if (chat_scroll_lines == 0)
        {
            // 自动跟随时没有本轮可见字，保留聆听提示，不要回退画出上一轮全文。
            return;
        }
        for (int screen_line = 0; screen_line < CHAT_VISIBLE_LINE_COUNT; screen_line++)
        {
            int source_line = first_visible_line + screen_line;
            if (source_line >= chat_render_line_count)
            {
                break;
            }
            visible_source_lines[visible_source_count++] = source_line;
        }
    }
    // 只按最终可见窗口判重，并把空白占位行也纳入签名。窗口上移时一定会触发整帧刷新。
    String signature = String(chat_scroll_lines) + "/" + String(visible_source_count) + "/";
    for (int index = 0; index < visible_source_count; index++)
    {
        int source_line = visible_source_lines[index];
        if (source_line < 0)
        {
            signature += "BLANK|";
            continue;
        }
        signature += chat_render_lines[source_line].from_user ? "U:" : "A:";
        signature += String(chat_render_lines[source_line].message_index) + ":";
        signature += chat_render_lines[source_line].text;
        signature += "|";
    }
    if (signature == chat_last_render_signature)
    {
        return;
    }
    chat_last_render_signature = signature;
    const int chat_left = UI_BUBBLE_LEFT;
    const int chat_width = UI_BUBBLE_RIGHT - UI_BUBBLE_LEFT + 1;
    const int chat_height = CHAT_TEXT_BOTTOM - CHAT_TEXT_TOP + 1;
    bool use_canvas = chat_canvas != NULL && chat_canvas->getBuffer() != NULL;
    if (use_canvas == true)
    {
        uint16_t *canvas_pixels = chat_canvas->getBuffer();
        if (chat_background_cache != NULL)
        {
            memcpy(canvas_pixels, chat_background_cache,
                   (size_t)chat_width * chat_height * sizeof(uint16_t));
        }
        else if (sd_ui_framebuffer != NULL)
        {
            for (int row = 0; row < chat_height; row++)
            {
                memcpy(canvas_pixels + (size_t)row * chat_width,
                       sd_ui_framebuffer + (size_t)(CHAT_TEXT_TOP + row) * 240 + chat_left,
                       (size_t)chat_width * sizeof(uint16_t));
            }
        }
        else
        {
            chat_canvas->fillScreen(0x0000);
        }
        chat_canvas_font.setFont(u8g2_font_wqy16_t_gb2312);
    }
    else
    {
        // 内存不足时保留旧绘制路径，确保屏幕仍可使用。
        clear_dialog_text_area();
        chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
    }
    const int bubble_height = visible_source_count > 0
        ? min(CHAT_TEXT_BOTTOM - CHAT_TEXT_TOP + 1, 22 + visible_source_count * CHAT_LINE_HEIGHT)
        : 0;
    if (bubble_height > 0)
    {
        if (use_canvas == true)
        {
            chat_canvas->fillRoundRect(0, 0, chat_width, bubble_height, 12, UI_BUBBLE_COLOR);
        }
        else
        {
            display.fillRoundRect(UI_BUBBLE_LEFT, CHAT_TEXT_TOP, chat_width, bubble_height, 12, UI_BUBBLE_COLOR);
        }
    }
    for (int screen_line = 0; screen_line < visible_source_count; screen_line++)
    {
        int source_line = visible_source_lines[screen_line];
        if (source_line < 0)
        {
            continue;
        }
        String line_text = chat_render_lines[source_line].text;
        if (line_text.length() == 0)
        {
            continue;
        }
        int baseline = CHAT_TEXT_TOP + 16 + screen_line * CHAT_LINE_HEIGHT;
        if (true)
        {
            // 机器人回复使用白色文字显示在蓝色气泡内。
            if (use_canvas == true)
            {
                chat_canvas_font.setForegroundColor(UI_BUBBLE_TEXT_COLOR);
                draw_bold_chat_text_to_canvas(CHAT_TEXT_LEFT - chat_left, baseline - CHAT_TEXT_TOP, line_text);
            }
            else
            {
                chinese_font.setForegroundColor(UI_BUBBLE_TEXT_COLOR);
                draw_bold_chat_text(CHAT_TEXT_LEFT, baseline, line_text);
            }
        }
    }
    if (use_canvas == true)
    {
        // 文字和背景已经完整合成，只进行一次整块传输，肉眼看不到擦除过程。
        digitalWrite(PIN_SD_CS, HIGH);
        display.drawRGBBitmap(chat_left, CHAT_TEXT_TOP, chat_canvas->getBuffer(), chat_width, chat_height);
    }
}

void update_chat_speech_sync(void)
{
    // 没有正在朗读的机器人消息时无需刷新。
    if (chat_speaking_message_index < 0 || chat_speaking_message_index >= chat_history_count)
    {
        return;
    }
    // 用户正在翻阅旧记录时不自动把屏幕拉回最新位置。
    if (chat_scroll_lines != 0)
    {
        return;
    }
    unsigned long current_time = millis();
    if (current_time - chat_last_sync_refresh_time < CHAT_SYNC_REFRESH_INTERVAL_MS)
    {
        return;
    }
    // 读取已经播放的单声道 PCM 采样数。
    unsigned long played_samples = tts_played_sample_count;
    unsigned long total_samples = tts_total_sample_count;
    const String &full_text = chat_history[chat_speaking_message_index].text;
    size_t total_character_count = count_utf8_characters(full_text);
    if (total_character_count == 0)
    {
        return;
    }

    // 按实际播放进度逐字显示，排版只在接近屏幕边缘时换行，不再按句子整段跳出。
    size_t visible_end = chat_speaking_visible_bytes;
    // 必须是本轮已经开播后的排空，才能一次补全余下文字。
    // 上一轮留下的 drained 会把新回复先整段画出来，随后又被播放进度擦掉重打。
    if (tts_playback_drained == true && tts_playback_started == true)
    {
        visible_end = full_text.length();
    }
    else if (tts_playback_started == true && played_samples > 0)
    {
        uint64_t lead_samples = (uint64_t)SPEAKER_SAMPLE_RATE * TTS_SENTENCE_DISPLAY_LEAD_MS / 1000;
        uint64_t reveal_samples = (uint64_t)played_samples + lead_samples;
        size_t target_character_count = 0;
        if (total_samples > 0)
        {
            if (reveal_samples > total_samples)
            {
                reveal_samples = total_samples;
            }
            target_character_count = (size_t)(((uint64_t)total_character_count * reveal_samples) / total_samples);
        }
        else
        {
            double characters_per_second = TTS_ESTIMATED_CHARACTERS_PER_SECOND * current_tts_rate;
            target_character_count = (size_t)(((double)played_samples * characters_per_second) / SPEAKER_SAMPLE_RATE);
        }
        if (target_character_count == 0)
        {
            target_character_count = 1;
        }
        if (target_character_count > total_character_count)
        {
            target_character_count = total_character_count;
        }
        visible_end = utf8_bytes_for_character_count(full_text, target_character_count);
    }
    if (visible_end != chat_speaking_visible_bytes)
    {
        chat_speaking_visible_bytes = visible_end;
        chat_last_sync_refresh_time = current_time;
        redraw_chat_history();
    }
}

void scroll_chat_history(int direction)
{
    // direction 为正表示查看更早的记录，为负表示回到较新的记录。
    chat_scroll_lines = chat_scroll_lines + direction;
    redraw_chat_history();
}

void draw_dialog_reply_page(void)
{
    // 保留旧函数名以减少其他逻辑改动，实际调用新的聊天记录绘制函数。
    redraw_chat_history();
}

void update_dialog_reply_page(void)
{
    // 保留旧函数名以减少主循环改动，实际按 TTS PCM 播放位置同步文字。
    update_chat_speech_sync();
}

