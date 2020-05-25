/*
* This is an example of an rgb ws2812_i2s led strip
*
* NOTE:
*    1) the ws2812_i2s library uses hardware I2S so output pin is GPIO3 and cannot be changed.
*    2) on some ESP8266 such as the Wemos D1 mini, GPIO3 is the same pin used for serial comms.
*    3) Toggle button can be placed on GPIO0. Click to toggle on/off. Hold to reset config.

* Debugging printf statements are disabled below because of note (2) - you can uncomment
* them if your hardware supports serial comms that do not conflict with I2S on GPIO3.
*
*/
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_common.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>
#include <math.h>
#include "button.h"
#include <etstimer.h>
#include <time.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>
#include "ws2812_i2s/ws2812_i2s.h"
#include "ota-api.h"
#include <math.h>

#define max(a,b) \
({ __typeof__ (a) _a = (a); \
__typeof__ (b) _b = (b); \
_a > _b ? _a : _b; })
#define min(a,b) \
({ __typeof__ (a) _a = (a); \
__typeof__ (b) _b = (b); \
_a < _b ? _a : _b; })

#define DEG_TO_RAD(X) (M_PI*(X)/180)


 #define randnum(min, max) \
        ((rand() % (int)(((max) + 1) - (min))) + (min))

#define LED_ON 0                // this is the value to write to GPIO for led on (0 = GPIO low)
#define LED_INBUILT_GPIO 2      // this is the onboard LED used to show on/off only
#define LED_COUNT 100            // this is the number of WS2812B leds on the strip
#define LED_RGB_SCALE 255       // this is the scaling factor used for color conversion

TimerHandle_t xColorTimer = NULL;
int color_delay = 20;

// Global variables
float led_hue = 0;              // hue is scaled 0 to 360
float led_saturation = 59;      // saturation is scaled 0 to 100
float led_brightness = 100;     // brightness is scaled 0 to 100
bool power_on = false;            // on is boolean on or off
const int button_gpio = 0;      // Button GPIO pin - Click On/Off, 10s Hold Reset
ws2812_pixel_t pixels[LED_COUNT];

ws2812_pixel_t rgbw = { { 0, 0, 0, 0 } };
ws2812_pixel_t rgb1 = { { 0, 0, 0, 0 } };
int  x = 1,x1 = 1,x2 = 1;
bool color_loop_bool = false;

void switch_on_callback(homekit_characteristic_t *_ch, homekit_value_t on, void *context);
void update_hue(homekit_characteristic_t *ch, homekit_value_t value, void *context);


void button_callback(uint8_t gpio, button_event_t event);

void c_loop(homekit_characteristic_t *_ch, homekit_value_t on, void *context);

 ws2812_pixel_t current_color = { { 0, 0, 0, 0 } };
ws2812_pixel_t target_color = { { 0, 0, 0, 0 } };


//http://blog.saikoled.com/post/44677718712/how-to-convert-from-hsi-to-rgb-white
void hsi2rgbw(float H, float S, float I, ws2812_pixel_t* rgbw) {
	int r, g, b, w;
	float cos_h, cos_1047_h;
  while (H < 0) { H += 360.0F; };     // cycle h around to 0-360 degrees
  while (H >= 360) { H -= 360.0F; };
  
  H = 3.14159*H/(float)180; // Convert to radians.
  S /= 100.0F;                        // from percentage to ratio
  I /= 100.0F;                        // from percentage to ratio
  S = S>0?(S<1?S:1):0; // clamp S and I to interval [0,1]
  I = I>0?(I<1?I:1):0;
  I = I * sqrt(I);                    // shape intensity to have finer granularity near 0
  // Math! Thanks in part to Kyle Miller.                 // shape intensity to have finer granularity near 0

     if(H < 2.09439) {
    cos_h = cos(H);
    cos_1047_h = cos(1.047196667-H);
    r = S*255*I/3*(1+cos_h/cos_1047_h);
    g = S*255*I/3*(1+(1-cos_h/cos_1047_h));
    b = 0;
    w = 255*(1-S)*I;
  } else if(H < 4.188787) {
    H = H - 2.09439;
    cos_h = cos(H);
    cos_1047_h = cos(1.047196667-H);
    g = S*255*I/3*(1+cos_h/cos_1047_h);
    b = S*255*I/3*(1+(1-cos_h/cos_1047_h));
    r = 0;
    w = 255*(1-S)*I;
  } else {
    H = H - 4.188787;
    cos_h = cos(H);
    cos_1047_h = cos(1.047196667-H);
    b = S*255*I/3*(1+cos_h/cos_1047_h);
    r = S*255*I/3*(1+(1-cos_h/cos_1047_h));
    g = 0;
    w = 255*(1-S)*I;
  }
    rgbw->red = (uint8_t) r;
    rgbw->green = (uint8_t) g;
    rgbw->blue = (uint8_t) b;
    rgbw->white = (uint8_t) w;           // white channel is not used
}


void led_string_fill(ws2812_pixel_t rgbw) {

  // write out the new color to each pixel
  for (int i = 0; i < LED_COUNT; i++) {
    pixels[i] = rgbw;
  }
  ws2812_i2s_update(pixels, PIXEL_RGBW);
}


void led_init() {
  // initialise the onboard led as a secondary indicator (handy for testing)
  gpio_enable(LED_INBUILT_GPIO, GPIO_OUTPUT);
	
  // initialise the LED strip
  ws2812_i2s_init(LED_COUNT, PIXEL_RGBW);

  // set the initial state
 // led_string_set();
}

void led_identify_task(void *_args) {
  const ws2812_pixel_t COLOR_PINK = { { 255, 0, 127, 0 } };
  const ws2812_pixel_t COLOR_BLACK = { { 0, 0, 0, 0 } };

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      gpio_write(LED_INBUILT_GPIO, LED_ON);
      led_string_fill(COLOR_PINK);
      vTaskDelay(200 / portTICK_PERIOD_MS);
      gpio_write(LED_INBUILT_GPIO, 1 - LED_ON);
      led_string_fill(COLOR_BLACK);
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    vTaskDelay(350 / portTICK_PERIOD_MS);
  }

 // led_string_set();
  vTaskDelete(NULL);
}

void led_identify(homekit_value_t _value) {
  // printf("LED identify\n");
  xTaskCreate(led_identify_task, "LED identify", 128, NULL, 2, NULL);
}


homekit_value_t led_brightness_get() {
  return HOMEKIT_INT(led_brightness);
}
void led_brightness_set(homekit_value_t value) {
  if (value.format != homekit_format_int) {
    // printf("Invalid brightness-value format: %d\n", value.format);
    return;
  }
  led_brightness = value.int_value;
 // led_string_set();
}

homekit_value_t led_saturation_get() {
  return HOMEKIT_FLOAT(led_saturation);
}

void led_saturation_set(homekit_value_t value) {
  if (value.format != homekit_format_float) {
    // printf("Invalid sat-value format: %d\n", value.format);
    return;
  }
  led_saturation = value.float_value;
 // led_string_set();
}



void reset_configuration_task() {
  //Flash the LED first before we start the reset
  const ws2812_pixel_t COLOR_PINK = { { 255, 0, 127, 0 } };
  const ws2812_pixel_t COLOR_BLACK = { { 0, 0, 0, 0 } };
  for (int i=0; i<3; i++) {
    gpio_write(LED_INBUILT_GPIO, LED_ON);
    led_string_fill(COLOR_PINK);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    gpio_write(LED_INBUILT_GPIO, 1 - LED_ON);
    led_string_fill(COLOR_BLACK);
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
  printf("Resetting Wifi Config\n");
  wifi_config_reset();
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  printf("Resetting HomeKit Config\n");
  homekit_server_reset();
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  printf("Restarting\n");
  sdk_system_restart();
  vTaskDelete(NULL);
}

void reset_configuration() {
  printf("Resetting configuration\n");
  xTaskCreate(reset_configuration_task, "Reset configuration", 256, NULL, 2, NULL);
}



homekit_characteristic_t lightbulb_on = HOMEKIT_CHARACTERISTIC_(ON, false, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(switch_on_callback));


void switch_on_callback(homekit_characteristic_t *_ch, homekit_value_t on, void *context) {
	power_on = lightbulb_on.value.bool_value;
	
	
//	led_string_set();
}

void button_callback(uint8_t gpio, button_event_t event) {
  switch (event) {
    case button_event_single_press:
 
    lightbulb_on.value.bool_value = !lightbulb_on.value.bool_value;
	homekit_characteristic_notify(&lightbulb_on, lightbulb_on.value);
	
//	 printf("button %d\n", lightbulb_on.value.bool_value);
	  break;
    case button_event_long_press:
      reset_configuration();
      break;
    default:
     printf("unknown button event: %d\n", event);
  }
}



void led_strip_set(int delay, float hue){

	 if (power_on) {
    // convert HSI to RGBW
    hsi2rgbw(hue, led_saturation, led_brightness, &rgbw);
  // printf("h=%d,s=%d,b=%d => ", (int)led_hue, (int)led_saturation, (int)led_brightness);
  // printf("r=%d,g=%d,b=%d,w=%d\n", rgb.red, rgb.green, rgb.blue, rgb.white);
	     	target_color.red = rgbw.red;
            target_color.green = rgbw.green;
            target_color .blue = rgbw.blue;
            target_color .white = rgbw.white;
		
    // set the inbuilt led
    gpio_write(LED_INBUILT_GPIO, LED_ON);
 //   printf("on\n");
  }
  else {
   // printf("off\n");
   			target_color.red = 0;
            target_color.green = 0;
            target_color .blue = 0;
            target_color .white = 0;
    gpio_write(LED_INBUILT_GPIO, 1 - LED_ON);
  }
  
  //////////////////////////
  
  
  	   if(current_color.red < target_color.red){
      
      x = (target_color.red - rgb1.red )/ 20;
    	
    
 	  
 	  	rgb1.red += (x < 1)? 1 : x;
 	  if(rgb1.red >= target_color.red){
      	rgb1.red = target_color.red;
      	current_color.red = target_color.red;
      	}
      	
      
        }
        
       else if(current_color.red > target_color.red){
      
      x = (rgb1.red - target_color.red)/ 20;
    
 	 
 	  	rgb1.red -= (x < 1)? 1 : x;
 	  if(rgb1.red <= target_color.red){
      	rgb1.red = target_color.red;
      	current_color.red = target_color.red;
      	}
      	
      
        }
        
          if(current_color.green < target_color.green){
      
      x1 = (target_color.green - rgb1.green )/ 20;
    
 	  
 	  	rgb1.green += (x1 < 1)? 1 : x1;
 	  if(rgb1.green >= target_color.green){
      	rgb1.green = target_color.green;
      	current_color.green = target_color.green;
      	}
      	
      
        }
        
       else if(current_color.green > target_color.green){
      
      x1 = (rgb1.green - target_color.green)/ 20;
    
 	  
 	  	rgb1.green -= (x1 < 1)? 1 : x1;
 	  if(rgb1.green <= target_color.green){
      	rgb1.green = target_color.green;
      	current_color.green = target_color.green;
      	}
      	
      
        }
        
       if(current_color.blue < target_color.blue){
      
      x2 = (target_color.blue - rgb1.blue )/ 20;
    
 	  
 	  	rgb1.blue += (x2 < 1)? 1 : x2;
 	  if(rgb1.blue >= target_color.blue){
      	rgb1.blue = target_color.blue;
      	current_color.blue = target_color.blue;
      	}
      	
      
        }else if(current_color.blue > target_color.blue){
      
      x2 = (rgb1.blue - target_color.blue)/ 20;
    
 	  
 	  	rgb1.blue -= (x2 < 1)? 1 : x2;
 	  if(rgb1.blue <= target_color.blue){
      	rgb1.blue = target_color.blue;
      	current_color.blue = target_color.blue;
      	}
      	
      
        }
  		
  		led_string_fill(rgb1);
  
  
           			vTaskDelay(delay / portTICK_PERIOD_MS);

        
}

void led_strip_task(void *pvParameters) {
  

	while(1){
	
	
		 led_strip_set(color_delay,led_hue);
	
	}

 
}

void c_loop(homekit_characteristic_t *_ch, homekit_value_t on, void *context) {
	 printf(" color-loop is: %d\n", on.bool_value);
	color_loop_bool = on.bool_value;
	
	if(color_loop_bool && power_on){
		
		xTimerStart(xColorTimer, 0 );
		color_delay = 50;
		srand(time(NULL));
		}else{
		xTimerStop(xColorTimer,0);
		color_delay = 20;
		}
		
	
		
  
		
}

homekit_characteristic_t hue_set  = HOMEKIT_CHARACTERISTIC_(HUE, 0, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(update_hue));
homekit_characteristic_t color_loop  = HOMEKIT_CHARACTERISTIC_(CUSTOM_COLOR_LOOP,  false, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(c_loop));


void timer_color_loop(){
	
	
	if(power_on){
   // printf("%d\n", randnum(1, 70));
    
     led_hue  = randnum(0,359);
      printf("set color random %f\n", led_hue);	
       
	 hue_set.value.float_value = led_hue;
     homekit_characteristic_notify(&hue_set, hue_set.value);
     }else{
      color_loop.value.bool_value = false;
	homekit_characteristic_notify(&color_loop, color_loop.value);
     }
            	
}



homekit_characteristic_t ota_trigger  = API_OTA_TRIGGER;
homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, "WS2812_i2s");
homekit_characteristic_t man = HOMEKIT_CHARACTERISTIC_(MANUFACTURER, "Kriss");

void update_hue(homekit_characteristic_t *ch, homekit_value_t value, void *context) {

    printf("update_hue %f\n", hue_set.value.float_value);
    led_hue = hue_set.value.float_value;
    
   
     
}


homekit_accessory_t *accessories[] = {
  HOMEKIT_ACCESSORY(.id = 1, .category = homekit_accessory_category_lightbulb, .services = (homekit_service_t*[]) {
    HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics = (homekit_characteristic_t*[]) {
      &name,
      &man,
      HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "037A2BABF19D"),
      HOMEKIT_CHARACTERISTIC(MODEL, "WS2812_i2s"),
      HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.2.9"),
      HOMEKIT_CHARACTERISTIC(IDENTIFY, led_identify),
      NULL
    }),
    HOMEKIT_SERVICE(LIGHTBULB, .primary = true, .characteristics = (homekit_characteristic_t*[]) {
      HOMEKIT_CHARACTERISTIC(NAME, "WS2812_i2s"),
      HOMEKIT_CHARACTERISTIC(
        BRIGHTNESS, 100,
        .getter = led_brightness_get,
        .setter = led_brightness_set
      ),
  
      HOMEKIT_CHARACTERISTIC(
        SATURATION, 0,
        .getter = led_saturation_get,
        .setter = led_saturation_set
      ),
      &hue_set,
      &lightbulb_on,
      &ota_trigger,
      &color_loop,
      NULL
    }),
    NULL
  }),
  NULL
};

homekit_server_config_t config = {
  .accessories = accessories,
  .password = "111-11-111"
};

void on_wifi_ready() {
  homekit_server_init(&config);
}

void user_init(void) {
  // uart_set_baud(0, 115200);

  // This example shows how to use same firmware for multiple similar accessories
  // without name conflicts. It uses the last 3 bytes of accessory's MAC address as
  // accessory name suffix.
  uint8_t macaddr[6];
  sdk_wifi_get_macaddr(STATION_IF, macaddr);
  int name_len = snprintf(NULL, 0, "WS2812_i2s-%02X%02X%02X", macaddr[3], macaddr[4], macaddr[5]);
  char *name_value = malloc(name_len + 1);
  snprintf(name_value, name_len + 1, "WS2812_i2s-%02X%02X%02X", macaddr[3], macaddr[4], macaddr[5]);
  name.value = HOMEKIT_STRING(name_value);

  wifi_config_init2("WS2812_i2s Strip", NULL, on_wifi_ready);
  led_init();
	    xTaskCreate(led_strip_task, "led string task", 512, NULL, 2, NULL);
	    xColorTimer = xTimerCreate("Color Timer",(5000/portTICK_PERIOD_MS),pdTRUE,0, timer_color_loop);

  gpio_enable(button_gpio, GPIO_INPUT);
  if (button_create(button_gpio, 0, 10000, button_callback)) {
    printf("Failed to initialize button\n");
  }
}