// WiFi and wake-word services
// Shared state and declarations are kept in the main sketch.

bool device_wifi_connected(void)
{
    return wifi_provision_active == false && WiFi.status() == WL_CONNECTED;
}

bool ensure_wifi_connected(unsigned long timeout_ms)
{
    (void)timeout_ms;
    if (wifi_provision_active == true || wifi_credentials_available() == false)
    {
        return false;
    }
    if (WiFi.status() == WL_CONNECTED)
    {
        wifi_ready = true;
        return true;
    }
    wifi_ready = false;
    // Never wait here: this function is called from button and audio paths.
    // The regular Wi-Fi service owns retries and keeps the main loop responsive.
    if (millis() - wifi_last_reconnect_attempt < 10000UL)
    {
        return false;
    }
    if (WiFi.status() == WL_IDLE_STATUS &&
        millis() - wifi_connection_start_time < 30000UL)
    {
        return false;
    }
    WiFi.disconnect(false, false);
    if (wifi_sta_password.length() == 0)
    {
        WiFi.begin(wifi_sta_ssid.c_str());
    }
    else
    {
        WiFi.begin(wifi_sta_ssid.c_str(), wifi_sta_password.c_str());
    }
    wifi_last_reconnect_attempt = millis();
    wifi_connection_start_time = millis();
    Serial.println("WiFi 已发起异步重连");
    return false;
}

void service_wifi_connection(void)
{
    if (wifi_provision_active == true || wifi_credentials_available() == false)
    {
        return;
    }
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED)
    {
        wifi_ready = true;
        if (wifi_connection_announced == false)
        {
            Serial.print("WiFi 已连接，IP=");
            Serial.println(WiFi.localIP());
            wifi_connection_announced = true;
        }
        if (wifi_reconnect_user_requested == true)
        {
            wifi_reconnect_user_requested = false;
            show_button_toast("WiFi已连接");
        }
        return;
    }
    if (wifi_ready == true || wifi_connection_announced == true)
    {
        Serial.println("WiFi 已断开，后台自动重连");
    }
    wifi_ready = false;
    wifi_connection_announced = false;
    // 连接过程最多安静等待 30 秒，避免每几秒重复 reconnect() 干扰底层状态机。
    if (status == WL_IDLE_STATUS && millis() - wifi_connection_start_time < 30000)
    {
        return;
    }
    if (millis() - wifi_last_reconnect_attempt < 10000)
    {
        return;
    }
    wifi_last_reconnect_attempt = millis();
    wifi_connection_start_time = millis();
    WiFi.disconnect(false, false);
    if (wifi_sta_password.length() == 0)
    {
        WiFi.begin(wifi_sta_ssid.c_str());
    }
    else
    {
        WiFi.begin(wifi_sta_ssid.c_str(), wifi_sta_password.c_str());
    }
}

void service_network_offline_notice(void)
{
    if (screen_ready == false || wifi_provision_active == true)
    {
        network_offline_notice_visible = false;
        return;
    }
    if (device_wifi_connected() == true)
    {
        if (network_offline_notice_visible == true)
        {
            network_offline_notice_visible = false;
            chat_last_render_signature = "";
            if (english_mode_enabled == true)
            {
                if (chat_history_count == 0)
                {
                    draw_english_listen_prompt();
                }
                else
                {
                    redraw_chat_history();
                }
            }
            else if (continuous_dialog_active == false &&
                     asr_active == false &&
                     tts_active == false &&
                     turn_busy == false)
            {
                draw_wake_prompt();
            }
        }
        return;
    }
    if (asr_active == true || tts_active == true || turn_busy == true ||
        english_announce_active == true)
    {
        return;
    }
    if (network_offline_notice_visible == false)
    {
        draw_network_offline_notice();
    }
}

void request_wifi_reconnect(void)
{
    if (wifi_provision_active == true)
    {
        Serial.println("配网进行中，忽略重复进入");
        return;
    }
    // 长按始终打开配网热点。已保存的网络先留着，等手机页里选了新网络再替换。
    wifi_reconnect_user_requested = false;
    wifi_ready = false;
    wifi_connection_announced = false;
    wifi_last_reconnect_attempt = 0;
    Serial.println("K1 长按，打开配网热点");
    wifi_provision_begin("K1_LONG");
}

void service_local_wake(void)
{
    // 按键模式不运行本地语音模型；此函数保留为空以兼容旧主循环结构。
}

/*
    (void)parameter;
    srmodel_list_t *models = NULL;
    esp_mn_iface_t *multinet = NULL;
    model_iface_data_t *model_data = NULL;
    int16_t *pcm_buffer = NULL;
    int32_t *raw_buffer = NULL;
    char *model_name = NULL;
    esp_mn_error_t *command_error = NULL;
    bool command_list_allocated = false;
    int chunk_samples = 0;
    uint32_t diagnostic_frames = 0;
    uint32_t timeout_count = 0;
    int32_t diagnostic_raw_peak = 0;
    int32_t diagnostic_pcm_peak = 0;
    unsigned long last_diagnostic_time = millis();

    Serial.println("ESP-SR 本地中文唤醒初始化");
    models = esp_srmodel_init("model");
    if (models == NULL)
    {
        Serial.println("本地唤醒失败：无法挂载 model 分区");
        goto cleanup;
    }
    model_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    if (model_name == NULL)
    {
        Serial.println("本地唤醒失败：model 分区没有中文 MultiNet 模型");
        goto cleanup;
    }
    Serial.print("本地唤醒模型: " );
    Serial.println(model_name);
    multinet = esp_mn_handle_from_name(model_name);
    if (multinet == NULL)
    {
        Serial.println("本地唤醒失败：无法取得中文 MultiNet 接口");
        goto cleanup;
    }
    model_data = multinet->create(model_name, LOCAL_WAKE_MODEL_TIMEOUT_MS);
    if (model_data == NULL)
    {
        Serial.println("本地唤醒失败：中文 MultiNet 模型加载失败");
        goto cleanup;
    }
    if (multinet->check_speech_command(model_data, LOCAL_WAKE_COMMAND_PHONEMES) == 0)
    {
        Serial.println("本地唤醒失败：中文模型拒绝 xiao yun bao");
        goto cleanup;
    }
    // ESP-SR 3.x owns the command graph globally.  Building a private node
    // chain here leaves the model's internal command root unset and causes a
    // null dereference in mn7_cn during command registration.
    if (esp_mn_commands_alloc(multinet, model_data) != ESP_OK)
    {
        Serial.println("本地唤醒失败：无法初始化中文命令列表");
        goto cleanup;
    }
    command_list_allocated = true;
    if (esp_mn_commands_phoneme_add(LOCAL_WAKE_COMMAND_ID,
                                    LOCAL_WAKE_COMMAND_TEXT,
                                    LOCAL_WAKE_COMMAND_PHONEMES) != ESP_OK)
    {
        Serial.println("本地唤醒失败：无法添加小云宝命令");
        goto cleanup;
    }
    command_error = esp_mn_commands_update();
    if (command_error != NULL && command_error->num > 0)
    {
        Serial.print("本地唤醒失败：模型无法解析小云宝音素，错误数量: " );
        Serial.println(command_error->num);
        for (int error_index = 0; error_index < command_error->num; error_index++)
        {
            if (command_error->phrases[error_index] != NULL)
            {
                Serial.print("  无效命令ID: " );
                Serial.println(command_error->phrases[error_index]->command_id);
            }
        }
        goto cleanup;
    }
    Serial.println("本地唤醒命令注册成功");
    multinet->print_active_speech_commands(model_data);
    multinet->set_det_threshold(model_data, LOCAL_WAKE_DETECTION_THRESHOLD);
    chunk_samples = multinet->get_samp_chunksize(model_data);
    if (chunk_samples <= 0)
    {
        Serial.println("本地唤醒失败：模型采样块长度无效");
        goto cleanup;
    }
    pcm_buffer = (int16_t *)heap_caps_malloc(chunk_samples * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    raw_buffer = (int32_t *)heap_caps_malloc(chunk_samples * 2 * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pcm_buffer == NULL || raw_buffer == NULL)
    {
        Serial.println("本地唤醒失败：音频缓冲区内存不足");
        goto cleanup;
    }
    Serial.print("本地唤醒词: " );
    Serial.print(LOCAL_WAKE_COMMAND_TEXT);
    Serial.print(" / " );
    Serial.println(LOCAL_WAKE_COMMAND_PHONEMES);
    Serial.print("本地唤醒采样块: " );
    Serial.println(chunk_samples);
    Serial.println("本地唤醒已启动，待机不连接百炼 ASR");

    while (local_wake_stop_requested == false)
    {
        size_t raw_bytes_read = 0;
        esp_err_t read_result = i2s_read(
            I2S_NUM_0,
            raw_buffer,
            chunk_samples * 2 * sizeof(int32_t),
            &raw_bytes_read,
            pdMS_TO_TICKS(250));
        if (read_result != ESP_OK || raw_bytes_read < 2 * sizeof(int32_t))
        {
            continue;
        }
        size_t frame_count = raw_bytes_read / (2 * sizeof(int32_t));
        if (frame_count > (size_t)chunk_samples)
        {
            frame_count = (size_t)chunk_samples;
        }
        for (size_t index = 0; index < frame_count; index++)
        {
            int32_t left = raw_buffer[index * 2];
            int32_t right = raw_buffer[index * 2 + 1];
            int64_t left_abs = left < 0 ? -(int64_t)left : (int64_t)left;
            int64_t right_abs = right < 0 ? -(int64_t)right : (int64_t)right;
            int32_t selected = left_abs >= right_abs ? left : right;
            int64_t selected_abs = selected < 0 ? -(int64_t)selected : (int64_t)selected;
            if (selected_abs > diagnostic_raw_peak)
            {
                diagnostic_raw_peak = selected_abs > INT32_MAX ? INT32_MAX : (int32_t)selected_abs;
            }
            int32_t scaled = selected >> LOCAL_WAKE_INPUT_SHIFT;
            if (scaled > 32767)
            {
                scaled = 32767;
            }
            else if (scaled < -32768)
            {
                scaled = -32768;
            }
            pcm_buffer[index] = (int16_t)scaled;
            int32_t scaled_abs = scaled < 0 ? -scaled : scaled;
            if (scaled_abs > diagnostic_pcm_peak)
            {
                diagnostic_pcm_peak = scaled_abs;
            }
        }
        diagnostic_frames += frame_count;
        if (frame_count < (size_t)chunk_samples)
        {
            memset(pcm_buffer + frame_count, 0, (chunk_samples - frame_count) * sizeof(int16_t));
        }
        esp_mn_state_t state = multinet->detect(model_data, pcm_buffer);
        if (state == ESP_MN_STATE_TIMEOUT)
        {
            timeout_count++;
            multinet->clean(model_data);
        }
        if (millis() - last_diagnostic_time >= LOCAL_WAKE_DIAGNOSTIC_INTERVAL_MS)
        {
            Serial.print("本地唤醒音量: 原始峰值=");
            Serial.print(diagnostic_raw_peak);
            Serial.print(", PCM峰值=");
            Serial.print(diagnostic_pcm_peak);
            Serial.print(", 读取帧数=");
            Serial.print(diagnostic_frames);
            Serial.print(", 模型超时=");
            Serial.println(timeout_count);
            diagnostic_raw_peak = 0;
            diagnostic_pcm_peak = 0;
            diagnostic_frames = 0;
            timeout_count = 0;
            last_diagnostic_time = millis();
        }
        if (state != ESP_MN_STATE_DETECTED)
        {
            continue;
        }
        esp_mn_results_t *results = multinet->get_results(model_data);
        if (results != NULL)
        {
            Serial.print("本地唤醒检测到候选，数量: ");
            Serial.println(results->num);
            for (int candidate = 0; candidate < results->num; candidate++)
            {
                Serial.print("  候选命令ID=");
                Serial.print(results->command_id[candidate]);
                Serial.print(", 置信度=");
                Serial.println(results->prob[candidate], 3);
            }
        }
        if (results != NULL && results->num > 0 && results->command_id[0] == LOCAL_WAKE_COMMAND_ID)
        {
            Serial.print("本地唤醒命中，置信度: " );
            Serial.println(results->prob[0], 3);
            local_wake_detected = true;
            break;
        }
        multinet->clean(model_data);
    }

cleanup:
    if (pcm_buffer != NULL)
    {
        heap_caps_free(pcm_buffer);
    }
    if (raw_buffer != NULL)
    {
        heap_caps_free(raw_buffer);
    }
    if (command_list_allocated)
    {
        esp_mn_commands_free();
    }
    if (model_data != NULL && multinet != NULL)
    {
        multinet->destroy(model_data);
    }
    if (models != NULL)
    {
        esp_srmodel_deinit(models);
    }
    if (local_wake_detected == false && local_wake_stop_requested == false)
    {
        local_wake_available = false;
        Serial.println("本地唤醒已停用；待机仍不会连接云端，可使用本地按键或串口 r");
    }
    local_wake_task_handle = NULL;
    local_wake_task_active = false;
*/
