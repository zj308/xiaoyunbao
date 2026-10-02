// 手机配网：设备开热点，手机打开 192.168.4.1 填写家里 Wi-Fi 或热点。
// 凭据写入 NVS，固件不再依赖 secrets.local.h 里的固定网络名称和密码。

#ifndef WIFI_PROVISION_AP_PASSWORD
#define WIFI_PROVISION_AP_PASSWORD "xiaoyunbao"
#endif
#ifndef WIFI_PROVISION_CONNECT_TIMEOUT_MS
#define WIFI_PROVISION_CONNECT_TIMEOUT_MS 25000
#endif

enum WifiProvisionPhase
{
    WIFI_PROVISION_IDLE = 0,
    WIFI_PROVISION_PORTAL,
    WIFI_PROVISION_CONNECTING
};

static Preferences wifi_provision_prefs;
static WebServer wifi_provision_server(80);
static DNSServer wifi_provision_dns;
static WifiProvisionPhase wifi_provision_phase = WIFI_PROVISION_IDLE;
static unsigned long wifi_provision_connect_start = 0;
static String wifi_provision_status_message = "";
static bool wifi_provision_handlers_ready = false;
static bool wifi_provision_connect_pending = false;

static String wifi_json_escape(const String &input)
{
    String output;
    output.reserve(input.length() + 8);
    for (size_t index = 0; index < input.length(); index++)
    {
        const char item = input[index];
        if (item == '"' || item == '\\')
        {
            output += '\\';
        }
        if (item >= 32 || item == '\t')
        {
            output += item;
        }
    }
    return output;
}

static String wifi_url_decode(const String &input)
{
    String output;
    output.reserve(input.length());
    for (size_t index = 0; index < input.length(); index++)
    {
        const char item = input[index];
        if (item == '+')
        {
            output += ' ';
            continue;
        }
        if (item == '%' && index + 2 < input.length())
        {
            char hex[3] = {input[index + 1], input[index + 2], 0};
            output += (char)strtol(hex, NULL, 16);
            index += 2;
            continue;
        }
        output += item;
    }
    return output;
}

static bool wifi_secret_ssid_usable(void)
{
#ifdef XIYUNBAO_WIFI_SSID
    const char *ssid = XIYUNBAO_WIFI_SSID;
    if (ssid[0] == 0)
    {
        return false;
    }
    if (strcmp(ssid, "你的WiFi名称") == 0)
    {
        return false;
    }
    return true;
#else
    return false;
#endif
}

void wifi_load_credentials(void)
{
    wifi_provision_prefs.begin("xybwifi", true);
    wifi_sta_ssid = wifi_provision_prefs.getString("ssid", "");
    wifi_sta_password = wifi_provision_prefs.getString("pass", "");
    wifi_provision_prefs.end();
    if (wifi_sta_ssid.length() == 0 && wifi_secret_ssid_usable() == true)
    {
        wifi_sta_ssid = XIYUNBAO_WIFI_SSID;
#ifdef XIYUNBAO_WIFI_PASSWORD
        wifi_sta_password = XIYUNBAO_WIFI_PASSWORD;
#endif
        Serial.println("NVS 尚无配网记录，先使用编译期网络作为一次性回退");
    }
}

bool wifi_credentials_available(void)
{
    return wifi_sta_ssid.length() > 0;
}

void wifi_save_credentials(const String &ssid, const String &password)
{
    wifi_sta_ssid = ssid;
    wifi_sta_password = password;
    wifi_provision_prefs.begin("xybwifi", false);
    wifi_provision_prefs.putString("ssid", wifi_sta_ssid);
    wifi_provision_prefs.putString("pass", wifi_sta_password);
    wifi_provision_prefs.end();
    Serial.print("已保存手机配网网络: ");
    Serial.println(wifi_sta_ssid);
}

void wifi_clear_credentials(void)
{
    wifi_sta_ssid = "";
    wifi_sta_password = "";
    wifi_provision_prefs.begin("xybwifi", false);
    wifi_provision_prefs.clear();
    wifi_provision_prefs.end();
    Serial.println("已清除保存的 Wi-Fi");
}

void wifi_build_ap_ssid(void)
{
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    wifi_ap_ssid = "XiaoYunBao-";
    wifi_ap_ssid += suffix;
}

static void draw_provision_centered_line(int y, const String &text, uint16_t color)
{
    chinese_font.setFont(u8g2_font_wqy16_t_gb2312);
    chinese_font.setFontMode(0);
    chinese_font.setBackgroundColor(ST77XX_BLACK);
    chinese_font.setForegroundColor(color);
    int width = chinese_font.getUTF8Width(text.c_str());
    int x = (240 - width) / 2;
    if (x < 6)
    {
        x = 6;
    }
    chinese_font.drawUTF8(x, y, text.c_str());
}

void draw_wifi_provision_screen(void)
{
    if (screen_ready == false)
    {
        return;
    }
    digitalWrite(PIN_SD_CS, HIGH);
    display.fillScreen(ST77XX_BLACK);
    chinese_font.setFontMode(0);
    if (wifi_provision_phase == WIFI_PROVISION_CONNECTING)
    {
        draw_provision_centered_line(110, "正在连接", ST77XX_WHITE);
        if (wifi_sta_ssid.length() > 0)
        {
            draw_provision_centered_line(150, wifi_sta_ssid, 0x07FF);
        }
        draw_provision_centered_line(200, "请稍候", ST77XX_WHITE);
    }
    else
    {
        draw_provision_centered_line(56, "连接网络", ST77XX_WHITE);
        draw_provision_centered_line(108, wifi_ap_ssid, 0x07FF);
        String password_line = "密码 ";
        password_line += WIFI_PROVISION_AP_PASSWORD;
        draw_provision_centered_line(148, password_line, ST77XX_WHITE);
        draw_provision_centered_line(200, "手机连接热点后打开", ST77XX_WHITE);
        draw_provision_centered_line(240, "192.168.4.1", 0x07FF);
    }
    chinese_font.setFontMode(1);
}

static const char WIFI_PROVISION_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>小云宝配网</title>
<style>
body{font-family:sans-serif;margin:16px;background:#111;color:#fff}
h1{font-size:20px}
.card{background:#1e2a44;border-radius:12px;padding:14px;margin:12px 0}
button,input{font-size:16px;padding:10px;border-radius:8px;border:0;width:100%;box-sizing:border-box}
input{margin:8px 0;background:#fff;color:#111}
button{background:#3d7eff;color:#fff;margin-top:8px}
button.secondary{background:#445}
.net{padding:10px 0;border-bottom:1px solid #334;display:flex;justify-content:space-between}
.ok{color:#8f8}.err{color:#f88}
</style>
</head>
<body>
<h1>小云宝 Wi-Fi 设置</h1>
<div class="card">
<p>选择家里的 Wi-Fi，或手动填写手机热点名称和密码。</p>
<p>同一部手机不能一边连配网热点、一边开个人热点。要连本机热点：先填写名称密码并连接，再断开本页、打开个人热点。</p>
<div id="list">正在扫描附近网络...</div>
<button class="secondary" onclick="scanNets()">重新扫描</button>
</div>
<div class="card">
<input id="ssid" placeholder="网络名称 / 热点名称" maxlength="32">
<input id="pass" type="password" placeholder="密码，开放网络可留空" maxlength="64">
<button onclick="saveNet()">连接此网络</button>
<button class="secondary" onclick="forgetNet()">忘记已保存网络</button>
<p id="msg"></p>
</div>
<script>
function pick(name){document.getElementById('ssid').value=name;}
function show(t,ok){var m=document.getElementById('msg');m.className=ok?'ok':'err';m.textContent=t;}
async function scanNets(){
  document.getElementById('list').textContent='正在扫描...';
  try{
    const r=await fetch('/scan'); const d=await r.json();
    if(!d.networks.length){document.getElementById('list').textContent='没有扫到网络，请手动填写。';return;}
    document.getElementById('list').innerHTML=d.networks.map(n=>
      '<div class="net" onclick="pick(\''+n.ssid.replace(/'/g,'')+'\')"><span>'+n.ssid+
      '</span><span>'+(n.secure?'加密 ':'开放 ')+n.rssi+'dBm</span></div>').join('');
  }catch(e){document.getElementById('list').textContent='扫描失败，请手动填写网络名称。';}
}
async function saveNet(){
  const ssid=document.getElementById('ssid').value.trim();
  const pass=document.getElementById('pass').value;
  if(!ssid){show('请填写网络名称',false);return;}
  show('正在提交...',true);
  const body='ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass);
  const r=await fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const d=await r.json();
  show(d.message||'已提交',d.ok);
  if(d.ok){pollStatus();}
}
async function forgetNet(){
  const r=await fetch('/forget',{method:'POST'});
  const d=await r.json();
  show(d.message||'已清除',d.ok);
}
async function pollStatus(){
  const timer=setInterval(async()=>{
    const r=await fetch('/status'); const d=await r.json();
    show(d.message,d.state==='connected');
    if(d.state==='connected'){clearInterval(timer);}
    if(d.state==='portal'&&d.message&&d.message.indexOf('失败')>=0){clearInterval(timer);}
  },1000);
}
scanNets();
</script>
</body>
</html>)HTML";

static void wifi_provision_send_portal(void)
{
    wifi_provision_server.sendHeader("Cache-Control", "no-cache");
    wifi_provision_server.send_P(200, "text/html; charset=utf-8", WIFI_PROVISION_PAGE);
}

static void wifi_provision_handle_scan(void)
{
    const int count = WiFi.scanNetworks(false, false);
    String json = "{\"networks\":[";
    int added = 0;
    for (int index = 0; index < count && added < 20; index++)
    {
        const String ssid = WiFi.SSID(index);
        if (ssid.length() == 0)
        {
            continue;
        }
        if (added > 0)
        {
            json += ',';
        }
        json += "{\"ssid\":\"";
        json += wifi_json_escape(ssid);
        json += "\",\"rssi\":";
        json += String(WiFi.RSSI(index));
        json += ",\"secure\":";
        json += (WiFi.encryptionType(index) == WIFI_AUTH_OPEN) ? "false" : "true";
        json += '}';
        added++;
    }
    json += "]}";
    WiFi.scanDelete();
    wifi_provision_server.send(200, "application/json", json);
}

static void wifi_provision_handle_status(void)
{
    String json = "{\"state\":\"";
    if (wifi_provision_phase == WIFI_PROVISION_CONNECTING)
    {
        json += "connecting";
    }
    else if (WiFi.status() == WL_CONNECTED && wifi_provision_active == false)
    {
        json += "connected";
    }
    else
    {
        json += "portal";
    }
    json += "\",\"ssid\":\"";
    json += wifi_json_escape(wifi_sta_ssid);
    json += "\",\"ip\":\"";
    if (WiFi.status() == WL_CONNECTED)
    {
        json += WiFi.localIP().toString();
    }
    json += "\",\"message\":\"";
    json += wifi_json_escape(wifi_provision_status_message);
    json += "\"}";
    wifi_provision_server.send(200, "application/json", json);
}

static void wifi_provision_begin_station_connect(void)
{
    wifi_provision_phase = WIFI_PROVISION_CONNECTING;
    wifi_provision_connect_start = millis();
    wifi_provision_status_message = "正在连接 " + wifi_sta_ssid;
    wifi_ready = false;
    wifi_connection_announced = false;
    WiFi.softAPdisconnect(true);
    wifi_provision_dns.stop();
    wifi_provision_server.stop();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.disconnect(false, false);
    delay(80);
    if (wifi_sta_password.length() == 0)
    {
        WiFi.begin(wifi_sta_ssid.c_str());
    }
    else
    {
        WiFi.begin(wifi_sta_ssid.c_str(), wifi_sta_password.c_str());
    }
    wifi_connection_start_time = millis();
    wifi_last_reconnect_attempt = millis();
    draw_wifi_provision_screen();
    Serial.print("手机配网开始连接: ");
    Serial.println(wifi_sta_ssid);
}

static void wifi_provision_handle_connect(void)
{
    String body = wifi_provision_server.arg("plain");
    if (body.length() == 0)
    {
        body = wifi_provision_server.arg("ssid").length()
            ? String("ssid=") + wifi_provision_server.arg("ssid") + "&pass=" + wifi_provision_server.arg("pass")
            : "";
    }
    String ssid;
    String password;
    int cursor = 0;
    while (cursor < (int)body.length())
    {
        const int amp = body.indexOf('&', cursor);
        const String pair = body.substring(cursor, amp < 0 ? body.length() : amp);
        const int eq = pair.indexOf('=');
        if (eq > 0)
        {
            const String key = wifi_url_decode(pair.substring(0, eq));
            const String value = wifi_url_decode(pair.substring(eq + 1));
            if (key == "ssid")
            {
                ssid = value;
            }
            else if (key == "pass")
            {
                password = value;
            }
        }
        if (amp < 0)
        {
            break;
        }
        cursor = amp + 1;
    }
    ssid.trim();
    if (ssid.length() == 0)
    {
        wifi_provision_server.send(200, "application/json",
            "{\"ok\":false,\"message\":\"网络名称不能为空\"}");
        return;
    }
    wifi_save_credentials(ssid, password);
    wifi_provision_connect_pending = true;
    wifi_provision_status_message = "已保存，设备正在连接该网络";
    wifi_provision_server.send(200, "application/json",
        "{\"ok\":true,\"message\":\"已保存，设备正在连接该网络\"}");
}

static void wifi_provision_handle_forget(void)
{
    wifi_clear_credentials();
    wifi_provision_status_message = "已清除保存的网络，请重新选择";
    wifi_provision_server.send(200, "application/json",
        "{\"ok\":true,\"message\":\"已清除保存的网络\"}");
}

static void wifi_provision_setup_handlers(void)
{
    if (wifi_provision_handlers_ready == true)
    {
        return;
    }
    wifi_provision_server.on("/", HTTP_GET, wifi_provision_send_portal);
    wifi_provision_server.on("/scan", HTTP_GET, wifi_provision_handle_scan);
    wifi_provision_server.on("/status", HTTP_GET, wifi_provision_handle_status);
    wifi_provision_server.on("/connect", HTTP_POST, wifi_provision_handle_connect);
    wifi_provision_server.on("/forget", HTTP_POST, wifi_provision_handle_forget);
    wifi_provision_server.on("/generate_204", HTTP_GET, wifi_provision_send_portal);
    wifi_provision_server.on("/gen_204", HTTP_GET, wifi_provision_send_portal);
    wifi_provision_server.on("/hotspot-detect.html", HTTP_GET, wifi_provision_send_portal);
    wifi_provision_server.on("/library/test/success.html", HTTP_GET, wifi_provision_send_portal);
    wifi_provision_server.on("/connecttest.txt", HTTP_GET, wifi_provision_send_portal);
    wifi_provision_server.on("/ncsi.txt", HTTP_GET, wifi_provision_send_portal);
    wifi_provision_server.on("/fwlink", HTTP_GET, wifi_provision_send_portal);
    wifi_provision_server.onNotFound(wifi_provision_send_portal);
    wifi_provision_handlers_ready = true;
}

void wifi_provision_begin(const char *reason)
{
    if (continuous_dialog_active == true)
    {
        stop_continuous_dialog("WIFI_PROVISION");
    }
    wifi_build_ap_ssid();
    wifi_provision_active = true;
    wifi_provision_phase = WIFI_PROVISION_PORTAL;
    wifi_provision_connect_pending = false;
    wifi_ready = false;
    wifi_connection_announced = false;
    wifi_provision_status_message = "请用手机连接配网热点";
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true, false);
    delay(80);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    const bool ap_ok = WiFi.softAP(wifi_ap_ssid.c_str(), WIFI_PROVISION_AP_PASSWORD, 1, 0, 4);
    wifi_provision_setup_handlers();
    wifi_provision_dns.start(53, "*", WiFi.softAPIP());
    wifi_provision_server.begin();
    stop_listen_animation();
    stop_greeting_animation();
    draw_wifi_provision_screen();
    Serial.print("进入手机配网: ");
    Serial.println(reason);
    Serial.print("请连接热点 ");
    Serial.print(wifi_ap_ssid);
    Serial.print(" 密码 ");
    Serial.print(WIFI_PROVISION_AP_PASSWORD);
    Serial.println("，浏览器打开 http://192.168.4.1");
    if (ap_ok == false)
    {
        Serial.println("配网热点启动失败，请复位后重试");
    }
}

void wifi_provision_finish_success(void)
{
    wifi_provision_active = false;
    wifi_provision_phase = WIFI_PROVISION_IDLE;
    wifi_provision_connect_pending = false;
    wifi_provision_status_message = "已连接 " + wifi_sta_ssid;
    wifi_ready = true;
    wifi_connection_announced = true;
    Serial.print("手机配网成功，IP=");
    Serial.println(WiFi.localIP());
    if (sd_ui_image_loaded == true)
    {
        restore_sd_ui_background();
    }
    start_greeting_animation();
    report_state("IDLE");
    draw_status("小云宝", "请唤醒我开始交流");
}

void wifi_start_station(void)
{
    if (wifi_credentials_available() == false)
    {
        wifi_provision_begin("NO_SAVED_WIFI");
        return;
    }
    wifi_provision_active = false;
    wifi_provision_phase = WIFI_PROVISION_IDLE;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    if (wifi_sta_password.length() == 0)
    {
        WiFi.begin(wifi_sta_ssid.c_str());
    }
    else
    {
        WiFi.begin(wifi_sta_ssid.c_str(), wifi_sta_password.c_str());
    }
    wifi_connection_start_time = millis();
    Serial.print("使用已保存网络后台连接: ");
    Serial.println(wifi_sta_ssid);
}

void service_wifi_provision(void)
{
    if (wifi_provision_phase == WIFI_PROVISION_PORTAL)
    {
        wifi_provision_dns.processNextRequest();
        wifi_provision_server.handleClient();
        if (wifi_provision_connect_pending == true)
        {
            wifi_provision_connect_pending = false;
            wifi_provision_begin_station_connect();
        }
        return;
    }
    if (wifi_provision_phase != WIFI_PROVISION_CONNECTING)
    {
        return;
    }
    if (WiFi.status() == WL_CONNECTED)
    {
        wifi_provision_finish_success();
        return;
    }
    if (millis() - wifi_provision_connect_start >= WIFI_PROVISION_CONNECT_TIMEOUT_MS)
    {
        Serial.println("手机配网连接超时，重新打开配网页");
        wifi_provision_status_message = "连接失败，请检查密码后重试";
        wifi_provision_begin("CONNECT_TIMEOUT");
    }
}
