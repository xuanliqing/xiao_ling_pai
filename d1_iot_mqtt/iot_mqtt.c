/*
 * Copyright (c) 2021 FuZhou LOCKZHINER Electronic Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cJSON.h>
#include "ohos_init.h"
#include "los_task.h"
#include "oc_mqtt.h"
#include "MQTTClient.h"
#include <stdio.h>
#include <string.h>
#include "config_network.h"
#include "e53_intelligent_agriculture.h"

#define ROUTE_SSID              "oneleo"
#define ROUTE_PASSWORD          "88888888"
// #define ROUTE_SSID              "MATE40"
// #define ROUTE_PASSWORD          "88888888"

// #define ROUTE_SSID              "lab436"
// #define ROUTE_PASSWORD          "436436436"

#define IOT_MQTT_EXAMPLE                "iot_mqtt"

//服务器IP
#define MQTT_SERVER_IP                  "219.228.57.145" 

//MQTT端口
#define MQTT_SERVER_PORT                1883 

//Topic
// #define MQTT_TOPIC_PUB                  "liteos/sensor/data"
// #define MQTT_TOPIC_SUB                  "liteos/sensor/cmd"
#define MQTT_TOPIC_PUB "liteos/dev002/data"
#define MQTT_TOPIC_SUB "liteos/dev002/cmd"

static unsigned char sendBuf[1000];
static unsigned char readBuf[1000];

static Network network;
float temperature = 1; // 温度
float humidity = 60; // 湿度

MQTTString clientId = MQTTString_initializer;

// 接收回调函数
void message_receive(MessageData* data)
{
    printf("message arrived on topic %.*s: %.*s\n", 
           data->topicName->lenstring.len, data->topicName->lenstring.data,
           data->message->payloadlen, data->message->payload);

    // 提取 MQTT Payload
    char payload_str[100] = {0};
    if (data->message->payloadlen >= sizeof(payload_str)) {
        printf("Payload too long, ignore.\n");
        return;
    }
    
    // payload 预处理
    memcpy(payload_str, data->message->payload, data->message->payloadlen);
    payload_str[data->message->payloadlen] = '\0';

    int len = strlen(payload_str);
    if (len > 0 && (payload_str[len-1] == '\n' || payload_str[len-1] == '\r')) {
        payload_str[len-1] = '\0';
    }
    len = strlen(payload_str);
    if (len > 0 && (payload_str[len-1] == '\n' || payload_str[len-1] == '\r')) {
        payload_str[len-1] = '\0';
    }

    // 判断指令
    printf(payload_str);
    if (strcmp(payload_str, "light_on") == 0) 
    {
        printf("Command Received: Turn ON LED\n");
        light_set(ON);
        // motor_set_status(ON);
    }
    else if (strcmp(payload_str, "light_off") == 0) 
    {
        printf("Command Received: Turn OFF LED\n");
        light_set(OFF);
        // motor_set_status(OFF);
    }
    else 
    {
        printf("Unknown command: %s\n", payload_str);
    }
}

void iot_mqtt_thread()
{
    e53_ia_data_t data_env;

    e53_ia_init();

    int rc;
    MQTTClient client;
    MQTTMessage message;
    
    char payload[128]; 
    
    MQTTString clientId = MQTTString_initializer;
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;

    printf("Set WIFI\n");
    set_wifi_config_route_ssid(printf,   ROUTE_SSID);     	// 路由的WiFi名称
    set_wifi_config_route_passwd(printf, ROUTE_PASSWORD);   // 路由器WiFi密码
    printf("Set Wifi OK\n");

    /* WIFI连接 */
    SetWifiModeOn();

    printf("NetworkInit...\n");
    NetworkInit(&network);

begin:
    printf("NetworkConnect... IP: %s, Port: %d\n", MQTT_SERVER_IP, MQTT_SERVER_PORT);
    rc = NetworkConnect(&network, MQTT_SERVER_IP, MQTT_SERVER_PORT);

    //修改点: 设置 socket 为非阻塞模式
    int nb = 1;
    if (lwip_ioctl(network.my_socket, FIONBIO, &nb) != 0) {
        printf("set nonblocking failed, errno=%d\n", errno);
    }

    printf("MQTTClientInit...\n");
    MQTTClientInit(&client, &network, 2000, sendBuf, sizeof(sendBuf), readBuf, sizeof(readBuf));

    clientId.cstring = "ecnu_dev002";

    data.clientID          = clientId;
    data.willFlag          = 0;
    data.MQTTVersion       = 3;
    data.keepAliveInterval = 60;
    data.cleansession      = 1;

    printf("MQTTConnect...\n");
    rc = MQTTConnect(&client, &data);
    if (rc != 0) {
        printf("MQTTConnect fail:%d\n", rc);
        NetworkDisconnect(&network);
        MQTTDisconnect(&client);
        LOS_Msleep(2000);
        goto begin;
    }

    printf("MQTTSubscribe...\n");
    rc = MQTTSubscribe(&client, MQTT_TOPIC_SUB, 2, message_receive);
    if (rc != 0) {
        printf("MQTTSubscribe fail:%d\n", rc);
        LOS_Msleep(200);
        goto begin;
    }

    // 【新增】定义一个计数器，用来控制发送频率
    int loop_cnt = 0;

    while (1)
    {
        // -----------------------------------------------------------
        // 【核心修改点】: 
        // 以前是直接死睡 2000ms (LOS_Msleep)，导致这期间收不到指令。
        // 现在改为: 每 100ms 醒来一次，检查有没有收到 MQTT 指令。
        // -----------------------------------------------------------
        // printf("MQTTYield\n");
        MQTTYield(&client, 100); 
        
        // 计数器 +1
        loop_cnt++;
        // printf("%d",loop_cnt);
        // 如果还没满 20 次 (100ms * 20 = 2000ms = 2秒)，就跳过下面的发送逻辑，继续回去监听指令
        if (loop_cnt < 20) {
            continue;
        }

        // 满 2 秒了，重置计数器，准备发送数据
        loop_cnt = 0;

        // --- 以下是原本的业务逻辑 (读取 -> 打印 -> 发送) ---
        
        e53_ia_read_data(&data_env);

        printf("\nLuminance is %.2f\n", data_env.luminance);
        printf("\nHumidity is %.2f\n", data_env.humidity);
        printf("\nTemperature is %.2f\n", data_env.temperature);
        temperature = data_env.temperature;
        humidity = data_env.humidity;
        
        memset(payload, 0, sizeof(payload));
        // 这里建议顺便把 light_status 也加上，让网页能同步真实状态(前提是你定义了这个变量)
        sprintf(payload, "{\"temperature\": %.1f, \"humidity\": %.1f}", temperature, humidity);

        printf("Sending: %s\n", payload);

        message.qos = 1;
        message.retained = 0;
        message.payload = payload;
        message.payloadlen = strlen(payload);

        if ((rc = MQTTPublish(&client, MQTT_TOPIC_PUB, &message)) != 0){
            printf("Return code from MQTT publish is %d\n", rc);
            NetworkDisconnect(&network);
            MQTTDisconnect(&client);
            goto begin;
        }

        // 【核心修改点】: 
        // 这里的 LOS_Msleep(2000); 已经被删除了！
        // 因为时间已经通过上面的 MQTTYield 消耗掉了。
    }
}

void iot_mqtt_example()
{
    unsigned int threadID;
    unsigned int ret = LOS_OK;
    TSK_INIT_PARAM_S task = {0};

    task.pfnTaskEntry = (TSK_ENTRY_FUNC)iot_mqtt_thread;
    task.uwStackSize = 10240;
    task.pcName = IOT_MQTT_EXAMPLE;
    task.usTaskPrio = 6;
    ret = LOS_TaskCreate(&threadID, &task);
    if (LOS_OK != ret)
    {
        printf("Falied to create %s\n", IOT_MQTT_EXAMPLE);
        return;
    }
    printf("mqtt ok\n");
}

APP_FEATURE_INIT(iot_mqtt_example);