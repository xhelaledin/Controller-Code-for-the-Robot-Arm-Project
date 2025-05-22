#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// Button pins
const uint8_t buttonPins[] = {A3, A2, A1, A0, 13, 12, 11};
const uint8_t numButtons = 7;

// Encoder pins - Original mapping (ENC3, ENC2, ENC1)
const uint8_t encoderPins[3][2] = {
  {6, 5},  // ENC3 
  {8, 7},  // ENC2 
  {10, 9}  // ENC1 
};

// Encoder configuration
const int16_t ENCODER_STEPS[3] = {3, 3, 3};
int16_t ENCODER_MAX_VALUES[3] = {150, 180, 180};  // ENC3, ENC2, ENC1

// Encoder values
int16_t encoderValues[3] = {90, 90, 90};
int16_t lastSentEncoderValues[3] = {90, 90, 90};
uint8_t encoderStates[3] = {0};

// EEPROM
const uint8_t EEPROM_START = 0;
const uint8_t EEPROM_CONFIG_START = 100;
bool encoderChanged = false;
uint32_t lastEEPROMWrite = 0;
const uint32_t EEPROMWriteDelay = 2000; // ms

// Buttons
uint8_t buttonStates[numButtons] = {HIGH};
uint8_t lastButtonStates[numButtons] = {HIGH};
uint32_t lastDebounceTimes[numButtons] = {0};
const uint16_t debounceDelay = 50;
bool buttonPressed = false;
uint8_t lastPressedButton = 0;

uint32_t buttonPressTime = 0;
const uint32_t BUTTON_DISPLAY_DURATION = 1000; // 1 second display time
bool showingButtonPress = false;

// Custom button names
const char* buttonNames[] = {
  "notSet", "moveToStart", "openButton", 
  "closeButton", "startRecord", "stopRecord", "playRecord"
};

// Display
uint32_t lastDisplayUpdate = 0;
const uint16_t displayUpdateInterval = 100;

// Standard lookup table
const int8_t encoderLookupTable[16] = {
  0, -1, 1, 0,
  1, 0, 0, -1,
  -1, 0, 0, 1,
  0, 1, -1, 0
};

void setup() {
  Serial.begin(9600);

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Initializing..."));
  display.display();

  // Load configuration
  loadConfig();

  // Setup pins
  for (uint8_t i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    buttonStates[i] = digitalRead(buttonPins[i]);
    lastButtonStates[i] = buttonStates[i];
  }
  
  for (uint8_t i = 0; i < 3; i++) {
    pinMode(encoderPins[i][0], INPUT_PULLUP);
    pinMode(encoderPins[i][1], INPUT_PULLUP);
    encoderStates[i] = (digitalRead(encoderPins[i][0]) << 1) | digitalRead(encoderPins[i][1]);
  }

  updateDisplay();
}

void loadConfig() {
  // Load from EEPROM
  for (uint8_t i = 0; i < 3; i++) {
    EEPROM.get(EEPROM_START + i * sizeof(int16_t), encoderValues[i]);
    EEPROM.get(EEPROM_CONFIG_START + i * sizeof(int16_t), ENCODER_MAX_VALUES[i]);
    
    // Validate loaded values
    ENCODER_MAX_VALUES[i] = constrain(ENCODER_MAX_VALUES[i], 1, 360);
    encoderValues[i] = constrain(encoderValues[i], 0, ENCODER_MAX_VALUES[i]);
    encoderValues[i] = (encoderValues[i] / ENCODER_STEPS[i]) * ENCODER_STEPS[i];
    lastSentEncoderValues[i] = encoderValues[i];
  }
}

void saveConfig() {
  for (uint8_t i = 0; i < 3; i++) {
    EEPROM.put(EEPROM_START + i * sizeof(int16_t), encoderValues[i]);
    EEPROM.put(EEPROM_CONFIG_START + i * sizeof(int16_t), ENCODER_MAX_VALUES[i]);
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(F("Encoder Values:"));

  // Display encoder values
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t displayIndex = (i == 0) ? 2 : (i == 2) ? 0 : 1;
    display.setCursor(0, 12 + i * 10);
    display.print(F("ENC"));
    display.print(displayIndex + 1);
    display.print(F(": "));
    display.print(encoderValues[displayIndex]);
    display.print(F("/"));
    display.print(ENCODER_MAX_VALUES[displayIndex]);
  }

  // Show button press immediately and for 2 seconds
  if (showingButtonPress && (millis() - buttonPressTime < BUTTON_DISPLAY_DURATION)) {
    display.setCursor(0, 45);
    display.print(F("Pressed: "));
    display.print(buttonNames[lastPressedButton-1]);
  } else {
    showingButtonPress = false;
  }

  display.display();
}


void readEncoder(uint8_t encoderIndex) {
  uint8_t state = encoderStates[encoderIndex];
  state = (state << 2) | (digitalRead(encoderPins[encoderIndex][0]) << 1) | digitalRead(encoderPins[encoderIndex][1]);
  state &= 0x0F;

  int8_t direction = encoderLookupTable[state];
  
  // Reverse direction for ENC1 (index 2) and ENC3 (index 0)
  if (encoderIndex == 2 ) {
    direction = -direction; // Reverse the direction
  }

  if (direction != 0) {
    int16_t newVal = encoderValues[encoderIndex] + (direction * ENCODER_STEPS[encoderIndex]);
    newVal = constrain(newVal, 0, ENCODER_MAX_VALUES[encoderIndex]);
    newVal = (newVal / ENCODER_STEPS[encoderIndex]) * ENCODER_STEPS[encoderIndex];
    
    if (newVal != encoderValues[encoderIndex]) {
      encoderValues[encoderIndex] = newVal;
      encoderChanged = true;
      lastEEPROMWrite = millis();
    }
  }
  encoderStates[encoderIndex] = state >> 2;
}

void sendEncoderValuesIfChanged() {
  // Send in original order: ENC3, ENC2, ENC1
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t valueIndex = (i == 0) ? 2 : (i == 2) ? 0 : 1; // Maintain original mapping
    uint8_t labelNumber = (i == 0) ? 3 : (i == 2) ? 1 : 2; // Original numbering
    
    if (encoderValues[valueIndex] != lastSentEncoderValues[valueIndex]) {
      Serial.print(F("ENC"));
      Serial.print(labelNumber);
      Serial.print(F(": "));
      Serial.println(encoderValues[valueIndex]);
      lastSentEncoderValues[valueIndex] = encoderValues[valueIndex];
    }
  }
}

void checkButtons() {
  for (uint8_t i = 0; i < numButtons; i++) {
    uint8_t reading = digitalRead(buttonPins[i]);
    
    if (reading != lastButtonStates[i]) {
      lastDebounceTimes[i] = millis();
    }
    
    if ((millis() - lastDebounceTimes[i]) > debounceDelay) {
      if (reading != buttonStates[i]) {
        buttonStates[i] = reading;
        
        if (buttonStates[i] == LOW) { // Button pressed
          lastPressedButton = i + 1;
          buttonPressTime = millis(); // Record press time
          showingButtonPress = true;  // Flag to show press
          Serial.println(buttonNames[lastPressedButton-1]);
          updateDisplay(); // Immediate display update
        }
      }
    }
    lastButtonStates[i] = reading;
  }
}

void processSerialCommands() {
  while (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (input.startsWith("SETMAX:")) {
      int colonPos = input.indexOf(':');
      int spacePos = input.indexOf(' ');
      if (colonPos > 0 && spacePos > colonPos) {
        int encoderNum = input.substring(colonPos + 1, spacePos).toInt();
        int newMax = input.substring(spacePos + 1).toInt();
        
        if (encoderNum >= 1 && encoderNum <= 3 && newMax > 0 && newMax <= 360) {
          uint8_t index = (encoderNum == 3) ? 0 : (encoderNum == 1) ? 2 : 1;
          ENCODER_MAX_VALUES[index] = newMax;
          
          encoderValues[index] = constrain(encoderValues[index], 0, newMax);
          encoderValues[index] = (encoderValues[index] / ENCODER_STEPS[index]) * ENCODER_STEPS[index];
          
          Serial.print(F("ENC"));
          Serial.print(encoderNum);
          Serial.print(F(" max set to: "));
          Serial.println(newMax);
          
          encoderChanged = true;
          lastEEPROMWrite = millis();
        }
      }
    }
  }
}

void loop() {
  uint32_t now = millis();

  processSerialCommands();
  checkButtons();

  // Encoder handling
  bool valueChanged = false;
  for (uint8_t i = 0; i < 3; i++) {
    int16_t prev = encoderValues[i];
    readEncoder(i);
    if (encoderValues[i] != prev) valueChanged = true;
  }

  // Send encoder values if changed
  if (valueChanged) {
    sendEncoderValuesIfChanged();
  }

  // Display update
  if (valueChanged || buttonPressed || (now - lastDisplayUpdate >= displayUpdateInterval)) {
    updateDisplay();
    lastDisplayUpdate = now;
    buttonPressed = false;
  }

  // EEPROM save after delay
  if (encoderChanged && (now - lastEEPROMWrite) > EEPROMWriteDelay) {
    saveConfig();
    encoderChanged = false;
  }
}