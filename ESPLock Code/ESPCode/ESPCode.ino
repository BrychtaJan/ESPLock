#include "ToneESP32.h"
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>

// ==================== Definice PINŮ a parametrů ====================
const char* ssid = "Upsilon";
const char* password = "omikrongaming";
#define EEPROM_SIZE 512
#define MAX_CARDS 20
#define CARD_LENGTH 10
#define BUZZER_PIN 32
#define BUZZER_CHANNEL 15
#define SERVO_PIN 25
#define LED_PIN 13
#define NUMPIXELS 60
#define SDA_PIN 21
#define RST_PIN 22
#define BUTTON_PIN 4
#define DELAYVAL 3
#define BRIGHT 5
#define MAX_HISTORY 50  // Max number of access history entries

// ==================== Inicializace zařízení ====================
String allowedCards[MAX_CARDS];
int cardCount = 0;
WebServer server(80);
Servo myservo;
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
MFRC522 rfid(SDA_PIN, RST_PIN);
ToneESP32 buzzer(BUZZER_PIN, BUZZER_CHANNEL);

// ==================== Proměnné ====================
bool status = false;
bool cardPresent = false;
bool learningMode = false;
bool ledsAreOn = false;
bool webLearningMode = false;  // For web-triggered learning mode
byte masterCard[4] = { 0x03, 0x97, 0x00, 0xFE };
unsigned long ledOnTime = 0;
uint8_t currentLanguage = 0; // 0=Czech, 1=English, 2=German

// Access history structure
struct AccessHistory {
  String cardId;
  unsigned long timestamp;
  bool accessGranted;
};
AccessHistory accessHistory[MAX_HISTORY];
int historyCount = 0;

// ==================== Language Strings ====================
const char* langLocked[] = {"Zamceno", "Locked", "Gesperrt"};
const char* langUnlocked[] = {"Odemceno", "Unlocked", "Entsperrt"};
const char* langLock[] = {"Zamknout", "Lock", "Sperren"};
const char* langUnlock[] = {"Odemknout", "Unlock", "Entsperren"};
const char* langCardManagement[] = {"Sprava karet", "Card Management", "Kartenverwaltung"};
const char* langAddCard[] = {"Pridat kartu", "Add card", "Karte hinzufügen"};
const char* langRemoveCard[] = {"Odebrat kartu", "Remove card", "Karte entfernen"};
const char* langAddCardReader[] = {"Pridat kartu cteckou", "Add card with reader", "Karte mit Leser hinzufügen"};
const char* langCardIdPlaceholder[] = {"ID karty (hex)", "Card ID (hex)", "Karten-ID (hex)"};
const char* langLearningStatus[] = {"Cekam na kartu...", "Waiting for card...", "Warte auf Karte..."};
const char* langAllowedCards[] = {"Povolene karty", "Allowed cards", "Erlaubte Karten"};
const char* langAccessHistory[] = {"Historie pristupu", "Access history", "Zugriffsverlauf"};
const char* langGranted[] = {"Povolen", "Granted", "Erlaubt"};
const char* langDenied[] = {"Zamitnut", "Denied", "Abgelehnt"};
const char* langTitle[] = {"RFID Zamek", "RFID Lock", "RFID Schloss"};

// ==================== Setup ====================
void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();

  pinMode(BUTTON_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  pixels.begin();
  pixels.clear();
  pixels.show();

  myservo.attach(SERVO_PIN);
  myservo.write(90);

  EEPROM.begin(EEPROM_SIZE);
  loadCardsFromEEPROM();
  loadLanguageSetting(); // Load language setting from EEPROM

  initWiFi();
  initWebServer();

  buzzer.tone(500, 50);
  delay(20);
  buzzer.tone(1500, 50);
  delay(20);
  buzzer.tone(3000, 50);
  delay(20);
  buzzer.noTone();
}

// ==================== Hlavní smyčka ====================
void loop() {
  handleLEDTimeout();
  handleLearningMode();
  handleWebLearningMode();

  if (!rfid.PICC_IsNewCardPresent()) {
    cardPresent = false;
    server.handleClient();
    return;
  }

  if (cardPresent || !rfid.PICC_ReadCardSerial()) {
    server.handleClient();
    return;
  }

  cardPresent = true;
  buzzer.tone(500, 50);

  handleAuthorizedCard(rfid.uid.uidByte, rfid.uid.size);

  rfid.PICC_HaltA();
  server.handleClient();
}

// ==================== Language Functions ====================
void loadLanguageSetting() {
  currentLanguage = EEPROM.read(250); // Store language setting at address 250
  if (currentLanguage > 2) currentLanguage = 0; // Default to Czech if invalid
}

void saveLanguageSetting() {
  EEPROM.write(250, currentLanguage);
  EEPROM.commit();
}

// ==================== Web Server Functions ====================
void initWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/lock", HTTP_POST, handleLock);
  server.on("/unlock", HTTP_POST, handleUnlock);
  server.on("/addcard", HTTP_POST, handleAddCard);
  server.on("/removecard", HTTP_POST, handleRemoveCard);
  server.on("/listcards", HTTP_GET, handleListCards);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/startlearning", HTTP_POST, handleStartLearning);
  server.on("/history", HTTP_GET, handleHistory);
  server.on("/setlanguage", HTTP_POST, handleSetLanguage);

  server.begin();
  Serial.println("HTTP server spuštěn");
}

void handleRoot() {
  String lockStatus = status ? langUnlocked[currentLanguage] : langLocked[currentLanguage];
  String lockColor = status ? "#4CAF50" : "#f44336";
  
  String html = R"=====(
  <!DOCTYPE html><html><head>
  <title>)=====" + String(langTitle[currentLanguage]) + R"=====(</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; }
    .btn { padding: 10px 20px; margin: 5px; border: none; border-radius: 5px; color: white; }
    .lock { background-color: #f44336; }
    .unlock { background-color: #4CAF50; }
    .add { background-color: #2196F3; }
    .remove { background-color: #ff9800; }
    .learn { background-color: #9c27b0; }
    .card { padding: 10px; margin: 5px; background: #f1f1f1; border-radius: 5px; }
    .status { padding: 10px; margin: 10px 0; border-radius: 5px; text-align: center; font-weight: bold; }
    .history-entry { padding: 8px; margin: 3px 0; background: #f9f9f9; border-radius: 3px; }
    .granted { color: green; }
    .denied { color: red; }
    .language-selector { margin: 10px 0; padding: 5px; }
  </style>
  <script>
    function updateStatus() {
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          document.getElementById('statusText').textContent = data.status ? ")=====" + String(langUnlocked[currentLanguage]) + R"=====(" : ")=====" + String(langLocked[currentLanguage]) + R"=====(";
          document.getElementById('statusBox').style.backgroundColor = data.status ? "#4CAF50" : "#f44336";
          setTimeout(updateStatus, 1000);
        });
    }
    
    window.onload = function() {
      updateStatus();
      loadCards();
      loadHistory();
    };
    
    function setLanguage(lang) {
      fetch('/setlanguage', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'lang=' + lang
      }).then(r => r.text()).then(data => {
        alert(data);
        location.reload();
      });
    }
  </script>
  </head>
  <body>
    <h1>)=====" + String(langTitle[currentLanguage]) + R"=====(</h1>
    
    <div class="language-selector">
      <select onchange="setLanguage(this.value)">
        <option value="0" )=====" + (currentLanguage == 0 ? "selected" : "") + R"=====(>cesky</option>
        <option value="1" )=====" + (currentLanguage == 1 ? "selected" : "") + R"=====(>English</option>
        <option value="2" )=====" + (currentLanguage == 2 ? "selected" : "") + R"=====(>Deutsch</option>
      </select>
    </div>
    
    <div id="statusBox" class="status" style="background-color: )=====" + lockColor + R"=====(;">
      <span id="statusText">)=====" + lockStatus + R"=====(</span>
    </div>
    
    <div>
      <button class="btn lock" onclick="sendCommand('lock')">)=====" + String(langLock[currentLanguage]) + R"=====(</button>
      <button class="btn unlock" onclick="sendCommand('unlock')">)=====" + String(langUnlock[currentLanguage]) + R"=====(</button>
    </div>
    
    <h2>)=====" + String(langCardManagement[currentLanguage]) + R"=====(</h2>
    <div>
      <input type="text" id="cardId" placeholder=")=====" + String(langCardIdPlaceholder[currentLanguage]) + R"=====(" />
      <button class="btn add" onclick="addCard()">)=====" + String(langAddCard[currentLanguage]) + R"=====(</button>
      <button class="btn remove" onclick="removeCard()">)=====" + String(langRemoveCard[currentLanguage]) + R"=====(</button>
      <button class="btn learn" onclick="startLearning()">)=====" + String(langAddCardReader[currentLanguage]) + R"=====(</button>
      <div id="learningStatus" style="margin-top: 5px;"></div>
    </div>
    
    <h2>)=====" + String(langAllowedCards[currentLanguage]) + R"=====(</h2>
    <div id="cardsList"></div>
    
    <h2>)=====" + String(langAccessHistory[currentLanguage]) + R"=====(</h2>
    <div id="accessHistory"></div>
    
    <script>
      function sendCommand(cmd) {
        fetch('/' + cmd, { method: 'POST' })
          .then(r => r.text()).then(alert);
      }
      
      function addCard() {
        const cardId = document.getElementById('cardId').value.trim();
        if(!cardId) return alert(')=====" + String(langCardIdPlaceholder[currentLanguage]) + R"=====(');
        
        fetch('/addcard', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'card=' + encodeURIComponent(cardId)
        }).then(r => r.text()).then(data => { alert(data); loadCards(); });
      }
      
      function removeCard() {
        const cardId = document.getElementById('cardId').value.trim();
        if(!cardId) return alert(')=====" + String(langCardIdPlaceholder[currentLanguage]) + R"=====(');
        
        fetch('/removecard', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'card=' + encodeURIComponent(cardId)
        }).then(r => r.text()).then(data => { alert(data); loadCards(); });
      }
      
      function startLearning() {
        document.getElementById('learningStatus').textContent = ")=====" + String(langLearningStatus[currentLanguage]) + R"=====(";
        fetch('/startlearning', { method: 'POST' })
          .then(r => r.text())
          .then(data => {
            alert(data);
            document.getElementById('learningStatus').textContent = "";
            loadCards();
          })
          .catch(() => {
            document.getElementById('learningStatus').textContent = "";
          });
      }
      
      function loadCards() {
        fetch('/listcards')
          .then(r => r.json())
          .then(cards => {
            const list = document.getElementById('cardsList');
            list.innerHTML = '';
            cards.forEach(card => {
              const div = document.createElement('div');
              div.className = 'card';
              div.textContent = card;
              list.appendChild(div);
            });
          });
      }
      
      function loadHistory() {
        fetch('/history')
          .then(r => r.json())
          .then(history => {
            const container = document.getElementById('accessHistory');
            container.innerHTML = '';
            
            history.forEach(entry => {
              const div = document.createElement('div');
              div.className = 'history-entry ' + (entry.granted ? 'granted' : 'denied');
              
              const status = entry.granted ? ')=====" + String(langGranted[currentLanguage]) + R"=====(' : ')=====" + String(langDenied[currentLanguage]) + R"=====(';
              div.textContent = 'Karta: ' + entry.card + ' - ' + status + ' - ' + entry.time;
              
              container.appendChild(div);
            });
          });
      }
    </script>
  </body></html>
  )=====";

  server.send(200, "text/html", html);
}

void handleSetLanguage() {
  if (!server.hasArg("lang")) {
    server.send(400, "text/plain", "Missing language parameter");
    return;
  }

  uint8_t lang = server.arg("lang").toInt();
  if (lang > 2) {
    server.send(400, "text/plain", "Invalid language code");
    return;
  }

  currentLanguage = lang;
  saveLanguageSetting();
  server.send(200, "text/plain", "Language set successfully");
}

// [Rest of your existing functions remain unchanged...]
// ==================== Funkce pro LED ====================
void handleLEDTimeout() {
  if (ledsAreOn && millis() - ledOnTime > 5000) {
    pixels.clear();
    pixels.show();
    ledsAreOn = false;
  }
}

// ==================== Funkce pro učení karet ====================
void handleLearningMode() {
  if (digitalRead(BUTTON_PIN) == HIGH && !learningMode && !webLearningMode) {
    Serial.println("REŽIM NASTAVENÍ KARTY: Přilož novou kartu nebo znovu stiskni tlačítko pro zrušení.");
    delay(200);  // Debounce

    indicateLearningMode();
    learningMode = true;
    ledsAreOn = true;
    ledOnTime = millis();

    unsigned long startTime = millis();
    while (millis() - startTime < 10000) {  // 10s timeout
      if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        for (byte i = 0; i < 4; i++) masterCard[i] = rfid.uid.uidByte[i];

        String newCard = "";
        for (byte i = 0; i < 4; i++) {
          if (masterCard[i] < 0x10) newCard += "0";
          newCard += String(masterCard[i], HEX);
        }

        // Přidání karty do seznamu
        if (cardCount < MAX_CARDS) {
          allowedCards[cardCount++] = newCard;
          saveCardsToEEPROM();
        }

        Serial.print("Nová karta nastavena: ");
        printUID(masterCard);
        Serial.println();

        indicateNewCardSet();
        rfid.PICC_HaltA();
        break;
      }

      if (digitalRead(BUTTON_PIN) == HIGH) {
        Serial.println("Změna karty zrušena.");
        break;
      }

      delay(50);
    }

    learningMode = false;
    clearLEDs();
    delay(500);
  }
}

// Handle web-triggered learning mode
void handleWebLearningMode() {
  if (webLearningMode) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      for (byte i = 0; i < 4; i++) masterCard[i] = rfid.uid.uidByte[i];

      String newCard = "";
      for (byte i = 0; i < 4; i++) {
        if (masterCard[i] < 0x10) newCard += "0";
        newCard += String(masterCard[i], HEX);
      }

      // Přidání karty do seznamu
      if (cardCount < MAX_CARDS) {
        allowedCards[cardCount++] = newCard;
        saveCardsToEEPROM();
      }

      Serial.print("Nová karta nastavena přes web: ");
      printUID(masterCard);
      Serial.println();

      indicateNewCardSet();
      rfid.PICC_HaltA();
      webLearningMode = false;
    }
  }
}

// ==================== Funkce pro ovládání zámku ====================
void handleAuthorizedCard(byte* uid, byte uidLength) {
  String uidStr = "";
  for (byte i = 0; i < uidLength; i++) {
    if (uid[i] < 0x10) uidStr += "0";
    uidStr += String(uid[i], HEX);
  }

  bool authorized = false;
  for (int i = 0; i < cardCount; i++) {
    if (allowedCards[i].equalsIgnoreCase(uidStr)) {
      authorized = true;
      break;
    }
  }

  // Add to access history
  addToHistory(uidStr, authorized);

  if (authorized) {
    Serial.println("Autorizovaná karta: " + uidStr);
    toggleLock();
  } else {
    Serial.println("Nepovolená karta: " + uidStr);
    indicateAccessDenied();
    buzzer.tone(1000, 500);
    delay(500);
    buzzer.noTone();
  }
}

void toggleLock() {
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

  buzzer.tone(2000, 250);
  delay(50);
  buzzer.tone(4000, 250);
  delay(50);
  buzzer.noTone();

  ledOnTime = millis();
  ledsAreOn = true;
}

// ==================== Access History Functions ====================
void addToHistory(String cardId, bool granted) {
  if (historyCount >= MAX_HISTORY) {
    // Shift all entries down to make room
    for (int i = 0; i < MAX_HISTORY - 1; i++) {
      accessHistory[i] = accessHistory[i + 1];
    }
    historyCount = MAX_HISTORY - 1;
  }
  
  accessHistory[historyCount].cardId = cardId;
  accessHistory[historyCount].timestamp = millis();
  accessHistory[historyCount].accessGranted = granted;
  historyCount++;
}

String getFormattedTime(unsigned long timestamp) {
  unsigned long seconds = timestamp / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  
  seconds %= 60;
  minutes %= 60;
  hours %= 24;
  
  String result = "";
  if (days > 0) result += String(days) + "d ";
  if (hours > 0 || !result.isEmpty()) result += String(hours) + "h ";
  if (minutes > 0 || !result.isEmpty()) result += String(minutes) + "m ";
  result += String(seconds) + "s ago";
  
  return result;
}

// ==================== Pomocné funkce ====================
void pulsePin() {
  digitalWrite(LED_PIN, HIGH);
  delay(20);
  digitalWrite(LED_PIN, LOW);
}

void clearLEDs() {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, 0);
  }
  pixels.show();
  ledsAreOn = false;
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
  ledsAreOn = true;
}

void indicateAccessDenied() {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(255 / BRIGHT, 0, 0));
    pixels.show();
    delay(DELAYVAL);
  }
  ledsAreOn = true;
}

void printUID(byte* uid) {
  for (byte i = 0; i < 4; i++) {
    if (uid[i] < 0x10) Serial.print("0");
    Serial.print(uid[i], HEX);
    if (i < 3) Serial.print(" ");
  }
}

// ==================== EEPROM Funkce ====================
void loadCardsFromEEPROM() {
  cardCount = EEPROM.read(0);
  if (cardCount > MAX_CARDS) cardCount = 0;

  for (int i = 0; i < cardCount; i++) {
    String card = "";
    int address = 1 + (i * CARD_LENGTH);

    for (int j = 0; j < CARD_LENGTH - 1; j++) {
      char c = EEPROM.read(address + j);
      if (c != 0xFF && c != 0) card += c;
    }

    if (card.length() >= 8) {
      allowedCards[i] = card;
    } else {
      for (int k = i; k < cardCount - 1; k++) {
        allowedCards[k] = allowedCards[k + 1];
      }
      cardCount--;
      i--;
    }
  }
  Serial.printf("Načteno %d karet z EEPROM\n", cardCount);
}

void saveCardsToEEPROM() {
  EEPROM.write(0, cardCount);

  for (int i = 0; i < cardCount; i++) {
    int address = 1 + (i * CARD_LENGTH);
    String card = allowedCards[i];

    for (int j = 0; j < CARD_LENGTH - 1; j++) {
      if (j < card.length()) {
        EEPROM.write(address + j, card[j]);
      } else {
        EEPROM.write(address + j, 0);
      }
    }
  }

  EEPROM.commit();
  Serial.printf("Uloženo %d karet do EEPROM\n", cardCount);
}

// ==================== Web Server Funkce ====================
void initWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Připojování k WiFi...");
  }
  Serial.println("Připojeno k WiFi");
  Serial.print("IP adresa: ");
  Serial.println(WiFi.localIP());
}

void handleLock() {
  if (!status) {
    server.send(200, "text/plain", "Zámek již je zamčený");
    return;
  }
  toggleLock();
  server.send(200, "text/plain", "Zámek zamčen");
}

void handleUnlock() {
  if (status) {
    server.send(200, "text/plain", "Zámek již je odemčený");
    return;
  }
  toggleLock();
  server.send(200, "text/plain", "Zámek odemčen");
}

void handleAddCard() {
  if (!server.hasArg("card")) {
    server.send(400, "text/plain", "Chybějící ID karty");
    return;
  }

  String cardId = server.arg("card");
  cardId.trim();

  if (cardId.length() < 8) {
    server.send(400, "text/plain", "Neplatný formát karty (min 8 hex znaků)");
    return;
  }

  for (int i = 0; i < cardCount; i++) {
    if (allowedCards[i].equalsIgnoreCase(cardId)) {
      server.send(200, "text/plain", "Karta již existuje: " + cardId);
      return;
    }
  }

  if (cardCount >= MAX_CARDS) {
    server.send(200, "text/plain", "Maximální počet karet (" + String(MAX_CARDS) + ") dosažen");
    return;
  }

  allowedCards[cardCount++] = cardId;
  saveCardsToEEPROM();
  server.send(200, "text/plain", "Karta přidána: " + cardId);
}

void handleRemoveCard() {
  if (!server.hasArg("card")) {
    server.send(400, "text/plain", "Chybějící ID karty");
    return;
  }

  String cardId = server.arg("card");
  bool found = false;

  for (int i = 0; i < cardCount; i++) {
    if (allowedCards[i].equalsIgnoreCase(cardId)) {
      for (int j = i; j < cardCount - 1; j++) {
        allowedCards[j] = allowedCards[j + 1];
      }
      cardCount--;
      found = true;
      break;
    }
  }

  if (found) {
    saveCardsToEEPROM();
    server.send(200, "text/plain", "Karta odebrána: " + cardId);
  } else {
    server.send(200, "text/plain", "Karta nenalezena");
  }
}

void handleListCards() {
  String json = "[";
  for (int i = 0; i < cardCount; i++) {
    if (i != 0) json += ",";
    json += "\"" + allowedCards[i] + "\"";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleStatus() {
  String json = "{\"status\":" + String(status ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleStartLearning() {
  if (webLearningMode) {
    server.send(200, "text/plain", "Již čekám na kartu");
    return;
  }
  
  webLearningMode = true;
  indicateLearningMode();
  ledsAreOn = true;
  ledOnTime = millis();
  
  server.send(200, "text/plain", "Přiložte kartu ke čtečce");
}

void handleHistory() {
  String json = "[";
  for (int i = 0; i < historyCount; i++) {
    if (i != 0) json += ",";
    json += "{\"card\":\"" + accessHistory[i].cardId + "\",";
    json += "\"granted\":";
    json += accessHistory[i].accessGranted ? "true" : "false";
    json += ",";
    json += "\"time\":\"" + getFormattedTime(millis() - accessHistory[i].timestamp) + "\"}";
  }
  json += "]";
  server.send(200, "application/json", json);
}