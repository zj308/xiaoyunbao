// Computer relay and ASR communication
// Shared state and declarations are kept in the main sketch.

void report_state(const char *state)
{
    // 状态只用于 ESP32 本地调试和屏幕流程，不再发送给外部 MCU。
    Serial.print("ESP32 状态: ");
    Serial.println(state);
}

void abort_current_listen(const char *reason)
{
    if (asr_active == true || recording == true || relay_listen_start_pending == true)
    {
        if (relay_connected == true)
        {
            relay_send_json("{\"type\":\"listen.stop\"}");
        }
    }
    asr_active = false;
    recording = false;
    asr_started = false;
    asr_finish_sent = false;
    asr_result_ready = false;
    asr_preview_dirty = false;
    asr_sentence_end_pending = false;
    relay_listen_start_pending = false;
    ui_listening_indicator_active = false;
    listen_commit_pending = false;
    listen_start_sent_time = 0;
    listen_start_retry_count = 0;
    Serial.print("已中止当前聆听: ");
    Serial.println(reason != NULL ? reason : "UNKNOWN");
}

void commit_current_listen(void)
{
    if (asr_active == false)
    {
        return;
    }
    listen_commit_pending = true;
    if (relay_connected == true && asr_finish_sent == false)
    {
        relay_send_json("{\"type\":\"listen.stop\"}");
        asr_finish_sent = true;
    }
    recording = false;
    ui_listening_indicator_active = false;
    asr_sentence_end_pending = false;
    listen_start_sent_time = 0;
    listen_start_retry_count = 3;
    stop_listen_animation();
    report_state("THINKING");
    Serial.println("K1 结束收音，有语音则回答，没有则退出");
}

void barge_in_current_turn(void)
{
    Serial.println("K1 打断当前对话");
    ignore_incoming_reply = true;
    continuous_dialog_restart_pending = false;
    english_announce_active = false;
    abort_current_listen("K1_INTERRUPT");
    tts_playback_abort = true;
    tts_graceful_drain_pending = false;
    tts_error_pending = false;
    tts_finished = true;
    tts_response_done = true;
    tts_disconnect_expected = true;
    tts_sentence_sequence_active = false;
    tts_pending_reply = "";
    tts_active = false;
    tts_playback_started = false;
    tts_playback_drained = true;
    tts_cleanup_done = true;
    turn_busy = false;
    relay_audio_started = false;
    reset_relay_opus_decoder();
    clear_tts_audio_queue();
    stop_i2s();
    setup_microphone();
    request_voice_turn_forced("K1_INTERRUPT");
}

void request_voice_turn_forced(const char *source)
{
    last_voice_trigger_time = 0;
    request_voice_turn(source);
}

void request_voice_turn(const char *source)
{
    if (wifi_provision_active == true)
    {
        Serial.println("配网中，忽略语音唤醒");
        return;
    }
    // 读取当前系统时间，用于实体按键和 USB 调试命令防抖判断。
    unsigned long current_time = millis();
    // 两次触发间隔过短时，认为是按键抖动或重复触发。
    if (last_voice_trigger_time != 0 && current_time - last_voice_trigger_time < VOICE_TRIGGER_DEBOUNCE_MS)
    {
        Serial.println("忽略重复语音命令，仍在防抖时间内");
        return;
    }
    // 记录本次真正接受的触发时间。
    last_voice_trigger_time = current_time;
    voice_turn_pending = true;
    voice_turn_pending_source = source != NULL ? source : "UNKNOWN";
    // 按键或 USB 调试命令触发时直接启动云端 ASR；待机阶段没有本地语音模型占用 I2S。
    Serial.print("语音触发来源: ");
    Serial.println(source);
    note_user_activity();
    // 用户主动触发一次后进入连续会话；之后每轮无需再次按键。
    if (CONTINUOUS_DIALOG_ENABLED == true)
    {
        continuous_dialog_active = true;
        continuous_dialog_last_voice_time = millis();
        continuous_dialog_restart_pending = false;
        Serial.println("连续对话已开启；按唤醒键或输入 x 可退出");
    }
    // Start immediately when possible; otherwise the main loop retries without blocking.
    start_voice_turn();
}

void service_pending_voice_turn(void)
{
    if (voice_turn_pending == false || wifi_provision_active == true)
    {
        return;
    }
    if (turn_busy == true || asr_active == true || tts_active == true ||
        english_announce_active == true || language_switch_pending == true)
    {
        return;
    }
    if (device_wifi_connected() == false)
    {
        ensure_wifi_connected(0);
        return;
    }
    start_voice_turn();
}

void stop_continuous_dialog(const char *reason)
{
    // 清除会话标记，使本轮结束后不再自动连接 ASR。
    continuous_dialog_active = false;
    // 清除待机唤醒标记，允许主循环重新建立唤醒监听。
    wake_listening_active = false;
    ui_listening_indicator_active = false;
    // 清除已经排队的自动续听任务。
    continuous_dialog_restart_pending = false;
    continuous_dialog_last_voice_time = 0;
    voice_turn_pending = false;
    voice_turn_pending_source = "";
    wake_prompt_printed = false;
    // 按键模式下退出连续会话后保持待机，不重新启动任何本地语音模型。
    local_wake_restart_pending = false;
    // 打印退出来源，便于区分用户停止、没听清和网络错误。
    Serial.print("连续对话已退出，原因: " );
    if (reason != NULL)
    {
        Serial.println(reason);
    }
    else
    {
        Serial.println("UNKNOWN");
    }
}

static void mic_preroll_reset(void);
static void mic_preroll_flush(void);

void start_voice_turn(void)
{
    if (turn_busy == true || asr_active == true || tts_active == true)
    {
        Serial.println("上一轮语音尚未结束");
        return;
    }
    if (device_wifi_connected() == false)
    {
        voice_turn_pending = true;
        if (voice_turn_pending_source.length() == 0)
        {
            voice_turn_pending_source = "AUTO_RETRY";
        }
        ensure_wifi_connected(0);
        Serial.println("网络尚未恢复，已排队等待 ASR");
        draw_network_offline_notice();
        return;
    }
    voice_turn_pending = false;
    voice_turn_pending_source = "";
    // A new listening turn is allowed to accept its own reply after a barge-in
    // or language switch discarded the previous audio stream.
    ignore_incoming_reply = false;
    final_text = "";
    // 每轮录音重新估计直流偏置和环境噪声，避免上一轮的大声说话抬高噪声门。
    microphone_dc_estimate = 0;
    microphone_noise_floor = 0;
    microphone_gate_hold_samples = 0;
    wake_listening_active = false;
    ui_listening_indicator_active = true;
    ui_listening_has_speech = false;
    ui_listening_dots_phase = 0;
    ui_listening_dots_next_time = millis();
    tts_playback_drained = false;
    chat_last_render_signature = "";
    // 电脑中转模式不会进入旧版千问 SSE 的 reset_qwen_stream_pipeline()。
    // 新一轮聆听必须在这里结束上一轮的文字同步状态，否则下一轮首句会被追加到旧回复。
    chat_speaking_message_index = -1;
    chat_speaking_visible_bytes = 0;
    chat_last_sync_refresh_time = 0;
    last_asr_printed_text = "";
    asr_preview_dirty = false;
    asr_sentence_end_pending = false;
    asr_result_ready = false;
    asr_started = false;
    asr_finish_sent = false;
    asr_task_finished = false;
    asr_disconnect_expected = false;
    asr_active = true;
    listen_commit_pending = false;
    setup_microphone();
    mic_preroll_reset();
    recording = true;
    record_hard_stop_time = millis() + (english_mode_enabled == true ? RECORD_TIME_EN_MS : RECORD_TIME_MS);
    record_stop_time = record_hard_stop_time;
    asr_timeout = millis() + 25000;
    relay_listen_start_pending = true;
    listen_start_sent_time = millis();
    listen_start_retry_count = 0;
    if (english_mode_enabled == true)
    {
        draw_english_listen_prompt();
    }
    else
    {
        stop_listen_animation();
        draw_companion_status_text("正在倾听");
        start_listen_animation();
    }
    if (relay_connected == true)
    {
        relay_send_json(english_mode_enabled == true
            ? "{\"type\":\"listen.start\",\"language\":\"en\"}"
            : "{\"type\":\"listen.start\",\"language\":\"zh\"}");
        relay_listen_start_pending = false;
    }
    else if (relay_connecting == false)
    {
        asr_socket.onEvent(handle_relay_event);
        asr_socket.setReconnectInterval(2000);
        // 重新建立连接时沿用同一组心跳参数，避免空闲连接被路由器回收。
        asr_socket.enableHeartbeat(15000, 8000, 2);
        asr_socket.begin(XIYUNBAO_RELAY_HOST, XIYUNBAO_RELAY_PORT, XIYUNBAO_RELAY_PATH);
        relay_connecting = true;
        Serial.print("正在连接电脑中转: ws://");
        Serial.print(XIYUNBAO_RELAY_HOST);
        Serial.print(":");
        Serial.print(XIYUNBAO_RELAY_PORT);
        Serial.println(XIYUNBAO_RELAY_PATH);
    }
}

static const size_t MIC_PREROLL_BYTES = 16000 * 2 * 2;
static uint8_t *mic_preroll_buffer = NULL;
static size_t mic_preroll_used = 0;

static void mic_preroll_reset(void)
{
    mic_preroll_used = 0;
}

static void mic_preroll_append(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0)
    {
        return;
    }
    if (mic_preroll_buffer == NULL)
    {
        mic_preroll_buffer = (uint8_t *)heap_caps_malloc(MIC_PREROLL_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (mic_preroll_buffer == NULL)
        {
            mic_preroll_buffer = (uint8_t *)malloc(MIC_PREROLL_BYTES);
        }
    }
    if (mic_preroll_buffer == NULL)
    {
        return;
    }
    if (length >= MIC_PREROLL_BYTES)
    {
        memcpy(mic_preroll_buffer, data + (length - MIC_PREROLL_BYTES), MIC_PREROLL_BYTES);
        mic_preroll_used = MIC_PREROLL_BYTES;
        return;
    }
    if (mic_preroll_used + length > MIC_PREROLL_BYTES)
    {
        size_t drop = mic_preroll_used + length - MIC_PREROLL_BYTES;
        memmove(mic_preroll_buffer, mic_preroll_buffer + drop, mic_preroll_used - drop);
        mic_preroll_used = mic_preroll_used - drop;
    }
    memcpy(mic_preroll_buffer + mic_preroll_used, data, length);
    mic_preroll_used = mic_preroll_used + length;
}

static void mic_preroll_flush(void)
{
    if (mic_preroll_buffer == NULL || mic_preroll_used == 0 || relay_connected == false)
    {
        mic_preroll_used = 0;
        return;
    }
    size_t offset = 0;
    while (offset < mic_preroll_used)
    {
        size_t chunk = mic_preroll_used - offset;
        if (chunk > sizeof(microphone_buffer))
        {
            chunk = sizeof(microphone_buffer);
        }
        asr_socket.sendBIN(mic_preroll_buffer + offset, chunk);
        offset = offset + chunk;
    }
    microphone_bytes_sent = microphone_bytes_sent + mic_preroll_used;
    Serial.print("已补发开口前的音频，字节数: ");
    Serial.println((unsigned int)mic_preroll_used);
    mic_preroll_used = 0;
}

void send_microphone_audio(void)
{
    if (microphone_ready == false || asr_active == false)
    {
        return;
    }
    // 记录原始 32 位采样实际读取的字节数。
    size_t raw_bytes_read = 0;
    // 从 I2S 驱动读取一批 INMP441 原始音频。
    esp_err_t read_result = i2s_read(I2S_NUM_0, microphone_raw_buffer, sizeof(microphone_raw_buffer), &raw_bytes_read, 100);
    // 只有读取成功且至少得到一个完整采样时才继续处理。
    if (read_result == ESP_OK && raw_bytes_read >= sizeof(int32_t))
    {
        // 计算当前批次包含的 32 位数据槽数量。
        size_t slot_count = raw_bytes_read / sizeof(int32_t);
        // 计算当前批次包含的左右声道采样帧数量。
        size_t frame_count = slot_count / 2;
        // 计算转换后 16 位单声道 PCM 的字节数量。
        size_t pcm_bytes = frame_count * sizeof(int16_t);
        // 逐帧读取左右声道，并自动选择音量较大的有效声道。
        for (size_t frame_index = 0; frame_index < frame_count; frame_index = frame_index + 1)
        {
            // 取出当前帧的左声道原始采样。
            int32_t left_sample = microphone_raw_buffer[frame_index * 2];
            // 取出当前帧的右声道原始采样。
            int32_t right_sample = microphone_raw_buffer[frame_index * 2 + 1];
            // 计算左声道原始采样的绝对值。
            int32_t left_raw_absolute = left_sample < 0 ? -left_sample : left_sample;
            // 计算右声道原始采样的绝对值。
            int32_t right_raw_absolute = right_sample < 0 ? -right_sample : right_sample;
            // 保存原始 I2S 数据中的最大绝对值。
            if (left_raw_absolute > microphone_raw_max_volume)
            {
                microphone_raw_max_volume = left_raw_absolute;
            }
            if (right_raw_absolute > microphone_raw_max_volume)
            {
                microphone_raw_max_volume = right_raw_absolute;
            }
            // 只打印一次首帧左右声道原始值，避免串口被音频循环刷屏。
            if (microphone_first_frame_printed == false && frame_index == 0)
            {
                Serial.print("INMP441 首帧左原始值: " );
                Serial.println(left_sample);
                Serial.print("INMP441 首帧右原始值: " );
                Serial.println(right_sample);
                microphone_first_frame_printed = true;
            }
            // 计算左声道原始采样的绝对值。
            int32_t left_absolute = left_sample < 0 ? -left_sample : left_sample;
            // 计算右声道原始采样的绝对值。
            int32_t right_absolute = right_sample < 0 ? -right_sample : right_sample;
            // INMP441 的 L/R 脚决定有效声道；固定读取该声道，避免用环境噪声较大的
            // 声道在左右之间跳换。若硬件把 L/R 接到 3V3，可将宏改为 true。
            int32_t raw_sample = MIC_USE_RIGHT_CHANNEL ? right_sample : left_sample;
            // INMP441 的有效音频位位于高位，右移后保留可用语音幅度。
            // 将 INMP441 高位中的有效采样缩放到 16 位 PCM。
            // 先取高 16 位，再适度放大轻声信号；使用 64 位中间值避免乘法溢出。
            // 一阶直流跟踪相当于低频高通，去掉 INMP441 的静态偏置和低频风噪。
            int32_t raw_pcm_sample = raw_sample >> 16;
            microphone_dc_estimate += (raw_pcm_sample - microphone_dc_estimate) >> MIC_DC_TRACK_SHIFT;
            int32_t centered_sample = raw_pcm_sample - microphone_dc_estimate;
            int32_t centered_absolute = centered_sample < 0 ? -centered_sample : centered_sample;
            // 噪声底只在当前门关闭时缓慢跟踪，避免把正在说话的音量当成噪声。
            if (microphone_gate_hold_samples == 0)
            {
                int32_t noise_delta = centered_absolute - (int32_t)microphone_noise_floor;
                microphone_noise_floor = (uint32_t)((int32_t)microphone_noise_floor +
                    (noise_delta >> MIC_NOISE_TRACK_SHIFT));
            }
            uint32_t gate_minimum = english_mode_enabled == true
                ? (uint32_t)MIC_NOISE_GATE_MINIMUM_EN
                : (uint32_t)MIC_NOISE_GATE_MINIMUM;
            uint32_t gate_threshold = microphone_noise_floor * MIC_NOISE_GATE_MULTIPLIER + MIC_NOISE_GATE_OFFSET;
            if (gate_threshold < gate_minimum)
            {
                gate_threshold = gate_minimum;
            }
            uint32_t gate_hold_ms = english_mode_enabled == true
                ? (uint32_t)MIC_GATE_HOLD_EN_MS
                : (uint32_t)MIC_GATE_HOLD_MS;
            if ((uint32_t)centered_absolute >= gate_threshold)
            {
                microphone_gate_hold_samples = (uint16_t)((MIC_SAMPLE_RATE * gate_hold_ms) / 1000);
            }
            else if (microphone_gate_hold_samples > 0)
            {
                microphone_gate_hold_samples--;
            }
            // 门关闭时：中文仍轻度衰减，英语改送静音，避免环境声被听成杂词。
            int32_t gated_sample = 0;
            if (microphone_gate_hold_samples > 0)
            {
                gated_sample = centered_sample;
            }
            else
            {
                gated_sample = centered_sample >> 2;
            }
            int gain = 3;
            int64_t amplified_sample = (int64_t)gated_sample * gain;
            int32_t scaled_sample = amplified_sample > INT32_MAX ? INT32_MAX :
                                    (amplified_sample < INT32_MIN ? INT32_MIN : (int32_t)amplified_sample);
            // 防止转换结果超出 16 位有符号音频范围。
            if (scaled_sample > 32767)
            {
                scaled_sample = 32767;
            }
            // 防止转换结果低于 16 位有符号音频范围。
            if (scaled_sample < -32768)
            {
                scaled_sample = -32768;
            }
            // 把转换后的采样写入发送缓冲区。
            int16_t pcm_sample = (int16_t)scaled_sample;
            microphone_buffer[frame_index * 2] = (uint8_t)(pcm_sample & 0xFF);
            microphone_buffer[frame_index * 2 + 1] = (uint8_t)((pcm_sample >> 8) & 0xFF);
            // 计算该采样的绝对值，用于判断麦克风是否真的收到声音。
            int32_t absolute_sample = scaled_sample;
            if (absolute_sample < 0)
            {
                absolute_sample = -absolute_sample;
            }
            // 保存当前录音中的最大音量。
            if (absolute_sample > microphone_max_volume)
            {
                microphone_max_volume = absolute_sample;
            }
        }
        if (asr_started == true && relay_connected == true)
        {
            asr_socket.sendBIN(microphone_buffer, pcm_bytes);
            microphone_bytes_sent = microphone_bytes_sent + pcm_bytes;
        }
        else
        {
            mic_preroll_append(microphone_buffer, pcm_bytes);
        }
    }
}

void relay_send_json(const String &message)
{
    String mutable_message = message;
    if (relay_connected == true)
    {
        asr_socket.sendTXT(mutable_message);
    }
}

void reset_relay_opus_decoder(void)
{
    // 递增代次即可让解码任务丢弃正在处理的旧包；解码器只由独立任务管理。
    relay_opus_generation = relay_opus_generation + 1;
    relay_audio_is_opus = false;
    relay_opus_expected_sequence = 0;
    clear_relay_opus_packet_queue();
}

bool prepare_relay_opus_decoder(void)
{
    if (relay_opus_packet_queue == NULL || relay_opus_decoder_task_handle == NULL ||
        relay_opus_decoder_ready == false)
    {
        Serial.println("Opus 独立解码任务尚未就绪");
        return false;
    }
    relay_audio_is_opus = true;
    return enqueue_relay_opus_control(RELAY_OPUS_ITEM_START);
}

bool decode_and_queue_relay_opus(const uint8_t *packet, size_t packet_length, uint32_t generation)
{
    if (packet == NULL || packet_length < RELAY_OPUS_HEADER_BYTES || relay_opus_decoder == NULL)
    {
        return false;
    }
    if (packet[0] != 'X' || packet[1] != 'Y' || packet[2] != 'O' || packet[3] != 'P' ||
        packet[4] != RELAY_OPUS_PROTOCOL_VERSION)
    {
        Serial.println("电脑下发的 Opus 包头或版本无效");
        return false;
    }
    uint16_t sequence = (uint16_t)packet[6] | ((uint16_t)packet[7] << 8);
    uint16_t valid_samples = (uint16_t)packet[8] | ((uint16_t)packet[9] << 8);
    uint16_t opus_length = (uint16_t)packet[10] | ((uint16_t)packet[11] << 8);
    if (valid_samples == 0 || valid_samples > RELAY_OPUS_FRAME_SAMPLES || opus_length == 0 ||
        packet_length != RELAY_OPUS_HEADER_BYTES + opus_length)
    {
        Serial.println("电脑下发的 Opus 包长度无效");
        return false;
    }
    if (sequence != relay_opus_expected_sequence)
    {
        Serial.print("Opus 包序号不连续，期望/收到: " );
        Serial.print(relay_opus_expected_sequence);
        Serial.print("/");
        Serial.println(sequence);
        return false;
    }
    int decoded_samples = opus_decode(
        relay_opus_decoder,
        packet + RELAY_OPUS_HEADER_BYTES,
        opus_length,
        relay_opus_pcm_buffer,
        RELAY_OPUS_FRAME_SAMPLES,
        0);
    if (decoded_samples < 0 || valid_samples > (uint16_t)decoded_samples)
    {
        Serial.print("Opus 音频解码失败，返回值: " );
        Serial.println(decoded_samples);
        return false;
    }
    // 解码期间如果发生断线或新一轮 audio.start，旧音频不得进入新一轮播放队列。
    if (generation != relay_opus_generation)
    {
        return true;
    }
    relay_opus_expected_sequence = (uint16_t)(relay_opus_expected_sequence + 1);
    size_t queued_length = 0;
    return queue_tts_pcm_copy(
        (const uint8_t *)relay_opus_pcm_buffer,
        (size_t)valid_samples * sizeof(int16_t),
        &queued_length);
}

void clear_relay_opus_packet_queue(void)
{
    if (relay_opus_packet_queue == NULL)
    {
        return;
    }
    RelayOpusItem item;
    while (xQueueReceive(relay_opus_packet_queue, &item, 0) == pdPASS)
    {
        if (item.data != NULL)
        {
            heap_caps_free(item.data);
        }
    }
}

bool enqueue_relay_opus_control(uint8_t type)
{
    if (relay_opus_packet_queue == NULL)
    {
        return false;
    }
    RelayOpusItem item;
    item.type = type;
    item.data = NULL;
    item.length = 0;
    item.generation = relay_opus_generation;
    return xQueueSend(relay_opus_packet_queue, &item, 0) == pdPASS;
}

bool enqueue_relay_opus_packet_copy(const uint8_t *packet, size_t packet_length)
{
    if (packet == NULL || packet_length < RELAY_OPUS_HEADER_BYTES || relay_opus_packet_queue == NULL)
    {
        return false;
    }
    uint8_t *copy = (uint8_t *)heap_caps_malloc(packet_length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (copy == NULL)
    {
        copy = (uint8_t *)heap_caps_malloc(packet_length, MALLOC_CAP_8BIT);
    }
    if (copy == NULL)
    {
        return false;
    }
    memcpy(copy, packet, packet_length);
    RelayOpusItem item;
    item.type = RELAY_OPUS_ITEM_DATA;
    item.data = copy;
    item.length = packet_length;
    item.generation = relay_opus_generation;
    if (xQueueSend(relay_opus_packet_queue, &item, 0) != pdPASS)
    {
        heap_caps_free(copy);
        return false;
    }
    return true;
}

void relay_opus_decoder_task(void *parameter)
{
    (void)parameter;
    int opus_error = OPUS_OK;
    relay_opus_decoder = opus_decoder_create(RELAY_OPUS_SAMPLE_RATE, RELAY_OPUS_CHANNELS, &opus_error);
    if (relay_opus_decoder == NULL || opus_error != OPUS_OK)
    {
        Serial.print("Opus 解码器初始化失败，错误码: ");
        Serial.println(opus_error);
        relay_opus_decoder_ready = false;
        vTaskDelete(NULL);
        return;
    }
    relay_opus_decoder_ready = true;
    for (;;)
    {
        RelayOpusItem item;
        if (xQueueReceive(relay_opus_packet_queue, &item, portMAX_DELAY) != pdPASS)
        {
            continue;
        }
        if (item.generation != relay_opus_generation)
        {
            if (item.data != NULL)
            {
                heap_caps_free(item.data);
            }
            continue;
        }
        if (item.type == RELAY_OPUS_ITEM_START)
        {
            opus_decoder_ctl(relay_opus_decoder, OPUS_RESET_STATE);
            relay_opus_expected_sequence = 0;
            continue;
        }
        if (item.type == RELAY_OPUS_ITEM_DATA)
        {
            bool decoded = decode_and_queue_relay_opus(item.data, item.length, item.generation);
            heap_caps_free(item.data);
            if (decoded == false)
            {
                relay_opus_decode_error_pending = true;
            }
            continue;
        }
        if (item.type == RELAY_OPUS_ITEM_END)
        {
            tts_final_audio_packet_count = tts_audio_packet_count;
            tts_total_sample_count = tts_audio_byte_count / 2;
            tts_response_done = true;
            tts_finished = true;
            Serial.println("Opus 压缩包已全部解码，等待播放队列排空");
        }
    }
}

void prepare_relay_audio_playback(const String &format)
{
    if (tts_audio_queue == NULL || tts_playback_task_handle == NULL)
    {
        mark_tts_failure("播放队列不可用", false);
        return;
    }
    clear_tts_audio_queue();
    stop_i2s();
    setup_speaker();
    if (speaker_ready == false)
    {
        mark_tts_failure("扬声器初始化失败", false);
        return;
    }
    reset_relay_opus_decoder();
    if (format == "opus" && prepare_relay_opus_decoder() == false)
    {
        mark_tts_failure("Opus 解码器初始化失败", false);
        return;
    }
    tts_error_pending = false;
    tts_error_message = "";
    tts_playback_abort = false;
    tts_playback_failed = false;
    tts_playback_started = false;
    tts_playback_rebuffering = false;
    tts_playback_drained = false;
    tts_response_done = false;
    tts_graceful_drain_pending = false;
    tts_final_audio_packet_count = 0;
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
    // 电脑中转路径不经过旧版 start_tts()，必须在这里单独初始化活动超时。
    // 否则全局默认值 0 会在 audio.start 后被主循环立即判定为“已超时”。
    tts_timeout = millis() + TTS_INACTIVITY_TIMEOUT_MS;
    tts_start_buffer_target_bytes = TTS_SHORT_REPLY_BUFFER_BYTES;
    tts_active = true;
    tts_finished = false;
    tts_cleanup_done = false;
    relay_audio_started = true;
    turn_busy = true;
    report_state("SPEAKING");
}

void handle_relay_event(WStype_t type, uint8_t *payload, size_t length)
{
    if (type == WStype_CONNECTED)
    {
        relay_connected = true;
        relay_connecting = false;
        Serial.println("电脑中转 WebSocket 已连接");
        // 每次重连都同步当前语言，避免服务器重启后退回默认中文。
        relay_send_json(english_mode_enabled
            ? "{\"type\":\"mode.set\",\"language\":\"en\"}"
            : "{\"type\":\"mode.set\",\"language\":\"zh\"}");
        relay_send_json(english_mode_enabled
            ? "{\"type\":\"listen.prepare\",\"language\":\"en\"}"
            : "{\"type\":\"listen.prepare\",\"language\":\"zh\"}");
        if (english_announce_pending == true && english_mode_enabled == true)
        {
            send_english_ready_announce();
            return;
        }
        if (relay_listen_start_pending == true)
        {
            relay_send_json(english_mode_enabled == true
                ? "{\"type\":\"listen.start\",\"language\":\"en\"}"
                : "{\"type\":\"listen.start\",\"language\":\"zh\"}");
            relay_listen_start_pending = false;
        }
        return;
    }
    if (type == WStype_BIN)
    {
        if (ignore_incoming_reply == true || relay_audio_started == false)
        {
            return;
        }
        bool queued = false;
        if (relay_audio_is_opus == true)
        {
            queued = enqueue_relay_opus_packet_copy(payload, length);
        }
        else
        {
            size_t queued_length = 0;
            queued = queue_tts_pcm_copy(payload, length, &queued_length);
        }
        if (queued == false)
        {
            mark_tts_failure(relay_audio_is_opus ? "电脑下发的 Opus 压缩包入队失败" : "电脑下发的 PCM 入队失败", false);
        }
        return;
    }
    if (type == WStype_TEXT)
    {
        String message;
        message.reserve(length + 1);
        for (size_t index = 0; index < length; index = index + 1)
        {
            message += (char)payload[index];
        }
        String event_type = json_string(message, "type");
        if (event_type == "hello")
        {
            Serial.println("电脑中转协议握手成功：上行16k PCM，下行24k Opus");
            return;
        }
        if (event_type == "mode.changed")
        {
            String language = json_string(message, "language");
            Serial.print("电脑中转语言模式已同步: ");
            Serial.println(language == "en" ? "English" : "中文");
            if ((language == "en") == english_mode_enabled)
            {
                // The relay has finished discarding the previous language session.
                ignore_incoming_reply = false;
            }
            return;
        }
        if (event_type == "listen.started")
        {
            if (asr_active == false || english_announce_active == true ||
                listen_commit_pending == true)
            {
                Serial.println("忽略过期的 listen.started");
                return;
            }
            microphone_bytes_sent = 0;
            microphone_max_volume = 0;
            microphone_raw_max_volume = 0;
            microphone_first_frame_printed = false;
            if (microphone_ready == false)
            {
                setup_microphone();
            }
            mic_preroll_flush();
            asr_started = true;
            asr_finish_sent = false;
            recording = true;
            listen_start_sent_time = 0;
            listen_start_retry_count = 0;
            record_hard_stop_time = millis() + (english_mode_enabled == true ? RECORD_TIME_EN_MS : RECORD_TIME_MS);
            record_stop_time = record_hard_stop_time;
            asr_sentence_end_pending = false;
            asr_timeout = millis() + 25000;
            report_state("LISTENING");
            if (english_mode_enabled == true)
            {
                draw_english_listen_prompt();
            }
            else if (ui_listen_animation_active == false)
            {
                start_listen_animation();
            }
            Serial.print("电脑中转已开始收音，最多 ");
            Serial.print((english_mode_enabled == true ? RECORD_TIME_EN_MS : RECORD_TIME_MS) / 1000);
            Serial.println(" 秒");
            return;
        }
        if (event_type == "stt.partial")
        {
            if (english_announce_active == true || asr_active == false || asr_started == false)
            {
                return;
            }
            String recognized = json_string(message, "text");
            if (recognized.length() > 0 && recognized != final_text)
            {
                if (ui_listening_has_speech == false)
                {
                    ui_listening_has_speech = true;
                    if (english_mode_enabled == false)
                    {
                        draw_companion_status_text("正在倾听");
                    }
                }
                final_text = recognized;
                asr_preview_dirty = true;
                asr_sentence_end_pending = false;
                unsigned long extended_stop_time = millis() +
                    (english_mode_enabled == true ? ASR_TEXT_TAIL_EN_MS : ASR_TEXT_TAIL_MS);
                record_stop_time = (long)(extended_stop_time - record_hard_stop_time) < 0
                    ? extended_stop_time : record_hard_stop_time;
                Serial.print("实时识别：");
                Serial.println(final_text);
            }
            bool sentence_finished = message.indexOf("\"sentence_end\":true") >= 0 ||
                                     message.indexOf("\"sentence_end\": true") >= 0;
            if (recording == true && asr_finish_sent == false && sentence_finished == true &&
                final_text.length() > 0)
            {
                if (is_ignorable_asr_text(final_text) == true)
                {
                    asr_sentence_end_pending = false;
                }
                else
                {
                    asr_sentence_end_pending = true;
                    asr_sentence_confirm_time = millis() +
                        (english_mode_enabled == true ? ASR_SENTENCE_CONFIRM_EN_MS : ASR_SENTENCE_CONFIRM_MS);
                }
            }
            return;
        }
        if (event_type == "stt.final")
        {
            if (english_announce_active == true || asr_active == false || asr_started == false)
            {
                Serial.println("忽略过期的 stt.final");
                return;
            }
            final_text = json_string(message, "text");
            // 最终识别到达后立即停止聆听绘制，避免后续动画帧擦掉 AI 回复。
            ui_listening_indicator_active = false;
            ui_listening_has_speech = true;
            recording = false;
            asr_started = false;
            asr_active = false;
            asr_preview_dirty = false;
            if (final_text.length() > 0)
            {
                ignore_incoming_reply = false;
                continuous_dialog_last_voice_time = millis();
                continuous_dialog_empty_retry_count = 0;
                set_dialog_user_text(final_text);
                Serial.print("你说：");
                Serial.println(final_text);
                turn_busy = true;
                report_state("THINKING");
                draw_status("思考中", "正在生成回答");
            }
            return;
        }
        if (event_type == "turn.empty")
        {
            if (english_announce_active == true || asr_active == false || asr_started == false)
            {
                Serial.println("忽略过期的 turn.empty");
                return;
            }
            ui_listening_indicator_active = false;
            recording = false;
            asr_active = false;
            asr_started = false;
            turn_busy = false;
            if (english_mode_enabled == true)
            {
                draw_english_listen_prompt();
            }
            else
            {
                draw_status("没听清", "请再说一次");
            }
            if (continuous_dialog_active == true)
            {
                continuous_dialog_restart_pending = true;
                continuous_dialog_restart_time = millis() + CONTINUOUS_DIALOG_EMPTY_RETRY_DELAY_MS;
            }
            return;
        }
        if (event_type == "reply.emotion")
        {
            String emotion = json_string(message, "emotion");
            current_emotion = is_valid_emotion(emotion) ? emotion : "neutral";
            set_tts_parameters_for_emotion(current_emotion);
            return;
        }
        if (event_type == "vision.emotion")
        {
            bool vision_ok = message.indexOf("\"ok\":true") >= 0;
            bool vision_face = message.indexOf("\"face\":true") >= 0;
            String emotion = json_string(message, "emotion");
            float confidence = 0.0f;
            int confidence_at = message.indexOf("\"confidence\":");
            if (confidence_at >= 0)
            {
                confidence = message.substring(confidence_at + 13).toFloat();
            }
            Serial.print("视觉情绪: ");
            Serial.print(emotion);
            Serial.print(" ok=");
            Serial.print(vision_ok ? "yes" : "no");
            Serial.print(" face=");
            Serial.print(vision_face ? "yes" : "no");
            Serial.print(" conf=");
            Serial.println(confidence, 2);
            if (vision_ok == true && is_valid_emotion(emotion) == true)
            {
                current_emotion = emotion;
            }
            camera_on_vision_emotion(emotion, vision_ok, vision_face, confidence);
            return;
        }
        if (event_type == "reply.sentence")
        {
            if (ignore_incoming_reply == true || english_announce_active == true)
            {
                return;
            }
            String sentence = json_string(message, "text");
            sentence = strip_emotion_markup(sentence);
            sentence.trim();
            if (sentence.length() > 0)
            {
                append_streaming_sentence_to_chat(sentence);
                Serial.print("小云宝句子：");
                Serial.println(sentence);
                Serial.print("聊天回复消息下标/历史数量: " );
                Serial.print(chat_speaking_message_index);
                Serial.print("/");
                Serial.println(chat_history_count);
            }
            return;
        }
        if (event_type == "audio.start")
        {
            if (ignore_incoming_reply == true)
            {
                Serial.println("打断后忽略上一轮音频");
                return;
            }
            prepare_relay_audio_playback(json_string(message, "format"));
            return;
        }
        if (event_type == "audio.end")
        {
            if (ignore_incoming_reply == true)
            {
                relay_audio_started = false;
                return;
            }
            relay_audio_started = false;
            if (relay_audio_is_opus == true)
            {
                if (enqueue_relay_opus_control(RELAY_OPUS_ITEM_END) == false)
                {
                    mark_tts_failure("Opus 结束标记入队失败", false);
                }
                else
                {
                    Serial.println("电脑中转音频接收完成，等待 Opus 解码队列排空");
                }
            }
            else
            {
                tts_final_audio_packet_count = tts_audio_packet_count;
                tts_total_sample_count = tts_audio_byte_count / 2;
                tts_response_done = true;
                tts_finished = true;
                Serial.println("电脑中转音频接收完成，等待播放队列排空");
            }
            return;
        }
        if (event_type == "error")
        {
            String error_message = json_string(message, "message");
            Serial.print("电脑中转错误：");
            Serial.println(error_message);
            recording = false;
            asr_active = false;
            asr_started = false;
            if (relay_audio_started == true || tts_active == true)
            {
                reset_relay_opus_decoder();
                mark_tts_failure(error_message.length() > 0 ? error_message : "中转服务错误", false);
            }
            else
            {
                turn_busy = false;
                if (english_announce_active == true && english_mode_enabled == true)
                {
                    english_announce_active = false;
                    Serial.println("英语开场白中转失败，仍进入英语聆听");
                    request_voice_turn("ENGLISH_READY_AFTER_ERROR");
                    return;
                }
                report_state("ERROR");
                draw_status("中转服务错误", "稍后自动重试");
                if (continuous_dialog_active == true)
                {
                    continuous_dialog_restart_pending = true;
                    continuous_dialog_restart_time = millis() + 1500;
                }
            }
            return;
        }
        return;
    }
    if (type == WStype_DISCONNECTED || type == WStype_ERROR)
    {
        const bool had_asr = asr_active == true || relay_listen_start_pending == true;
        relay_connected = false;
        relay_connecting = false;
        relay_client_started = false;
        relay_listen_start_pending = false;
        relay_audio_started = false;
        reset_relay_opus_decoder();
        recording = false;
        asr_started = false;
        asr_active = false;
        asr_finish_sent = false;
        listen_start_sent_time = 0;
        listen_start_retry_count = 0;
        Serial.print("电脑中转连接已断开，事件=");
        Serial.print(type == WStype_ERROR ? "ERROR" : "DISCONNECTED");
        if (payload != NULL && length > 0)
        {
            Serial.print("，原因=");
            for (size_t index = 0; index < length; index = index + 1)
            {
                char character = (char)payload[index];
                if (character >= 32 && character <= 126)
                {
                    Serial.print(character);
                }
            }
        }
        Serial.println("，等待自动重连");
        if (turn_busy == true || tts_active == true)
        {
            mark_tts_failure("电脑中转连接断开", false);
        }
        else if (continuous_dialog_active == true || had_asr == true)
        {
            voice_turn_pending = continuous_dialog_active == true;
            voice_turn_pending_source = voice_turn_pending ? "RELAY_RECONNECT" : "";
            continuous_dialog_restart_pending = true;
            continuous_dialog_restart_time = millis() + 2000;
        }
    }
}

void asr_event(WStype_t type, uint8_t *payload, size_t length)
{
    // 正常的连接、文本中间事件和断开不打印，避免待机监听持续刷屏。
    if (type == WStype_ERROR)
    {
        Serial.println("百炼 ASR WebSocket 报错");
        if (payload != NULL && length > 0)
        {
            Serial.print("WebSocket 附加信息: " );
            for (size_t index = 0; index < length; index = index + 1)
            {
                uint8_t value = payload[index];
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
    }
    if (type == WStype_DISCONNECTED)
    {
        Serial.println("百炼 ASR WebSocket 已断开");
    }
    if (type == WStype_CONNECTED)
    {
        asr_task_id = "asr-";
        asr_task_id += String((uint32_t)esp_random(), HEX);
        asr_started = false;
        asr_finish_sent = false;
        // 创建启动识别任务的 JSON 报文，并先写入报文头和任务编号字段。
        String message = "{\"header\":{\"action\":\"run-task\",\"task_id\":\"";
        // 把本次实时识别任务的唯一编号写入启动报文。
        message += asr_task_id;
        // 指定双工流式通信、音频任务、语音识别功能和模型字段。
        message += "\",\"streaming\":\"duplex\"},\"payload\":{\"task_group\":\"audio\",\"task\":\"asr\",\"function\":\"recognition\",\"model\":\"";
        // 写入本地配置文件中指定的百炼实时语音识别模型名称。
        message += XIYUNBAO_ASR_MODEL;
        // 声明麦克风发送的是 16000 赫兹裸 PCM，并加入协议强制要求的 payload.input 空对象。
        message += "\",\"parameters\":{\"sample_rate\":16000,\"format\":\"pcm\"},\"input\":{}}}";
        // 把完整的启动任务报文发送给百炼实时语音识别服务。
        asr_socket.sendTXT(message);
        setup_microphone();
        recording = false;
        // 等待服务端返回 task-started 后开始发送麦克风数据。
    }
    if (type == WStype_TEXT)
    {
        String message;
        for (size_t i = 0; i < length; i = i + 1)
        {
            message += (char)payload[i];
        }
        // 从服务端嵌套响应中的 text 字段提取当前识别文字。
        String text = json_last_nonempty_string(message, "text");
        // 记录服务端返回的关键任务事件，避免每个空的中间结果刷屏。
        bool is_task_started = message.indexOf("task-started") >= 0 || message.indexOf("task_started") >= 0;
        bool is_task_finished = message.indexOf("task-finished") >= 0 || message.indexOf("task_finished") >= 0;
        bool is_task_failed = message.indexOf("task-failed") >= 0 || message.indexOf("task_failed") >= 0;
        // 只打印任务错误和非空识别结果，不打印完整 JSON。
        // 检查服务端是否明确报告了识别任务失败。
        if (is_task_failed == true)
        {
            // 提取服务端返回的错误码，方便在串口中准确定位问题。
            String error_code = json_string(message, "error_code");
            // 提取服务端返回的详细错误说明。
            String error_message = json_string(message, "error_message");
            Serial.print("百炼 ASR 任务失败: ");
            Serial.print(error_code);
            Serial.print(" ");
            Serial.println(error_message);
            // 标记实时识别任务已经停止。
            asr_active = false;
            // 停止继续读取和发送麦克风音频。
            recording = false;
            // 主动关闭当前失败的 WebSocket，避免下一次语音复用失效任务。
            asr_socket.disconnect();
            // 明确标记当前没有可供后续处理的识别结果。
            asr_result_ready = false;
            // 单轮 ASR 失败不退出连续会话，避免用户必须再次说唤醒词。
            wake_listening_active = false;
            ui_listening_indicator_active = false;
            report_state("ERROR");
            draw_status("识别失败", "稍后继续聆听");
            if (continuous_dialog_active == true)
            {
                // 连续会话稍后自动重试下一轮 ASR。
                continuous_dialog_restart_pending = true;
                continuous_dialog_restart_time = millis() + CONTINUOUS_DIALOG_RESTART_DELAY_MS;
            }
            // 结束本次 WebSocket 事件处理，避免继续把失败报文当成识别结果。
            return;
        }
        if (is_task_started == true)
        {
            // 清零当前录音的 PCM 发送字节统计。
            microphone_bytes_sent = 0;
            // 清零当前录音的最大音量统计。
            microphone_max_volume = 0;
            // 清零当前录音的原始 I2S 最大值统计。
            microphone_raw_max_volume = 0;
            // 允许下一次录音重新打印首帧诊断。
            microphone_first_frame_printed = false;
            asr_started = true;
            asr_finish_sent = false;
            recording = true;
            ui_listening_indicator_active = true;
            record_hard_stop_time = millis() + (english_mode_enabled == true ? RECORD_TIME_EN_MS : RECORD_TIME_MS);
            record_stop_time = record_hard_stop_time;
            asr_sentence_end_pending = false;
            report_state("LISTENING");
            draw_status("请说话", "正在聆听...");
            Serial.println("持续监听已开始，最多 10 秒，句末确认后提交");
        }
        if (text.length() > 0)
        {
            bool text_changed = text != final_text;
            final_text = text;
            if (final_text != last_asr_printed_text)
            {
                Serial.print("实时识别：");
                Serial.println(final_text);
                last_asr_printed_text = final_text;
            }
            if (text_changed == true)
            {
                // 新增文字说明用户可能仍在说话，取消旧句末并保留 2.5 秒尾音空间。
                asr_preview_dirty = true;
                asr_sentence_end_pending = false;
                unsigned long extended_stop_time = millis() +
                    (english_mode_enabled == true ? ASR_TEXT_TAIL_EN_MS : ASR_TEXT_TAIL_MS);
                record_stop_time = (long)(extended_stop_time - record_hard_stop_time) < 0
                    ? extended_stop_time
                    : record_hard_stop_time;
            }
        }
        bool sentence_finished = message.indexOf("\"sentence_end\":true") >= 0 ||
                                 message.indexOf("\"sentence_end\": true") >= 0;
        if (recording == true && asr_finish_sent == false && sentence_finished == true &&
            final_text.length() > 0)
        {
            // sentence_end 只是候选句末；继续收音一小段，防止“你在”截断“你在干嘛”。
            if (is_ignorable_asr_text(final_text) == true)
            {
                asr_sentence_end_pending = false;
            }
            else
            {
                asr_sentence_end_pending = true;
                asr_sentence_confirm_time = millis() +
                    (english_mode_enabled == true ? ASR_SENTENCE_CONFIRM_EN_MS : ASR_SENTENCE_CONFIRM_MS);
                Serial.println("ASR 检测到候选句末，等待后续语音");
            }
        }
        if (is_task_finished == true)
        {
            Serial.print("ASR 完成，识别文本字节数: ");
            Serial.println((unsigned int)final_text.length());
            // 标记服务端已经正常完成本次 ASR 任务。
            asr_task_finished = true;
            // 标记主循环可以处理最终识别文字。
            asr_result_ready = true;
            // 最终结果即使未到刷新周期，也由主循环下一步强制写入正式消息。
            asr_preview_dirty = false;
            asr_active = false;
            recording = false;
            ui_listening_indicator_active = false;
            asr_socket.disconnect();
        }
    }
    if (type == WStype_DISCONNECTED || type == WStype_ERROR)
    {
        if (asr_disconnect_expected == true)
        {
            asr_disconnect_expected = false;
            Serial.println("百炼 ASR 已按本地请求关闭");
            return;
        }
        // 收到 task-finished 后的断开属于正常关闭，不应误报网络错误。
        if (asr_task_finished == true)
        {
            Serial.println("百炼 ASR WebSocket 正常关闭");
        }
        // 未收到 task-finished 的断开才按照连接异常处理。
        else
        {
            Serial.println("百炼 ASR WebSocket 已断开或报错");
            asr_active = false;
            recording = false;
            ui_listening_indicator_active = false;
            // 待机唤醒阶段只结束当前连接，主循环稍后会自动重新监听。
            if (wake_listening_active == true)
            {
                wake_listening_active = false;
                report_state("IDLE");
        draw_status("小云宝", "请唤醒我开始交流");
                Serial.println("唤醒监听连接异常，稍后自动重试");
            }
            else
            {
                // 已进入连续对话后，异常断开只重试当前轮，不退出唤醒会话。
                report_state("ERROR");
                draw_status("识别失败", "稍后继续聆听");
                continuous_dialog_restart_pending = true;
                continuous_dialog_restart_time = millis() + CONTINUOUS_DIALOG_RESTART_DELAY_MS;
            }
        }
    }
}

void process_asr_result(void)
{
    asr_result_ready = false;
    ui_listening_indicator_active = false;
    const bool committed = listen_commit_pending;
    listen_commit_pending = false;
    if (final_text.length() == 0)
    {
        // 没有识别到文字属于本轮没有听清，不属于网络或设备故障。
        report_state("IDLE");
        if (committed == true)
        {
            stop_continuous_dialog("K1_COMMIT_EMPTY");
            draw_wake_prompt();
            Serial.println("K1 结束收音且没有语音，已完全打断");
            return;
        }
        draw_status("没听清", "请再说一次");
        // 没有有效语音时结束连续会话，避免无人说话时反复产生云端请求。
        wake_listening_active = false;
        if (CONTINUOUS_DIALOG_STOP_ON_EMPTY_RESULT == true)
        {
            stop_continuous_dialog("EMPTY_ASR_RESULT");
        }
        else if (continuous_dialog_active == true)
        {
            continuous_dialog_empty_retry_count = continuous_dialog_empty_retry_count + 1;
            unsigned long retry_delay = CONTINUOUS_DIALOG_EMPTY_RETRY_DELAY_MS;
            if (continuous_dialog_empty_retry_count > CONTINUOUS_DIALOG_MAX_EMPTY_RETRIES)
            {
                retry_delay = CONTINUOUS_DIALOG_EMPTY_RETRY_DELAY_MS * 3;
            }
            continuous_dialog_restart_pending = true;
            continuous_dialog_restart_time = millis() + retry_delay;
            Serial.println("连续对话暂未听到声音，稍后继续聆听");
        }
        return;
    }
    // 待机阶段只处理唤醒词，不把普通环境声音发送给千问，避免误触发和额外延迟。
    if (continuous_dialog_active == false)
    {
        bool wake_word_found = final_text.indexOf(VOICE_WAKE_WORD) >= 0;
        wake_listening_active = false;
        if (wake_word_found == false)
        {
            Serial.print("待机监听忽略：");
            Serial.println(final_text);
            report_state("IDLE");
            draw_status("小云宝", "请唤醒我开始交流");
            return;
        }
        // 唤醒词属于用户消息，立即显示在聊天框中，不发送给千问。
        add_chat_message(VOICE_WAKE_WORD, true);
        chat_scroll_lines = 0;
        redraw_chat_history();
        continuous_dialog_active = true;
        continuous_dialog_last_voice_time = millis();
        continuous_dialog_restart_pending = false;
        Serial.println("唤醒成功：连续对话已开启");
        int wake_word_position = final_text.indexOf(VOICE_WAKE_WORD);
        String wake_remainder = final_text.substring(wake_word_position + strlen(VOICE_WAKE_WORD));
        wake_remainder.trim();
        wake_remainder = remove_wake_word_punctuation(wake_remainder);
        if (wake_remainder.length() == 0)
        {
            // 唤醒后同时显示并播报确认语，用户能明确知道设备已经进入连续交流状态。
            current_emotion = "happy";
            set_tts_parameters_for_emotion(current_emotion);
            set_dialog_reply("我在呢，请说话");
            draw_status("小云宝", "我在呢，请说话");
            start_tts("我在呢，请说话");
            return;
        }
        final_text = wake_remainder;
    }
    if (is_ignorable_asr_text(final_text) == true)
    {
        Serial.print("忽略无效短语音：");
        Serial.println(final_text);
        discard_dialog_user_preview();
        report_state("IDLE");
        if (continuous_dialog_active == true)
        {
            continuous_dialog_restart_pending = true;
            continuous_dialog_restart_time = millis() + CONTINUOUS_DIALOG_EMPTY_RETRY_DELAY_MS;
        }
        return;
    }
    // 连续会话里单独再叫一次唤醒词时直接本地应答，省去一次千问网络往返。
    String wake_only_text = remove_wake_word_punctuation(final_text);
    wake_only_text.replace(" ", "");
    wake_only_text.replace("　", "");
    if (wake_only_text == VOICE_WAKE_WORD)
    {
        continuous_dialog_last_voice_time = millis();
        continuous_dialog_empty_retry_count = 0;
        Serial.print("你说：");
        Serial.println(final_text);
        set_dialog_user_text(final_text);
        current_emotion = "happy";
        set_tts_parameters_for_emotion(current_emotion);
        String wake_reply = "我在呢，爸爸请说。";
        Serial.println("纯唤醒词本地快速回复，不请求千问");
        start_reply_sentence_sequence(wake_reply);
        return;
    }
    turn_busy = true;
    // 只有确认得到有效用户话语才刷新 30 秒静音计时；空结果不会延长会话。
    continuous_dialog_last_voice_time = millis();
    continuous_dialog_empty_retry_count = 0;
    Serial.print("你说：");
    Serial.println(final_text);
    // 将本轮识别完成的用户话语保存到对话框，后续以右对齐浅灰显示。
    set_dialog_user_text(final_text);
    unsigned long qwen_request_start_time = millis();
    String reply = call_qwen(final_text);
    Serial.print("千问回复耗时 ms: ");
    Serial.println(millis() - qwen_request_start_time);
    Serial.print("小云宝：");
    Serial.println(reply);
    // SSE 流水线已经把完整句子陆续交给同一 TTS 会话时，不能再次整段合成。
    if (qwen_reply_streamed_to_tts == false)
    {
        start_reply_sentence_sequence(reply);
    }
}
