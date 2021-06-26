#include "boot.h"

#include <esp_err.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <freertos/event_groups.h>
#include <string.h>
#include <driver/gpio.h>
#include <stdio.h>

#include "task_priorities.h"
#include "ports.h"
#include "co2_reading.h"
#include "bme_reading.h"
#include "sync_sensors.h"
#include "control_task.h"
#include "co2_control.h"
#include "humidity_control.h"
#include "temperature_control.h"
#include "rtc.h"
#include "rf_transmitter.h"
#include "mqtt_manager.h"
#include "network_settings.h"
#include "nvs_manager.h"
#include "deep_sleep_manager.c"
#include "grow_manager.h"

void boot_sequence() {
	// Init nvs
	init_nvs();

	nvs_clear();
	// Initialize deep sleep
	init_power_button();

	// Init WiFi Stack
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// Init network properties
	init_network_connections();

	sensor_event_group = xEventGroupCreate();

	// Init i2cdev
	ESP_ERROR_CHECK(i2cdev_init());

	// Set all sync bits var
	set_sensor_sync_bits();

	// Init time rtc
	init_sntp();
	init_rtc();
	// Init sensor control
	init_control();

	vTaskPrioritySet(NULL, configMAX_PRIORITIES-1);

	// Create core 0 tasks
	xTaskCreatePinnedToCore(rf_transmitter, "rf_transmitter_task", 2500, NULL, RF_TRANSMITTER_TASK_PRIORITY, &rf_transmitter_task_handle, 0);
	xTaskCreatePinnedToCore(manage_timers_alarms, "timer_alarm_task", 2500, NULL, TIMER_ALARM_TASK_PRIORITY, &timer_alarm_task_handle, 0);
	xTaskCreatePinnedToCore(publish_sensor_data, "publish_task", 2500, NULL, MQTT_PUBLISH_TASK_PRIORITY, &publish_task_handle, 0);
	xTaskCreatePinnedToCore(sensor_control, "sensor_control_task", 3000, NULL, SENSOR_CONTROL_TASK_PRIORITY, &sensor_control_task_handle, 0);

	// Create core 1 tasks
	xTaskCreatePinnedToCore(measure_co2, "co2_task", 2500, NULL, CO2_TASK_PRIORITY, sensor_get_task_handle(get_co2_sensor()), 1);
	xTaskCreatePinnedToCore(measure_bme, "bme_task", 2500, NULL, BME_TASK_PRIORITY, sensor_get_task_handle(get_temperature_sensor()), 1); //temperature sensor is used for task handle //
	xTaskCreatePinnedToCore(sync_task, "sync_task", 2500, NULL, SYNC_TASK_PRIORITY, &sync_task_handle, 1);


	// Init grow manager
	init_grow_manager();
}

void restart_esp32() { // Restart ESP32
	ESP_LOGE("", "RESTARTING ESP32");
	fflush(stdout);
	esp_restart();
}
