#include <Servo.h>
Servo myservo;

#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include <MFRC522.h>

#define PIN        6
#define NUMPIXELS 61

#define SDA_PIN 10
#define RST_PIN 9
#define bright 3
#define BUTTON_PIN 7

Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);
MFRC522 rfid(SDA_PIN, RST_PIN);

#define DELAYVAL 3

bool status = false;
bool cardPresent = false;
bool learningMode = false;

int pos1 = 0;
int pos2 = 180;

// Výchozí autorizovaná karta (03 97 00 FE)
byte masterCard[4] = {0x03, 0x97, 0x00, 0xFE};

// Časovač pro automatické zhasnutí LED
unsigned long ledOnTime = 0;
bool ledsAreOn = false;

void setup() {
  pixels.begin();
  SPI.begin();
  rfid.PCD_Init();

  Serial.begin(9600);
  pinMode(5, OUTPUT);
  pinMode(BUTTON_PIN, INPUT); // externí pulldown
  pixels.clear();
  pixels.show();
  myservo.attach(3);
}

void loop() {
  // === Automatické zhasnutí LED po 5 sekundách ===
  if (ledsAreOn && millis() - ledOnTime > 5000) {
    pixels.clear();
    pixels.show();
    ledsAreOn = false;
  }

  // === Režim učení nové karty ===
  if (digitalRead(BUTTON_PIN) == HIGH && !learningMode) {
    Serial.println("REŽIM NASTAVENÍ KARTY: Přilož novou kartu nebo znovu stiskni tlačítko pro zrušení.");
    
    if (digitalRead(BUTTON_PIN) == HIGH) {
    return;
  }

    // ORANŽOVÉ LEDKY – indikace režimu nastavení
    for (int i = 0; i < NUMPIXELS; i++) {
      pixels.setPixelColor(i, pixels.Color(255 / bright, 75 / bright, 0)); // oranžová
      pixels.show();
      delay(DELAYVAL);
    }
    pixels.show();
    ledsAreOn = true;
    ledOnTime = millis();

    learningMode = true;

    // Čekání na kartu nebo další stisk tlačítka pro zrušení
    while (true) {
      if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        for (byte i = 0; i < 4; i++) {
          masterCard[i] = rfid.uid.uidByte[i];
        }

        Serial.print("Nová karta nastavena: ");
        printUID(masterCard);
        Serial.println();

        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
        for (int i = 0; i < NUMPIXELS; i++) {
      pixels.setPixelColor(i, pixels.Color( 250 / bright, 0, 175 / bright)); // oranžová
      pixels.show();
      delay(DELAYVAL);
    }
    delay(500);
    for (int i = 0; i < NUMPIXELS; i++) {
      pixels.setPixelColor(i, pixels.Color( 0, 0, 0)); // oranžová
      pixels.show();
      delay(DELAYVAL);
    }
        break;
      }

      if (digitalRead(BUTTON_PIN) == HIGH) {
        Serial.println("Změna karty zrušena. Zámek se zavírá.");
        status = false;

        for (int i = 0; i < NUMPIXELS; i++) {
      pixels.setPixelColor(i, pixels.Color( 0, 0, 0)); // oranžová
      pixels.show();
      delay(DELAYVAL);
    }
    
        pixels.show();
        ledsAreOn = false;
        delay(500); // debounce
        break;
      }

      delay(50);
    }

    learningMode = false;
    delay(500); // debounce
  }

  // === Běžný provoz ===
  if (!rfid.PICC_IsNewCardPresent()) {
    cardPresent = false;
    return;
  }

  if (cardPresent) {
    return;
  }

  if (!rfid.PICC_ReadCardSerial())
    return;

  cardPresent = true;

  if (compareUID(rfid.uid.uidByte, masterCard)) {
    Serial.println("Autorizovaná karta přiložena.");

    digitalWrite(5, HIGH);
    delay(10);
    digitalWrite(5, LOW);

    if (!status) {
      for (int i = 0; i < NUMPIXELS; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 255 / bright, 0)); // ZELENÁ při odemčení
        pixels.show();
        delay(DELAYVAL);
      }
      myservo.write(pos1);
      status = true;
    } else {
      for (int i = 0; i < NUMPIXELS; i++) {
        pixels.setPixelColor(i, pixels.Color(255 / bright, 0, 0)); // ČERVENÁ při zamčení
        pixels.show();
        delay(DELAYVAL);
      }
      myservo.write(pos2);
      status = false;
    }

    ledOnTime = millis();
    ledsAreOn = true;
  } else {
    Serial.print("Nepovolená karta: ");
    printUID(rfid.uid.uidByte);
    Serial.println();
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// === Porovnání UID ===
bool compareUID(byte *a, byte *b) {
  for (byte i = 0; i < 4; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

// === Tisk UID na sériový port ===
void printUID(byte *uid) {
  for (byte i = 0; i < 4; i++) {
    if (uid[i] < 0x10) Serial.print("0");
    Serial.print(uid[i], HEX);
    if (i < 3) Serial.print(" ");
  }
}
