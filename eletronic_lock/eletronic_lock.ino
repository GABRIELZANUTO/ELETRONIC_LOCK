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


const int lockPin = 25;
const int unlockPin = 26;
const int pwmChannelLock = 0;
const int pwmChannelUnlock = 1;
const int pwmFreq = 8000;
const int pwmResolution = 8;
const int maxDutyCycle = 255;

const int EEPROM_SIZE = 64;


enum LockState {
  LOCKED,
  UNLOCKED,
  WAITING_FOR_PASSWORD,
  ERROR_STATE,
  CHANGE_PASSWORD
};
LockState currentState = LOCKED;


unsigned long previousMillis = 0;
unsigned long errorDisplayMillis = 0;
unsigned long unlockMillis = 0;
unsigned long lockDuration = 10000;

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
String newPassword = "";

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


  display.init();
  display.clear();
  display.display();

 
  lockServo.attach(servoPin);

  ledcSetup(pwmChannelLock, pwmFreq, pwmResolution);
  ledcSetup(pwmChannelUnlock, pwmFreq, pwmResolution);
  ledcAttachPin(lockPin, pwmChannelLock);
  ledcAttachPin(unlockPin, pwmChannelUnlock);

  ledcWrite(pwmChannelLock, 0);
  ledcWrite(pwmChannelUnlock, 0);

  if (currentState == LOCKED) {
    closeLock();
    displayStatus("Trancado");
  } else if (currentState == UNLOCKED) {
    openLock();
    displayStatus("Destrancado");
    unlockMillis = millis();
  } else {
    currentState = LOCKED;
    closeLock();
    displayStatus("Trancado");
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
        displayStatus("Trancado");
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
          currentState = UNLOCKED;
          saveState();
          openLock();
          displayStatus("Destrancado");
          unlockMillis = millis();
        } else {
          currentState = ERROR_STATE;
          errorDisplayMillis = millis();  
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

    case ERROR_STATE:
      if (millis() - errorDisplayMillis >= 3000) {
        resetErrorState();
      }
      break;

    case UNLOCKED:
      if (key == '*') {
        currentState = LOCKED;
        saveState();
        closeLock();
        displayStatus("Trancado");
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
      Serial.println("Senha atualizada.");
    } else {
      Serial.println("Senha muito curta.");
    }
  } else if (command.startsWith("SET_LOCK_TIME ")) {
    unsigned long timeInSeconds = command.substring(14).toInt();
    if (timeInSeconds > 0) {
      lockDuration = timeInSeconds * 1000;
      EEPROM.writeULong(50, lockDuration);
      EEPROM.commit();
      Serial.println("Duração da trava atualizada.");
    } else {
      Serial.println("Tempo de trava inválido.");
    }
  } else if (command == "STATUS") {
    Serial.println("Status: " + String(currentState == LOCKED ? "Trancado" : "Destrancado"));
  } else {
    Serial.println("Comando desconhecido.");
  }
}

void resetErrorState() {
  currentState = LOCKED;
  displayStatus("Trancado");
}
