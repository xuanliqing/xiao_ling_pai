import time, random
import paho.mqtt.client as mqtt

BROKER = "192.168.1.113"
PORT = 1883
DEVICE_IDS = ["dev001", "dev003", "dev004"]

client = mqtt.Client()
client.connect(BROKER, PORT, 60)

while True:
    for dev in DEVICE_IDS:
        payload = {
            "temperature": round(random.uniform(20, 30), 1),
            "humidity": round(random.uniform(40, 70), 1)
        }
        topic = f"liteos/{dev}/data"
        client.publish(topic, str(payload).replace("'", '"'))
    time.sleep(2)
