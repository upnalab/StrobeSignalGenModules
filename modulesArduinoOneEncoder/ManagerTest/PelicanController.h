#include <Arduino.h>
#include <Wire.h>
//external libs
#include <Encoder.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

#define PELICAN_CTRL_MAX_PARAMS 4
#define PELICAN_EEPROM_CODE 0x5ECB

class PelicanController {
public:
  bool printDebugInfo = false;

  PelicanController(int encPinClk, int encPinData, int btn, uint16_t ticksButtonPress=400)
    : display(128, 64, &Wire, -1),
      encoder(encPinClk, encPinData),
      pinBtn(btn),
      ticksForButtonPress(ticksButtonPress){

  }

  void init() {
    pinMode(pinBtn, INPUT_PULLUP);
    lastBtn = digitalRead(pinBtn);
    encLastPos = encoder.read() / 4;
    loadFromEEPROM();
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.setTextColor(SSD1306_WHITE);
    render();
  }

  void addNumber(const char* name, long minV, long maxV, long val, uint8_t nDecimals=0) {
    if (paramCount >= PELICAN_CTRL_MAX_PARAMS) return;
    Param& p = params[paramCount++];
    p.name = name;
    p.type = NUM;
    p.num.minVal = minV;
    p.num.maxVal = maxV;
    p.num.value = val;
    p.num.nDigits = calcNDigits(maxV);
    p.num.nDecimals = nDecimals;
  }

  void addBoolean(const char* name, bool val) {
    if (paramCount >= PELICAN_CTRL_MAX_PARAMS) return;
    Param& p = params[paramCount++];
    p.name = name;
    p.type = BOOL;
    p.boolean.value = val;
  }

  void addStringList(const char* name, const char** opts) {
    if (paramCount >= PELICAN_CTRL_MAX_PARAMS) return;
    Param& p = params[paramCount++];
    p.name = name;
    p.type = STR;
    p.str.options = opts;
    p.str.index = 0;
    p.str.nOptions = countOptions(opts);
  }

  // -------------------------------------------------------------------
  // MAIN UPDATE (returns true if a parameter changed)
  // -------------------------------------------------------------------
  bool update() {
    needsUpdate = false;

    ticks++;

    // ------------------ ROTARY ENCODER --------------------
    long newPos = encoder.read() / 4;
    long delta = newPos - encLastPos;
    if (delta != 0) {
      rotate(delta);
      encLastPos = newPos;
      needsRender = true;
    }

    // ----------------------- BUTTON ------------------------
    bool btn = !digitalRead(pinBtn);  // active low
    uint32_t diff = ticks - btnDownTick;
    // button pressed (with debounce)
    if (btn && !lastBtn && diff > ticksForButtonPress) {
      if (printDebugInfo){
        Serial.print( "pressing" );
        Serial.println( diff );
      }
      click();  
      btnDownTick = ticks;
      needsRender = true;
    }

    // button released
    else if (!btn && lastBtn) {
      if (printDebugInfo){
        Serial.print( "releasing" );
        Serial.println( diff );
      }
      if (diff > ticksForButtonPress * 1024) {   // long press → cycle mode
        if (printDebugInfo) Serial.println( "Changing mode" );
        mode = (mode + 1) % MODE_LAST_MODE;
        needsRender = true;
      } else if (diff > ticksForButtonPress * 256) { // medium press → save
        if (printDebugInfo) Serial.println( "Saving config" );
        saveToEEPROM();
      }
      btnDownTick = ticks;
    }

    lastBtn = btn;

    return needsUpdate;
  }

  // -------------------------------------------------------------------
  // RENDER SCREEN
  // -------------------------------------------------------------------
  void render() {
    display.clearDisplay();

    for (int i = 0; i < paramCount; i++) {
      Param& p = params[i];
      int y = i * 16;
      display.setCursor(0, y);
      display.setTextSize(1);
      display.print(p.name);

      display.setCursor(48, y);
      display.setTextSize(2);
      printValue(p);

      if (p.type == NUM && p.num.nDecimals > 0){
        int xPos = 47 + (p.num.nDigits - p.num.nDecimals) * 12;
        display.drawPixel(xPos, y+14, SSD1306_WHITE);
      }
    }

    int yPos = current * 16;
    display.fillRect(45, yPos, (state == DIALING_DIGIT ? 2 : 1), 16, SSD1306_WHITE);
    if (state == SELECTING_DIGIT || state == DIALING_DIGIT ||
        mode == MODE_DIGIT_ROT || mode == MODE_DIGIT_PRESS) {
      Param& p = params[current];
      if (p.type == NUM) {
        int xPos = 48 + (p.num.nDigits - currentDigit - 1) * 12;
        if (state == DIALING_DIGIT) {
          display.drawRect(xPos, yPos, 12, 16, SSD1306_WHITE);
        } else {
          display.drawRect(xPos, yPos + 15, 12, 1, SSD1306_WHITE);
        }
      }
    }


    display.display();
    needsRender = false;
  }

  // -------------------------------------------------------------------
  // GETTERS
  // -------------------------------------------------------------------
  boolean needsToRender() const {
    return needsRender;
  }

  long getNumber(uint8_t i) const {
    return params[i].num.value;
  }
  bool getBoolean(uint8_t i) const {
    return params[i].boolean.value;
  }
  const uint8_t getStringIndex(uint8_t i) const {
    return params[i].str.index;
  }

private:
  enum ParamType { NUM, BOOL, STR };
  enum ControllerState { NAVIGATION, SELECTING_DIGIT, DIALING_DIGIT };
  enum ControllerMode { MODE_PARAM = 0, MODE_DIGIT_ROT, MODE_DIGIT_PRESS, MODE_LAST_MODE };
  ControllerMode mode = MODE_PARAM;
  ControllerState state = NAVIGATION;

  struct Param {
    const char* name;
    ParamType type;

    union {
      struct {
        long value;
        long minVal;
        long maxVal;
        uint8_t nDigits;
        uint8_t nDecimals;
      } num;

      struct {
        bool value;
      } boolean;

      struct {
        const char** options;
        int8_t nOptions;
        int8_t index;
      } str;
    };
  };

  Param params[PELICAN_CTRL_MAX_PARAMS];
  uint8_t paramCount = 0;
  uint8_t current = 0;
  uint8_t currentDigit = 0;

  const uint16_t ticksForButtonPress;

  // ticks
  uint32_t ticks = 0;
  uint32_t btnDownTick = 0;

  // encoder
  Encoder encoder;
  long encLastPos;
  int pinBtn;
  bool lastBtn;

  Adafruit_SSD1306 display;
  bool needsRender = true;
  bool needsUpdate = true;

  void printValue(const Param& p) {
    char str[] = "0000000";
    if (p.type == NUM) {
      uintToString(p.num.value, str, p.num.nDigits);
      display.print(str);
    } else if (p.type == BOOL) {
      display.print(p.boolean.value ? "ON" : "off");
    } else if (p.type == STR) {
      display.print(p.str.options[p.str.index]);
    }
  }

  void click() {
    if (mode == MODE_PARAM) {
      Param& p = params[current];
      if (p.type == NUM) {
        if (state == NAVIGATION) {
          state = SELECTING_DIGIT;
          currentDigit = 0;
        } else if (state == SELECTING_DIGIT) {
          state = DIALING_DIGIT;
        } else if (state == DIALING_DIGIT) {
          state = SELECTING_DIGIT;
        }
      } else {
        increaseDigit(1);
      }
    } else if (mode == MODE_DIGIT_ROT) {
      changeSelectedDigit(-1);
    } else if (mode == MODE_DIGIT_PRESS) {
      state = (state == NAVIGATION) ? DIALING_DIGIT : NAVIGATION;
    }
  }

  void rotate(int dir) {
    if (mode == MODE_PARAM) {
      if (state == NAVIGATION) {
        current = (current + paramCount - dir) % paramCount;
        return;
      }
      Param& p = params[current];
      if (p.type == NUM) {
        if (state == SELECTING_DIGIT) {
          changeSelectedDigit(dir);
        } else if (state == DIALING_DIGIT) {
          increaseDigit(dir);
        }
      }
    } else if (mode == MODE_DIGIT_ROT) {
      if (lastBtn){
        changeSelectedDigit(dir);
        btnDownTick = ticks;
      } else {
        increaseDigit(dir);
      }
    } else if (mode == MODE_DIGIT_PRESS) {
      if (state == NAVIGATION) changeSelectedDigit(dir);
      else increaseDigit(dir);
    }
  }

  void increaseDigit(int dir) {
    Param& p = params[current];
    if (p.type == NUM) {
      int16_t step = pow10(currentDigit);
      p.num.value -= step * dir;
      p.num.value = constrain(p.num.value, p.num.minVal, p.num.maxVal);
    } else if (p.type == BOOL) {
      p.boolean.value = !p.boolean.value;
    } else if (p.type == STR) {
      p.str.index = (p.str.index + p.str.nOptions + dir) % p.str.nOptions;
    }
    needsUpdate = true;
  }

  void changeSelectedDigit(int dir) {
    Param& p = params[current];
    if (p.type == NUM){
      currentDigit += dir;
      if (currentDigit < 0 || currentDigit >= p.num.nDigits) {
        state = NAVIGATION;
        current = (current + paramCount - dir) % paramCount;
      } else {
        return;
      }
    } else {
      current = (current + paramCount - dir) % paramCount;
    }

    Param& next = params[current];
    if (next.type == NUM){
      if (dir > 0 ) currentDigit = 0;
      else currentDigit = next.num.nDigits-1;
    }
  }

  void uintToString(uint16_t val, char* str, uint8_t nDigits) {
    str[nDigits] = '\0';
    int i = nDigits - 1;
    while (val > 0 && i >= 0) {
      str[i] = (val % 10) + '0';
      val /= 10;
      i--;
    }
    for (; i >= 0; i--) {
      str[i] = '0';
    }
  }

  uint8_t calcNDigits(uint16_t maxV) {
    uint8_t digits = 1;
    while (maxV > 9) {
      maxV /= 10;
      digits++;
    }
    return digits;
  }

  uint16_t pow10(uint8_t digits) {
    uint16_t val = 1;
    while (digits > 0) {
      val *= 10;
      digits--;
    }
    return val;
  }

  uint8_t countOptions(const char** opts) {
    uint8_t count = 0;
    while (opts[count] != nullptr) count++;
    return count;
  }

  // -------------------------------------------------------------------
  // EEPROM
  // -------------------------------------------------------------------
  void saveToEEPROM() {
    int addr = 0;

    const uint32_t KEY_CODE = PELICAN_EEPROM_CODE;
    EEPROM.put(addr, KEY_CODE);
    addr += sizeof(KEY_CODE);
    EEPROM.put(addr, mode);
    addr += sizeof(mode);

    for (int i = 0; i < paramCount; i++) {
      const Param& p = params[i];
      if (p.type == NUM) {
        EEPROM.put(addr, p.num.value);
        addr += sizeof(p.num.value);
      } else if (p.type == BOOL) {
        EEPROM.write(addr, p.boolean.value);
        addr += sizeof(p.boolean.value);
      } else if (p.type == STR) {
        EEPROM.write(addr, p.str.index);
        addr += sizeof(p.str.index);
      }
    }
  }

  void loadFromEEPROM() {
    int addr = 0;
    uint32_t KEY_CODE;

    EEPROM.get(addr, KEY_CODE);
    addr += sizeof(KEY_CODE);

    if (KEY_CODE != PELICAN_EEPROM_CODE) {
      saveToEEPROM();
    }

    EEPROM.get(addr, mode);
    addr += sizeof(mode);

    for (int i = 0; i < paramCount; i++) {
      Param& p = params[i];
      if (p.type == NUM) {
        EEPROM.get(addr, p.num.value);
        addr += sizeof(p.num.value);
        p.num.value = constrain(p.num.value, p.num.minVal, p.num.maxVal);
      } else if (p.type == BOOL) {
        p.boolean.value = EEPROM.read(addr);
        addr += sizeof(p.boolean.value);
      } else if (p.type == STR) {
        p.str.index = EEPROM.read(addr);
        addr += sizeof(p.str.index);
        p.str.index %= p.str.nOptions;
      }
    }

    needsUpdate = true;
    needsRender = true;
  }
};
