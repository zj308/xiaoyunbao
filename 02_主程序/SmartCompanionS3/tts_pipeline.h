// Sentence queue, TTS streaming and playback
// Shared state and declarations are kept in the main sketch.

void start_reply_sentence_sequence(const String &reply)
{
    // 完整回复只发送给 CosyVoice 一次，消除逐句重连和重复合成等待。
    String parsed_emotion = "neutral";
    String spoken_reply = reply;
    parse_emotion_reply(reply, &parsed_emotion, &spoken_reply);
    spoken_reply = strip_emotion_markup(spoken_reply);
    spoken_reply.trim();
    if (spoken_reply.length() == 0)
    {
        turn_busy = false;
        return;
    }
    if (is_valid_emotion(parsed_emotion) == true)
    {
        current_emotion = parsed_emotion;
        set_tts_parameters_for_emotion(current_emotion);
    }
    tts_pending_reply = "";
    tts_sentence_sequence_active = true;
    set_dialog_reply_visible(spoken_reply, 0);
    Serial.print("连续回复已加入气泡，按语音逐字显示，整条一次合成，字节数: ");
    Serial.println((unsigned int)spoken_reply.length());
    start_tts(spoken_reply);
}

bool take_next_reply_sentence(String *sentence)
{
    if (sentence == NULL)
    {
        return false;
    }
    sentence->remove(0);
    tts_pending_reply.trim();
    if (tts_pending_reply.length() == 0)
    {
        return false;
    }
    size_t read_index = 0;
    size_t sentence_end_bytes = tts_pending_reply.length();
    while (read_index < tts_pending_reply.length())
    {
        size_t character_bytes = utf8_character_length(tts_pending_reply, read_index);
        if (character_bytes == 0)
        {
            break;
        }
        String one_character = tts_pending_reply.substring(read_index, read_index + character_bytes);
        read_index = read_index + character_bytes;
        if (is_sentence_ending_character(one_character) == true || one_character == "\n")
        {
            sentence_end_bytes = read_index;
            break;
        }
    }
    *sentence = tts_pending_reply.substring(0, sentence_end_bytes);
    sentence->trim();
    tts_pending_reply = tts_pending_reply.substring(sentence_end_bytes);
    tts_pending_reply.trim();
    if (sentence->length() == 0)
    {
        return take_next_reply_sentence(sentence);
    }
    return true;
}

String remove_wake_word_punctuation(const String &text)
{
    String cleaned_text = text;
    cleaned_text.trim();
    cleaned_text.replace("。", "");
    cleaned_text.replace("，", "");
    cleaned_text.replace("！", "");
    cleaned_text.replace("？", "");
    cleaned_text.replace("、", "");
    cleaned_text.replace(".", "");
    cleaned_text.replace(",", "");
    cleaned_text.replace("!", "");
    cleaned_text.replace("?", "");
    cleaned_text.replace(":", "");
    cleaned_text.replace(";", "");
    cleaned_text.trim();
    return cleaned_text;
}

bool is_ignorable_asr_text(const String &text)
{
    String cleaned_text = remove_wake_word_punctuation(text);
    cleaned_text.replace(" ", "");
    cleaned_text.replace("　", "");
    cleaned_text.toLowerCase();
    if (cleaned_text.length() == 0)
    {
        return true;
    }
    if (cleaned_text == "啊" || cleaned_text == "嗯" || cleaned_text == "哦" ||
        cleaned_text == "呃" || cleaned_text == "唉" || cleaned_text == "额" ||
        cleaned_text == "啊啊" || cleaned_text == "嗯嗯" || cleaned_text == "哦哦" ||
        cleaned_text == "uh" || cleaned_text == "um" || cleaned_text == "hmm" ||
        cleaned_text == "mm" || cleaned_text == "ah" || cleaned_text == "oh" ||
        cleaned_text == "er" || cleaned_text == "eh" || cleaned_text == "huh" ||
        cleaned_text == "ha" || cleaned_text == "mhm")
    {
        return true;
    }
    if (english_mode_enabled == true && cleaned_text.length() <= 1)
    {
        return true;
    }
    return false;
}

void start_tts(const String &text)
{
    String spoken_text = strip_emotion_markup(text);
    spoken_text.trim();
    if (asr_active == true || tts_active == true)
    {
        Serial.println("TTS 测试未启动：当前设备忙碌");
        return;
    }
    // 串口 t 命令没有经过 ASR 流程，需要在这里标记本轮事务已开始。
    turn_busy = true;
    // 独立 TTS 测试也创建机器人聊天消息，确保屏幕文字与测试语音同步。
    if (chat_speaking_message_index < 0 || chat_history_count == 0 || chat_history[chat_history_count - 1].text != spoken_text)
    {
        set_dialog_reply(spoken_text);
    }
    // 保存模型原文供屏幕显示；只在合成副本中压缩连续英文点号，避免三次停顿。
    tts_plain_text = spoken_text;
    tts_synthesis_text = spoken_text;
    while (tts_synthesis_text.indexOf("...") >= 0)
    {
        tts_synthesis_text.replace("...", "，");
    }
    while (tts_synthesis_text.indexOf("..") >= 0)
    {
        tts_synthesis_text.replace("..", "，");
    }
    // 每次启动时都根据当前情绪重新生成 SSML，避免复用上一轮参数。
    tts_ssml_text = build_ssml_text(tts_synthesis_text);
    // 保留原有 tts_text 为纯文本，确保屏幕逐字同步不把 SSML 标签计算在内。
    tts_text = tts_plain_text;
    if (tts_plain_text.length() == 0)
    {
        Serial.println("TTS 测试未启动：文字为空");
        turn_busy = false;
        return;
    }
    if (ensure_wifi_connected(6000) == false)
    {
        Serial.println("TTS 暂未启动：网络尚未恢复");
        report_state("ERROR");
        draw_network_offline_notice();
        turn_busy = false;
        return;
    }
    // 流式模式只把首句带入启动函数，后续句子由千问 SSE 继续追加。
    bool start_as_streaming_input = tts_streaming_input_requested;
    tts_streaming_input_requested = false;
    reset_tts_text_queue();
    tts_task_ready = false;
    tts_streaming_input_active = start_as_streaming_input;
    tts_text_input_complete = start_as_streaming_input == false;
    if (enqueue_tts_text_sentence(tts_synthesis_text) == false)
    {
        Serial.println("TTS 流式首句入队失败");
        turn_busy = false;
        return;
    }
    size_t reply_character_count = count_utf8_characters(tts_plain_text);
    // 千问流式输入启动时只知道首句长度，后面仍会继续入队，不能误判成短回复。
    if (start_as_streaming_input == false && reply_character_count <= TTS_SHORT_REPLY_CHARACTER_LIMIT)
    {
        tts_start_buffer_target_bytes = TTS_SHORT_REPLY_BUFFER_BYTES;
    }
    else
    {
        tts_start_buffer_target_bytes = TTS_START_BUFFER_BYTES;
    }
    Serial.print("TTS 本轮起播缓冲 ms/字符数: ");
    Serial.print((unsigned long)(tts_start_buffer_target_bytes * 1000UL / (SPEAKER_SAMPLE_RATE * 2UL)));
    Serial.print("/");
    Serial.println((unsigned int)reply_character_count);
    if (tts_audio_queue == NULL || tts_playback_task_handle == NULL)
    {
        Serial.println("TTS 测试未启动：音频播放任务不可用");
        turn_busy = false;
        report_state("ERROR");
        return;
    }
    if (tts_playback_busy == true)
    {
        Serial.println("TTS 测试未启动：上一次音频仍在播放");
        turn_busy = false;
        return;
    }
    clear_tts_audio_queue();
    tts_error_pending = false;
    tts_error_message = "";
    tts_disconnect_expected = false;
    tts_connect_retry_count = 0;
    tts_playback_abort = false;
    tts_playback_failed = false;
    tts_playback_started = false;
    tts_playback_rebuffering = false;
    // 新一轮播放开始前，明确标记 DMA 尚未排空。
    tts_playback_drained = false;
    tts_response_done = false;
    tts_graceful_drain_pending = false;
    tts_final_audio_packet_count = 0;
    // 同一条多句回复只在首句切换一次 I2S。后续句复用功放，避免每句之间
    // 卸载驱动、恢复麦克风又重新安装功放造成可感知的额外停顿。
    if (speaker_ready == false)
    {
        setup_speaker();
    }
    if (speaker_ready == false)
    {
        turn_busy = false;
        report_state("ERROR");
        return;
    }
    tts_socket.onEvent(tts_event);
    // 首次握手失败只重试有限次数；1.2 秒间隔兼顾及时回复和网络恢复时间。
    tts_socket.setReconnectInterval(1200);
    // CosyVoice 使用 Workspace 北京 WebSocket 端点；把鉴权头直接放入
    // 握手头，避免旧版 WebSockets 库 setAuthorization() 的兼容性差异。
    tts_workspace_header = "Origin: https://dashscope.aliyuncs.com\r\n";
    tts_workspace_header += "Authorization: Bearer ";
    tts_workspace_header += XIYUNBAO_DASHSCOPE_API_KEY;
    if (String(XIYUNBAO_WORKSPACE_ID).length() > 0)
    {
        // 仅在两个请求头之间加入换行。末尾不能带 CRLF，因为库会自行追加；
        // 否则会产生空行并提前结束 HTTP 请求头。
        tts_workspace_header += "\r\nX-DashScope-WorkSpace: ";
        tts_workspace_header += XIYUNBAO_WORKSPACE_ID;
    }
    tts_socket.beginSSL(XIYUNBAO_TTS_HOST, 443, XIYUNBAO_TTS_PATH);
    // beginSSL() 完成客户端初始化后再写入完整握手头，确保使用本轮配置。
    tts_socket.setExtraHeaders(tts_workspace_header.c_str());
    Serial.print("TTS 连接目标: wss://");
    Serial.print(XIYUNBAO_TTS_HOST);
    Serial.println(XIYUNBAO_TTS_PATH);
    Serial.print("TTS API Key 已设置，长度: ");
    Serial.println((unsigned int)String(XIYUNBAO_DASHSCOPE_API_KEY).length());
    Serial.print("TTS API Key 指纹: ");
    Serial.println((unsigned long)text_fingerprint(XIYUNBAO_DASHSCOPE_API_KEY), HEX);
    Serial.println("TTS 鉴权方式: EXTRA_HEADERS");
    Serial.print("TTS Workspace: ");
    Serial.println(XIYUNBAO_WORKSPACE_ID);
    tts_active = true;
    tts_finished = false;
    tts_text_sent = false;
    tts_session_finish_sent = false;
    tts_task_ready = false;
    tts_cleanup_done = false;
    // 清零本轮 TTS 音频诊断计数。
    tts_audio_packet_count = 0;
    tts_audio_byte_count = 0;
    tts_queue_underrun_count = 0;
    tts_queue_underrun_ms = 0;
    tts_queue_underrun_started_at = 0;
    tts_queue_minimum_packet_count = TTS_AUDIO_QUEUE_LENGTH;
    tts_playback_packet_count = 0;
    tts_playback_byte_count = 0;
    tts_played_sample_count = 0;
    tts_total_sample_count = 0;
    tts_pcm_peak = 0;
    tts_pcm_near_clip_count = 0;
    tts_request_start_time = millis();
    tts_first_audio_time = 0;
    tts_first_playback_time = 0;
    tts_timeout = millis() + TTS_INACTIVITY_TIMEOUT_MS;
}

uint32_t text_fingerprint(const char *text)
{
    uint32_t fingerprint = 2166136261UL;
    if (text == NULL)
    {
        return fingerprint;
    }
    while (*text != '\0')
    {
        fingerprint = fingerprint ^ (uint8_t)(*text);
        fingerprint = fingerprint * 16777619UL;
        text = text + 1;
    }
    return fingerprint;
}

void tts_event(WStype_t type, uint8_t *payload, size_t length)
{
    if (type != WStype_DISCONNECTED && type != WStype_ERROR)
    {
        tts_timeout = millis() + TTS_INACTIVITY_TIMEOUT_MS;
    }
    if (type == WStype_CONNECTED)
    {
        // CosyVoice 首帧必须先启动 duplex run-task，之后等待 task-started。
        // DashScope task_id 使用固定三十二位十六进制值，不加入可读前缀。
        tts_task_id = create_task_id(NULL);
        String run_task = "{\"header\":{\"action\":\"run-task\",\"task_id\":\"";
        run_task += tts_task_id;
        run_task += "\",\"streaming\":\"duplex\"},\"payload\":{\"task_group\":\"audio\",\"task\":\"tts\",\"function\":\"SpeechSynthesizer\",\"model\":\"";
        run_task += XIYUNBAO_TTS_MODEL;
        run_task += "\",\"parameters\":{\"text_type\":\"";
        if (XIYUNBAO_TTS_ENABLE_SSML == true)
        {
            run_task += "SSML";
        }
        else
        {
            run_task += "PlainText";
        }
        run_task += "\",\"voice\":\"";
        run_task += XIYUNBAO_COSYVOICE_VOICE_ID;
        run_task += "\",\"format\":\"pcm\",\"sample_rate\":24000,\"volume\":50,\"rate\":";
        run_task += String(current_tts_rate, 2);
        run_task += ",\"pitch\":";
        run_task += String(current_tts_pitch, 2);
        run_task += ",\"enable_ssml\":";
        if (XIYUNBAO_TTS_ENABLE_SSML == true)
        {
            run_task += "true";
        }
        else
        {
            run_task += "false";
        }
        run_task += "},\"input\":{}}}";
        tts_send_event(run_task);
        Serial.println("CosyVoice run-task 已发送");
        return;
    }
    if (type == WStype_BIN)
    {
        size_t queued_length = 0;
        if (queue_tts_pcm_copy(payload, length, &queued_length) == false)
        {
            Serial.println("TTS 二进制音频入队失败");
            mark_tts_failure("音频队列已满或内存不足", true);
            return;
        }
        return;
    }
    if (type == WStype_TEXT)
    {
        String message;
        for (size_t i = 0; i < length; i = i + 1)
        {
            message += (char)payload[i];
        }
        // CosyVoice 事件名位于 header.event；兼容少数网关返回的顶层 event/type。
        String event_type = json_string(message, "event");
        if (event_type.length() == 0)
        {
            event_type = json_string(message, "type");
        }
        if (event_type == "task-started")
        {
            tts_connect_retry_count = 0;
            tts_task_ready = true;
            report_state("SPEAKING");
            Serial.println("CosyVoice task-started，开始消费短句队列");
            pump_tts_text_queue();
            return;
        }
        if (event_type == "task-failed" || event_type == "error")
        {
            String error_message = json_string(message, "error_message");
            if (error_message.length() == 0)
            {
                error_message = json_string(message, "message");
            }
            Serial.print("TTS 服务端错误: ");
            Serial.println(error_message);
            mark_tts_failure(
                error_message.length() > 0 ? error_message : "TTS 服务端错误",
                true);
            return;
        }
        if (event_type == "task-finished")
        {
            // task-finished 到来时所有二进制 PCM 已入队，播放任务可以连续消费。
            tts_final_audio_packet_count = tts_audio_packet_count;
            // CosyVoice 返回 16 位单声道 PCM，每两个字节对应一个输入采样。
            tts_total_sample_count = tts_audio_byte_count / 2;
            // 必须最后公布完成标志，避免播放任务先取包却看不到末包编号。
            tts_response_done = true;
            tts_finished = true;
            tts_disconnect_expected = true;
            Serial.println("CosyVoice task-finished，等待播放任务完成句尾收尾");
            // 音频已经全部收到，主动关闭网络连接，不影响播放任务继续消费队列。
            tts_active = false;
            tts_socket.disconnect();
            return;
        }
        // 保留非音频事件，便于确认 CosyVoice 的任务状态变化。
        if (event_type.length() > 0 && event_type != "result-generated")
        {
            Serial.print("TTS 事件: ");
            Serial.println(event_type);
        }
    }
    if (type == WStype_ERROR || type == WStype_DISCONNECTED)
    {
        Serial.print("TTS WebSocket 事件: ");
        Serial.println(type == WStype_ERROR ? "ERROR" : "DISCONNECTED");
        Serial.print("TTS WebSocket 附加字节数: ");
        Serial.println((unsigned int)length);
        if (payload != NULL && length > 0)
        {
            Serial.print("TTS WebSocket 原因: ");
            for (size_t i = 0; i < length; i = i + 1)
            {
                uint8_t value = payload[i];
                if (value >= 32 && value <= 126)
                {
                    Serial.print((char)value);
                }
                else
                {
                    Serial.print(".");
                }
            }
            Serial.println();
        }
        // 尚未建立任务、没有发送文字且没有收到 PCM 时，属于握手阶段瞬断。
        // WebSocketsClient 会自动重连，短句仍完整保留在文本队列中。
        bool can_retry_initial_connection = tts_finished == false &&
                                            tts_error_pending == false &&
                                            tts_disconnect_expected == false &&
                                            tts_task_ready == false &&
                                            tts_text_sent == false &&
                                            tts_audio_packet_count == 0 &&
                                            tts_connect_retry_count < TTS_CONNECT_RETRY_LIMIT;
        if (can_retry_initial_connection == true)
        {
            tts_connect_retry_count = tts_connect_retry_count + 1;
            tts_timeout = millis() + TTS_INACTIVITY_TIMEOUT_MS;
            Serial.print("TTS 首次建连瞬断，保留短句并快速重试: " );
            Serial.print(tts_connect_retry_count);
            Serial.print("/");
            Serial.println(TTS_CONNECT_RETRY_LIMIT);
            return;
        }
        if (tts_finished == false && tts_error_pending == false && tts_disconnect_expected == false)
        {
            Serial.print("TTS 异常断开，音频包数: ");
            Serial.print((unsigned long)tts_audio_packet_count);
            Serial.print(", PCM 字节数: ");
            Serial.println((unsigned long)tts_audio_byte_count);
            mark_tts_failure("TTS WebSocket 异常断开", false);
        }
    }
}

void tts_send_event(const String &event_json)
{
    String mutable_event_json = event_json;
    tts_socket.sendTXT(mutable_event_json);
}

void reset_tts_text_queue(void)
{
    for (unsigned int index = 0; index < TTS_TEXT_QUEUE_LENGTH; index = index + 1)
    {
        tts_text_queue[index] = "";
    }
    tts_text_queue_head = 0;
    tts_text_queue_tail = 0;
    tts_text_queue_count = 0;
    tts_text_sentence_sent_count = 0;
}

bool enqueue_tts_text_sentence(const String &sentence)
{
    String synthesis_sentence = sentence;
    synthesis_sentence.trim();
    while (synthesis_sentence.indexOf("...") >= 0)
    {
        synthesis_sentence.replace("...", "，");
    }
    while (synthesis_sentence.indexOf("..") >= 0)
    {
        synthesis_sentence.replace("..", "，");
    }
    if (synthesis_sentence.length() == 0)
    {
        return true;
    }
    if (tts_text_queue_count >= TTS_TEXT_QUEUE_LENGTH)
    {
        Serial.println("TTS 短句队列已满");
        return false;
    }
    tts_text_queue[tts_text_queue_tail] = synthesis_sentence;
    tts_text_queue_tail = (tts_text_queue_tail + 1) % TTS_TEXT_QUEUE_LENGTH;
    tts_text_queue_count = tts_text_queue_count + 1;
    return true;
}

void pump_tts_text_queue(void)
{
    if (tts_active == false || tts_task_ready == false || tts_session_finish_sent == true)
    {
        return;
    }
    while (tts_text_queue_count > 0)
    {
        String sentence = tts_text_queue[tts_text_queue_head];
        tts_text_queue[tts_text_queue_head] = "";
        tts_text_queue_head = (tts_text_queue_head + 1) % TTS_TEXT_QUEUE_LENGTH;
        tts_text_queue_count = tts_text_queue_count - 1;
        String continue_task = "{\"header\":{\"action\":\"continue-task\",\"task_id\":\"";
        continue_task += tts_task_id;
        continue_task += "\",\"streaming\":\"duplex\"},\"payload\":{\"input\":{\"text\":\"";
        continue_task += json_escape(sentence);
        continue_task += "\"}}}";
        tts_send_event(continue_task);
        tts_text_sent = true;
        tts_text_sentence_sent_count = tts_text_sentence_sent_count + 1;
        Serial.print("TTS 短句已提交: " );
        Serial.println(tts_text_sentence_sent_count);
    }
    if (tts_text_input_complete == true && tts_text_queue_count == 0)
    {
        String finish_task = "{\"header\":{\"action\":\"finish-task\",\"task_id\":\"";
        finish_task += tts_task_id;
        finish_task += "\",\"streaming\":\"duplex\"},\"payload\":{\"input\":{}}}";
        tts_send_event(finish_task);
        tts_session_finish_sent = true;
        Serial.println("TTS finish-task 已发送");
    }
}

void finish_tts_text_input(void)
{
    tts_text_input_complete = true;
    tts_streaming_input_active = false;
    pump_tts_text_queue();
}

void append_streaming_sentence_to_chat(const String &sentence)
{
    String visible_sentence = sentence;
    visible_sentence.trim();
    if (visible_sentence.length() == 0)
    {
        return;
    }
    if (chat_speaking_message_index < 0 ||
        chat_speaking_message_index >= chat_history_count ||
        chat_history[chat_speaking_message_index].from_user == true)
    {
        // 首句先建立消息，随后按实际 PCM 播放进度逐字显示。
        set_dialog_reply_visible(visible_sentence, 0);
        return;
    }
    String &full_reply = chat_history[chat_speaking_message_index].text;
    // 后续句只写入完整回复缓存，不扩大可见范围，也不提前刷新屏幕。
    full_reply += visible_sentence;
    chat_scroll_lines = 0;
}

void reset_qwen_stream_pipeline(void)
{
    qwen_stream_emotion_ready = false;
    qwen_reply_streamed_to_tts = false;
    qwen_stream_label_buffer = "";
    qwen_stream_sentence_buffer = "";
    qwen_stream_plain_reply = "";
    // 新一轮流式回复必须创建新的机器人消息，不能追加到上一轮消息。
    chat_speaking_message_index = -1;
    chat_speaking_visible_bytes = 0;
}

size_t complete_sentence_end_bytes(const String &text)
{
    size_t read_index = 0;
    while (read_index < text.length())
    {
        size_t character_bytes = utf8_character_length(text, read_index);
        if (character_bytes == 0 || read_index + character_bytes > text.length())
        {
            break;
        }
        String one_character = text.substring(read_index, read_index + character_bytes);
        size_t character_end = read_index + character_bytes;
        if (is_sentence_ending_character(one_character) == true || one_character == "\n")
        {
            return character_end;
        }
        if (one_character == ".")
        {
            size_t dot_end = character_end;
            while (dot_end < text.length() && text[dot_end] == '.')
            {
                dot_end = dot_end + 1;
            }
            // 两个及以上连续点号是省略号，只作为句内停顿，不能切成多个独立短句。
            if (dot_end - read_index >= 2)
            {
                read_index = dot_end;
                continue;
            }
            if (dot_end < text.length())
            {
                return dot_end;
            }
            return 0;
        }
        read_index = character_end;
    }
    return 0;
}

void consume_qwen_stream_chunk(const String &chunk_text)
{
    if (chunk_text.length() == 0)
    {
        return;
    }
    String body_chunk = chunk_text;
    if (qwen_stream_emotion_ready == false)
    {
        qwen_stream_label_buffer += body_chunk;
        int label_end = qwen_stream_label_buffer.indexOf("】");
        if (label_end < 0 && qwen_stream_label_buffer.length() < 64)
        {
            return;
        }
        String buffered = qwen_stream_label_buffer;
        buffered.trim();
        int normalized_label_end = buffered.indexOf("】");
        if (buffered.startsWith("【emotion:") == true && normalized_label_end > 0)
        {
            String candidate = buffered.substring(String("【emotion:").length(), normalized_label_end);
            candidate.trim();
            current_emotion = is_valid_emotion(candidate) ? candidate : "neutral";
            body_chunk = buffered.substring(normalized_label_end + String("】").length());
        }
        else
        {
            current_emotion = "neutral";
            body_chunk = buffered;
        }
        qwen_stream_label_buffer = "";
        qwen_stream_emotion_ready = true;
        set_tts_parameters_for_emotion(current_emotion);
    }
    if (body_chunk.length() == 0)
    {
        return;
    }
    qwen_stream_plain_reply += body_chunk;
    qwen_stream_sentence_buffer += body_chunk;
    while (true)
    {
        size_t sentence_end = complete_sentence_end_bytes(qwen_stream_sentence_buffer);
        if (sentence_end == 0)
        {
            break;
        }
        String sentence = qwen_stream_sentence_buffer.substring(0, sentence_end);
        qwen_stream_sentence_buffer = qwen_stream_sentence_buffer.substring(sentence_end);
        sentence.trim();
        if (sentence.length() == 0)
        {
            continue;
        }
        append_streaming_sentence_to_chat(sentence);
        if (qwen_reply_streamed_to_tts == false)
        {
            tts_streaming_input_requested = true;
            start_tts(sentence);
            if (tts_active == true)
            {
                qwen_reply_streamed_to_tts = true;
            }
        }
        else if (enqueue_tts_text_sentence(sentence) == true)
        {
            pump_tts_text_queue();
        }
    }
}

void flush_qwen_stream_sentence_buffer(void)
{
    if (qwen_stream_emotion_ready == false && qwen_stream_label_buffer.length() > 0)
    {
        String buffered = qwen_stream_label_buffer;
        qwen_stream_label_buffer = "";
        buffered.trim();
        current_emotion = "neutral";
        qwen_stream_emotion_ready = true;
        set_tts_parameters_for_emotion(current_emotion);
        qwen_stream_plain_reply += buffered;
        qwen_stream_sentence_buffer += buffered;
    }
    String sentence = qwen_stream_sentence_buffer;
    qwen_stream_sentence_buffer = "";
    sentence.trim();
    if (sentence.length() > 0)
    {
        append_streaming_sentence_to_chat(sentence);
        if (qwen_reply_streamed_to_tts == false)
        {
            tts_streaming_input_requested = true;
            start_tts(sentence);
            if (tts_active == true)
            {
                qwen_reply_streamed_to_tts = true;
            }
        }
        else if (enqueue_tts_text_sentence(sentence) == true)
        {
            pump_tts_text_queue();
        }
    }
    if (qwen_reply_streamed_to_tts == true)
    {
        finish_tts_text_input();
    }
}

bool enqueue_tts_pcm_owned(uint8_t *pcm_data, size_t pcm_length, size_t *queued_length)
{
    if (queued_length != NULL)
    {
        *queued_length = 0;
    }
    if (pcm_data == NULL || pcm_length == 0 || tts_audio_queue == NULL)
    {
        if (pcm_data != NULL)
        {
            heap_caps_free(pcm_data);
        }
        return false;
    }
    TtsAudioChunk chunk;
    chunk.data = pcm_data;
    chunk.length = pcm_length;
    chunk.packet_number = tts_audio_packet_count + 1;
    if (xQueueSend(tts_audio_queue, &chunk, 0) != pdPASS)
    {
        heap_caps_free(pcm_data);
        return false;
    }
    tts_audio_packet_count = tts_audio_packet_count + 1;
    tts_audio_byte_count = tts_audio_byte_count + pcm_length;
    if (tts_first_audio_time == 0)
    {
        tts_first_audio_time = millis();
        Serial.print("TTS 首个 PCM 到达延迟 ms: ");
        Serial.println(tts_first_audio_time - tts_request_start_time);
    }
    tts_timeout = millis() + TTS_INACTIVITY_TIMEOUT_MS;
    if (queued_length != NULL)
    {
        *queued_length = pcm_length;
    }
    return true;
}

bool queue_tts_pcm_copy(const uint8_t *pcm_data, size_t pcm_length, size_t *queued_length)
{
    if (queued_length != NULL)
    {
        *queued_length = 0;
    }
    if (pcm_data == NULL || pcm_length == 0)
    {
        return false;
    }
    uint8_t *copy = (uint8_t *)heap_caps_malloc(pcm_length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (copy == NULL)
    {
        copy = (uint8_t *)heap_caps_malloc(pcm_length, MALLOC_CAP_8BIT);
    }
    if (copy == NULL)
    {
        return false;
    }
    memcpy(copy, pcm_data, pcm_length);
    return enqueue_tts_pcm_owned(copy, pcm_length, queued_length);
}

bool decode_and_queue_tts_audio(const uint8_t *base64_audio, size_t base64_length, size_t *decoded_length)
{
    if (decoded_length != NULL)
    {
        *decoded_length = 0;
    }
    if (base64_audio == NULL || base64_length == 0)
    {
        return false;
    }
    size_t required_length = 0;
    int size_result = mbedtls_base64_decode(
        NULL,
        0,
        &required_length,
        base64_audio,
        base64_length);
    if (size_result != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || required_length == 0)
    {
        Serial.print("TTS Base64 长度计算失败，错误码: ");
        Serial.println(size_result);
        return false;
    }
    uint8_t *pcm_data = (uint8_t *)heap_caps_malloc(required_length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm_data == NULL)
    {
        pcm_data = (uint8_t *)heap_caps_malloc(required_length, MALLOC_CAP_8BIT);
    }
    if (pcm_data == NULL)
    {
        Serial.print("TTS PCM 内存不足，需要字节: ");
        Serial.println((unsigned int)required_length);
        return false;
    }
    size_t output_length = 0;
    int result = mbedtls_base64_decode(
        pcm_data,
        required_length,
        &output_length,
        base64_audio,
        base64_length);
    if (result != 0 || output_length == 0)
    {
        heap_caps_free(pcm_data);
        Serial.print("TTS Base64 解码错误码: ");
        Serial.println(result);
        return false;
    }
    if (enqueue_tts_pcm_owned(pcm_data, output_length, decoded_length) == false)
    {
        return false;
    }
    return true;
}

void tts_playback_task(void *parameter)
{
    (void)parameter;
    for (;;)
    {
        if (tts_playback_abort == true)
        {
            tts_playback_busy = false;
            tts_playback_started = false;
            if (tts_audio_queue != NULL)
            {
                TtsAudioChunk dropped;
                if (xQueueReceive(tts_audio_queue, &dropped, 0) == pdPASS && dropped.data != NULL)
                {
                    heap_caps_free(dropped.data);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (tts_audio_queue == NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        // 达到本轮自适应安全水位便边接收边播放；短回复在 task-finished 后立即播放。
        unsigned long buffered_bytes = tts_audio_byte_count >= tts_playback_byte_count
                                           ? tts_audio_byte_count - tts_playback_byte_count
                                           : 0;
        // 一旦确认断粮，不在每个新包到达时立刻播一点；积累到恢复水位后再连续输出。
        // 若服务端已经生成完成，则不再等待水位，直接把剩余音频完整排空。
        if (tts_playback_rebuffering == true)
        {
            if (tts_response_done == false &&
                buffered_bytes < TTS_REBUFFER_BYTES &&
                tts_playback_abort == false)
            {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            unsigned long now = millis();
            if (tts_queue_underrun_started_at > 0)
            {
                tts_queue_underrun_ms = tts_queue_underrun_ms +
                                         (now - tts_queue_underrun_started_at);
            }
            tts_queue_underrun_started_at = 0;
            tts_playback_rebuffering = false;
            Serial.print("TTS 断粮后恢复播放，缓冲字节/包数: ");
            Serial.print(buffered_bytes);
            Serial.print("/");
            Serial.println((unsigned long)uxQueueMessagesWaiting(tts_audio_queue));
        }
        if (tts_playback_started == false &&
            tts_response_done == false &&
            buffered_bytes < tts_start_buffer_target_bytes &&
            (tts_error_pending == false || tts_graceful_drain_pending == true) &&
            tts_playback_abort == false)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        TtsAudioChunk chunk;
        // 等待队列数据期间不能标记为正在写 I2S，否则主循环无法执行 TTS 收尾。
        // I2S DMA 约能覆盖 85 ms；80 ms 内等到新包属于正常抖动，不算真实断粮。
        BaseType_t receive_result = xQueueReceive(tts_audio_queue, &chunk, pdMS_TO_TICKS(80));
        if (receive_result != pdPASS)
        {
            // DMA 储备已经接近耗尽且服务端尚未完成，进入一次再缓冲，避免反复短停顿。
            if (tts_playback_started == true &&
                tts_response_done == false &&
                tts_playback_abort == false &&
                tts_playback_rebuffering == false)
            {
                tts_queue_underrun_count = tts_queue_underrun_count + 1;
                tts_queue_underrun_started_at = millis();
                tts_playback_rebuffering = true;
            }
            // 最后一包可能在 task-finished 到达前已经播放；完成事件到达后统一收尾。
            if (tts_response_done == true &&
                tts_final_audio_packet_count > 0 &&
                uxQueueMessagesWaiting(tts_audio_queue) == 0 &&
                tts_playback_packet_count == tts_final_audio_packet_count &&
                tts_playback_drained == false)
            {
                tts_playback_busy = true;
                finish_tts_playback_output();
                tts_playback_busy = false;
            }
            // 服务端已完成且队列为空时，允许无音频回复正常收尾。
            if (tts_finished == true && tts_response_done == true &&
                uxQueueMessagesWaiting(tts_audio_queue) == 0 &&
                tts_audio_packet_count == 0)
            {
                tts_playback_drained = true;
            }
            continue;
        }
        if (tts_playback_abort == true || speaker_ready == false)
        {
            heap_caps_free(chunk.data);
            continue;
        }
        // 只有真正开始向 I2S 写 PCM 时才设置忙标记。
        tts_playback_busy = true;
        // 首次取包时已具备安全缓冲，可以开始低延迟连续播放。
        if (tts_playback_started == false)
        {
            tts_first_playback_time = millis();
            Serial.print("TTS 安全缓冲后开始连续播放，缓冲字节/包数: ");
            Serial.print(buffered_bytes);
            Serial.print("/");
            Serial.print((unsigned long)uxQueueMessagesWaiting(tts_audio_queue) + 1);
            Serial.print(", 请求到首声 ms: ");
            Serial.println(tts_first_playback_time - tts_request_start_time);
        }
        tts_playback_started = true;
        // 记录当前包取走后的队列余量；数值接近零代表后半句有断粮风险。
        unsigned long remaining_packet_count = (unsigned long)uxQueueMessagesWaiting(tts_audio_queue);
        if (remaining_packet_count < tts_queue_minimum_packet_count)
        {
            tts_queue_minimum_packet_count = remaining_packet_count;
        }
        size_t written = 0;
        bool last_chunk = tts_response_done == true &&
                          tts_final_audio_packet_count > 0 &&
                          chunk.packet_number == tts_final_audio_packet_count;
        esp_err_t write_result = write_speaker_pcm(chunk.data, chunk.length, &written, last_chunk);
        if (write_result == ESP_OK)
        {
            tts_playback_packet_count = tts_playback_packet_count + 1;
            tts_playback_byte_count = tts_playback_byte_count + chunk.length;
        }
        if (write_result != ESP_OK)
        {
            tts_playback_failed = true;
            tts_playback_abort = true;
        }
        if (write_result == ESP_OK && last_chunk)
        {
            finish_tts_playback_output();
        }
        heap_caps_free(chunk.data);
        tts_playback_busy = false;
    }
}

void finish_tts_playback_output(void)
{
    if (tts_playback_drained == true)
    {
        return;
    }
    // 没有收到并播放任何 PCM 时无需排空 DMA，更不能向麦克风 RX 模式写 I2S。
    if (tts_playback_started == false || tts_playback_packet_count == 0 ||
        speaker_ready == false || i2s_driver_ready == false)
    {
        tts_playback_drained = true;
        return;
    }
    // 尾部静音和 DMA 排空只执行一次，而且只发生在整条回复末尾。
    const size_t silence_bytes = (SPEAKER_SAMPLE_RATE * TTS_TAIL_SILENCE_MS / 1000) * 4;
    memset(speaker_pcm_buffer, 0, silence_bytes);
    size_t silence_written = 0;
    esp_err_t silence_result = i2s_write(
        I2S_NUM_0, speaker_pcm_buffer, silence_bytes, &silence_written, pdMS_TO_TICKS(100));
    if (silence_result != ESP_OK || silence_written != silence_bytes)
    {
        Serial.print("TTS 末尾静音写入异常，错误码/字节: ");
        Serial.print((int)silence_result);
        Serial.print("/");
        Serial.println((unsigned int)silence_written);
    }
    vTaskDelay(pdMS_TO_TICKS(TTS_DMA_DRAIN_WAIT_MS));
    i2s_zero_dma_buffer(I2S_NUM_0);
    tts_playback_drained = true;
    Serial.print("TTS PCM 峰值: ");
    Serial.print((unsigned long)tts_pcm_peak);
    Serial.print(", 接近满幅采样数: ");
    Serial.print((unsigned long)tts_pcm_near_clip_count);
    Serial.print(", 数字音量: ");
    Serial.print(speaker_muted == true ? 0 : (int)speaker_volume_percent);
    Serial.print("%, 断粮重缓冲次数: ");
    Serial.print((unsigned long)tts_queue_underrun_count);
    Serial.print(", 累计重缓冲 ms: ");
    Serial.print((unsigned long)tts_queue_underrun_ms);
    Serial.print(", 最低队列包数: ");
    Serial.print((unsigned long)tts_queue_minimum_packet_count);
    Serial.print(", 收到/播放包数: ");
    Serial.print((unsigned long)tts_audio_packet_count);
    Serial.print("/");
    Serial.print((unsigned long)tts_playback_packet_count);
    Serial.print(", 收到/播放 PCM 字节: ");
    Serial.print((unsigned long)tts_audio_byte_count);
    Serial.print("/");
    Serial.println((unsigned long)tts_playback_byte_count);
    if (tts_playback_packet_count != tts_audio_packet_count ||
        tts_playback_byte_count != tts_audio_byte_count)
    {
        Serial.println("TTS 完整性警告：收到与播放的 PCM 数据不一致");
    }
}

void clear_tts_audio_queue(void)
{
    if (tts_audio_queue == NULL)
    {
        return;
    }
    TtsAudioChunk chunk;
    while (xQueueReceive(tts_audio_queue, &chunk, 0) == pdPASS)
    {
        if (chunk.data != NULL)
        {
            heap_caps_free(chunk.data);
        }
    }
}

void mark_tts_failure(const String &message, bool disconnect_socket)
{
    if (tts_cleanup_done == true)
    {
        return;
    }
    if (tts_error_pending == false)
    {
        tts_error_message = message;
        Serial.print("TTS 失败: ");
        Serial.println(tts_error_message);
    }
    // WebSocket 提前断开或超时但已经收到 PCM 时，不再清空队列硬切喇叭；
    // 先把已收到的音频自然播完，再进入错误恢复状态。I2S 写入失败除外。
    bool can_drain_received_audio = message.indexOf("I2S") < 0 &&
                                    tts_audio_packet_count > 0;
    if (can_drain_received_audio == true)
    {
        tts_error_pending = true;
        tts_graceful_drain_pending = true;
        tts_finished = true;
        tts_response_done = true;
        tts_final_audio_packet_count = tts_audio_packet_count;
        tts_total_sample_count = tts_audio_byte_count / 2;
        tts_playback_abort = false;
        tts_disconnect_expected = true;
        tts_active = false;
        Serial.print("TTS 将先播放已收到 PCM，包数: " );
        Serial.println((unsigned long)tts_audio_packet_count - tts_playback_packet_count);
    }
    else
    {
        tts_error_pending = true;
        tts_graceful_drain_pending = false;
        tts_finished = true;
        tts_response_done = true;
        tts_playback_abort = true;
        tts_disconnect_expected = true;
        clear_tts_audio_queue();
        tts_active = false;
    }
    if (disconnect_socket == true)
    {
        tts_socket.disconnect();
    }
}

void finalize_tts_if_ready(void)
{
    if (tts_cleanup_done == true || (tts_finished == false && tts_error_pending == false))
    {
        return;
    }
    if (tts_playback_busy == true)
    {
        return;
    }
    // 正常播放或可排空的部分音频都必须等播放任务确认 DMA 已经排空。
    if ((tts_error_pending == false || tts_graceful_drain_pending == true) &&
        tts_playback_drained == false)
    {
        return;
    }
    if (tts_audio_queue != NULL && uxQueueMessagesWaiting(tts_audio_queue) > 0)
    {
        return;
    }
    tts_cleanup_done = true;
    tts_active = false;
    // 播放和 DMA 收尾已经结束；此后才允许连续会话的静音超时生效。
    tts_playback_started = false;
    if (tts_error_pending == true && tts_graceful_drain_pending == false)
    {
        // 错误后不再继续逐句播放，立即恢复麦克风。
        stop_i2s();
        setup_microphone();
        turn_busy = false;
        // 当前句失败后终止后续句子，避免错误状态下继续创建 TTS 任务。
        tts_sentence_sequence_active = false;
        tts_pending_reply = "";
        if (english_announce_active == true && english_mode_enabled == true)
        {
            english_announce_active = false;
            Serial.println("英语开场白失败，仍进入英语聆听");
            request_voice_turn("ENGLISH_READY_AFTER_TTS_ERROR");
            return;
        }
        // TTS 失败时仍然把已经生成的完整回复显示到聊天框，避免屏幕停在空白机器人行。
        if (chat_speaking_message_index >= 0 && chat_speaking_message_index < chat_history_count)
        {
            chat_speaking_visible_bytes = chat_history[chat_speaking_message_index].text.length();
            chat_last_sync_refresh_time = millis();
            redraw_chat_history();
        }
        // 单轮 TTS 失败不退出连续会话，避免用户必须再次说唤醒词。
        report_state("ERROR");
        draw_status("合成失败", "稍后继续聆听");
        Serial.println("TTS 失败收尾完成，连续会话保留");
        if (continuous_dialog_active == true)
        {
            // 清除本轮错误后延迟重新启动 ASR。
            continuous_dialog_restart_time = millis() + CONTINUOUS_DIALOG_RESTART_DELAY_MS;
            continuous_dialog_restart_pending = true;
        }
        else
        {
            // 非连续模式才恢复到普通待机。
            draw_status("小云宝", "请唤醒我开始交流");
        }
    }
    else
    {
        if (tts_graceful_drain_pending == true)
        {
            Serial.println("TTS 连接提前结束，已收到音频已完整播放，准备恢复聆听");
                report_state("ERROR");
            draw_status("语音连接中断", "已播放已收到内容");
        }
        // 整条回复已在同一次 TTS 流中连续播放完成，不再为后续句子重新建连。
        tts_sentence_sequence_active = false;
        tts_pending_reply = "";
        tts_graceful_drain_pending = false;
        // 只有整条回复全部播放结束后才切回麦克风，减少句间等待。
        stop_i2s();
        setup_microphone();
        turn_busy = false;
            report_state("IDLE");
        if (english_announce_active == true && english_mode_enabled == true)
        {
            english_announce_active = false;
            Serial.println("英语开场白播放完成，开始英语聆听");
            request_voice_turn("ENGLISH_READY");
            return;
        }
        if (continuous_dialog_active == true)
        {
            continuous_dialog_last_voice_time = millis();
            if (chat_speaking_message_index >= 0 && chat_speaking_message_index < chat_history_count)
            {
                chat_speaking_visible_bytes = chat_history[chat_speaking_message_index].text.length();
            }
            Serial.println("回复结束，立即打开麦克风");
            start_voice_turn();
        }
        else
        {
            draw_status("小云宝", "等待语音输入");
        }
        Serial.println("TTS 完成并播放队列清空");
    }
}
