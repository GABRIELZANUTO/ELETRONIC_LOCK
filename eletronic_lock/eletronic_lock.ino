#include <Wire.h>
#include <SSD1306Wire.h>  
#include <ESP32Servo.h>
#include <Keypad.h>
#include <EEPROM.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
SSD1306Wire display(0x3C, 32, 13); 

Servo lockServo;
const int servoPin = 14;

const int EEPROM_SIZE = 64;

enum LockState {
  LOCKED,
  UNLOCKED,
  WAITING_FOR_PASSWORD,
  ERROR_STATE
};
LockState currentState = LOCKED;

unsigned long unlockMillis = 0;
unsigned long lockDuration = 5000;
unsigned long displayDelayMillis = 0; 
bool isDisplayLocked = false;

const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','3','2','A'},
  {'4','6','5','B'},
  {'7','9','8','C'},
  {'*','#','0','D'}
};

byte rowPins[COLS] = {21, 22, 23, 19};  
byte colPins[ROWS] = {18, 5, 17, 16};  

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

String inputPassword = "";
String savedPassword = "1234";

void displayStatus(const char* status);
void openLock();
void closeLock();
void saveState();
void loadState();
void handleKeypadInput(char key);
void resetErrorState();

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  loadState();

  display.init();
  display.clear();
  display.display();

  if (currentState == LOCKED) {
    closeLock();
    displayStatus("Trancado");
  } else {
    currentState = LOCKED;
    closeLock();
    displayStatus("Trancado");
  }
}

void loop() {
  unsigned long currentMillis = millis();

  char key = keypad.getKey();
  if (key) {
    handleKeypadInput(key);
  }

  switch (currentState) {
    case UNLOCKED:
      if (currentMillis - unlockMillis >= lockDuration) {
        closeLock(); 
        displayDelayMillis = currentMillis; 
        isDisplayLocked = true; 
        currentState = LOCKED;  
      }
      break;

    case LOCKED:
      if (isDisplayLocked && currentMillis - displayDelayMillis >= 3000) { 
        displayStatus("Trancado");
        isDisplayLocked = false; 
      }
      break;

    case ERROR_STATE:
      if (currentMillis - unlockMillis >= 3000) { 
        resetErrorState();
      }
      break;
  }
}

void handleKeypadInput(char key) {
  Serial.println(key);  
  switch (currentState) {
    case LOCKED:
      if (key == '*') {
        inputPassword = "";
        currentState = WAITING_FOR_PASSWORD;
        displayStatus("Digite a Senha:");
      }
      break;

    case WAITING_FOR_PASSWORD:
      if (key == '#') {
        if (inputPassword == savedPassword) {
          openLock();
          displayStatus("Destrancado");
          unlockMillis = millis();
          currentState = UNLOCKED;
        } else {
          currentState = ERROR_STATE;
          unlockMillis = millis();  
          displayStatus("Senha Incorreta!");
        }
        inputPassword = ""; 
      } else {
        inputPassword += key;
        String maskedPassword = "";
        for (unsigned int i = 0; i < inputPassword.length(); i++) {
          maskedPassword += '*';
        }
        displayStatus(maskedPassword.c_str());
      }
      break;

    case UNLOCKED:
      if (key == '*') {
        closeLock();
        displayStatus("Trancado");
        currentState = LOCKED;
      }
      break;
  }
}

void displayStatus(const char* status) {
  display.clear();
  display.setFont(ArialMT_Plain_16); 
  int16_t textWidth = display.getStringWidth(status);
  int16_t x = (SCREEN_WIDTH - textWidth) / 2;
  int16_t y = (SCREEN_HEIGHT - 16) / 2; 
  display.drawString(x, y, status);
  display.display();
}

void openLock() {
  lockServo.attach(servoPin);
  lockServo.write(90);  
  delay(1000);          
  lockServo.detach();
}

void closeLock() {
  lockServo.attach(servoPin);
  lockServo.write(0);  // Fecha o servo
  delay(1000);        
  lockServo.detach();
}

void saveState() {
  EEPROM.write(0, (uint8_t)currentState);
  EEPROM.commit();
}

void loadState() {
  currentState = (LockState)EEPROM.read(0);
  savedPassword = EEPROM.readString(1);
  if (savedPassword == "") {
    savedPassword = "1234";
  }
}

void resetErrorState() {
  currentState = LOCKED;
  displayStatus("Trancado");
}
