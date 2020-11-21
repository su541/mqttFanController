#define MQTT_SERVER "mqtt_server"
#define MQTT_USER "mqtt_auth_user"
#define MQTT_PASS "mqtt_auth_pass"
#define MQTT_TOPIC "mqtt_subscription_topic"
#define DISPLAY_TIME "displayTime"
#define FAN_START_TEMP "fanStartTemp"
#define FAN_STOP_TEMP "fanStopTemp"

struct Config {
    char mqtt_server[40];
    char subscriptionTopic[100];
    char mqtt_auth_user[20];
    char mqtt_auth_pass[20];
    unsigned int displayTime;
    double fanStartTemp;
    double fanStopTemp;
};