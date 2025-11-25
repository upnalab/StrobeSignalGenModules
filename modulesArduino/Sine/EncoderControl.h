class EncoderControl {
public:
  EncoderControl(Encoder& encoder, uint8_t buttonPin, int16_t minVal, int16_t maxVal, int16_t initialValue)
    : enc(encoder), btnPin(buttonPin), minVal(minVal), maxVal(maxVal), lastPos(0), lastBtnState(HIGH),
      value(constrain(initialValue, minVal, maxVal)), digit(0) {}

  int16_t getValue() const { return value; }
  void setValue(int16_t val) {value = constrain(val, minVal, maxVal); }
  uint8_t getDigit() const { return digit; }
  void setDigit(uint8_t d) {digit = constrain(d, 0, 3); }
  
  bool handleRotation(bool& needsUpdate) {
    long newPos = enc.read() / 4; // Adjust sensitivity
    if (newPos != lastPos) {
      long delta = newPos - lastPos;
      int16_t step = 1;
      if (digit == 1) step = 10;
      else if (digit == 2) step = 100;
      else if (digit == 3) step = 1000;
      value -= delta * step;
      value = constrain(value, minVal, maxVal);
      lastPos = newPos;
      needsUpdate = true;
    }
  }

  void handleButton(bool& needsUpdate, uint16_t& tPressed) {
    tPressed = 0;
    uint8_t btnState = digitalRead(btnPin);
    if (btnState == LOW){ //pressed
      if (lastBtnState == HIGH){
        digit = (digit + 1) % 4;
        needsUpdate = true;
      }
      ticksPressed += 1;
    }else{ //not pressed
      tPressed = ticksPressed;
      ticksPressed = 0;
    }
    lastBtnState = btnState;
  }
  
private:
  Encoder& enc;
  long lastPos;

  const uint8_t btnPin;
  uint8_t lastBtnState;
  uint8_t digit;
  uint16_t ticksPressed;

  int16_t value;
  const int16_t minVal, maxVal;
};
