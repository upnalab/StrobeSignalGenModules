
#include <avr/sleep.h>
#include <avr/power.h>
#include <MD_AD9833.h>
//#include <SPI.h>
#include "PelicanController.h"

#define OUTPUT_PIN 9

MD_AD9833  AD(A0, A1, A2); //signal generator module data,clk,fsync SW spi
PelicanController controller(3,4,5);  //Encoder CLK, DT and BTN. Screen uses default A4,A5

const char *WAVE_NAMES[] = {"OFF","Sine","Squ1","Squ2","Trian", NULL};
enum MD_AD9833::mode_t MODES[] = {MD_AD9833::MODE_OFF , MD_AD9833::MODE_SINE , MD_AD9833::MODE_SQUARE1 , MD_AD9833::MODE_SQUARE2 , MD_AD9833::MODE_TRIANGLE};

void updateModule();

void setup() {
  // turn off everything we can
  power_adc_disable();
  ADCSRA = 0;
  power_spi_disable();
  //power_timer0_disable(); //disabling timer0 breaks the encoder library
  TIMSK0 &= ~_BV(TOIE0);  //so we just disable its interrupt to avoid jitter

  AD.begin();
  AD.setMode(MD_AD9833::MODE_OFF);

  Serial.begin(115200);

  controller.addNumber("Freq", 0, 9999, 1500, 2);
  controller.addStringList("Wave", WAVE_NAMES, 1);
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
    uint8_t index = controller.getStringIndex(1);
    enum MD_AD9833::mode_t mode = MODES[index];
    AD.setMode( mode );
    AD.setFrequency(MD_AD9833::CHAN_0, controller.getNumber(0) / 100.f);
  } else {
    AD.setMode(MD_AD9833::MODE_OFF);
  }
}

