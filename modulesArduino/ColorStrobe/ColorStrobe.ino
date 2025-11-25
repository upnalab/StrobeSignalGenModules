#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#include <avr/power.h>
//external libs
#include <PololuLedStrip.h>
#include <Encoder.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//internal
#include "EncoderControl.h"

Adafruit_SSD1306 display(128, 64, &Wire, -1);  //A4->SDA and A5->SCL

// ---------- Encoders: better if first pin has interrups (D2,D3 in Nano
Encoder encFreq(3, 4);
#define BTN_FREQ 5
Encoder encColormap(2, 6);
#define BTN_COLORMAP 7

#define PIN_LED_STRIP 9
#define N_COLORMAPS 5
#define COLORS_PER_MAP 16
const char* COLORMAP_NAMES[N_COLORMAPS] = { "fire", "gray", "checkboard", "hsv", "zebra" };
rgb_color COLORMAPS[N_COLORMAPS][COLORS_PER_MAP];
volatile uint8_t currentColor = 0;
#define STROBE_N_LEDS 8
rgb_color dataForLedStrip[STROBE_N_LEDS];

EncoderControl freqCtrl(encFreq, BTN_FREQ, 1, 9999, 1500);                      // start at 15 Hz it is divided by 100
EncoderControl colormapCtrl(encColormap, BTN_COLORMAP, 0, N_COLORMAPS - 1, 0);  // start at gradient 0
bool switchedOn = false;

PololuLedStrip<PIN_LED_STRIP> ledStrip;

// ---------- EEPROM values: save/load with persistance
typedef struct {
  int32_t _signature;
  int16_t value1;
  int16_t value2;
  bool switchedOn;
} ConfigData;
ConfigData config;
void saveConfig() {
  config.value1 = freqCtrl.getValue();
  config.value2 = colormapCtrl.getValue();
  config.switchedOn = switchedOn;
  EEPROM.put(0, config);
}
void loadConfig() {
  EEPROM.get(0, config);
  if (config._signature != 0xBEEF) {  //first time loading the Config, use default values
    config._signature = 0xBEEF;
    config.value1 = 1500;
    config.value2 = 10;
    config.switchedOn = false;
    saveConfig();
  }
  freqCtrl.setValue(config.value1);
  colormapCtrl.setValue(config.value2);
  switchedOn = config.switchedOn;
}


// ---------- Function prototypes ----------
void drawDisplay();
void uintToString(uint16_t val, char* str);
void fillInColorMaps();
void setTimer1InterruptAt(float frequency);

void setup() {
  // turn off everything we can 
  power_adc_disable();
  ADCSRA = 0;
  power_spi_disable();
  //power_timer0_disable();
  TIMSK0 &= ~_BV(TOIE0); //so we just disable its interrupt to avoid jitter
  
  fillInColorMaps();
  pinMode(BTN_FREQ, INPUT_PULLUP);
  pinMode(BTN_COLORMAP, INPUT_PULLUP);

  loadConfig();
  freqCtrl.setDigit(2);
  colormapCtrl.setDigit(0);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(SSD1306_WHITE);
  drawDisplay();

  //Serial.begin(115200);

  noInterrupts();  //setup timer1
  TCCR1A = TCCR1B = 0;
  TCCR1B |= (1 << WGM12);   // Set CTC mode (Clear Timer on Compare Match)
  TCCR1B |= (1 << CS12);    // Set prescaler to 256
  setTimer1InterruptAt(freqCtrl.getValue() / 100 *  COLORS_PER_MAP);
  interrupts();             

 
}

void loop() {
  bool needsUpdate = false;
  uint16_t ticksPressed = 0;

  freqCtrl.handleRotation(needsUpdate);
  freqCtrl.handleButton(needsUpdate, ticksPressed);
  if (ticksPressed > 30) {  //30 is around 1s -> save current parameters to EPROM
    //Serial.println( ticksPressed );
    saveConfig();
  }

  colormapCtrl.handleRotation(needsUpdate);
  colormapCtrl.handleButton(needsUpdate, ticksPressed);
  if (ticksPressed > 30) {  //switch on/off
    switchedOn = switchedOn ? false : true;
    needsUpdate = true;
  }

  if (needsUpdate) {
    drawDisplay();
    if (switchedOn) {
        setTimer1InterruptAt(freqCtrl.getValue() / 100.0f *  COLORS_PER_MAP);
    }
  }

  delay(20);
}

#define TIMER1_OCR_FOR_FREQ(f) ( (16000000UL / (2UL * 256UL * (f))) - 1 )
void setTimer1InterruptAt(float freq){
  if (freq <= 0 || freq >= 9999)
    return;
  noInterrupts();
  OCR1A = (uint16_t) TIMER1_OCR_FOR_FREQ(freq);
  TIMSK1 |= (1 << OCIE1A);  // Enable Timer Compare Interrupt
  interrupts();
}

ISR(TIMER1_COMPA_vect) {  //show the next color on the strobe
  ledStrip.write(dataForLedStrip, STROBE_N_LEDS);

  int16_t colorMapIndex = colormapCtrl.getValue();
  
  rgb_color col = COLORMAPS[colorMapIndex][currentColor];
  for (int i = 0; i < STROBE_N_LEDS; i++) {
    dataForLedStrip[i] = col;
  }
  currentColor = (currentColor + 1) % COLORS_PER_MAP;

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
  display.fillRect(1 + (3 - freqCtrl.getDigit()) * 12, 29, 10, 2, SSD1306_WHITE);  // Highlight frequency digit

  display.setCursor(0, 35);
  display.setTextSize(1);
  display.print("Colormap");
  display.setCursor(0, 45);
  display.setTextSize(2);
  uint8_t val = colormapCtrl.getValue();
  display.print(COLORMAP_NAMES[val]);
  display.fillRect(1 + (3 - colormapCtrl.getDigit()) * 12, 63, 10, 2, SSD1306_WHITE);  //duty digit

  display.setCursor(100, 1);
  display.setTextSize(1);
  if (switchedOn) {
    display.print("ON");
  } else {
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

rgb_color hsvToRgb(uint16_t h, uint8_t s, uint8_t v);
void fillInColorMaps() {
  //fire
  for (int i = 0; i < COLORS_PER_MAP; i++) {
    uint8_t val = i * 255 / COLORS_PER_MAP;
    int val3 = val*3;
    COLORMAPS[0][i] = rgb_color( constrain(val3,0,255) , constrain(val3 - 256,0,255)  , constrain(val3 - 256 -256,0,255) );
  }

  //gray
  for (int i = 0; i < COLORS_PER_MAP; i++) {
    uint8_t val = i * 255 / COLORS_PER_MAP;
    COLORMAPS[1][i] = rgb_color(val, val, val);
  }

  //checker increasing grid 010011000011110000000011111111...
  for (int index = 0, block = 1; index < COLORS_PER_MAP; block <<= 1) {
    for (int count = 0; count < block && index < COLORS_PER_MAP; count++)
      COLORMAPS[2][index++] = rgb_color(0,0,0);
    for (int count = 0; count < block && index < COLORS_PER_MAP; count++)
      COLORMAPS[2][index++] = rgb_color(255,255,255);
  }


  //hsv
  for (int i = 0; i < COLORS_PER_MAP; i++) {
    uint8_t val = i * 360 / COLORS_PER_MAP;
    COLORMAPS[3][i] = hsvToRgb(val,255,255);
  }

  //zebra
  for (int i = 0; i < COLORS_PER_MAP; i++) {
    COLORMAPS[4][i] = i % 2 == 0 ? rgb_color(0,0,0) : rgb_color(255,255,255);
  }
}

// Converts a color from HSV to RGB.
// h is hue, as a number between 0 and 360.
// s is the saturation, as a number between 0 and 255.
// v is the value, as a number between 0 and 255.
rgb_color hsvToRgb(uint16_t h, uint8_t s, uint8_t v)
{
    uint8_t f = (h % 60) * 255 / 60;
    uint8_t p = (255 - s) * (uint16_t)v / 255;
    uint8_t q = (255 - f * (uint16_t)s / 255) * (uint16_t)v / 255;
    uint8_t t = (255 - (255 - f) * (uint16_t)s / 255) * (uint16_t)v / 255;
    uint8_t r = 0, g = 0, b = 0;
    switch((h / 60) % 6){
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
    }
    return rgb_color(r, g, b);
}