#include <Wire.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#include <avr/power.h>
//external libs
#include <Encoder.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
//internal
#include "EncoderControl.h"

Adafruit_SSD1306 display(128, 64, &Wire, -1); //A4->SDA and A5->SCL

#define PIN_PWM_1 9 
#define PIN_GND_1 8
#define PIN_PWM_2 11 
#define PIN_GND_2 10

// ---------- Encoders: better if first pin has interrups (D2,D3 in Nano
Encoder encDuty1(3, 4);
#define BTN_DUTY1 5
Encoder encDuty2(2, 6);
#define BTN_DUTY2 7

EncoderControl duty1Ctrl(encDuty1, BTN_DUTY1, 0, 1000, 10);   // start at 1% it is divided by 10
EncoderControl duty2Ctrl(encDuty2, BTN_DUTY2, 0, 1000, 10);    // start at 1% it is divided by 10
bool switchedOn = false;

// ---------- EEPROM values: save/load with persistance
typedef struct{
  int32_t _signature;
  int16_t value1;
  int16_t value2;
  bool switchedOn;
} ConfigData;
ConfigData config;
void saveConfig() {
  config._signature = 0xBEEF;
  config.value1 = duty1Ctrl.getValue();
  config.value2 = duty2Ctrl.getValue();
  config.switchedOn = switchedOn;
  EEPROM.put(0, config);
}
void loadConfig() {
  EEPROM.get(0, config);
  if (config._signature != 0xBEEF){ //first time loading, put default values
      config._signature = 0xBEEF;
      config.value1 = 10;
      config.value2 = 10;
      config.switchedOn = false;
      saveConfig();
  }
  duty1Ctrl.setValue( config.value1 );
  duty2Ctrl.setValue( config.value2 );
  switchedOn = config.switchedOn;
}


// ---------- Function prototypes ----------
void drawDisplay();
void uintToString(uint16_t val, char* str);
void gndPin(uint8_t pin) {pinMode(pin, OUTPUT); digitalWrite(pin, LOW);};
void setPWMs(uint16_t pin1, uint16_t pin2);

void setup() {
  // turn off everything we can 
  power_adc_disable();
  ADCSRA = 0;
  power_spi_disable();
  //power_timer0_disable(); //disabling timer0 breaks the encoder library
  TIMSK0 &= ~_BV(TOIE0); //so we just disable its interrupt to avoid jitter
  
  //set PWM at timer1 (pins 9,10) and timer2 (3,11) at 21kHz
  TCCR1B = TCCR1B & B11111000 | B00000001;    // set timer 1 divisor to     1 for PWM frequency of 31372.55 Hz
  TCCR2B = TCCR2B & B11111000 | B00000001;    // set timer 2 divisor to     1 for PWM frequency of 31372.55 Hz

  pinMode(BTN_DUTY1, INPUT_PULLUP);
  pinMode(BTN_DUTY2, INPUT_PULLUP);
  pinMode(PIN_PWM_1, OUTPUT); digitalWrite(PIN_PWM_1, LOW);
  pinMode(PIN_PWM_2, OUTPUT); digitalWrite(PIN_PWM_2, LOW);
  gndPin(PIN_GND_1); gndPin(PIN_GND_2); 
  
  loadConfig();
  duty1Ctrl.setDigit( 1 );
  duty2Ctrl.setDigit( 1 );
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(SSD1306_WHITE);
  drawDisplay();

  setPWMs(duty1Ctrl.getValue(), duty2Ctrl.getValue());
  Serial.begin( 115200 );
}

void loop() {
  bool needsUpdate = false;
  uint16_t ticksPressed = 0;

  duty1Ctrl.handleRotation(needsUpdate);
  duty1Ctrl.handleButton(needsUpdate, ticksPressed);
  if (ticksPressed > 500){ // save current parameters to EPROM
    //Serial.println( ticksPressed );
    saveConfig();
  }

  duty2Ctrl.handleRotation(needsUpdate);
  duty2Ctrl.handleButton(needsUpdate, ticksPressed);
  if (ticksPressed > 500){ //switch on/off
    Serial.println( ticksPressed );
    if( switchedOn ){ //it is on, we switch off
      digitalWrite(PIN_PWM_1, LOW);
      digitalWrite(PIN_PWM_2, LOW);
    }
    switchedOn = switchedOn?false:true;
    needsUpdate = true;
  }

  if (needsUpdate) {
    drawDisplay();
    if (switchedOn)
      setPWMs(duty1Ctrl.getValue(), duty2Ctrl.getValue());
  }

  delay(20);
}

void setPWMs(uint16_t pin1, uint16_t pin2){ 
  analogWrite(PIN_PWM_1, (uint32_t)pin1 * 256 / 1000);
  analogWrite(PIN_PWM_2, (uint32_t)pin2 * 256 / 1000);
}


void drawDisplay() {
  char str[] = "0000";
  display.clearDisplay();
  //display.setRotation(2);
  
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("Duty1 (%)");
  display.setCursor(0, 10);
  display.setTextSize(2);
  uintToString(duty1Ctrl.getValue(), str);
  display.print(str);
  display.drawPixel(35, 25, SSD1306_WHITE);
  display.fillRect(1+(3- duty1Ctrl.getDigit() )*12, 29, 10, 2, SSD1306_WHITE);    // Highlight frequency digit
  
  display.setCursor(0, 35);
  display.setTextSize(1);
  display.print("Duty2 (%)");
  display.setCursor(0, 45);
  display.setTextSize(2);
  uintToString(duty2Ctrl.getValue(), str);
  display.print(str);
  display.drawPixel(35, 59, SSD1306_WHITE);
  display.fillRect(1+(3- duty2Ctrl.getDigit() )*12, 63, 10, 2, SSD1306_WHITE); //duty digit

  display.setCursor(100, 1);
  display.setTextSize(1);
  if (switchedOn){
    display.print("ON");
  }else{
    display.print("off");
  }
  
  display.display();
}

void uintToString(uint16_t val, char* str) {
  int len = 0;
  for (; str[len] != '\0'; len++)
    str[len] = '0';

  for (int i = len - 1; val > 0 && i >= 0; i--) {
    str[i] = (val % 10) + '0';
    val /= 10;
  }
}
