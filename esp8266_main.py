import network
import socket
import time
import urequests
import json
import machine
import os
from umqtt.simple import MQTTClient

# ===================== 基础配置 =====================
AP_SSID = "ESP8266-MQTT-Config"
AP_PWD = "12345678"
AP_IP_CFG = ("192.168.4.1", "255.255.255.0", "192.168.4.1")
IP_QUERY_INTERVAL = 300000  # 5分钟查询一次外网IP
CFG_FILE = "/cfg.json"

# 全局对象
wlan_sta = network.WLAN(network.STA_IF)
wlan_ap = network.WLAN(network.AP_IF)
mqtt_client = None
public_ip = "未获取"
last_ip_query = 0

# ===================== 文件读写替代NVS（ESP8266无nvs） =====================
def load_config():
    default_cfg = {
        "wifissid": "",
        "wifipwd": "",
        "mqttbroker": "broker.emqx.io",
        "mqttport": 1883,
        "clientid": f"esp8266_{time.ticks_ms()%10000}",
        "subtopic": "esp32/cfg/sub",
        "pubtopic": "esp32/cfg/pub"
    }
    try:
        with open(CFG_FILE, "r") as f:
            return json.load(f)
    except OSError:
        return default_cfg

def save_config(cfg):
    try:
        with open(CFG_FILE, "w") as f:
            json.dump(cfg, f)
    except Exception as e:
        print("保存配置失败:", e)

# ===================== 获取外网IP（多接口容错） =====================
def get_public_ip():
    urls = [
        "http://api.ipify.org",
        "http://icanhazip.com",
        "http://ifconfig.me/ip"
    ]
    for url in urls:
        try:
            resp = urequests.get(url, timeout=5)
            ip = resp.text.strip()
            resp.close()
            if ip:
                return ip
        except Exception:
            continue
    return "获取失败"

# ===================== MQTT 回调 & 重连 =====================
def mqtt_callback(topic, msg):
    print(f"收到 [{topic.decode()}] : {msg.decode()}")

def mqtt_connect(cfg):
    global mqtt_client
    client_id = cfg["clientid"]
    broker = cfg["mqttbroker"]
    port = cfg["mqttport"]
    try:
        mqtt_client = MQTTClient(client_id, broker, port=port)
        mqtt_client.set_callback(mqtt_callback)
        mqtt_client.connect()
        mqtt_client.subscribe(cfg["subtopic"].encode())
        mqtt_client.publish(cfg["pubtopic"].encode(), b"ESP8266上线，配置已加载")
        print("MQTT连接成功")
        return True
    except Exception as e:
        print("MQTT连接失败:", e)
        return False

def mqtt_reconnect(cfg):
    while True:
        if mqtt_connect(cfg):
            break
        time.sleep(2)

# ===================== AP配网Web服务 =====================
def web_server(cfg):
    addr = socket.getaddrinfo("0.0.0.0", 80)[0][-1]
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(addr)
    s.listen(1)
    print("Web配置页启动，访问：http://192.168.4.1")

    while True:
        try:
            conn, _ = s.accept()
            req = conn.recv(1024).decode()
            if not req:
                conn.close()
                continue

            # 保存配置接口 /save
            if "/save?" in req:
                query_str = req.split("?")[1].split(" ")[0]
                params = {}
                for kv in query_str.split("&"):
                    item = kv.split("=")
                    if len(item) == 2:
                        params[item[0]] = item[1]
                new_cfg = {
                    "wifissid": params.get("wifissid", ""),
                    "wifipwd": params.get("wifipwd", ""),
                    "mqttbroker": params.get("broker", "broker.emqx.io"),
                    "mqttport": int(params.get("port", "1883")),
                    "clientid": params.get("cid", f"esp8266_{time.ticks_ms()%10000}"),
                    "subtopic": params.get("sub", "esp32/cfg/sub"),
                    "pubtopic": params.get("pub", "esp32/cfg/pub")
                }
                save_config(new_cfg)
                resp = b'<h2>配置保存成功，3秒后重启...</h2>'
                conn.send(b'HTTP/1.1 200 OK\r\nContent-Type:text/html\r\nContent-Length:%d\r\n\r\n' % len(resp) + resp)
                conn.close()
                time.sleep(3)
                machine.reset()
                return

            # 主页配置页面
            sta_ip = wlan_sta.ifconfig()[0] if wlan_sta.isconnected() else "未联网"
            html = f"""HTTP/1.1 200 OK
Content-Type: text/html

<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP8266 WiFi+MQTT配置</title>
<style>
*{{box-sizing:border-box;}}
body{{font-size:16px;padding:15px;margin:0;font-family:Arial;}}
.box{{margin:10px 0;}}
label{{display:block;margin-bottom:3px;font-weight:bold;}}
input{{width:100%;padding:8px;font-size:16px;border:1px #ccc solid;border-radius:4px;}}
button{{margin-top:8px;padding:10px 25px;font-size:16px;background:#007bff;color:#fff;border:none;border-radius:4px;}}
.info{{margin-top:12px;padding:8px;background:#f5f5f5;border-radius:4px;font-size:14px;}}
</style>
</head>
<body>
<h2>WiFi & MQTT 参数配置</h2>
<form action="/save" method="GET">
<div class="box"><label>WiFi名称</label><input name="wifissid" value="{cfg['wifissid']}"></div>
<div class="box"><label>WiFi密码</label><input name="wifipwd" value="{cfg['wifipwd']}"></div>
<div class="box"><label>MQTT服务器</label><input name="broker" value="{cfg['mqttbroker']}"></div>
<div class="box"><label>MQTT端口</label><input name="port" type="number" value="{cfg['mqttport']}"></div>
<div class="box"><label>客户端ID</label><input name="cid" value="{cfg['clientid']}"></div>
<div class="box"><label>订阅主题</label><input name="sub" value="{cfg['subtopic']}"></div>
<div class="box"><label>发布主题</label><input name="pub" value="{cfg['pubtopic']}"></div>
<button type="submit">保存配置重启</button>
</form>
<div class="info">
WiFi:{cfg['wifissid']}<br>
MQTT:{cfg['mqttbroker']}:{cfg['mqttport']}<br>
内网IP:{sta_ip}<br>
外网IP:{public_ip}
</div>
</body>
</html>
"""
            conn.send(html.encode())
            conn.close()
        except Exception:
            conn.close()
            continue

# ===================== 开启AP模式 =====================
def start_ap_mode():
    wlan_ap.active(True)
    wlan_ap.ifconfig(AP_IP_CFG)
    wlan_ap.config(essid=AP_SSID, password=AP_PWD)
    print(f"AP热点开启：{AP_SSID} | 密码：{AP_PWD}")
    print(f"AP地址：{wlan_ap.ifconfig()[0]}")

# ===================== 连接STA WiFi =====================
def connect_sta_wifi(cfg):
    wlan_sta.active(True)
    wlan_sta.connect(cfg["wifissid"], cfg["wifipwd"])
    timeout = 0
    while not wlan_sta.isconnected() and timeout < 20:
        time.sleep(0.5)
        print(".", end="")
        timeout += 1
    if wlan_sta.isconnected():
        print(f"\nWiFi连接成功，IP:{wlan_sta.ifconfig()[0]}")
        return True
    else:
        print("\nWiFi连接超时失败")
        wlan_sta.active(False)
        return False

# ===================== 主逻辑入口 =====================
def main():
    global public_ip, last_ip_query, mqtt_client
    cfg = load_config()

    # 无WiFi配置 → 进入AP配网模式
    if len(cfg["wifissid"]) == 0:
        start_ap_mode()
        web_server(cfg)
        return

    # 尝试连接WiFi
    if not connect_sta_wifi(cfg):
        start_ap_mode()
        web_server(cfg)
        return

    # MQTT连接
    if not mqtt_connect(cfg):
        mqtt_reconnect(cfg)

    # 开机立即获取并上报外网IP
    public_ip = get_public_ip()
    last_ip_query = time.ticks_ms()
    print("开机外网IP:", public_ip)
    ip_payload = json.dumps({
        "localIP": wlan_sta.ifconfig()[0],
        "publicIP": public_ip
    })
    mqtt_client.publish((cfg["pubtopic"] + "/ip").encode(), ip_payload.encode())

    # 常驻主循环
    while True:
        try:
            mqtt_client.check_msg()
        except Exception:
            mqtt_reconnect(cfg)

        # 定时5分钟刷新外网IP并上报
        if time.ticks_diff(time.ticks_ms(), last_ip_query) >= IP_QUERY_INTERVAL:
            last_ip_query = time.ticks_ms()
            public_ip = get_public_ip()
            print("更新外网IP:", public_ip)
            ip_payload = json.dumps({
                "localIP": wlan_sta.ifconfig()[0],
                "publicIP": public_ip
            })
            mqtt_client.publish((cfg["pubtopic"] + "/ip").encode(), ip_payload.encode())

        time.sleep(0.2)

if __name__ == "__main__":
    try:
        main()
    except Exception as err:
        print("程序异常重启：", err)
        time.sleep(2)
        machine.reset()