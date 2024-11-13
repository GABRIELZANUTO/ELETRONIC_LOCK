#include <Wire.h>
#include <SSD1306Wire.h>  
#include <ESP32Servo.h>
#include <Keypad.h>
#include <EEPROM.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
SSD1306Wire display(0x3C, 32, 13); // Endereço I2C e pinos SDA (32) e SCL (13) para ESP32

// Configurações do Servo
Servo lockServo;
const int servoPin = 23;

// Configurações do PWM para controle de trava
const int lockPin = 25;
const int unlockPin = 26;
const int pwmChannelLock = 0;
const int pwmChannelUnlock = 1;
const int pwmFreq = 8000;
const int pwmResolution = 8;
const int maxDutyCycle = 255;

// Configurações do EEPROM
const int EEPROM_SIZE = 64;

// Gerenciamento de Estado
enum LockState {
  LOCKED,
  UNLOCKED,
  WAITING_FOR_PASSWORD,
  ERROR_STATE,
  CHANGE_PASSWORD
};
LockState currentState = LOCKED;

// Variáveis de Temporização
unsigned long previousMillis = 0;
unsigned long errorDisplayMillis = 0;
unsigned long unlockMillis = 0;
unsigned long lockDuration = 10000;

// Configurações do Keypad
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','4','7','*'},
  {'2','5','8','0'},
  {'3','6','9','#'},
  {'A','B','C','D'}
};

byte rowPins[COLS] = {21, 22, 23, 19};  // Adaptado para o ESP32
byte colPins[ROWS] = {18, 5, 17, 16};   // Adaptado para o ESP32
 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

String inputPassword = "";
String savedPassword = "1234";
String newPassword = "";

// Funções de controle
void displayStatus(const char* status);
void openLock();
void closeLock();
void saveState();
void loadState();
void processSerialCommand(String command);
void softStartPWM(int pin, int channel);
void handleKeypadInput(char key);
void resetErrorState();
void changePassword();

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  loadState();

  // Inicialização do display OLED
  display.init();
  display.clear();
  display.display();

  // Inicialização do Servo
  lockServo.attach(servoPin);

  // Inicialização dos canais PWM
  ledcSetup(pwmChannelLock, pwmFreq, pwmResolution);
  ledcSetup(pwmChannelUnlock, pwmFreq, pwmResolution);
  ledcAttachPin(lockPin, pwmChannelLock);
  ledcAttachPin(unlockPin, pwmChannelUnlock);

  ledcWrite(pwmChannelLock, 0);
  ledcWrite(pwmChannelUnlock, 0);

  if (currentState == LOCKED) {
    closeLock();
    displayStatus("Locked");
  } else if (currentState == UNLOCKED) {
    openLock();
    displayStatus("Unlocked");
    unlockMillis = millis();
  } else {
    currentState = LOCKED;
    closeLock();
    displayStatus("Locked");
  }
}

void loop() {
  unsigned long currentMillis = millis();

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    processSerialCommand(command);
  }

  char key = keypad.getKey();
  if (key) {
    handleKeypadInput(key);
  }

  switch (currentState) {
    case LOCKED:
      break;
    case UNLOCKED:
      if (currentMillis - unlockMillis >= lockDuration) {
        currentState = LOCKED;
        saveState();
        closeLock();
        displayStatus("Locked");
      }
      break;
    case ERROR_STATE:
      if (currentMillis - errorDisplayMillis >= 3000) {
        resetErrorState();
      }
      break;
  }
}

void handleKeypadInput(char key) {
  Serial.println(key);  // Exibe no monitor serial a tecla pressionada

  switch (currentState) {
    case LOCKED:
      if (key == '*') {
        inputPassword = "";
        currentState = WAITING_FOR_PASSWORD;
        displayStatus("Enter PWD:");
      }
      break;

    case WAITING_FOR_PASSWORD:
      if (key == '#') {
        if (inputPassword == savedPassword) {
          currentState = UNLOCKED;
          saveState();
          openLock();
          displayStatus("Unlocked");
          unlockMillis = millis();
        } else {
          currentState = ERROR_STATE;
          errorDisplayMillis = millis();
          displayStatus("Incorrect!");
        }
        inputPassword = "";
      } else if (key == 'A') {
        currentState = CHANGE_PASSWORD;
        newPassword = "";
        displayStatus("New PWD:");
      } else {
        inputPassword += key;
        displayStatus(inputPassword.c_str());
      }
      break;

    case CHANGE_PASSWORD:
      if (key == '#') {
        if (newPassword.length() >= 4) {
          savedPassword = newPassword;
          EEPROM.writeString(1, savedPassword);
          EEPROM.commit();
          currentState = LOCKED;
          displayStatus("PWD Changed");
        } else {
          currentState = ERROR_STATE;
          errorDisplayMillis = millis();
          displayStatus("PWD Error");
        }
        newPassword = "";
      } else {
        newPassword += key;
        displayStatus(newPassword.c_str());
      }
      break;

    case UNLOCKED:
      if (key == '*') {
        currentState = LOCKED;
        saveState();
        closeLock();
        displayStatus("Locked");
      }
      break;
  }
}

void displayStatus(const char* status) {
  display.clear();
  display.drawString(0, 0, status);
  display.display();
}

void openLock() {
  ledcWrite(pwmChannelLock, 0);
  softStartPWM(unlockPin, pwmChannelUnlock);
  lockServo.write(90);
}

void closeLock() {
  ledcWrite(pwmChannelUnlock, 0);
  softStartPWM(lockPin, pwmChannelLock);
  lockServo.write(0);
}

void softStartPWM(int pin, int channel) {
  for (int dutyCycle = 0; dutyCycle <= maxDutyCycle; dutyCycle++) {
    ledcWrite(channel, dutyCycle);
    delay(1);
  }
  unsigned long startMillis = millis();
  while (millis() - startMillis < 1000) {
    ledcWrite(channel, maxDutyCycle);
    if (channel == pwmChannelLock) {
      ledcWrite(pwmChannelUnlock, 0);
    } else {
      ledcWrite(pwmChannelLock, 0);
    }
  }
  for (int dutyCycle = maxDutyCycle; dutyCycle >= 0; dutyCycle--) {
    ledcWrite(channel, dutyCycle);
    delay(1);
  }
  ledcWrite(pwmChannelLock, 0);
  ledcWrite(pwmChannelUnlock, 0);
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

void processSerialCommand(String command) {
  command.trim();
  if (command.startsWith("SET_PASSWORD ")) {
    String newPass = command.substring(13);
    if (newPass.length() >= 4) {
      savedPassword = newPass;
      EEPROM.writeString(1, savedPassword);
      EEPROM.commit();
      Serial.println("Password updated.");
     
    } else {
      Serial.println("Password too short.");
    }
  } else if (command.startsWith("SET_LOCK_TIME ")) {
    unsigned long timeInSeconds = command.substring(14).toInt();
    if (timeInSeconds > 0) {
      lockDuration = timeInSeconds * 1000;
      EEPROM.writeULong(50, lockDuration);
      EEPROM.commit();
      Serial.println("Lock duration updated.");
    } else {
      Serial.println("Invalid lock time.");
    }
  } else if (command == "STATUS") {
    Serial.println("Status: " + String(currentState == LOCKED ? "Locked" : "Unlocked"));
  } else {
    Serial.println("Unknown command.");
  }
}

void resetErrorState() {
  currentState = LOCKED;
  displayStatus("Locked");
}
