#include <Arduino.h>
#include <avr/power.h>
#include <avr/pgmspace.h>
#include "PelicanController.h"

// -----------------------------------------------------------------------------
// PWM carrier selection: change ONLY this line.
// -----------------------------------------------------------------------------
#define PWM_CARRIER_31K25 0
#define PWM_CARRIER_40K   1
#define PWM_CARRIER_62K5  2

#define PWM_CARRIER PWM_CARRIER_62K5

// Arduino Uno/Nano, ATmega328P @ 16 MHz.
// D9 = OC1A, D10 = OC1B.
// Positive waveform: OCR1A=duty, OCR1B=0.
// Negative waveform: OCR1A=0, OCR1B=duty.

#define PWM_PIN_POS 9
#define PWM_PIN_NEG 10



#if PWM_CARRIER == PWM_CARRIER_31K25
constexpr uint32_t PWM_SAMPLE_RATE = 31250UL;
constexpr uint16_t PWM_TOP = 511;
#elif PWM_CARRIER == PWM_CARRIER_40K
constexpr uint32_t PWM_SAMPLE_RATE = 40000UL;
constexpr uint16_t PWM_TOP = 399;
#elif PWM_CARRIER == PWM_CARRIER_62K5
constexpr uint32_t PWM_SAMPLE_RATE = 62500UL;
constexpr uint16_t PWM_TOP = 255;
#else
#error Invalid PWM_CARRIER selection
#endif

PelicanController controller(3, 4, 5);

const char *WAVE_NAMES[] = {
  "Sine",
  "Square",
  "HalfSq",
  "Triang",
  "SawRis",
  "SawFal",
  NULL
};

enum WaveType {
  WAVE_SINE = 0,
  WAVE_SQUARE,
  WAVE_HALF_SQUARE,
  WAVE_TRIANGLE,
  WAVE_SAW_RISE,
  WAVE_SAW_FALL
};

// Only sine uses a LUT. Range: -127..+127.
const int8_t SINE_TABLE[256] PROGMEM = {
     0,    3,    6,    9,   12,   16,   19,   22,   25,   28,   31,   34,   37,   40,   43,   46,
    49,   51,   54,   57,   60,   63,   65,   68,   71,   73,   76,   78,   81,   83,   85,   88,
    90,   92,   94,   96,   98,  100,  102,  104,  106,  107,  109,  111,  112,  113,  115,  116,
   117,  118,  120,  121,  122,  122,  123,  124,  125,  125,  126,  126,  126,  127,  127,  127,
   127,  127,  127,  127,  126,  126,  126,  125,  125,  124,  123,  122,  122,  121,  120,  118,
   117,  116,  115,  113,  112,  111,  109,  107,  106,  104,  102,  100,   98,   96,   94,   92,
    90,   88,   85,   83,   81,   78,   76,   73,   71,   68,   65,   63,   60,   57,   54,   51,
    49,   46,   43,   40,   37,   34,   31,   28,   25,   22,   19,   16,   12,    9,    6,    3,
     0,   -3,   -6,   -9,  -12,  -16,  -19,  -22,  -25,  -28,  -31,  -34,  -37,  -40,  -43,  -46,
   -49,  -51,  -54,  -57,  -60,  -63,  -65,  -68,  -71,  -73,  -76,  -78,  -81,  -83,  -85,  -88,
   -90,  -92,  -94,  -96,  -98, -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116,
  -117, -118, -120, -121, -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127,
  -127, -127, -127, -127, -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118,
  -117, -116, -115, -113, -112, -111, -109, -107, -106, -104, -102, -100,  -98,  -96,  -94,  -92,
   -90,  -88,  -85,  -83,  -81,  -78,  -76,  -73,  -71,  -68,  -65,  -63,  -60,  -57,  -54,  -51,
   -49,  -46,  -43,  -40,  -37,  -34,  -31,  -28,  -25,  -22,  -19,  -16,  -12,   -9,   -6,   -3
};

volatile uint32_t phaseIncrement = 0;
volatile uint8_t currentWave = WAVE_SINE;
volatile uint16_t amplitudeScale = 0;
volatile bool signalEnabled = true;

volatile uint32_t phaseAccumulator = 0;

// Returns a normalized signed sample, approximately -127..+127.
// Only sine touches flash; the other waveforms are integer expressions.
inline int8_t getWaveSample(uint8_t phase, uint8_t wave) {
  switch (wave) {
    case WAVE_SINE:
      return (int8_t)pgm_read_byte(&SINE_TABLE[phase]);

    case WAVE_SQUARE:
      return phase < 128 ? 127 : -127;

    case WAVE_HALF_SQUARE:
      // Positive pulse for half a period, zero for the other half.
      return phase < 128 ? 127 : 0;

    case WAVE_TRIANGLE: {
      // Starts at zero, rises positive, crosses zero, then goes negative.
      uint8_t quadrant = phase >> 6;
      uint8_t x = phase & 0x3F;
      if (quadrant == 0) return (int8_t)(x << 1);
      if (quadrant == 1) return (int8_t)(127 - (x << 1));
      if (quadrant == 2) return (int8_t)-(x << 1);
      return (int8_t)(-127 + (x << 1));
    }

    case WAVE_SAW_RISE: {
      int16_t v = (int16_t)phase - 128;
      if (v < -127) v = -127;
      return (int8_t)v;
    }

    case WAVE_SAW_FALL: {
      int16_t v = 127 - (int16_t)phase;
      if (v < -127) v = -127;
      return (int8_t)v;
    }
  }

  return 0;
}

void setupPWM();
void updateSignalParameters();
inline int8_t getWaveSample(uint8_t phase, uint8_t wave);

void setup() {
  power_adc_disable();
  ADCSRA = 0;
  power_spi_disable();
  TIMSK0 &= ~_BV(TOIE0);

  Serial.begin(115200);

  controller.addNumber("Freq", 0, 9999, 1500, 2);
  controller.addNumber("Amp", 0, 99, 50);
  controller.addStringList("Wave", WAVE_NAMES, WAVE_SINE);

  controller.init();
  setupPWM();
  updateSignalParameters();
}

void loop() {
  if (controller.update()) updateSignalParameters();
  if (controller.needsToRender()) controller.render();
}

void setupPWM() {
  pinMode(PWM_PIN_POS, OUTPUT);
  pinMode(PWM_PIN_NEG, OUTPUT);
  digitalWrite(PWM_PIN_POS, LOW);
  digitalWrite(PWM_PIN_NEG, LOW);

  noInterrupts();

  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 0;
  OCR1B = 0;

  // Both OC1A (D9) and OC1B (D10) stay enabled in non-inverting PWM mode.
  // Prescaler is always 1. Only TOP / Fast-PWM mode changes.
#if PWM_CARRIER == PWM_CARRIER_31K25
  // 9-bit Fast PWM, TOP=511, Mode 6.
  // 16 MHz / 512 = 31.25 kHz.
  TCCR1A = _BV(COM1A1) | _BV(COM1B1) | _BV(WGM11);
  TCCR1B = _BV(WGM12) | _BV(CS10);

#elif PWM_CARRIER == PWM_CARRIER_40K
  // Fast PWM with ICR1 as TOP, TOP=399, Mode 14.
  // 16 MHz / (399 + 1) = 40 kHz.
  ICR1 = PWM_TOP;
  TCCR1A = _BV(COM1A1) | _BV(COM1B1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);

#elif PWM_CARRIER == PWM_CARRIER_62K5
  // 8-bit Fast PWM, TOP=255, Mode 5.
  // 16 MHz / 256 = 62.5 kHz.
  TCCR1A = _BV(COM1A1) | _BV(COM1B1) | _BV(WGM10);
  TCCR1B = _BV(WGM12) | _BV(CS10);
#endif

  TIFR1 = _BV(TOV1);
  TIMSK1 = _BV(TOIE1);

  interrupts();
}

void updateSignalParameters() {
  uint32_t freq100 = controller.getNumber(0);
  uint8_t amp = controller.getNumber(1);
  uint8_t wave = controller.getStringIndex(2);

  uint32_t newIncrement = 0;
  if (freq100 != 0) {
    newIncrement = ((uint64_t)freq100 << 32) / ((uint64_t)PWM_SAMPLE_RATE * 100ULL);
  }

  // Precompute amplitude scaling outside the ISR.
  // ISR keeps the cheap: duty = magnitude * amplitudeScale >> 7.
  // At Amp=99, magnitude=127 reaches PWM_TOP.
  const uint32_t denom = 99UL * 127UL;
  uint16_t newAmplitudeScale = ((uint32_t)amp * PWM_TOP * 128UL + denom - 1) / denom;

  noInterrupts();
  phaseIncrement = newIncrement;
  currentWave = wave;
  amplitudeScale = newAmplitudeScale;
  signalEnabled = (freq100 != 0 && amp != 0);
  interrupts();
}

ISR(TIMER1_OVF_vect) {
  phaseAccumulator += phaseIncrement;

  uint8_t phase = (uint8_t)(phaseAccumulator >> 24);
  int8_t sample = signalEnabled ? getWaveSample(phase, currentWave) : 0;

  uint8_t magnitude = sample < 0 ? (uint8_t)-sample : (uint8_t)sample;
  uint16_t duty = ((uint16_t)magnitude * amplitudeScale) >> 7;

  if (sample > 0) {
    OCR1A = duty;
    OCR1B = 0;
  } else if (sample < 0) {
    OCR1A = 0;
    OCR1B = duty;
  } else {
    OCR1A = 0;
    OCR1B = 0;
  }
}
