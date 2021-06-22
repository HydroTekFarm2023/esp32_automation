#include "hc12_lib.h"

#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_err.h>

#define NOP() asm volatile ("nop")


void uart_config_driver () {
    
}

void configure_protocol(int32_t pulse_width, int32_t repeat_transmission, int16_t transmit_pin, struct binary_bits low_bit, struct binary_bits high_bit, struct binary_bits sync_bit){
	power_outlet_protocol.pulse_width = pulse_width;
	power_outlet_protocol.repeat_transmission = repeat_transmission;
	power_outlet_protocol.low_bit = low_bit;
	power_outlet_protocol.high_bit = high_bit;
	power_outlet_protocol.sync_bit = sync_bit;
	power_outlet_protocol.transmit_pin = transmit_pin;
}

unsigned long IRAM_ATTR micros(){
	return (unsigned long) (esp_timer_get_time());
}

void IRAM_ATTR delayMicroseconds(uint32_t us)
{
    uint32_t m = micros();
    if(us){
        uint32_t e = (m + us);
        if(m > e){ //overflow
            while(micros() > e){
                NOP();
            }
        }
        while(micros() < e){
            NOP();
        }
    }
}

void transmit_message(unsigned long code, int32_t length){
	// Repeat Transmission for better accuracy
	for(int i = 0; i < power_outlet_protocol.repeat_transmission; i++){
		for(int j = length - 1; j >= 0; j--){
			if(code & (1L << j)){
				// Transmit Binary 1
				gpio_set_level(power_outlet_protocol.transmit_pin, 1);
				delayMicroseconds(power_outlet_protocol.pulse_width * power_outlet_protocol.high_bit.high_pulse_amount);
				gpio_set_level(power_outlet_protocol.transmit_pin, 0);
				delayMicroseconds(power_outlet_protocol.pulse_width * power_outlet_protocol.high_bit.low_pulse_amount);
			} else {
				// Transmit Binary 0
				gpio_set_level(power_outlet_protocol.transmit_pin, 1);
				delayMicroseconds(power_outlet_protocol.pulse_width * power_outlet_protocol.low_bit.high_pulse_amount);
				gpio_set_level(power_outlet_protocol.transmit_pin, 0);
				delayMicroseconds(power_outlet_protocol.pulse_width *  power_outlet_protocol.low_bit.low_pulse_amount);
			}
		}
		gpio_set_level(power_outlet_protocol.transmit_pin, 1);
		delayMicroseconds(power_outlet_protocol.pulse_width * power_outlet_protocol.sync_bit.high_pulse_amount);
		gpio_set_level(power_outlet_protocol.transmit_pin, 0);
		delayMicroseconds(power_outlet_protocol.pulse_width * power_outlet_protocol.sync_bit.low_pulse_amount);
		gpio_set_level(power_outlet_protocol.transmit_pin, 0);
	}
}

void send_message(const char* rf_address_ptr, const char* power_outlet_state_ptr){
	unsigned long code = 0;
	unsigned int length = 0;

	  for (const char* p = rf_address_ptr; *p; p++) {
		  // convert char to unsigned long
	    code <<= 1L;
	    if (*p != '0')
	      code |= 1L;
	    length++;	// Calculate length of binary
	  }

	  for (const char* p = power_outlet_state_ptr; *p; p++) {
		  // convert char to unsigned long
	    code <<= 1L;
	    if (*p != '0')
	      code |= 1L;
	    length++;	// Calculate length of binary
	  }

	  // Transmit combined message
	  transmit_message(code, length);
	  ESP_LOGI("Transmission", "%lu", code);
}


