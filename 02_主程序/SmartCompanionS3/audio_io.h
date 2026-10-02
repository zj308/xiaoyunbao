// INMP441 microphone and MAX98357A audio
// Shared state and declarations are kept in the main sketch.

static File english_ready_file;
static bool english_ready_playing = false;
static size_t english_ready_remaining = 0;

void setup_microphone(void)
{
    stop_i2s();
    i2s_config_t config = {};
    config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate = MIC_SAMPLE_RATE;
    // INMP441 输出 24 位有效数据并占用 32 位 I2S 数据槽。
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    // 同时读取左右声道，兼容 INMP441 的 L/R 接地或接 3.3V 两种接法。
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 8;
    config.dma_buf_len = 256;
    config.use_apll = false;
    config.tx_desc_auto_clear = false;
    config.fixed_mclk = 0;
    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = PIN_I2S_BCLK;
    pins.ws_io_num = PIN_I2S_WS;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = PIN_I2S_MIC_DIN;
    esp_err_t install_result = i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
    if (install_result == ESP_OK)
    {
        i2s_driver_ready = true;
        esp_err_t pin_result = i2s_set_pin(I2S_NUM_0, &pins);
        if (pin_result == ESP_OK)
        {
            microphone_ready = true;
            speaker_ready = false;
            Serial.println("INMP441 初始化完成");
        }
        else
        {
            Serial.print("INMP441 引脚初始化失败，错误码: " );
            Serial.println((int)pin_result);
            stop_i2s();
        }
    }
    else
    {
        Serial.print("INMP441 I2S 驱动安装失败，错误码: " );
        Serial.println((int)install_result);
    }
}

void setup_speaker(void)
{
    stop_i2s();
    i2s_config_t config = {};
    config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = SPEAKER_SAMPLE_RATE;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    // 同时发送左右两个声道，兼容 MAX98357A 的左声道或右声道硬件选择。
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 8;
    config.dma_buf_len = 256;
    // 扬声器使用 APLL 生成更稳定的 24 kHz 音频时钟，减少功放侧时钟抖动杂音。
    config.use_apll = true;
    config.tx_desc_auto_clear = true;
    config.fixed_mclk = 0;
    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = PIN_I2S_BCLK;
    pins.ws_io_num = PIN_I2S_WS;
    pins.data_out_num = PIN_I2S_SPK_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    esp_err_t install_result = i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
    if (install_result == ESP_OK)
    {
        i2s_driver_ready = true;
        esp_err_t pin_result = i2s_set_pin(I2S_NUM_0, &pins);
        if (pin_result == ESP_OK)
        {
            // 明确设置为 24 kHz、16 位、双声道，避免 MAX98357A 选择另一声道时无声。
            esp_err_t clock_result = i2s_set_clk(
                I2S_NUM_0,
                SPEAKER_SAMPLE_RATE,
                I2S_BITS_PER_SAMPLE_16BIT,
                I2S_CHANNEL_STEREO);
            if (clock_result != ESP_OK)
            {
                Serial.print("MAX98357A 时钟配置失败，错误码: ");
                Serial.println((int)clock_result);
                stop_i2s();
                return;
            }
            // 驱动刚切换到扬声器时先送零，避免旧 DMA 内容造成启动爆音。
            i2s_zero_dma_buffer(I2S_NUM_0);
            delay(2);
            speaker_ready = true;
            microphone_ready = false;
            Serial.println("MAX98357A 输出准备完成");
        }
        else
        {
            Serial.print("MAX98357A 引脚初始化失败，错误码: " );
            Serial.println((int)pin_result);
            stop_i2s();
        }
    }
    else
    {
        Serial.print("MAX98357A I2S 驱动安装失败，错误码: " );
        Serial.println((int)install_result);
    }
}

esp_err_t write_speaker_pcm(const uint8_t *pcm_data, size_t pcm_length, size_t *written_bytes, bool final_chunk)
{
    if (written_bytes != NULL)
    {
        *written_bytes = 0;
    }
    if (speaker_ready == false || pcm_data == NULL || pcm_length < 2)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // TTS 返回 16 位小端单声道 PCM；丢弃不完整的最后一个字节，避免破坏采样边界。
    size_t usable_length = pcm_length - (pcm_length % 2);
    size_t total_input_samples = usable_length / 2;
    size_t input_offset = 0;
    size_t total_written = 0;
    while (input_offset < usable_length)
    {
        size_t remaining = usable_length - input_offset;
        size_t chunk_length = remaining;
        // 双声道输出每个输入采样需要四个字节。
        size_t max_input_length = sizeof(speaker_pcm_buffer) / 2;
        if (chunk_length > max_input_length)
        {
            chunk_length = max_input_length;
        }
        chunk_length -= chunk_length % 2;

        for (size_t input_index = 0; input_index < chunk_length; input_index += 2)
        {
            uint16_t raw_bits = (uint16_t)pcm_data[input_offset + input_index] |
                                ((uint16_t)pcm_data[input_offset + input_index + 1] << 8);
            int16_t raw_sample = (int16_t)raw_bits;
            uint32_t magnitude = raw_sample == INT16_MIN ? 32768U : (uint32_t)abs((int)raw_sample);
            if (magnitude > tts_pcm_peak)
            {
                tts_pcm_peak = magnitude;
            }
            if (magnitude >= 32000U)
            {
                tts_pcm_near_clip_count = tts_pcm_near_clip_count + 1;
            }

            unsigned long sample_position = tts_played_sample_count;
            const int volume_percent = speaker_muted == true ? 0 : (int)speaker_volume_percent;
            int32_t gain_q15 = (volume_percent * 32768L) / 100L;
            if (sample_position < TTS_FADE_IN_SAMPLES)
            {
                gain_q15 = gain_q15 * (int32_t)(sample_position + 1) / TTS_FADE_IN_SAMPLES;
            }
            // 只对真正的最后一个 TTS 包淡出，绝不对普通网络包淡出。
            // 即使最后一个网络包短于 20 ms，也必须完整淡出，避免直接截断。
            if (final_chunk == true && total_input_samples > 0)
            {
                size_t chunk_sample_position = (input_offset / 2) + (input_index / 2);
                size_t fade_sample_count = total_input_samples;
                if (fade_sample_count > TTS_FADE_OUT_SAMPLES)
                {
                    fade_sample_count = TTS_FADE_OUT_SAMPLES;
                }
                size_t fade_start = total_input_samples - fade_sample_count;
                if (chunk_sample_position >= fade_start)
                {
                    size_t fade_remaining = total_input_samples - chunk_sample_position;
                    if (fade_sample_count <= 1 || fade_remaining <= 1)
                    {
                        gain_q15 = 0;
                    }
                    else
                    {
                        gain_q15 = gain_q15 * (int32_t)(fade_remaining - 1) / (int32_t)(fade_sample_count - 1);
                    }
                }
            }
            int16_t output_sample = (int16_t)(((int32_t)raw_sample * gain_q15) / 32768L);
            tts_played_sample_count = sample_position + 1;

            uint16_t output_bits = (uint16_t)output_sample;
            // 计算当前输入采样在双声道输出缓冲区中的位置。
            size_t output_index = input_index * 2;
            // 把处理后的采样写入左声道。
            speaker_pcm_buffer[output_index] = (uint8_t)(output_bits & 0xFF);
            speaker_pcm_buffer[output_index + 1] = (uint8_t)(output_bits >> 8);
            // 把同一个采样写入右声道，兼容模块的声道选择配置。
            speaker_pcm_buffer[output_index + 2] = speaker_pcm_buffer[output_index];
            speaker_pcm_buffer[output_index + 3] = speaker_pcm_buffer[output_index + 1];
        }

        size_t chunk_written = 0;
        esp_err_t result = i2s_write(
            I2S_NUM_0,
            speaker_pcm_buffer,
            chunk_length * 2,
            &chunk_written,
            1000);
        total_written += chunk_written;
        if (result != ESP_OK)
        {
            if (written_bytes != NULL)
            {
                *written_bytes = total_written;
            }
            return result;
        }
        input_offset += chunk_length;
    }
    if (written_bytes != NULL)
    {
        *written_bytes = total_written;
    }
    return ESP_OK;
}

void play_speaker_test_tone(void)
{
    if (turn_busy == true || asr_active == true || tts_active == true)
    {
        Serial.println("当前语音任务进行中，暂不播放测试音");
        return;
    }
    setup_speaker();
    if (speaker_ready == false)
    {
        Serial.println("测试音失败：MAX98357A 未准备好");
        setup_microphone();
        return;
    }

    // 输出说明：本测试音含有完整淡入、淡出、静音和 DMA 排空过程。
    // 若它的尾部仍有爆音或持续滋声，根因优先指向功放供电或接线，不是云端 TTS。
    Serial.println("开始播放平滑收尾的 440 Hz 测试音，持续约 1 秒");
    // 每个立体声采样帧有左右两个 16 位采样，因此 256 帧共需要 512 个元素。
    int16_t samples[SPEAKER_TEST_TONE_FRAMES_PER_BLOCK * 2];
    // 保存正弦波当前相位，连续块之间不归零，避免块边界产生跳变。
    float phase = 0.0f;
    // 保存整个测试音已经生成的采样帧数量，用于计算首尾增益。
    unsigned long output_frame_count = 0;
    // 计算整个测试音的总采样帧数。
    const unsigned long total_frame_count =
        (unsigned long)SPEAKER_TEST_TONE_BLOCK_COUNT * SPEAKER_TEST_TONE_FRAMES_PER_BLOCK;
    // 每个数据块包含固定数量的立体声采样帧。
    for (int block = 0; block < SPEAKER_TEST_TONE_BLOCK_COUNT; block++)
    {
        // 逐帧生成左右声道相同的 440 Hz 正弦波。
        for (int frame = 0; frame < SPEAKER_TEST_TONE_FRAMES_PER_BLOCK; frame++)
        {
            // 默认使用满增益，Q15 的 32768 表示 100% 音量。
            int32_t gain_q15 = 32768;
            // 播放开始的 20 ms 内逐渐提高音量，避免启动爆音。
            if (output_frame_count < SPEAKER_TEST_TONE_FADE_SAMPLES)
            {
                gain_q15 = gain_q15 * (int32_t)(output_frame_count + 1) /
                           SPEAKER_TEST_TONE_FADE_SAMPLES;
            }
            // 播放结束的 20 ms 内逐渐降低音量，避免停止时硬切断波形。
            else if (output_frame_count >= total_frame_count - SPEAKER_TEST_TONE_FADE_SAMPLES)
            {
                // 计算从当前采样到测试音结束还剩多少采样帧。
                unsigned long remaining_frame_count = total_frame_count - output_frame_count;
                // 最后一帧强制变为零，确保后续静音数据可以无跳变衔接。
                if (remaining_frame_count <= 1)
                {
                    gain_q15 = 0;
                }
                else
                {
                    gain_q15 = gain_q15 * (int32_t)(remaining_frame_count - 1) /
                               (SPEAKER_TEST_TONE_FADE_SAMPLES - 1);
                }
            }
            // 先生成未缩放的标准正弦波采样。
            int16_t raw_sample = (int16_t)(sinf(phase) * SPEAKER_TEST_TONE_AMPLITUDE);
            // 按照淡入淡出增益缩放采样。
            int16_t sample = (int16_t)(((int32_t)raw_sample * gain_q15) / 32768L);
            // 写入左声道采样。
            samples[frame * 2] = sample;
            // 写入右声道采样，兼容 MAX98357A 的左右声道选择。
            samples[frame * 2 + 1] = sample;
            // 计算下一个采样的正弦波相位。
            phase += 2.0f * PI * SPEAKER_TEST_TONE_FREQUENCY_HZ / (float)SPEAKER_SAMPLE_RATE;
            // 相位超过完整周期后回绕，防止浮点数持续变大。
            if (phase >= 2.0f * PI)
            {
                phase -= 2.0f * PI;
            }
            // 当前采样帧已经写入，计数加一。
            output_frame_count = output_frame_count + 1;
        }
        // 保存本数据块写入 I2S DMA 的字节数。
        size_t written = 0;
        // 把当前数据块写入 I2S，由 DMA 按真实采样率发送到 MAX98357A。
        esp_err_t result = i2s_write(I2S_NUM_0, samples, sizeof(samples), &written, 1000);
        if (result != ESP_OK)
        {
            Serial.print("测试音 I2S 写入失败，错误码: ");
            Serial.println((int)result);
            break;
        }
    }
    // 计算 40 ms 双声道静音需要的字节数。
    const size_t silence_bytes = (SPEAKER_SAMPLE_RATE * TTS_TAIL_SILENCE_MS / 1000) * 4;
    // 写入全零 PCM，让最后一帧的零电平稳定保持一段时间。
    memset(speaker_pcm_buffer, 0, silence_bytes);
    // 保存静音数据写入 I2S DMA 的字节数。
    size_t silence_written = 0;
    // 发送尾部静音 PCM，保证与淡出后的零采样连续衔接。
    esp_err_t silence_result = i2s_write(
        I2S_NUM_0,
        speaker_pcm_buffer,
        silence_bytes,
        &silence_written,
        1000);
    // 静音写入失败时打印诊断信息，但继续执行安全关闭流程。
    if (silence_result != ESP_OK || silence_written != silence_bytes)
    {
        Serial.print("测试音尾部静音写入异常，错误码: ");
        Serial.print((int)silence_result);
        Serial.print("，写入字节: ");
        Serial.println((unsigned int)silence_written);
    }
    // 等待 I2S DMA 和 MAX98357A 把最后的静音数据全部输出。
    delay(TTS_DMA_DRAIN_WAIT_MS);
    stop_i2s();
    setup_microphone();
    Serial.println("测试音播放结束");
}

void stop_i2s(void)
{
    if (i2s_driver_ready == true)
    {
        i2s_zero_dma_buffer(I2S_NUM_0);
        delay(8);
    }
    mute_speaker_data_pin();
    delay(2);
    i2s_driver_uninstall(I2S_NUM_0);
    i2s_driver_ready = false;
    microphone_ready = false;
    speaker_ready = false;
}

void mute_speaker_data_pin(void)
{
    // MAX98357A 的 SD 固定拉高时，卸载 I2S 后让 DIN 保持确定的零电平。
    pinMode(PIN_I2S_SPK_DOUT, OUTPUT);
    digitalWrite(PIN_I2S_SPK_DOUT, LOW);
}

void play_english_ready_local(void)
{
    english_announce_pending = true;
}

void stop_english_ready_announce(bool start_listen)
{
    if (english_ready_file)
    {
        english_ready_file.close();
    }
    english_ready_playing = false;
    english_ready_remaining = 0;
    english_announce_pending = false;
    english_announce_active = false;
    if (speaker_ready == true || i2s_driver_ready == true)
    {
        stop_i2s();
        setup_microphone();
    }
    asr_active = false;
    asr_started = false;
    recording = false;
    tts_active = false;
    turn_busy = false;
    if (start_listen == true &&
        english_mode_enabled == true &&
        english_announce_abort == false)
    {
        draw_english_listen_prompt();
        request_voice_turn_forced("ENGLISH_READY");
    }
}

void start_english_ready_announce(void)
{
    const char *paths[] = {
        "/ui/english_ready.pcm",
        "/english_ready.pcm",
        "/ui/enlish_ready.pcm"
    };
    english_announce_pending = false;
    if (english_ready_file)
    {
        english_ready_file.close();
    }
    english_ready_playing = false;
    english_ready_remaining = 0;
    const char *used_path = NULL;
    for (size_t index = 0; index < 3; index++)
    {
        english_ready_file = SD.open(paths[index], FILE_READ);
        if (english_ready_file)
        {
            used_path = paths[index];
            break;
        }
    }
    if (!english_ready_file)
    {
        Serial.println("未找到本地开场白 /ui/english_ready.pcm");
        stop_english_ready_announce(true);
        return;
    }
    if (turn_busy == true || asr_active == true || tts_active == true)
    {
        english_ready_file.close();
        Serial.println("设备忙碌，跳过本地开场白");
        stop_english_ready_announce(true);
        return;
    }
    Serial.print("播放本地开场白: ");
    Serial.println(used_path);
    Serial.println("开场白音色: qwen-audio-3.0-tts-flash-longtongxuxian / happy");
    setup_speaker();
    if (speaker_ready == false)
    {
        english_ready_file.close();
        Serial.println("开场白失败：扬声器未准备好");
        stop_english_ready_announce(true);
        return;
    }
    tts_played_sample_count = 0;
    tts_pcm_peak = 0;
    english_ready_remaining = english_ready_file.size();
    english_ready_playing = true;
    english_announce_active = true;
}

void service_english_ready_announce(void)
{
    if (english_announce_abort == true || english_mode_enabled == false)
    {
        if (english_ready_playing == true || english_announce_pending == true)
        {
            Serial.println("英语开场白已打断，立即切回当前界面");
            stop_english_ready_announce(false);
        }
        if (english_mode_enabled == false)
        {
            english_announce_abort = false;
        }
        return;
    }
    if (english_ready_playing == false)
    {
        if (english_announce_pending == true)
        {
            start_english_ready_announce();
        }
        return;
    }
    // Push several chunks each loop so brief main-loop stalls do not underrun I2S.
    uint8_t chunk[2048];
    for (int burst = 0; burst < 4; burst = burst + 1)
    {
        size_t want = english_ready_remaining > sizeof(chunk) ? sizeof(chunk) : english_ready_remaining;
        if (want == 0)
        {
            delay(TTS_DMA_DRAIN_WAIT_MS);
            stop_english_ready_announce(true);
            return;
        }
        size_t got = english_ready_file.read(chunk, want);
        if (got == 0)
        {
            stop_english_ready_announce(true);
            return;
        }
        english_ready_remaining -= got;
        write_speaker_pcm(chunk, got, NULL, english_ready_remaining == 0);
        if (english_ready_remaining == 0)
        {
            delay(TTS_DMA_DRAIN_WAIT_MS);
            stop_english_ready_announce(true);
            return;
        }
    }
}

