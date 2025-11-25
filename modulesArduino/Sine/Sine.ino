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

#define N_SAMPLES 128 //with 256 it fails
#define HALF_SAMPLES (N_SAMPLES/2)
#define PWM_FR 20000

uint8_t LUT_SIGNALS[N_SAMPLES]; //each sample is: 2 bits port masks | 6 bits microswait (0 to 63 at most)

//#define N_SIGNALS 5
//square, sin, triang, sawRise, sawFall
#define MAX_VAL 80 //this is in uS, it is the period of the PWM freq minus some time for calculations
#define HALF_VAL (MAX_VAL/2)

// ---------- Encoders: better if first pin has interrups (D2,D3 in Nano
Encoder encFreq(3,4); //3 4 5
#define BTN_FREQ 5
Encoder encAmp(2,6); //2 6 7
#define BTN_AMP 7

EncoderControl freqCtrl(encFreq, BTN_FREQ, 1, 9999, 1500);   // start at 15 Hz it is divided by 100
EncoderControl ampCtrl(encAmp, BTN_AMP, 0, 100, 35);    // start at 35% 
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
  config.value2 = ampCtrl.getValue();
  config.switchedOn = switchedOn;
  EEPROM.put(0, config);
}
void loadConfig() {
  EEPROM.get(0, config);
  if (config._signature != 0xBEEF){ //first time loading, put default values
      config._signature = 0xBEEF;
      config.value1 = 1500;
      config.value2 = 35;
      config.switchedOn = false;
      saveConfig();
  }
  freqCtrl.setValue( config.value1 );
  ampCtrl.setValue( config.value2 );
  switchedOn = config.switchedOn;
}


// ---------- Function prototypes ----------
void drawDisplay();
void uintToString(uint16_t val, char* str);
void gndPin(uint8_t pin) {pinMode(pin, OUTPUT); digitalWrite(pin, LOW);};
void fillInLUT();
void stopSignalGen();
void signalGen(int signalType, float fr);

void setup() {
  // turn off everything we can 
  power_adc_disable();
  ADCSRA = 0;
  power_spi_disable();
  //power_timer0_disable(); //disabling timer0 breaks the encoder library
  TIMSK0 &= ~_BV(TOIE0); //so we just disable its interrupt to avoid jitter

  pinMode(BTN_FREQ, INPUT_PULLUP);
  pinMode(BTN_AMP, INPUT_PULLUP);
  
  loadConfig();
  freqCtrl.setDigit( 2 );
  ampCtrl.setDigit( 0 );
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(SSD1306_WHITE);
  drawDisplay();

  Serial.begin(115200);
  
  fillInLUT();
  DDRB = 0b00000011; //set pins D8 to D9 as outputs
  PORTB = 0b00000000; //output low in all of them
  // initialize timer1 to interrupt at PWM_FR (usually 20kHz)
  noInterrupts();           // disable all interrupts
  TCCR1A = TCCR1B = TCNT1 = 0;
  OCR1A = (F_CPU / PWM_FR); 
  TCCR1B |= (1 << WGM12);   // CTC mode
  TCCR1B |= (1 << CS10);    // 1 prescaler, no prescaling
  interrupts();             // enable all interrupts
}

volatile uint8_t amplitude = 255;
bool needsUpdate = true;

void loop() {
  
  uint16_t ticksPressed = 0;

  freqCtrl.handleRotation(needsUpdate);
  freqCtrl.handleButton(needsUpdate, ticksPressed);
  if (ticksPressed > 30){ //30 is around 1s -> save current parameters to EPROM
    saveConfig();
  }

  ampCtrl.handleRotation(needsUpdate);
  ampCtrl.handleButton(needsUpdate, ticksPressed);
  if (ticksPressed > 30){ //switch on/off
    Serial.println( switchedOn );
    if( switchedOn ){ //it is on, we switch off
      stopSignalGen();
    }
    switchedOn = switchedOn?false:true;
    needsUpdate = true;
  }

  if (needsUpdate) {
    drawDisplay();
    if (switchedOn){
      amplitude = ampCtrl.getValue() * 255 / 100;
      signalGen(1, freqCtrl.getValue() / 100.0f );
    }
  }

  delay(20);
}

volatile uint32_t indexShift24 = 0;
volatile uint32_t indexIncShift24 = 4294967;

ISR(TIMER1_COMPA_vect) { // timer compare interrupt service routine
  static uint8_t portMask = 0b00000001;
  static uint8_t microsHigh = HALF_VAL;
  PORTB = portMask; //switch on the corresponding pins 
  delayMicroseconds(microsHigh);
  
  PORTB = 0b00000000; //all off

  indexShift24 += indexIncShift24; //increase. overflow makes it cycle
  
  const uint8_t sample = LUT_SIGNALS[ (indexShift24 >> 24) % N_SAMPLES];   //get sample  
  portMask = (sample >> 6) ;   //extract pin mask
  microsHigh = (sample & 0b00111111) * amplitude / 256; //extract wait micros
}

void stopSignalGen(){
  noInterrupts();  
  TIMSK1 &= ~(1 << OCIE1A); //disable the timer interrupt
  PORTB = 0b00000000;
  interrupts();
}

void signalGen(int signalType, float fr){
  //currentSignal = signalType % N_SIGNALS;
  TIMSK1 |= (1 << OCIE1A); //enable timer interrupt
  indexIncShift24 = (uint32_t) ( (float)((uint32_t)1<<24) * (float)N_SAMPLES * fr / (float)PWM_FR);
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
  display.print("Amp (%)");
  display.setCursor(0, 45);
  display.setTextSize(2);
  uintToString(ampCtrl.getValue(), str);
  display.print(str);
  //display.drawPixel(35, 59, SSD1306_WHITE);
  display.fillRect(1+(3- ampCtrl.getDigit() )*12, 63, 10, 2, SSD1306_WHITE); //duty digit

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


inline uint8_t packSample(uint8_t val) {
  if (val >= HALF_VAL){
    return 0b01000000 | (val-HALF_VAL);  
  }else{
    return 0b10000000 | (HALF_VAL-val);
  }
}

void fillInLUT() {
  for (int i = 0; i < N_SAMPLES; ++i) {/*
    LUT_SIGNALS[0][i] = packSample( i < HALF_SAMPLES ? MAX_VAL : 0 ); //square wave
    const double v = cos( i * 2.0 * PI / N_SAMPLES ); //sinusoidal
    LUT_SIGNALS[1][i] = packSample( (byte) ((v + 1.0) / 2.0 * MAX_VAL) ); //map from -1,1  to  0,MAX
    LUT_SIGNALS[2][i] = packSample( abs( i*MAX_VAL/N_SAMPLES*2 - MAX_VAL ) ); //triangular
    LUT_SIGNALS[3][i] = packSample( i * MAX_VAL / N_SAMPLES ); //saw-tooth rise
    LUT_SIGNALS[4][i] = packSample( (N_SAMPLES-i) * MAX_VAL / N_SAMPLES ); //saw-tooth fall
    */
    const double v = cos( i * 2.0 * PI / N_SAMPLES ); //sinusoidal
    LUT_SIGNALS[i] = packSample( (byte) ((v + 1.0) / 2.0 * MAX_VAL) ); //map from -1,1  to  0,MAX
  }
}
