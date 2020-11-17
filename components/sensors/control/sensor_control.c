#include "sensor_control.h"

#include <string.h>
#include <esp_log.h>
#include "rtc.h"
#include "sync_sensors.h"

// --------------------------------------------------- Helper functions ----------------------------------------------

void control_reset_checks(struct sensor_control *control_in) {
	for(int i = 0; i < NUM_CHECKS; i++) control_in->sensor_checks[i] = false;
	control_in->check_index = 0;
}

bool control_add_check(struct sensor_control *control_in) {
	if(control_in->check_index == NUM_CHECKS) {
		control_reset_checks(control_in);
		return true;
	}
	control_in->sensor_checks[control_in->check_index++] = true;
	ESP_LOGI(control_in->name, "Check %d done", control_in->check_index);
	return false;
}

float control_get_target_value(struct sensor_control *control_in) {
	return !is_day && control_in->is_day_night_active ? control_in->night_target_value : control_in->target_value;
}

// --------------------------------------------------------------------------------------------------------------------


// --------------------------------------------------- Public interface ----------------------------------------------

void init_sensor_control(struct sensor_control *control_in, char *name_in, bool is_enabled_in, float target_value_in,
						float margin_error_in, float night_target_value_in, bool is_day_night_in) {
	strcpy(control_in->name, name_in);
	control_in->is_control_enabled = is_enabled_in;
	control_in->is_control_active = false;
	control_in->is_doser = false;
	control_in->target_value = target_value_in;
	control_in->margin_error = margin_error_in;
	control_in->night_target_value = night_target_value_in;
	control_in->is_day_night_active = is_day_night_in;

	control_reset_checks(control_in);

	ESP_LOGI(control_in->name, "Control initialized");
}
void init_doser_control(struct sensor_control *control_in, float dose_time_in, float wait_time_in) {
	control_in->is_doser = true;
	control_in->dose_time = dose_time_in;
	control_in->wait_time = wait_time_in;
	control_in->dose_percentage = 1.;
}


bool control_get_enabled(struct sensor_control *control_in) { return control_in->is_control_enabled; }
bool control_get_active(struct sensor_control *control_in) { return control_in->is_control_active; }

struct timer* control_get_dose_timer(struct sensor_control *control_in) { return &control_in->dose_timer; }
struct timer* control_get_wait_timer(struct sensor_control *control_in) { return &control_in->wait_timer; }

void control_enable(struct sensor_control *control_in) {
	control_in->is_control_enabled = true;
	ESP_LOGI(control_in->name, "Enabled");
}
void control_disable(struct sensor_control *control_in) {
	control_in->is_control_enabled = false;
	control_in->is_control_active = false;
	control_in->dose_timer.active = false;
	control_in->wait_timer.active = false;

	//TODO turn off pumps if possible/ensure pumps are turned off (if doser)
	control_reset_checks(control_in);

	ESP_LOGI(control_in->name, "Disabled");
}

bool control_is_under_target(struct sensor_control *control_in, float current_value) {
	return current_value < (control_get_target_value(control_in) - control_in->margin_error);
}
bool control_is_over_target(struct sensor_control *control_in, float current_value) {
	return current_value > (control_get_target_value(control_in) + control_in->margin_error);
}

int control_check_sensor(struct sensor_control *control_in, float current_value) {
	if(!control_in->is_control_enabled) return 0;
	if(control_in->is_control_active) {
		if(control_in->is_doser && (control_in->dose_timer.active || control_in->wait_timer.active)) return 0;

	}

	bool under_target = control_is_under_target(control_in, current_value);
	bool over_target = control_is_over_target(control_in, current_value);

	if(under_target || over_target) {
		if(control_add_check(control_in)) {
			control_in->is_control_active = true;
			return under_target ? -1 : 1;
		}
	} else if(control_in->check_index > 0) {
		if(control_in->is_doser) control_in->is_control_active = false;
		control_reset_checks(control_in);
	}

	if(!control_in->is_doser) control_in->is_control_active = false;
	return 0;
}

void control_start_dose_timer(struct sensor_control *control_in) { enable_timer(&dev, &control_in->dose_timer, control_get_dose_time(control_in)); }
void control_start_wait_timer(struct sensor_control *control_in) { enable_timer(&dev, &control_in->wait_timer, control_in->wait_time - NUM_CHECKS * (SENSOR_MEASUREMENT_PERIOD / 1000)); }
void control_set_dose_percentage(struct sensor_control *control_in, float value) { control_in->dose_percentage = value; }
float control_get_dose_time(struct sensor_control *control_in) { return control_in->dose_time * control_in->dose_percentage; }

// --------------------------------------------------------------------------------------------------------------------
