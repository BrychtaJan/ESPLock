// ==================== Knihovny ====================
#include "ToneESP32.h"
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include <MFRC522.h>

// ==================== Definice PINŮ a parametrů ====================
#define BUZZER_PIN      32
#define BUZZER_CHANNEL  15
#define SERVO_PIN       25
#define LED_PIN         13
#define NUMPIXELS       60
#define SDA_PIN         21
#define RST_PIN         22
#define BUTTON_PIN      4
#define DELAYVAL        3
#define BRIGHT          5

// ==================== Inicializace zařízení ====================
Servo myservo;
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
MFRC522 rfid(SDA_PIN, RST_PIN);
ToneESP32 buzzer(BUZZER_PIN, BUZZER_CHANNEL);

// ==================== Proměnné ====================
bool status = false;
bool cardPresent = false;
bool learningMode = false;
bool ledsAreOn = false;

int pos1 = 0;
int pos2 = 180;

byte masterCard[4] = { 0x03, 0x97, 0x00, 0xFE };  // Výchozí autorizovaná karta
unsigned long ledOnTime = 0;

// ==================== Setup ====================
void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();

  pinMode(BUTTON_PIN, INPUT);

  pixels.begin();
  pixels.clear();
  pixels.show();

  myservo.attach(SERVO_PIN);
  myservo.write(90);  // výchozí pozice

  buzzer.tone(500, 50); delay(20);
  buzzer.tone(1500, 50); delay(20);
  buzzer.tone(3000, 50); delay(20);
  buzzer.noTone();
}

// ==================== Loop ====================
void loop() {
  handleLEDTimeout();
  handleLearningMode();

  if (!rfid.PICC_IsNewCardPresent()) {
    cardPresent = false;
    return;
  }

  if (cardPresent || !rfid.PICC_ReadCardSerial()) return;

  cardPresent = true;
  buzzer.tone(500, 50);

  if (compareUID(rfid.uid.uidByte, masterCard)) {
    handleAuthorizedCard();
  } else {
    Serial.print("Nepovolená karta: ");
    printUID(rfid.uid.uidByte);
    Serial.println();
  }

  rfid.PICC_HaltA();
}

// ==================== Funkce ====================

// Automatické zhasnutí LED po 5 sekundách
void handleLEDTimeout() {
  if (ledsAreOn && millis() - ledOnTime > 5000) {
    pixels.clear();
    pixels.show();
    ledsAreOn = false;
  }
}

// Režim učení nové karty
void handleLearningMode() {
  if (digitalRead(BUTTON_PIN) == HIGH && !learningMode) {
    Serial.println("REŽIM NASTAVENÍ KARTY: Přilož novou kartu nebo znovu stiskni tlačítko pro zrušení.");

    if (digitalRead(BUTTON_PIN) == HIGH) return;

    indicateLearningMode();

    learningMode = true;
    ledsAreOn = true;
    ledOnTime = millis();

    while (true) {
      if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        for (byte i = 0; i < 4; i++) masterCard[i] = rfid.uid.uidByte[i];

        Serial.print("Nová karta nastavena: ");
        printUID(masterCard);
        Serial.println();

        indicateNewCardSet();
        break;
      }

      if (digitalRead(BUTTON_PIN) == HIGH) {
        Serial.println("Změna karty zrušena. Zámek se zavírá.");
        status = false;
        clearLEDs();
        break;
      }

      delay(50);
    }

    learningMode = false;
    delay(500);  // debounce
  }
}

// Autorizace karty
void handleAuthorizedCard() {
  Serial.println("Autorizovaná karta přiložena.");

  if (!status) {
    myservo.write(20);
    pulsePin();
    myservo.write(90);
    indicateAccessGranted();
    status = true;
  } else {
    myservo.write(160);
    pulsePin();
    myservo.write(90);
    indicateAccessDenied();
    status = false;
  }

  buzzer.tone(2000, 250); delay(50);
  buzzer.tone(4000, 250); delay(50);
  buzzer.noTone();

  ledOnTime = millis();
  ledsAreOn = true;
}

// Pomocné funkce
void pulsePin() {
  digitalWrite(LED_PIN, HIGH);
  delay(20);
  digitalWrite(LED_PIN, LOW);
}

void clearLEDs() {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, 0);
    pixels.show();
    delay(DELAYVAL);
  }
  pixels.show();
  ledsAreOn = false;
  delay(500);
}

void indicateLearningMode() {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(255 / BRIGHT, 75 / BRIGHT, 0));
    pixels.show();
    delay(DELAYVAL);
  }
}

void indicateNewCardSet() {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(250 / BRIGHT, 0, 175 / BRIGHT));
    pixels.show();
    delay(DELAYVAL);
  }
  delay(500);
  clearLEDs();
}

void indicateAccessGranted() {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(0, 255 / BRIGHT, 0));
    pixels.show();
    delay(DELAYVAL);
  }
}

void indicateAccessDenied() {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(255 / BRIGHT, 0, 0));
    pixels.show();
    delay(DELAYVAL);
  }
}

bool compareUID(byte *a, byte *b) {
  for (byte i = 0; i < 4; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

void printUID(byte *uid) {
  for (byte i = 0; i < 4; i++) {
    if (uid[i] < 0x10) Serial.print("0");
    Serial.print(uid[i], HEX);
    if (i < 3) Serial.print(" ");
  }
}
