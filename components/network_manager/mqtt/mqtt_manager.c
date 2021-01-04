#include "mqtt_manager.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_err.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <cjson.h>

#include "boot.h"
#include "ec_reading.h"
#include "ph_reading.h"
#include "water_temp_reading.h"
#include "ec_control.h"
#include "ph_control.h"
#include "sync_sensors.h"
#include "rtc.h"
#include "network_settings.h"
#include "wifi_connect.h"

void mqtt_event_handler_cb(esp_mqtt_event_handle_t event) {
	const char *TAG = "MQTT_Event_Handler";
	switch (event->event_id) {
	case MQTT_EVENT_CONNECTED:
		xTaskNotifyGive(publish_task_handle);
		ESP_LOGI(TAG, "Connected\n");
		break;
	case MQTT_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "Disconnected\n");
		break;
	case MQTT_EVENT_SUBSCRIBED:
		ESP_LOGI(TAG, "Subscribed\n");
		break;
	case MQTT_EVENT_UNSUBSCRIBED:
		ESP_LOGI(TAG, "UnSubscribed\n");
		break;
	case MQTT_EVENT_PUBLISHED:
		ESP_LOGI(TAG, "Published\n");
		break;
	case MQTT_EVENT_DATA:
		ESP_LOGI(TAG, "Message received\n");
		data_handler(event->topic, event->topic_len, event->data, event->data_len);
		break;
	case MQTT_EVENT_ERROR:
		ESP_LOGI(TAG, "Error\n");
		break;
	case MQTT_EVENT_BEFORE_CONNECT:
		ESP_LOGI(TAG, "Before Connection\n");
		break;
	default:
		ESP_LOGI(TAG, "Other Command\n");
		break;
	}
}
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {		// MQTT Event Callback Functions
	mqtt_event_handler_cb(event_data);
}

void create_str(char** str, char* init_str) { // Create method to allocate memory and assign initial value to string
	*str = malloc(strlen(init_str) * sizeof(char)); // Assign memory based on size of initial value
	if(!(*str)) { // Restart if memory alloc fails
		ESP_LOGE("", "Memory allocation failed. Restarting ESP32");
		restart_esp32();
	}
	strcpy(*str, init_str); // Copy initial value into string
}
void append_str(char** str, char* str_to_add) { // Create method to reallocate and append string to already allocated string
	*str = realloc(*str, (strlen(*str) + strlen(str_to_add)) * sizeof(char) + 1); // Reallocate data based on existing and new string size
	if(!(*str)) { // Restart if memory alloc fails
		ESP_LOGE("", "Memory allocation failed. Restarting ESP32");
		restart_esp32();
	}
	strcat(*str, str_to_add); // Concatenate strings
}

// Add sensor data to JSON entry
void add_entry(char** data, bool* first, char* name, float num) {
	// Add a comma to the beginning of every entry except the first
	if(*first) *first = false;
	else append_str(data, ",");

	// Convert float data into string
	char value[8];
	snprintf(value, sizeof(value), "%.2f", num);

	// Create entry string
	char *entry = NULL;
	create_str(&entry, "{ \"name\": \"");

	// Create entry using key, value, and other JSON syntax
	append_str(&entry, name);
	append_str(&entry, "\", \"value\": \"");
	append_str(&entry, value);
	append_str(&entry, "\"}");

	// Add entry to overall JSON data
	append_str(data, entry);

	// Free allocated memory
	free(entry);
	entry = NULL;
}

void init_topic(char **topic, int topic_len) {
	*topic = malloc(sizeof(char) * topic_len);
	strcpy(*topic, get_network_settings()->device_id);
}

void add_heading(char *topic, char *heading) {
	strcat(topic, "/");
	strcat(topic, heading);
}

void make_topics() {
	ESP_LOGI("", "Starting make topics");

	int device_id_len = strlen(get_network_settings()->device_id);

	init_topic(&wifi_connect_topic, device_id_len + 1 + strlen(WIFI_CONNECT_HEADING) + 1);
	add_heading(wifi_connect_topic, WIFI_CONNECT_HEADING);
	ESP_LOGI("", "Wifi Topic: %s", wifi_connect_topic);

	init_topic(&sensor_data_topic, device_id_len + 1 + strlen(SENSOR_DATA_HEADING) + 1);
	add_heading(sensor_data_topic, SENSOR_DATA_HEADING);
	ESP_LOGI("", "Sensor data topic: %s", sensor_data_topic);

	init_topic(&sensor_settings_topic, device_id_len + 1 + strlen(SENSOR_SETTINGS_HEADING) + 1);
	add_heading(sensor_settings_topic, SENSOR_SETTINGS_HEADING);
	ESP_LOGI("", "Sensor settings topic: %s", sensor_settings_topic);
}

void subscribe_topics() {
	// Subscribe to topics
	esp_mqtt_client_subscribe(mqtt_client, sensor_settings_topic, SUBSCRIBE_DATA_QOS);
}

void init_mqtt() {
//	// Build uri string
//	char uri_str[30];
//	strcpy(uri_str, "http://");
//	strcat(uri_str, get_network_settings()->broker_ip);
//	strcat(uri_str, ":1883");

	// Set broker configuration
}

void mqtt_connect() {
	// First check if wifi is connected
	if(!is_wifi_connected) {
		is_mqtt_connected = false;
		return;
	}

	// Connect mqtt
    //esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);
    esp_mqtt_client_start(mqtt_client);

	// Subscribe to topics
	subscribe_topics();

	ESP_LOGI("", "Sending success message");
	// Send success message
	esp_mqtt_client_publish(mqtt_client, wifi_connect_topic, "1", 0, PUBLISH_DATA_QOS, 0);

	is_mqtt_connected = true;
}

void create_time_json(cJSON **time_json) {
	char time_str[TIME_STRING_LENGTH];

	struct tm time;
	get_date_time(&time);

	sprintf(time_str, "%.4d", time.tm_year + 1900);
	strcat(time_str, "-");
	sprintf(time_str + 5, "%.2d", time.tm_mon);
	strcat(time_str, "-");
	sprintf(time_str + 8, "%.2d", time.tm_mday);
	strcat(time_str, "T");
	sprintf(time_str + 11, "%.2d", time.tm_hour);
	strcat(time_str, "-");
	sprintf(time_str + 14, "%.2d", time.tm_min);
	strcat(time_str, "-");
	sprintf(time_str + 17, "%.2d", time.tm_sec);
	strcat(time_str, "Z");

	*time_json = cJSON_CreateString(time_str);
}

void publish_data(void *parameter) {			// MQTT Setup and Data Publishing Task
	const char *TAG = "Publisher";

	if(!is_wifi_connected) {
		ESP_LOGE(TAG, "MQTT will not work, wiif not connected");
	}

	esp_mqtt_client_config_t mqtt_cfg = {
			.host = "broker.hivemq.com",
			.port = 1883,
			.event_handle = mqtt_event_handler
	};

	// Create MQTT client
	mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

	//make_topics();

    esp_mqtt_client_start(mqtt_client);
    //ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

	// Subscribe to topics
//	subscribe_topics();
//
//	ESP_LOGI("", "Sending success message");
	// Send success message
//	esp_mqtt_client_publish(mqtt_client, wifi_connect_topic, "1", 0, PUBLISH_DATA_QOS, 0);

	is_mqtt_connected = true;

	ESP_LOGI(TAG, "Sensor data topic: %s", sensor_data_topic);

	for (;;) {
		if(!is_mqtt_connected) {
			ESP_LOGE(TAG, "Wifi not connected, cannot send MQTT data");

			// Wait for delay period and try again
			vTaskDelay(pdMS_TO_TICKS(SENSOR_MEASUREMENT_PERIOD));
			continue;
		}

		cJSON *root, *time, *sensor_arr, *sensor;

		// Initializing json object and sensor array
		root = cJSON_CreateObject();
		sensor_arr = cJSON_CreateArray();

		// Adding time
		create_time_json(&time);
		cJSON_AddItemToObject(root, "time", time);

		// Adding water temperature
		sensor_get_json(get_water_temp_sensor(), &sensor);
		cJSON_AddItemToArray(sensor_arr, sensor);

		// Adding ec
		sensor_get_json(get_ec_sensor(), &sensor);
		cJSON_AddItemToArray(sensor_arr, sensor);

		// Adding pH
		sensor_get_json(get_ph_sensor(), &sensor);
		cJSON_AddItemToArray(sensor_arr, sensor);

		// Adding array to object
		cJSON_AddItemToObject(root, "sensors", sensor_arr);

		// Creating string from JSON
		char *data = cJSON_PrintUnformatted(root);

		// Free memory
		cJSON_Delete(root);

		// Publish data to MQTT broker using topic and data
		esp_mqtt_client_publish(mqtt_client, sensor_data_topic, data, 0, PUBLISH_DATA_QOS, 0);

		ESP_LOGI(TAG, "Message: %s", data);
		ESP_LOGI(TAG, "Topic: %s", sensor_data_topic);

		// Publish data every sensor reading
		vTaskDelay(pdMS_TO_TICKS(SENSOR_MEASUREMENT_PERIOD));
	}

	free(wifi_connect_topic);
	free(sensor_data_topic);
	free(sensor_settings_topic);
}

void update_settings() {
	const char *TAG = "UPDATE_SETTINGS";
	ESP_LOGI(TAG, "Settings data");

	char *data_string = "{\"data\":[{\"ph\":{\"monitoring_only\":true,\"control\":{\"dosing_time\":10,\"dosing_interval\":2,\"day_and_night\":false,\"day_target_value\":6,\"night_target_value\":6,\"target_value\":5,\"pumps\":{\"pump_1_enabled\":true,\"pump_2_enabled\":false}},\"alarm_min\":3,\"alarm_max\":7}},{\"ec\":{\"monitoring_only\":false,\"control\":{\"dosing_time\":30,\"dosing_interval\":50,\"day_and_night\":true,\"day_target_value\":23,\"night_target_value\":4,\"target_value\":4,\"pumps\":{\"pump_1\":{\"enabled\":true,\"value\":10},\"pump_2\":{\"enabled\":false,\"value\":4},\"pump_3\":{\"enabled\":true,\"value\":2},\"pump_4\":{\"enabled\":false,\"value\":7},\"pump_5\":{\"enabled\":true,\"value\":3}}},\"alarm_min\":1.5,\"alarm_max\":4}}]}";
	cJSON *root = cJSON_Parse(data_string);
	cJSON *arr = root->child;

	for(int i = 0; i < cJSON_GetArraySize(arr); i++) {
		cJSON *subitem = cJSON_GetArrayItem(arr, i)->child;
		char *data_topic = subitem->string;

		if(strcmp("ph", data_topic) == 0) {
			ESP_LOGI(TAG, "pH data received");
			ph_update_settings(subitem);
		} else if(strcmp("ec", data_topic) == 0) {
			ESP_LOGI(TAG, "ec data received");
			ec_update_settings(subitem);
		} else if(strcmp("air_temperature", data_topic) == 0) {
			// Add air temp call when control is implemented
			ESP_LOGI(TAG, "air temperature data received");
		} else {
			ESP_LOGE(TAG, "Data %s not recognized", data_topic);
		}
	}

	cJSON_Delete(root);
}

void data_handler(char *topic, uint32_t topic_len, char *data, uint32_t data_len) {
	const char *TAG = "DATA_HANDLER";

	char topic_temp[topic_len-1];
	char data_temp[data_len];

	strncpy(topic_temp, topic, topic_len-1);
	strncpy(data_temp, data, data_len);

	if(/*strcmp(topic_temp, settings_data_topic)*/0 == 0) {
		update_settings();
	} else {
		ESP_LOGE(TAG, "Topic not recognized");
	}
}
