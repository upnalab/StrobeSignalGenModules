
#include <avr/sleep.h>
#include <avr/power.h>
#include "PelicanController.h"

PelicanController controller(3,4,5); //Encoder CLK, DT and BTN. Screen uses default A4,A5

const char *COLOR_MAP_NAMES[] = {"hsv","zebra","fire", "checker", "RGB", NULL};

void setup() {
  controller.addNumber("Freq", 0, 9999, 1500, 2);
  controller.addStringList("ColMap", COLOR_MAP_NAMES);
  controller.addNumber("Duty", 0, 1000, 5, 1);
  controller.addBoolean("Enable", false);
  Serial.begin( 115200 );
  controller.init();
}

void loop() {
  boolean needsUpdate = controller.update();
  if (needsUpdate){

  }
  if (controller.needsToRender()){
    controller.render();
  }
}
