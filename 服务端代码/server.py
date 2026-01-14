import json
import threading
import paho.mqtt.client as mqtt
from flask import Flask, jsonify, render_template, request
import pymysql

# --- 配置 ---
MQTT_BROKER = "127.0.0.1"
MQTT_PORT = 1883
# 【关键】订阅通配符 Topic，适配 dev001, dev002...
MQTT_TOPIC_SUB_PATTERN = "liteos/+/data"  

DB_HOST = "localhost"
DB_USER = "root"
DB_PASSWORD = "eyjafjalla1018" # 【请填入你的密码】
DB_NAME = "liteos_iot"

# --- 全局存储 (多设备核心) ---
# 结构: { "dev001": {"temp": 25, "hum": 60, "light_on": False} }
devices_store = {} 

app = Flask(__name__)
mqtt_client = None

# --- 数据库写入 ---
def save_to_mysql(device_id, temp, hum):
    conn = None
    try:
        conn = pymysql.connect(
            host=DB_HOST, user=DB_USER, password=DB_PASSWORD, database=DB_NAME,
            charset='utf8mb4', cursorclass=pymysql.cursors.DictCursor
        )
        with conn.cursor() as cursor:
            # 插入带 device_id 的数据
            sql = "INSERT INTO sensor_records (device_id, temperature, humidity) VALUES (%s, %s, %s)"
            cursor.execute(sql, (device_id, temp, hum))
        conn.commit()
        print(f"[{device_id}] 数据已存库")
    except Exception as e:
        print(f"SQL出错: {e}")
    finally:
        if conn: conn.close()

# --- MQTT 回调 ---
def on_connect(client, userdata, flags, rc):
    print(f"MQTT Connected code {rc}")
    client.subscribe(MQTT_TOPIC_SUB_PATTERN)

def on_message(client, userdata, msg):
    try:
        topic = msg.topic  # 例如: liteos/dev001/data
        payload = msg.payload.decode()
        data = json.loads(payload)
        
        # 【关键】从 Topic 解析设备ID
        parts = topic.split('/')
        if len(parts) >= 3:
            device_id = parts[1] 
        else:
            device_id = "unknown"

        temp = data.get("temperature", 0)
        hum = data.get("humidity", 0)
        
        # 初始化新设备
        if device_id not in devices_store:
            devices_store[device_id] = {"light_on": False} 

        # 更新状态
        devices_store[device_id]["temperature"] = temp
        devices_store[device_id]["humidity"] = hum
        
        print(f"收到设备[{device_id}]数据: {temp}℃, {hum}%")
        threading.Thread(target=save_to_mysql, args=(device_id, temp, hum)).start()
        
    except Exception as e:
        print(f"消息处理错误: {e}")

def start_mqtt():
    global mqtt_client
    mqtt_client = mqtt.Client()
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        mqtt_client.loop_forever()
    except Exception as e:
        print(f"MQTT连接失败: {e}")

# --- Web 接口 ---

@app.route('/')
def index():
    return render_template('index.html')

# 【关键接口】前端获取设备列表
@app.route('/api/devices')
def get_devices():
    return jsonify(devices_store)

# 控制指令
@app.route('/api/control', methods=['POST'])
def control_device():
    try:
        req = request.get_json()
        device_id = req.get('device_id')
        action = req.get('action')
        
        if not device_id or device_id not in devices_store:
            return jsonify({"status": "error", "message": "设备不存在"}), 404

        # 发送纯字符串指令
        topic_cmd = f"liteos/{device_id}/cmd"
        cmd_payload = "light_on" if action == "on" else "light_off"

        if mqtt_client:
            print(f"发送指令给 [{device_id}]: {cmd_payload}")
            mqtt_client.publish(topic_cmd, cmd_payload)
            devices_store[device_id]['light_on'] = (action == "on")
            return jsonify({"status": "success"})
        else:
            return jsonify({"status": "error", "message": "MQTT未连接"}), 500

    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# 历史数据
@app.route('/api/history')
def get_history():
    device_id = request.args.get('device_id')
    date_str = request.args.get('date')
    
    if not device_id or not date_str:
        return jsonify({"error": "Params missing"}), 400

    conn = None
    try:
        conn = pymysql.connect(
            host=DB_HOST, user=DB_USER, password=DB_PASSWORD, database=DB_NAME,
            charset='utf8mb4', cursorclass=pymysql.cursors.DictCursor
        )
        with conn.cursor() as cursor:
            # 双 % 转义
            sql = """
                SELECT temperature, DATE_FORMAT(created_at, '%%H:%%i:%%s') as time_str 
                FROM sensor_records 
                WHERE DATE(created_at) = %s AND device_id = %s
                ORDER BY created_at ASC
            """
            cursor.execute(sql, (date_str, device_id))
            results = cursor.fetchall()
            return jsonify({
                "times": [r['time_str'] for r in results],
                "temps": [r['temperature'] for r in results]
            })
    except Exception as e:
        return jsonify({"error": str(e)}), 500
    finally:
        if conn: conn.close()

if __name__ == '__main__':
    mqtt_thread = threading.Thread(target=start_mqtt)
    mqtt_thread.daemon = True
    mqtt_thread.start()
    print("服务已启动: http://localhost:5000")
    app.run(host='0.0.0.0', port=5000, debug=False)