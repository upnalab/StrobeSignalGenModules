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

// ---------- Encoders: better if first pin has interrups (D2,D3 in Nano
Encoder encFreq(3, 4);
#define BTN_FREQ 5
Encoder encDuty(2, 6);
#define BTN_DUTY 7

#define OUTPUT_PIN 9  // OC1A
EncoderControl freqCtrl(encFreq, BTN_FREQ, 1, 9999, 1500);   // start at 15 Hz it is divided by 100
EncoderControl dutyCtrl(encDuty, BTN_DUTY, 0, 1000, 10);    // start at 1% it is divided by 10
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
  config.value1 = freqCtrl.getValue();
  config.value2 = dutyCtrl.getValue();
  config.switchedOn = switchedOn;
  EEPROM.put(0, config);
}
void loadConfig() {
  EEPROM.get(0, config);
  if (config._signature != 0xBEEF){ //first time loading, put default values
      config._signature = 0xBEEF;
      config.value1 = 1500;
      config.value2 = 10;
      config.switchedOn = false;
      saveConfig();
  }
  freqCtrl.setValue( config.value1 );
  dutyCtrl.setValue( config.value2 );
  switchedOn = config.switchedOn;
}


// ---------- Function prototypes ----------
void updateTimer1(uint16_t freq, uint16_t duty);
void drawDisplay();
void uintToString(uint16_t val, char* str);
void gndPin(uint8_t pin) {pinMode(pin, OUTPUT); digitalWrite(pin, LOW);};

void setup() {
  // turn off everything we can 
  power_adc_disable();
  ADCSRA = 0;
  power_spi_disable();
  //power_timer0_disable(); //disabling timer0 breaks the encoder library
  TIMSK0 &= ~_BV(TOIE0); //so we just disable its interrupt to avoid jitter
  
  pinMode(BTN_FREQ, INPUT_PULLUP);
  pinMode(BTN_DUTY, INPUT_PULLUP);
  pinMode(OUTPUT_PIN, OUTPUT); //9
  gndPin(8); gndPin(10); gndPin(11);
  
  loadConfig();
  freqCtrl.setDigit( 2 );
  dutyCtrl.setDigit( 1 );
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(SSD1306_WHITE);
  drawDisplay();

  updateTimer1(freqCtrl.getValue(), dutyCtrl.getValue());
  Serial.begin( 115200 );
}

void loop() {
  bool needsUpdate = false;
  uint16_t ticksPressed = 0;

  freqCtrl.handleRotation(needsUpdate);
  freqCtrl.handleButton(needsUpdate, ticksPressed);
  if (ticksPressed > 30){ //30 is around 1s -> save current parameters to EPROM
    //Serial.println( ticksPressed );
    saveConfig();
  }

  dutyCtrl.handleRotation(needsUpdate);
  dutyCtrl.handleButton(needsUpdate, ticksPressed);
  if (ticksPressed > 30){ //switch on/off
    Serial.println( switchedOn );
    if( switchedOn ){ //it is on, we switch off
      noInterrupts();
      TCCR1A = TCCR1B = TCNT1 = 0;
      digitalWrite(OUTPUT_PIN, LOW);
      interrupts();
    }
    switchedOn = switchedOn?false:true;
    needsUpdate = true;
    
  }

  if (needsUpdate) {
    drawDisplay();
    if (switchedOn)
      updateTimer1(freqCtrl.getValue(), dutyCtrl.getValue());
  }

  delay(20);
}


void drawDisplay() {
  char str[] = "0000";
  display.clearDisplay();
  //display.setRotation(2);
  
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("Freq (Hz)");
  display.setCursor(0, 10);
  display.setTextSize(2);
  uintToString(freqCtrl.getValue(), str);
  display.print(str);
  display.drawPixel(23, 25, SSD1306_WHITE);
  display.fillRect(1+(3- freqCtrl.getDigit() )*12, 29, 10, 2, SSD1306_WHITE);    // Highlight frequency digit
  
  display.setCursor(0, 35);
  display.setTextSize(1);
  display.print("Duty (%)");
  display.setCursor(0, 45);
  display.setTextSize(2);
  uintToString(dutyCtrl.getValue(), str);
  display.print(str);
  display.drawPixel(35, 59, SSD1306_WHITE);
  display.fillRect(1+(3- dutyCtrl.getDigit() )*12, 63, 10, 2, SSD1306_WHITE); //duty digit

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

void updateTimer1(uint16_t freq, uint16_t duty) {
  freq = constrain(freq, 1, 9999); //freq is x100
  duty = constrain(duty, 0, 1000); //duty is from 0 to 1000

  const uint32_t F_CPU_HZ = 16000000UL;
  const uint16_t PRESCALER = 256;
  uint32_t top = F_CPU_HZ*100 / PRESCALER / freq  - 1;
  if (top > 65535) top = 65535;  // 16-bit limit
  uint32_t compare = (top * duty) / 1000;  
  if (compare > top) compare = top;

  noInterrupts();
  pinMode(OUTPUT_PIN, OUTPUT);
  TCCR1A = TCCR1B = TCNT1 = 0; // Reset Timer1
  ICR1  = (uint16_t)top;      // Period
  OCR1A = (uint16_t)compare;  // Duty cycle
  TCCR1A = (1 << COM1A1) | (1 << WGM11); // Fast PWM mode using ICR1 as TOP: Mode 14 (WGM13:WGM10 = 1110)
  TCCR1B = (1 << WGM13) | (1 << WGM12);
  TCCR1B |= (1 << CS12); //prescaler to 256
  interrupts();
}

