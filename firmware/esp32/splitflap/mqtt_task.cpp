/*
   Copyright 2021 Scott Bezek and the splitflap contributors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#if MQTT
#include "mqtt_task.h"
#include "secrets.h"
#include <HTTPClient.h>
#include <json11.hpp>

using namespace json11;

MQTTTask::MQTTTask(SplitflapTask& splitflap_task, Logger& logger, const uint8_t task_core) :
        Task("MQTT", 8192, 1, task_core),
        splitflap_task_(splitflap_task),
        logger_(logger),
        wifi_client_(),
        mqtt_client_(wifi_client_) {
    auto callback = [this](char *topic, byte *payload, unsigned int length) { mqttCallback(topic, payload, length); };
    logger_.log("GRAHAM SET CALLBACK");

    timeKeeper = ESP32Time(-3600*5);

    mqtt_client_.setCallback(callback);
}

void MQTTTask::connectWifi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        logger_.log("Establishing connection to WiFi..");
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "Connected to network %s", WIFI_SSID);
    logger_.log(buf);
}

void MQTTTask::mqttCallback(char *topic, byte *payload, unsigned int length) {
    logger_.log("graham callback");
    char buf[256];
    snprintf(buf, sizeof(buf), "Received mqtt callback for topic %s, length %u", topic, length);
    logger_.log(buf);
    splitflap_task_.showString((const char *)payload, length);
}

void MQTTTask::connectMQTT() {
    char buf[256];
    mqtt_client_.setServer(MQTT_SERVER, 1883);
    logger_.log("Attempting MQTT connection...");
    logger_.log("Graham");
    if (mqtt_client_.connect(HOSTNAME "-" MQTT_USER, MQTT_USER, MQTT_PASSWORD)) {
        logger_.log("MQTT connected");
        bool bSubscribeSuccess = mqtt_client_.subscribe(MQTT_COMMAND_TOPIC);
        if (bSubscribeSuccess) {
            logger_.log("Graham Successfully subscribed");
        } else {
            logger_.log("Graham Failed to subscribe");
        }
        char buf[256];
        snprintf(buf, sizeof(buf), "{\"name\": \"%s\", \"command_topic\": \"%s\", \"state_topic\": \"%s\", \"unique_id\": \"%s\"}", HOSTNAME, MQTT_COMMAND_TOPIC, MQTT_COMMAND_TOPIC, HOSTNAME);
        mqtt_client_.publish("homeassistant/text/splitflap/config", buf);
        logger_.log("Published MQTT discovery message");
    } else {
        snprintf(buf, sizeof(buf), "MQTT failed rc=%d will try again in 5 seconds", mqtt_client_.state());
        logger_.log(buf);
    }
}

void MQTTTask::run() {
    connectWifi();
    connectMQTT();
    fetchTime();

    while(1) {
        long now = millis();
        if (!mqtt_client_.connected() && (now - mqtt_last_connect_time_) > 5000) {
            logger_.log("Reconnecting MQTT");
            mqtt_last_connect_time_ = now;
            connectMQTT();
        } 

        if (mqtt_client_.connected() && (now - last_time_check) > 3600000) {
            last_time_check = now;
            fetchTime();
        }

        mqtt_client_.loop();

        String updatedTime = timeKeeper.getTime();
        if (updatedTime != CurrentTime) {
            CurrentTime = updatedTime;
            //logger_.log(CurrentTime.c_str());
            char TimeString[7];
            int count = 5;
            const char* TimeArray = CurrentTime.c_str();
            for(int i = 0; i < 8; ++i) {
                char c = TimeArray[i];
                if (c != ':' && count>=0){
                    TimeString[count] = c;

                    --count;
                }

            }
            TimeString[6] = '\0';

            //logger_.log(TimeString);

            splitflap_task_.showString((const char *)TimeString, 6, false);
        }

        delay(1);
    }
}

bool MQTTTask::fetchTime() {
    char buf[200];
    uint32_t start = millis();
    HTTPClient http;

    http.begin("http://worldtimeapi.org/api/timezone/America/New_York");
    http.addHeader("Accept", "application/json");

    // Send the request as a GET
    logger_.log("Sending request for Time from worldtimeapi.org");
    int http_code = http.GET();

    snprintf(buf, sizeof(buf), "Finished request in %lu millis.", millis() - start);
    logger_.log(buf);
    if (http_code > 0) {
        String data = http.getString();
        http.end();

        snprintf(buf, sizeof(buf), "Response code: %d Data length: %d", http_code, data.length());
        logger_.log(buf);

        logger_.log(data.c_str());

        std::string err;
        Json json = Json::parse(data.c_str(), err);

        if (err.empty()) {
            return handleData(json);
        } else {
            snprintf(buf, sizeof(buf), "Error parsing response! %s", err.c_str());
            logger_.log(buf);
            return false;
        }
    } else {
        snprintf(buf, sizeof(buf), "Error on HTTP request (%d): %s", http_code, http.errorToString(http_code).c_str());
        logger_.log(buf);
        http.end();
        return false;
    }
}

bool MQTTTask::handleData(Json json) {
    // Example data:
    /*
        {
            "utc_offset":"-05:00",
            "timezone":"America/New_York",
            "day_of_week":0,
            "day_of_year":329,
            "datetime":"2024-11-24T11:01:08.023006-05:00",
            "utc_datetime":"2024-11-24T16:01:08.023006+00:00",
            "unixtime":1732464068,
            "raw_offset":-18000,
            "week_number":47,
            "dst":false,
            "abbreviation":"EST",
            "dst_offset":0,
            "dst_from":null,
            "dst_until":null,
            "client_ip":"172.56.4.80"
        }
    */
    auto UnixTime = json["unixtime"];
    auto RawOffset = json["raw_offset"];
    timeKeeper.setTime(UnixTime.int_value());
    timeKeeper.offset = RawOffset.int_value();
    return true;
}

#endif
