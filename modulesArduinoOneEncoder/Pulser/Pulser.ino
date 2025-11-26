
#include <avr/sleep.h>
#include <avr/power.h>
#include "PelicanController.h"

#define OUTPUT_PIN 9

PelicanController controller(3, 4, 5);  //Encoder CLK, DT and BTN. Screen uses default A4,A5

void gndPin(uint8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
};
void updateTimer1(uint16_t freq, uint16_t duty);
void stopTimer1();
void updateModule();

void setup() {
  // turn off everything we can
  power_adc_disable();
  ADCSRA = 0;
  power_spi_disable();
  //power_timer0_disable(); //disabling timer0 breaks the encoder library
  TIMSK0 &= ~_BV(TOIE0);  //so we just disable its interrupt to avoid jitter

  pinMode(OUTPUT_PIN, OUTPUT);  //9
  gndPin(8);
  gndPin(10);
  gndPin(11);


  Serial.begin(115200);

  controller.addNumber("Freq", 0, 9999, 1500, 2);
  controller.addNumber("Duty", 0, 1000, 10, 1);
  controller.addBoolean("Enable", true);

  controller.init();
  updateModule();
}

void loop() {
  boolean needsUpdate = controller.update();
  if (needsUpdate) {
    updateModule();
  }
  if (controller.needsToRender()) {
    controller.render();
  }
}

void updateModule() {
  bool isEnabled = controller.getBoolean(2);
  if (isEnabled) {
    updateTimer1(controller.getNumber(0), controller.getNumber(1));
  } else {
    stopTimer1();
  }
}

void stopTimer1() {
  noInterrupts();
  TCCR1A = TCCR1B = TCNT1 = 0;
  digitalWrite(OUTPUT_PIN, LOW);
  interrupts();
}

void updateTimer1(uint16_t freq, uint16_t duty) {
  freq = constrain(freq, 1, 9999);  //freq is x100
  duty = constrain(duty, 0, 1000);  //duty is from 0 to 1000

  const uint32_t F_CPU_HZ = 16000000UL;
  const uint16_t PRESCALER = 256;
  uint32_t top = F_CPU_HZ * 100 / PRESCALER / freq - 1;
  if (top > 65535) top = 65535;  // 16-bit limit
  uint32_t compare = (top * duty) / 1000;
  if (compare > top) compare = top;

  noInterrupts();
  pinMode(OUTPUT_PIN, OUTPUT);
  TCCR1A = TCCR1B = TCNT1 = 0;            // Reset Timer1
  ICR1 = (uint16_t)top;                   // Period
  OCR1A = (uint16_t)compare;              // Duty cycle
  TCCR1A = (1 << COM1A1) | (1 << WGM11);  // Fast PWM mode using ICR1 as TOP: Mode 14 (WGM13:WGM10 = 1110)
  TCCR1B = (1 << WGM13) | (1 << WGM12);
  TCCR1B |= (1 << CS12);  //prescaler to 256
  interrupts();
}
