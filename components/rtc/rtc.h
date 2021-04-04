#include "ds3231.h"

#include <cJSON.h>

// RTC dev
i2c_dev_t dev;

// Timer and alarm periods
#define TIMER_ALARM_URGENT_DELAY 10
#define TIMER_ALARM_REGULAR_DELAY 50

// Keys
#define IRRIGATION_ON_KEY "on_interval"
#define IRRIGATION_OFF_KEY "off_interval"

// Task handle
TaskHandle_t timer_alarm_task_handle;

// Timers
struct timer reservoir_change_timer;
struct timer irrigation_timer;

// Alarms
struct alarm day_time_alarm;
struct alarm night_time_alarm;

// Water pump timings
uint32_t irrigation_on_time;
uint32_t irrigation_off_time;
bool is_irrigation_on;

// Day or night time
bool is_day;

// Day and night times
uint32_t day_time_hour;
uint32_t day_time_min;
uint32_t night_time_hour;
uint32_t night_time_min;

// Initialize rtc
void init_rtc();

// Set current time
void set_time();

// Check if rtc needs to be reset
void check_rtc_reset();

// Get current day and time
void get_date_time(struct tm *time);

// Timer and alarm task
void manage_timers_alarms();

// Initialize irrigation control
void init_irrigation();

// Control irrigation
void irrigation_control();

// Update irrigation timings
void update_irrigation_timings(cJSON *obj);

// Turn irrigation on/off
void irrigation_on();
void irrigation_off();
