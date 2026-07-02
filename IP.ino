#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <HTTPClient.h>

//==================== AP热点配置 ====================
const char apSSID[]     = "ESP32-MQTT-Config";
const char apPassword[] = "12345678";
IPAddress apIP(192,168,4,1);
IPAddress apGateway(192,168,4,1);
IPAddress apSubnet(255,255,255,0);

WebServer server(80);
Preferences prefs;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
HTTPClient http;

//==================== 配置参数 ====================
String cfgWifiSsid;
String cfgWifiPwd;
String cfgMqttBroker;
int    cfgMqttPort;
String cfgMqttClientId;
String cfgSubTopic;
String cfgPubTopic;

String publicIP = "未获取";
unsigned long ipQueryTimer = 0;
const unsigned long IP_QUERY_INTERVAL = 300000; // 5分钟查询一次外网IP

//==================== 读写存储 ====================
void loadConfig() {
  prefs.begin("mqtt_cfg", false);
  cfgWifiSsid     = prefs.getString("wifissid", "");
  cfgWifiPwd      = prefs.getString("wifipwd", "");
  cfgMqttBroker   = prefs.getString("mqttbroker", "broker.emqx.io");
  cfgMqttPort     = prefs.getInt("mqttport", 1883);
  cfgMqttClientId = prefs.getString("clientid", "esp32_" + String(random(9999)));
  cfgSubTopic     = prefs.getString("subtopic", "esp32/c7ccc7dc/sub");
  cfgPubTopic     = prefs.getString("pubtopic", "esp32/c7ccc7dc/pub");
  prefs.end();
}

void saveConfig(String wifissid, String wifipwd, String broker, int port, String cid, String sub, String pub) {
  prefs.begin("mqtt_cfg", false);
  prefs.putString("wifissid", wifissid);
  prefs.putString("wifipwd", wifipwd);
  prefs.putString("mqttbroker", broker);
  prefs.putInt("mqttport", port);
  prefs.putString("clientid", cid);
  prefs.putString("subtopic", sub);
  prefs.putString("pubtopic", pub);
  prefs.end();

  cfgWifiSsid     = wifissid;
  cfgWifiPwd      = wifipwd;
  cfgMqttBroker   = broker;
  cfgMqttPort     = port;
  cfgMqttClientId = cid;
  cfgSubTopic     = sub;
  cfgPubTopic     = pub;
}

//==================== 获取外网IP ====================
String getPublicIP() {
  if(WiFi.status() != WL_CONNECTED) return "WiFi未连接";
  String ipRes = "";
  http.begin("http://icanhazip.com");
  int httpCode = http.GET();
  if(httpCode == HTTP_CODE_OK) {
    ipRes = http.getString();
    ipRes.trim(); // 去除换行空格
  } else {
    ipRes = "获取失败";
  }
  http.end();
  return ipRes;
}

//==================== MQTT回调 ====================
bool send_state;
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("收到消息 [");
  Serial.print(topic);
  Serial.print("]: ");
  String msg;
  for(int i=0; i<length; i++) msg += (char)payload[i];
  Serial.println(msg);
  send_state=true;
}

boolean mqttReconnect() {
  if(mqttClient.connect(cfgMqttClientId.c_str())) {
    mqttClient.subscribe(cfgSubTopic.c_str());
    mqttClient.publish(cfgPubTopic.c_str(), "ESP32上线，配置已加载");
    Serial.println("MQTT连接成功");
    return true;
  }
  Serial.print("MQTT连接失败 code:");
  Serial.println(mqttClient.state());
  return false;
}

//==================== 网页页面 ====================
void handleRoot() {
  String html = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 WiFi+MQTT配置页</title>
<style>
*{box-sizing:border-box;}
body{font-size:16px;padding:20px;margin:0;font-family:Arial;}
.box{margin:12px 0;}
label{display:block;margin-bottom:4px;font-weight:bold;}
input{width:100%;padding:9px;font-size:16px;border:1px #ccc solid;border-radius:4px;}
button{margin-top:10px;padding:12px 30px;font-size:17px;background:#007bff;color:#fff;border:none;border-radius:4px;cursor:pointer;}
.info{margin-top:15px;padding:10px;background:#f5f5f5;border-radius:4px;}
</style>
</head>
<body>
<h2>WiFi & MQTT 参数配置</h2>
<form action="/save" method="GET">
  <div class="box">
    <label>WiFi名称</label>
    <input name="wifissid" value=")HTML" + cfgWifiSsid + R"HTML(">
  </div>
  <div class="box">
    <label>WiFi密码</label>
    <input name="wifipwd" value=")HTML" + cfgWifiPwd + R"HTML(">
  </div>
  <div class="box">
    <label>MQTT服务器地址</label>
    <input name="broker" value=")HTML" + cfgMqttBroker + R"HTML(">
  </div>
  <div class="box">
    <label>MQTT端口</label>
    <input name="port" type="number" value=")HTML" + String(cfgMqttPort) + R"HTML(">
  </div>
  <div class="box">
    <label>MQTT客户端ID</label>
    <input name="cid" value=")HTML" + cfgMqttClientId + R"HTML(">
  </div>
  <div class="box">
    <label>订阅主题</label>
    <input name="sub" value=")HTML" + cfgSubTopic + R"HTML(">
  </div>
  <div class="box">
    <label>发布主题</label>
    <input name="pub" value=")HTML" + cfgPubTopic + R"HTML(">
  </div>
  <button type="submit">保存配置并重启</button>
</form>
<div class="info">
当前已保存配置：<br>
WiFi: )HTML" + cfgWifiSsid + R"HTML(<br>
MQTT: )HTML" + cfgMqttBroker + ":" + String(cfgMqttPort) + R"HTML(<br>
内网IP: )HTML" + ((WiFi.status()==WL_CONNECTED)?WiFi.localIP().toString():"未联网") + R"HTML(<br>
外网IP: )HTML" + publicIP + R"HTML(
</div>
</body>
</html>
)HTML";
  server.send(200, "text/html", html);
}

void handleSave() {
  String wifissid = server.arg("wifissid");
  String wifipwd  = server.arg("wifipwd");
  String broker   = server.arg("broker");
  int port        = server.arg("port").toInt();
  String cid      = server.arg("cid");
  String sub      = server.arg("sub");
  String pub      = server.arg("pub");

  saveConfig(wifissid, wifipwd, broker, port, cid, sub, pub);

  String page = "<h2>配置保存成功，设备3秒后重启...</h2>";
  server.send(200, "text/html", page);
  delay(3000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  loadConfig();

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apGateway, apSubnet);
  WiFi.softAP(apSSID, apPassword);
  Serial.println("======== AP模式开启 ========");
  Serial.print("热点名："); Serial.println(apSSID);
  Serial.print("密码："); Serial.println(apPassword);
  Serial.print("访问地址：http://"); Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.begin();

  if(cfgWifiSsid.length() > 0) {
    Serial.println("\n检测到已有WiFi配置，切换STA连接...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfgWifiSsid.c_str(), cfgWifiPwd.c_str());

    int timeout = 0;
    while(WiFi.status() != WL_CONNECTED && timeout < 20) {
      delay(500);
      Serial.print(".");
      timeout++;
    }

    if(WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi连接成功 IP:" + WiFi.localIP().toString());
      mqttClient.setServer(cfgMqttBroker.c_str(), cfgMqttPort);
      mqttClient.setCallback(mqttCallback);
      send_state=true;

      // 开机立刻获取一次外网IP并上报
      publicIP = getPublicIP();
      Serial.print("开机外网IP：");
      Serial.println(publicIP);
    } else {
      Serial.println("\nWiFi连接失败，停留在AP配置模式");
      WiFi.mode(WIFI_AP);
      WiFi.softAPConfig(apIP, apGateway, apSubnet);
      WiFi.softAP(apSSID, apPassword);
    }
  }
}

void loop() {
  server.handleClient();

  if(WiFi.status() == WL_CONNECTED) {
    if(!mqttClient.connected()) {
      mqttReconnect();
      delay(2000);
    }
    mqttClient.loop();

    // 定时获取外网IP并发布
    //if(millis() - ipQueryTimer >= IP_QUERY_INTERVAL) {
    if(send_state==true)
    {  
      ipQueryTimer = millis();
      publicIP = getPublicIP();
      Serial.print("更新外网IP: ");
      Serial.println(publicIP);
      // 发布外网IP到MQTT
      String ipMsg = "{\"localIP\":\""+WiFi.localIP().toString()+"\",\"publicIP\":\""+publicIP+"\"}";
      mqttClient.publish((cfgPubTopic+"/ip").c_str(), ipMsg.c_str());
      send_state=false;
    }

    // 原有定时心跳
    //static unsigned long pubTick = 0;
    //if(millis() - pubTick > 10000) {
    //  pubTick = millis();
    //  mqttClient.publish(cfgPubTopic.c_str(), "ESP32定时上报在线");
    //}
  }
}