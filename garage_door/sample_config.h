// Copy as config.h and modify with your values

// OTA
#define CONFIG_OTA_HOST "your_host_name"
#define CONFIG_OTA_PATH "/WebFirmwareUpgrade"
#define CONFIG_OTA_USER "your_username"
#define CONFIG_OTA_PASS "your_password"

// MQTT
#define CONFIG_MQTT_HOST "mqtt_host_or_ip"
#define CONFIG_MQTT_PORT 1883 // Usually 1883
#define CONFIG_MQTT_USER "your_mqtt_user"
#define CONFIG_MQTT_PASS "your_mqtt_password"
#define CONFIG_MQTT_CLIENT_ID "your_client_id" // Must be unique on the MQTT network
