# StrobeSignalGenModules
Tiny Modules to generate signals mainly to flash lights.
In general, they use a powerbank a usb-c trigger to get 5V or 12V, an Arduino to generate the signals, Encoder+OLED as UI, and a L298N to amplify.

PowerBank -> USBTrig
UsbTrig.V -> Arduino.VIn, L298N.V+
UsbTrig.GND -> Arduino.GND, L298N.GND

Arduino.GND -> OLED.GND, Encoder1.GND, Encoder2.GND
Arduino.5V -> OLED.+, Encoder1.+, Encoder2.+

Arduino.A4, A5 -> OLED.SDAT, SCLK
Arduino.D3, D4, D5 -> Encoder1.CLK, DT, SWB
Arduino.D2, D6, D7 -> Encoder2.CLK, DT, SWB [Optional]

Arduino.D8,D9,D10,D11 -> L298N.IN1, IN2, IN3, IN4

Sometimes there is a step-up or step-down after the UsbTrig if a different voltage is needed
