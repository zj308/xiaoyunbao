// Qwen request, SSE parsing and emotion mapping
// Shared state and declarations are kept in the main sketch.

String call_qwen(const String &text)
{
    // 每轮请求先恢复默认情绪，网络失败或标签异常时都能安全使用 neutral。
    current_emotion = "neutral";
    // 根据默认情绪初始化 TTS 参数，防止沿用上一轮情绪。
    set_tts_parameters_for_emotion(current_emotion);
    // 创建 TLS 客户端连接千问 HTTPS 接口。
    WiFiClientSecure client;
    // ESP32 端不保存根证书，继续沿用工程原有的不校验证书方式。
    client.setInsecure();
    // 创建本轮 HTTP 请求对象。
    HTTPClient http;
    // 使用 DashScope 文本生成接口；该接口通过请求头开启 SSE。
    String url = "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation";
    // 尝试建立 HTTPS 请求上下文。
    if (http.begin(client, url) == false)
    {
        // 连接上下文创建失败时发送默认情绪和容错回复。
        String fallback_reply = "网络连接失败，请稍后再试呀。";
        // 返回可以显示和播报的纯文本。
        return fallback_reply;
    }
    // 整个请求最多等待三十秒，避免网络异常时一直阻塞主循环。
    http.setTimeout(30000);
    // 声明请求体采用 JSON 格式。
    http.addHeader("Content-Type", "application/json");
    // 明确要求 DashScope 按 SSE 流式返回每个增量 chunk。
    http.addHeader("X-DashScope-SSE", "enable");
    // 生成 Bearer 鉴权请求头。
    String auth = "Bearer ";
    // 加入本地配置文件中的 API Key。
    auth += XIYUNBAO_DASHSCOPE_API_KEY;
    // 写入鉴权请求头。
    http.addHeader("Authorization", auth);
    // 组合 system、最近六轮历史和当前用户消息。
    String body = build_qwen_request_body(text);
    // 发出 POST；返回后响应正文仍由 read_qwen_sse_stream 逐块读取。
    int code = http.POST(body);
    // 非 200 状态表示服务端没有接受本轮流式生成请求。
    if (code != 200)
    {
        // 串口只打印状态码，不打印包含密钥的请求头或请求体。
        Serial.print("千问 HTTP 错误: ");
        Serial.println(code);
        // 结束 HTTP 连接并释放底层套接字。
        http.end();
        // 准备可正常显示和播报的容错回复。
        String fallback_reply = "我暂时无法回答，请稍后再试呀。";
        // 返回纯文本容错回复。
        return fallback_reply;
    }
    // 从 SSE 数据流逐个提取 content 增量并拼接完整原始回复。
    String raw_reply = read_qwen_sse_stream(&http);
    // 正文读取完成后关闭 HTTP 连接。
    http.end();
    // 为解析结果准备独立的情绪变量。
    String parsed_emotion = "neutral";
    // 为解析结果准备去掉标签后的纯文本变量。
    String parsed_reply = "";
    // 提取开头的 emotion 标签；缺失或非法时自动回退 neutral。
    parse_emotion_reply(raw_reply, &parsed_emotion, &parsed_reply);
    // 流水线未启动时沿用完整回复路径；已启动时情绪和句子已经逐句发送，不能重复。
    if (qwen_reply_streamed_to_tts == false)
    {
        current_emotion = parsed_emotion;
        set_tts_parameters_for_emotion(current_emotion);
    }
    // 只有真实取得千问回复时才写入上下文，网络容错文字不污染历史。
    if (raw_reply.length() > 0)
    {
        // 先保存本轮用户消息。
        append_qwen_history("user", text);
        // 再保存去掉控制标签的助手回复。
        append_qwen_history("assistant", parsed_reply);
    }
    // 屏幕与聊天历史只接收纯文本，不显示 emotion 和 SSML 标签。
    return parsed_reply;
}

String build_qwen_request_body(const String &user_text)
{
    // 创建请求 JSON 并预留空间，减少连续拼接造成的堆内存碎片。
    String body;
    // system 提示词和六轮历史通常小于八 KB。
    body.reserve(8192);
    // 写入模型和 messages 数组开头。
    body = "{\"model\":\"";
    // 使用 secrets.local.h 中配置的 qwen-turbo 模型名。
    body += XIYUNBAO_QWEN_MODEL;
    // 每次请求的第一条消息必须是固定 system 提示词。
    body += "\",\"input\":{\"messages\":[{\"role\":\"system\",\"content\":\"";
    // 对提示词中的换行和引号进行 JSON 转义。
    body += json_escape(String(QWEN_SYSTEM_PROMPT));
    // 结束 system 消息对象。
    body += "\"}";
    // 按原顺序加入最近六轮已完成对话。
    for (int history_index = 0; history_index < qwen_history_count; history_index = history_index + 1)
    {
        // 在上一条消息后加入数组分隔符并开始新的消息对象。
        body += ",{\"role\":\"";
        // 写入 user 或 assistant 角色。
        body += json_escape(qwen_history[history_index].role);
        // 开始写入历史消息正文。
        body += "\",\"content\":\"";
        // 转义历史正文，避免引号破坏 JSON 结构。
        body += json_escape(qwen_history[history_index].content);
        // 结束当前历史消息对象。
        body += "\"}";
    }
    // 在历史末尾加入本轮当前用户消息。
    body += ",{\"role\":\"user\",\"content\":\"";
    // 转义本轮识别文字。
    body += json_escape(user_text);
    // 结束消息数组，并开启流式增量输出。
    body += "\"}]},\"parameters\":{\"result_format\":\"message\",\"incremental_output\":true,\"max_tokens\":72}}";
    // 返回完整请求体。
    return body;
}

String read_qwen_sse_stream(HTTPClient *http)
{
    reset_qwen_stream_pipeline();
    // 保存所有 SSE chunk 拼接后的原始回复。
    String complete_reply = "";
    // 防止回复拼接频繁扩容。
    complete_reply.reserve(512);
    // 空指针无法读取流，直接返回空字符串交给上层容错。
    if (http == NULL)
    {
        return complete_reply;
    }
    // 获取 HTTPClient 管理的底层网络流。
    WiFiClient *stream = http->getStreamPtr();
    // 获取失败时直接返回空结果。
    if (stream == NULL)
    {
        return complete_reply;
    }
    // 保存当前正在接收的一行 SSE 文本。
    String line = "";
    // 为单个 JSON chunk 预留常用空间。
    line.reserve(1024);
    // 记录最后一次收到字节的时间。
    unsigned long last_data_time = millis();
    // 标记是否已经收到 [DONE] 或最终停止原因。
    bool stream_done = false;
    // 持续读取，直到服务端结束、连接断开或达到无数据超时。
    while (stream_done == false)
    {
        // 一次处理当前已经到达的全部字节。
        while (stream->available() > 0)
        {
            // 读取一个原始字节。
            char current_character = (char)stream->read();
            // 每收到一个字节就刷新无活动计时器。
            last_data_time = millis();
            // 换行符表示一条 SSE 行接收完整。
            if (current_character == '\n')
            {
                // 删除行首尾的空白和回车符。
                line.trim();
                // 只解析以 data: 开头的 SSE 数据行，忽略 event 和 chunk 长度行。
                if (line.startsWith("data:") == true)
                {
                    // 去掉 data: 前缀，保留后面的 JSON 或 [DONE]。
                    String data = line.substring(5);
                    // 删除 data: 后面的可选空格。
                    data.trim();
                    // [DONE] 表示本轮增量输出已经完整结束。
                    if (data == "[DONE]")
                    {
                        stream_done = true;
                    }
                    else
                    {
                        // 从当前 JSON chunk 中提取本次新增的 content。
                        String chunk_text = json_string(data, "content");
                        // 只拼接非空增量，避免空状态帧影响回复。
                        if (chunk_text.length() > 0)
                        {
                            complete_reply += chunk_text;
                            consume_qwen_stream_chunk(chunk_text);
                        }
                        // 兼容旧版 DashScope 在最终 chunk 中使用 finish_reason=stop 结束。
                        if (data.indexOf("\"finish_reason\":\"stop\"") >= 0)
                        {
                            stream_done = true;
                        }
                    }
                }
                // 当前行处理完成，清空后接收下一行。
                line = "";
                // SSE 数据持续到达时内层循环可能长时间不退出；每处理一行就驱动一次
                // TTS，保证握手、短句提交和 PCM 回调真正与千问生成并行。
                if (tts_active == true)
                {
                    tts_socket.loop();
                    pump_tts_text_queue();
                }
            }
            // 忽略回车符，其他字节原样加入当前行。
            else if (current_character != '\r')
            {
                line += current_character;
            }
        }
        // SSE 已明确结束时立即跳出外层循环。
        if (stream_done == true)
        {
            break;
        }
        // HTTP 已断开且缓冲区没有剩余数据时结束读取。
        if (http->connected() == false && stream->available() == 0)
        {
            break;
        }
        // 连续三十秒没有任何新字节时执行超时容错。
        if (millis() - last_data_time > QWEN_STREAM_INACTIVITY_TIMEOUT_MS)
        {
            Serial.println("\n千问 SSE 数据超时");
            break;
        }
        // HTTP 读取是同步循环；这里必须主动驱动同一轮 TTS WebSocket。
        if (tts_active == true)
        {
            tts_socket.loop();
            pump_tts_text_queue();
        }
        update_chat_speech_sync();
        // 短暂让出 CPU，保证 WiFi、WebSocket 和播放任务可以运行。
        delay(1);
    }
    // SSE 结束时把没有句末标点的尾句也提交，然后只发送一次 finish-task。
    flush_qwen_stream_sentence_buffer();
    // 返回所有增量拼成的完整原始回复。
    return complete_reply;
}

void append_qwen_history(const String &role, const String &content)
{
    // 空消息不写入历史，避免浪费请求体空间。
    if (role.length() == 0 || content.length() == 0)
    {
        return;
    }
    // 历史满十二条时先删除最旧的一条。
    if (qwen_history_count >= QWEN_HISTORY_CAPACITY)
    {
        // 把后面的十一条依次向前移动一个位置。
        for (int history_index = 1; history_index < QWEN_HISTORY_CAPACITY; history_index = history_index + 1)
        {
            qwen_history[history_index - 1] = qwen_history[history_index];
        }
        // 移动完成后最后一个位置留给新消息。
        qwen_history_count = QWEN_HISTORY_CAPACITY - 1;
    }
    // 写入新消息角色。
    qwen_history[qwen_history_count].role = role;
    // 写入新消息纯文本。
    qwen_history[qwen_history_count].content = content;
    // 增加有效历史数量。
    qwen_history_count = qwen_history_count + 1;
}

bool is_valid_emotion(const String &emotion)
{
    // 直接兼容用户提示词中的五个标签；同时保留旧固件的两个历史别名。
    if (emotion == "happy") return true;
    if (emotion == "sad") return true;
    if (emotion == "shy") return true;
    if (emotion == "surprise") return true;
    if (emotion == "surprised") return true;
    if (emotion == "neutral") return true;
    if (emotion == "calm") return true;
    if (emotion == "angry") return true;
    if (emotion == "fear") return true;
    return false;
}

static bool is_emotion_bracket_body(const String &body)
{
    String value = body;
    value.trim();
    String lowered = value;
    lowered.toLowerCase();
    lowered.replace(" ", "");
    if (lowered.startsWith("emotion:") == true || lowered.startsWith("情绪:") == true)
    {
        return true;
    }
    value.toLowerCase();
    return is_valid_emotion(value);
}

String strip_emotion_markup(const String &text)
{
    String result = text;
    result.trim();
    for (int pass = 0; pass < 4; pass = pass + 1)
    {
        int full_tag = result.indexOf("【");
        if (full_tag >= 0 && full_tag <= 8)
        {
            int full_end = result.indexOf("】", full_tag);
            if (full_end > full_tag)
            {
                String body = result.substring(full_tag + String("【").length(), full_end);
                if (is_emotion_bracket_body(body) == true)
                {
                    result = result.substring(0, full_tag) + result.substring(full_end + String("】").length());
                    result.trim();
                    continue;
                }
            }
        }
        int ascii_tag = result.indexOf('[');
        if (ascii_tag >= 0 && ascii_tag <= 8)
        {
            int ascii_end = result.indexOf(']', ascii_tag);
            if (ascii_end > ascii_tag)
            {
                String body = result.substring(ascii_tag + 1, ascii_end);
                if (is_emotion_bracket_body(body) == true)
                {
                    result = result.substring(0, ascii_tag) + result.substring(ascii_end + 1);
                    result.trim();
                    continue;
                }
            }
        }
        int newline = result.indexOf('\n');
        String first = newline >= 0 ? result.substring(0, newline) : result;
        first.trim();
        String first_lower = first;
        first_lower.toLowerCase();
        first_lower.replace(" ", "");
        first_lower.replace(".", "");
        if (first_lower.startsWith("emotion:") ||
            first_lower.startsWith("theemotionis") ||
            first_lower.startsWith("emotiontag"))
        {
            result = newline >= 0 ? result.substring(newline + 1) : String("");
            result.trim();
            continue;
        }
        break;
    }
    return result;
}

void parse_emotion_reply(const String &raw_reply, String *emotion, String *reply)
{
    // 输出指针为空时无法安全写入结果。
    if (emotion == NULL || reply == NULL)
    {
        return;
    }
    // 默认使用 neutral，满足标签缺失时的容错要求。
    *emotion = "neutral";
    // 复制原始回复并删除首尾空白。
    String normalized_reply = raw_reply;
    normalized_reply.trim();
    // 查找规定的全角标签起点。
    String label_prefix = "【emotion:";
    // 查找规定的全角右括号。
    String label_suffix = "】";
    // 标签必须出现在回复开头。
    int label_start = normalized_reply.indexOf(label_prefix);
    // 从标签起点之后查找右括号。
    int label_end = normalized_reply.indexOf(label_suffix, label_prefix.length());
    // 只有完整且位于开头的标签才参与解析。
    if (label_start == 0 && label_end > label_prefix.length())
    {
        // 截取 emotion: 后面、右括号前面的标签值。
        String candidate_emotion = normalized_reply.substring(label_prefix.length(), label_end);
        // 去掉模型偶尔附带的空格。
        candidate_emotion.trim();
        // 只有五种合法情绪才覆盖 neutral。
        if (is_valid_emotion(candidate_emotion) == true)
        {
            *emotion = candidate_emotion;
        }
        // 无论标签值是否合法，都从显示和播报文本中删除控制标签。
        *reply = normalized_reply.substring(label_end + label_suffix.length());
    }
    else
    {
        // 没有标签时保留模型正文，并继续使用 neutral。
        *reply = normalized_reply;
    }
    // 删除标签后正文两端可能残留的空白。
    reply->trim();
    // 流为空时生成可显示、可播报的短容错文本。
    if (reply->length() == 0)
    {
        *reply = "嗯，我没有听懂，再说一次好吗？";
    }
}

void set_tts_parameters_for_emotion(const String &emotion)
{
    // neutral 是所有未知情况的默认参数。
    current_tts_rate = 0.88f;
    current_tts_pitch = 1.00f;
    // happy 只保留轻微情绪变化；降低语速，避免中文回复听起来过快。
    if (emotion == "happy")
    {
        current_tts_rate = 0.92f;
        current_tts_pitch = 1.05f;
    }
    // sad 使用较慢语速和较低音高。
    else if (emotion == "sad")
    {
        current_tts_rate = 0.82f;
        current_tts_pitch = 0.90f;
    }
    // surprise 也限制在接近标准语速，避免情绪标签导致整段语音过快。
    else if (emotion == "surprise" || emotion == "surprised")
    {
        current_tts_rate = 0.96f;
        current_tts_pitch = 1.08f;
    }
    // shy 使用略慢、柔和的语速；兼容旧代码的 calm 别名。
    else if (emotion == "shy" || emotion == "calm")
    {
        current_tts_rate = 0.92f;
        current_tts_pitch = 0.98f;
    }
    else if (emotion == "angry")
    {
        current_tts_rate = 0.84f;
        current_tts_pitch = 0.92f;
    }
    else if (emotion == "fear")
    {
        current_tts_rate = 0.80f;
        current_tts_pitch = 0.90f;
    }
    Serial.print("TTS 情绪参数: emotion=");
    Serial.print(emotion);
    Serial.print(", rate=");
    Serial.print(current_tts_rate, 2);
    Serial.print(", pitch=");
    Serial.println(current_tts_pitch, 2);
}

String xml_escape(const String &text)
{
    // 保存转义后的 XML 文本。
    String result = "";
    // 预留少量额外空间供实体字符串使用。
    result.reserve(text.length() + 32);
    // 逐字节处理 ASCII XML 特殊符号；UTF-8 中文字节会原样保留。
    for (int index = 0; index < text.length(); index = index + 1)
    {
        char current_character = text[index];
        if (current_character == '&') result += "&amp;";
        else if (current_character == '<') result += "&lt;";
        else if (current_character == '>') result += "&gt;";
        else if (current_character == '\"') result += "&quot;";
        else if (current_character == '\'') result += "&apos;";
        else result += current_character;
    }
    // 返回合法 XML 正文。
    return result;
}

String build_ssml_text(const String &reply)
{
    // SSML 必须由 speak 根标签包围。
    String ssml = "<speak>";
    // 为 break 标签预留额外堆空间。
    ssml.reserve(reply.length() + 256);
    // 按 UTF-8 字节扫描正文并在指定标点后插入停顿。
    int index = 0;
    while (index < reply.length())
    {
        // 中文句号使用三字节 UTF-8 字符。
        if (reply.startsWith("。", index) == true)
        {
            ssml += "。<break time=\"350ms\"/>";
            index = index + String("。").length();
        }
        // 中文逗号同样使用三字节 UTF-8 字符。
        else if (reply.startsWith("，", index) == true)
        {
            ssml += "，<break time=\"250ms\"/>";
            index = index + String("，").length();
        }
        // 英文句号后插入 350 毫秒停顿。
        else if (reply[index] == '.')
        {
            ssml += ".<break time=\"350ms\"/>";
            index = index + 1;
        }
        // 英文逗号后插入 250 毫秒停顿。
        else if (reply[index] == ',')
        {
            ssml += ",<break time=\"250ms\"/>";
            index = index + 1;
        }
        else
        {
            // 普通字符先做 XML 转义，再加入 SSML。
            String one_byte = reply.substring(index, index + 1);
            ssml += xml_escape(one_byte);
            index = index + 1;
        }
    }
    // 关闭 speak 根标签。
    ssml += "</speak>";
    // 返回完整 SSML 文本。
    return ssml;
}

String create_task_id(const char *prefix)
{
    // 使用四个硬件随机数生成三十二位十六进制任务编号。
    char random_text[33];
    // 每段固定八位并补零，确保任务编号长度稳定。
    snprintf(
        random_text,
        sizeof(random_text),
        "%08lx%08lx%08lx%08lx",
        (unsigned long)esp_random(),
        (unsigned long)esp_random(),
        (unsigned long)esp_random(),
        (unsigned long)esp_random());
    // 可选前缀只用于串口识别，不影响随机部分。
    String task_id = prefix == NULL ? "" : String(prefix);
    // 拼接随机编号。
    task_id += random_text;
    // 返回本轮唯一任务编号。
    return task_id;
}

String json_escape(const String &text)
{
    String result;
    for (int i = 0; i < text.length(); i = i + 1)
    {
        char c = text[i];
        if (c == '\\') result += "\\\\";
        else if (c == '"') result += "\\\"";
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else result += c;
    }
    return result;
}

String json_string(const String &json, const String &key)
{
    String marker = "\"" + key + "\":\"";
    int start = json.indexOf(marker);
    if (start < 0) return "";
    start += marker.length();
    String result;
    bool escaped = false;
    for (int i = start; i < json.length(); i = i + 1)
    {
        char c = json[i];
        if (escaped == true)
        {
            if (c == 'n') result += '\n';
            else if (c == 'r') result += '\r';
            else result += c;
            escaped = false;
        }
        else if (c == '\\') escaped = true;
        else if (c == '"') return result;
        else result += c;
    }
    return result;
}

String json_last_nonempty_string(const String &json, const String &key)
{
    // 生成需要查找的 JSON 字段标记。
    String marker = "\"" + key + "\":\"";
    // 保存最后找到的非空字段值。
    String last_value = "";
    // 保存下一次搜索的起始位置。
    int search_position = 0;
    // 循环查找 JSON 中所有同名字段。
    while (search_position >= 0)
    {
        // 从当前起点查找下一个字段标记。
        int marker_position = json.indexOf(marker, search_position);
        // 没有更多字段时结束循环。
        if (marker_position < 0)
        {
            break;
        }
        // 计算字段值的起始位置。
        int value_start = marker_position + marker.length();
        // 创建当前字段的临时字符串。
        String current_value = "";
        // 标记下一个字符是否为转义字符。
        bool escaped = false;
        // 逐字符读取当前字段内容。
        for (int index = value_start; index < json.length(); index = index + 1)
        {
            // 读取当前 JSON 字符。
            char current_character = json[index];
            // 处理转义字符。
            if (escaped == true)
            {
                // 还原换行符。
                if (current_character == 'n') current_value += '\n';
                // 还原回车符。
                else if (current_character == 'r') current_value += '\r';
                // 还原其他转义字符。
                else current_value += current_character;
                // 清除转义状态。
                escaped = false;
            }
            // 遇到反斜杠时等待解析下一个字符。
            else if (current_character == '\\')
            {
                escaped = true;
            }
            // 遇到结束引号时结束当前字段读取。
            else if (current_character == '"')
            {
                break;
            }
            // 把普通字符追加到当前字段。
            else
            {
                current_value += current_character;
            }
        }
        // 只有字段非空时才更新最后有效值。
        if (current_value.length() > 0)
        {
            last_value = current_value;
        }
        // 把下一次查找位置移动到当前字段之后。
        search_position = value_start + current_value.length() + 1;
    }
    // 返回最后一条非空字段值。
    return last_value;
}
