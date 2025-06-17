#include "ToneESP32.h"
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>

// ==================== Definice PINŮ a parametrů ====================
const char *ssid = "Upsilon";
const char *password = "omikrongaming";
const char *rootpassword = "admin123"; // default root password
bool buttonEnabled = true;             // Default to enabled
#define EEPROM_SIZE 512
#define THEME_EEPROM_ADDR 251
#define BUTTON_ENABLED_EEPROM_ADDR 252
#define MAX_CARDS 20
#define CARD_LENGTH 10
#define BUZZER_PIN 14
#define BUZZER_CHANNEL 15
#define SERVO_PIN 13
#define LED_PIN 21
#define NUMPIXELS 20
#define SDA_PIN 5
#define RST_PIN 22
#define BUTTON_PIN 4
#define DELAYVAL 5
#define BRIGHT 2
#define MAX_HISTORY 50 // Max number of access history entries

// ==================== Inicializace zařízení ====================
String allowedCards[MAX_CARDS];
int cardCount = 0;
WebServer server(80);
Servo myservo;
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
MFRC522 rfid(SDA_PIN, RST_PIN);
ToneESP32 buzzer(BUZZER_PIN, BUZZER_CHANNEL);

// ==================== Proměnné ====================
bool isUserLoggedIn = false;                            // Stav přihlášení
unsigned long loginTime = 0;                            // Čas přihlášení
const unsigned long LOGIN_TIMEOUT = 8 * 60 * 60 * 1000; // 8 hodin v milisekundách
bool darkTheme = false;
bool status = false;
bool cardPresent = false;
bool learningMode = false;
bool ledsAreOn = false;
bool webLearningMode = false; // For web-triggered learning mode
byte masterCard[4] = {0x03, 0x97, 0x00, 0xFE};
unsigned long ledOnTime = 0;
uint8_t currentLanguage = 0; // 0=Czech, 1=English, 2=German

// Access history structure
struct AccessHistory
{
  String cardId;
  unsigned long timestamp;
  bool accessGranted;
};
AccessHistory accessHistory[MAX_HISTORY];
int historyCount = 0;

// ==================== Language Strings ====================
const char *langLocked[] = {"Zamčeno", "Locked", "Gesperrt"};
const char *langUnlocked[] = {"Odemčeno", "Unlocked", "Entsperrt"};
const char *langLock[] = {"Zamknout", "Lock", "Sperren"};
const char *langUnlock[] = {"Odemknout", "Unlock", "Entsperren"};
const char *langCardManagement[] = {"Správa karet", "Card Management", "Kartenverwaltung"};
const char *langAddCard[] = {"Přidat kartu", "Add card", "Karte hinzufügen"};
const char *langRemoveCard[] = {"Odebrat kartu", "Remove card", "Karte entfernen"};
const char *langAddCardReader[] = {"Přidat / odstranit kartu čtečkou", "Add card with reader", "Karte mit dem Lesegerät hinzufügen / entfernen"};
const char *langCardIdPlaceholder[] = {"ID karty (hex)", "Card ID (hex)", "Karten-ID (hex)"};
const char *langLearningStatus[] = {"Cekam na kartu...", "Waiting for card...", "Warte auf Karte..."};
const char *langAllowedCards[] = {"Povolené karty", "Allowed cards", "Erlaubte Karten"};
const char *langAccessHistory[] = {"Historie přistupu", "Access history", "Zugriffsverlauf"};
const char *langGranted[] = {"Povolen", "Granted", "Erlaubt"};
const char *langDenied[] = {"Zamítnut", "Denied", "Abgelehnt"};
const char *langTitle[] = {"ESPLock", "ESPLock", "ESPLock"};
const char *logoutText[] = {"Odhlásit se", "Logout", "Abmelden"};

// ==================== Setup ====================
void setup()
{
  buzzer.tone(500, 50);
  delay(50);
  buzzer.noTone();

  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();

  pinMode(BUTTON_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  pixels.begin();
  pixels.clear();
  pixels.show();

  myservo.attach(SERVO_PIN);
  myservo.write(180);

  EEPROM.begin(EEPROM_SIZE);
  loadCardsFromEEPROM();
  loadLanguageSetting(); // Load language setting from EEPROM
  loadThemeSetting();
  loadButtonSetting();

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
void loop()
{
  handleLEDTimeout();
  handleLearningMode();
  handleWebLearningMode();

  // Zpracuj kartu pouze pokud NENÍ aktivní režim učení
  if (!learningMode && !webLearningMode)
  {
    if (!rfid.PICC_IsNewCardPresent())
    {
      cardPresent = false;
      server.handleClient();
      return;
    }

    if (cardPresent || !rfid.PICC_ReadCardSerial())
    {
      server.handleClient();
      return;
    }

    cardPresent = true;
    buzzer.tone(500, 50);

    handleAuthorizedCard(rfid.uid.uidByte, rfid.uid.size);

    rfid.PICC_HaltA();
  }

  server.handleClient();

  // Automatické odhlášení po nečinnosti
  if (isUserLoggedIn && (millis() - loginTime > LOGIN_TIMEOUT))
  {
    isUserLoggedIn = false;
    Serial.println("Automatické odhlášení z důvodu nečinnosti");
  }
}

// ==================== Language Functions ====================
void loadLanguageSetting()
{
  currentLanguage = EEPROM.read(250); // Store language setting at address 250
  if (currentLanguage > 2)
    currentLanguage = 0; // Default to Czech if invalid
}

void saveLanguageSetting()
{
  EEPROM.write(250, currentLanguage);
  EEPROM.commit();
}

// ==================== Web Server Functions ====================
void initWebServer()
{
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
  server.on("/toggletheme", HTTP_POST, handleToggleTheme);
  server.on("/login", HTTP_GET, handleLogin);
  server.on("/login", HTTP_POST, handleLoginSubmit);
  server.on("/logout", HTTP_POST, handleLogout);
  server.on("/togglebutton", HTTP_POST, handleToggleButton);

  server.begin();
  Serial.println("HTTP server spuštěn");
}

void handleRoot()
{
  if (!isLoggedIn())
  {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
    return;
  }

  // Připravíme texty podle aktuálního jazyka
  const char *logoutText[] = {"Odhlásit se", "Logout", "Abmelden"};
  const char *timeoutText[] = {"Odhlásím se za:", "Logout in:", "Abmeldung in:"};
  const char *langOptions[] = {"Česky", "English", "Deutsch"};

  // Výpočet zbývajícího času
  unsigned long remainingTime = LOGIN_TIMEOUT - (millis() - loginTime);
  int hours = remainingTime / (60 * 60 * 1000);
  int minutes = (remainingTime % (60 * 60 * 1000)) / (60 * 1000);

  String lockStatus = status ? langUnlocked[currentLanguage] : langLocked[currentLanguage];
  String lockColor = status ? "#4CAF50" : "#f44336";

  String themeStyles = darkTheme ?
                                 R"=====(
    body { background-color: #121212; color: #e0e0e0; }
    .card, .history-entry { background: #1e1e1e !important; color: #e0e0e0; border-left: 4px solid #333; }
    .status { color: white; }
    input, select { background-color: #333; color: white; border: 1px solid #444; }
    .btn.theme { background-color: #555; }
    .logout-container {
      position: fixed;
      top: 15px;
      right: 15px;
      display: flex;
      align-items: center;
      gap: 10px;
      background: rgba(30, 30, 30, 0.8);
      padding: 8px 12px;
      border-radius: 20px;
      z-index: 1000;
    }
    .logout-btn { 
      background-color: #d32f2f; 
      color: white; 
      padding: 6px 12px; 
      border: none; 
      border-radius: 15px; 
      cursor: pointer;
      font-size: 14px;
    }
    .timeout-text {
      color: #e0e0e0;
      font-size: 14px;
    }
    .language-selector {
      padding: 8px;
      border-radius: 5px;
      background: #333;
      color: white;
      border: 1px solid #444;
    }
    )====="
                                 :
                                 R"=====(
    body { background-color: #f5f5f5; color: #333; }
    .card, .history-entry { background: #ffffff !important; color: #333; border-left: 4px solid #eee; }
    input, select { background-color: white; color: #333; border: 1px solid #ddd; }
    .btn.theme { background-color: #78909c; }
    .logout-container {
      position: fixed;
      top: 15px;
      right: 15px;
      display: flex;
      align-items: center;
      gap: 10px;
      background: rgba(255, 255, 255, 0.9);
      padding: 8px 12px;
      border-radius: 20px;
      box-shadow: 0 2px 5px rgba(0,0,0,0.1);
      z-index: 1000;
    }
    .logout-btn { 
      background-color: #f44336; 
      color: white; 
      padding: 6px 12px; 
      border: none; 
      border-radius: 15px; 
      cursor: pointer;
      font-size: 14px;
    }
    .timeout-text {
      color: #555;
      font-size: 14px;
    }
    .language-selector {
      padding: 8px;
      border-radius: 5px;
      background: white;
      color: #333;
      border: 1px solid #ddd;
    }
    )=====";

  String html = R"=====(
  <!DOCTYPE html><html><head>
  <meta charset="UTF-8">
  <title>)=====" +
                String(langTitle[currentLanguage]) + R"=====(</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    /* Základní styly */
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; }
    .container { max-width: 600px;}
    .btn { padding: 12px 24px; margin: 8px; border: none; border-radius: 6px; color: white; cursor: pointer; }
    .status { padding: 15px; margin: 15px 0; border-radius: 8px; text-align: center; font-weight: bold; }
    .settings-row { justify-content: space-between; margin-bottom: 20px; }
    
    /* Tlačítka */
    .lock { background-color: #f44336; }
    .unlock { background-color: #4CAF50; }
    .add { background-color: #2196F3; }
    .remove { background-color: #ff9800; }
    .learn { background-color: #9c27b0; }
    
    /* Karty a historie */
    .card { padding: 12px; margin: 8px 0; border-radius: 4px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
    .history-entry { padding: 10px; margin: 5px 0; border-radius: 4px; }
    .granted { color: #4CAF50; border-left: 4px solid #4CAF50; }
    .denied { color: #f44336; border-left: 4px solid #f44336; }
    
    /* Vstupní pole */
    input[type="text"] { padding: 10px; width: 200px; border-radius: 5px; margin-right: 10px; }
    
    /* Téma specifické styly */
    )=====" + themeStyles +
                R"=====(
  </style>
  </head>
  <body>
    <!-- Panel pro odhlášení -->
    <div class="logout-container">
      <span class="timeout-text">)=====" +
                String(timeoutText[currentLanguage]) + R"=====( 
        <span id="timeout-hours">)=====" +
                String(hours) + R"=====(</span>h 
        <span id="timeout-minutes">)=====" +
                String(minutes) + R"=====(</span>m
      </span>
      <form action="/logout" method="post">
        <button type="submit" class="logout-btn">
          <i>🚪</i>
          <span>)=====" +
                String(logoutText[currentLanguage]) + R"=====(</span>
        </button>
      </form>
    </div>
    
    <div class="container">
      <div class="settings-row">

      <h1>)=====" +
                String(langTitle[currentLanguage]) + R"=====(</h1>

      <select class="language-selector" onchange="setLanguage(this.value)">
            <option value="0")=====" +
                (currentLanguage == 0 ? " selected" : "") + R"=====(>Česky</option>
            <option value="1")=====" +
                (currentLanguage == 1 ? " selected" : "") + R"=====(>English</option>
            <option value="2")=====" +
                (currentLanguage == 2 ? " selected" : "") + R"=====(>Deutsch</option>
          </select>
          
          <!-- Tlačítko pro změnu tématu -->
          <button class="btn theme" onclick="toggleTheme()">
            <i>)=====" +
                (darkTheme ? "☀️" : "🌙") + R"=====(</i>
            <span>)=====" +
                (darkTheme ? "Light Mode" : "Dark Mode") + R"=====(</span>
          </button>

        <button class="btn" onclick="toggleButton()" style="background-color: )=====" +
                String(buttonEnabled ? "#4CAF50" : "#f44336") + R"=====(;">
      <i>🔄</i>
      <span>)=====" +
                String(buttonEnabled ? "Disable Button" : "Enable Button") + R"=====(</span>
    </button>
  </div>
      </div>
      
      <!-- Stav zámku -->
      <div id="statusBox" class="status" style="background-color: )=====" +
                lockColor + R"=====(;">
        <span id="statusText">)=====" +
                lockStatus + R"=====(</span>
      </div>
      
      <!-- Ovládací tlačítka -->
      <div class="controls">
        <button class="btn lock" onclick="sendCommand('lock')">
          <i>🔒</i>
          <span>)=====" +
                String(langLock[currentLanguage]) + R"=====(</span>
        </button>
        <button class="btn unlock" onclick="sendCommand('unlock')">
          <i>🔓</i>
          <span>)=====" +
                String(langUnlock[currentLanguage]) + R"=====(</span>
        </button>
      </div>
      
      <!-- Správa karet -->
      <h2>)=====" +
                String(langCardManagement[currentLanguage]) + R"=====(</h2>
      <div class="controls">
        <input type="text" id="cardId" placeholder=")=====" +
                String(langCardIdPlaceholder[currentLanguage]) + R"=====(" />
        <button class="btn add" onclick="addCard()">
          <i>➕</i>
          <span>)=====" +
                String(langAddCard[currentLanguage]) + R"=====(</span>
        </button>
        <button class="btn remove" onclick="removeCard()">
          <i>➖</i>
          <span>)=====" +
                String(langRemoveCard[currentLanguage]) + R"=====(</span>
        </button>
        <button class="btn learn" onclick="startLearning()">
          <i>📲</i>
          <span>)=====" +
                String(langAddCardReader[currentLanguage]) + R"=====(</span>
        </button>
      </div>
      <div id="learningStatus" style="margin-top: 5px;"></div>
      
      <!-- Seznam povolených karet -->
      <h2>)=====" +
                String(langAllowedCards[currentLanguage]) + R"=====(</h2>
      <div id="cardsList"></div>
      
      <!-- Historie přístupů -->
      <h2>)=====" +
                String(langAccessHistory[currentLanguage]) + R"=====(</h2>
      <div id="accessHistory"></div>
    </div>
    
    <script>

      function toggleButton() {
        fetch('/togglebutton', {
          method: 'POST'
       }).then(r => r.text()).then(data => {
          alert(data);
         location.reload();
       });
      }
      // Funkce pro změnu jazyka
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
      
      // Funkce pro aktualizaci stavu
      function updateStatus() {
        fetch('/status')
          .then(response => response.json())
          .then(data => {
            document.getElementById('statusText').textContent = 
              data.status ? ")=====" +
                String(langUnlocked[currentLanguage]) + R"=====(" : 
              ")=====" +
                String(langLocked[currentLanguage]) + R"=====(";
            document.getElementById('statusBox').style.backgroundColor = 
              data.status ? "#4CAF50" : "#f44336";
            setTimeout(updateStatus, 1000);
          });
      }
      
      // Funkce pro aktualizaci časovače
      function updateTimeout() {
        let hours = parseInt(document.getElementById('timeout-hours').textContent);
        let minutes = parseInt(document.getElementById('timeout-minutes').textContent);
        
        minutes--;
        if (minutes < 0) {
          minutes = 59;
          hours--;
        }
        
        document.getElementById('timeout-hours').textContent = hours;
        document.getElementById('timeout-minutes').textContent = minutes;
        setTimeout(updateTimeout, 60000);
      }
      
      // Načtení stránky
      window.onload = function() {
        updateStatus();
        updateTimeout();
        loadCards();
        loadHistory();
      };

      function sendCommand(cmd) {
      fetch('/' + cmd, { method: 'POST' })
        .then(r => r.text()).then(alert);
    }
    
    function addCard() {
      const cardId = document.getElementById('cardId').value.trim();
      if(!cardId) return alert(')=====" +
                String(langCardIdPlaceholder[currentLanguage]) + R"=====(');
      
      fetch('/addcard', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'card=' + encodeURIComponent(cardId)
      }).then(r => r.text()).then(data => { 
        alert(data); 
        loadCards();
        document.getElementById('cardId').value = '';
      });
    }
    
    function removeCard() {
      const cardId = document.getElementById('cardId').value.trim();
      if(!cardId) return alert(')=====" +
                String(langCardIdPlaceholder[currentLanguage]) + R"=====(');
      
      fetch('/removecard', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'card=' + encodeURIComponent(cardId)
      }).then(r => r.text()).then(data => { 
        alert(data); 
        loadCards();
        document.getElementById('cardId').value = '';
      });
    }
    
    function startLearning() {
      document.getElementById('learningStatus').textContent = ")=====" +
                String(langLearningStatus[currentLanguage]) + R"=====(";
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
          if (cards.length === 0) {
            list.innerHTML = '<div class="empty-state">No cards registered</div>';
            return;
          }
          cards.forEach(card => {
            const div = document.createElement('div');
            div.className = 'card';
            div.innerHTML = '<i>🪪</i> ' + card;
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
          
          if (history.length === 0) {
            container.innerHTML = '<div class="empty-state">No history available</div>';
            return;
          }
          
          history.forEach(entry => {
            const div = document.createElement('div');
            div.className = 'history-entry ' + (entry.granted ? 'granted' : 'denied');
            
            const status = entry.granted ? ')=====" +
                String(langGranted[currentLanguage]) + R"=====(' : ')=====" + String(langDenied[currentLanguage]) + R"=====(';
            div.innerHTML = '<i>' + (entry.granted ? '✅' : '❌') + '</i> ' + entry.card + ' - ' + status + ' - ' + entry.time;
            
            container.appendChild(div);
          });
        });
    }
    
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

    function toggleTheme() {
      fetch('/toggletheme', {
        method: 'POST'
      }).then(r => r.text()).then(data => {
        location.reload();
      });
    }
       
    </script>
  </body>
  </html>
  )=====";

  server.send(200, "text/html", html);
}

bool isLoggedIn()
{
  // Automatické odhlášení po 8 hodinách
  if (isUserLoggedIn && (millis() - loginTime > LOGIN_TIMEOUT))
  {
    isUserLoggedIn = false;
    Serial.println("Automatické odhlášení po 8 hodinách");
  }
  return isUserLoggedIn;
}

void handleSetLanguage()
{
  if (!server.hasArg("lang"))
  {
    server.send(400, "text/plain", "Missing language parameter");
    return;
  }

  uint8_t lang = server.arg("lang").toInt();
  if (lang > 2)
  {
    server.send(400, "text/plain", "Invalid language code");
    return;
  }

  currentLanguage = lang;
  saveLanguageSetting();
  server.send(200, "text/plain", "Language set successfully");
}

// [Rest of your existing functions remain unchanged...]
// ==================== Funkce pro LED ====================
void handleLEDTimeout()
{
  if (ledsAreOn && millis() - ledOnTime > 5000)
  {
    pixels.clear();
    pixels.show();
    ledsAreOn = false;
  }
}

// ==================== Funkce pro učení karet ====================
void handleLearningMode()
{
  if (buttonEnabled && digitalRead(BUTTON_PIN) == HIGH && !learningMode && !webLearningMode)
  {
    if (digitalRead(BUTTON_PIN) == HIGH && !learningMode && !webLearningMode)
    {
      Serial.println("REŽIM NASTAVENÍ KARTY: Přilož novou kartu nebo znovu stiskni tlačítko pro zrušení.");
      delay(200); // Debounce

      indicateLearningMode();
      learningMode = true;
      ledsAreOn = true;
      ledOnTime = millis();

      unsigned long startTime = millis();
      while (millis() - startTime < 10000)
      { // 10s timeout
        if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial())
        {
          for (byte i = 0; i < 4; i++)
            masterCard[i] = rfid.uid.uidByte[i];

          String newCard = "";
          for (byte i = 0; i < 4; i++)
          {
            if (masterCard[i] < 0x10)
              newCard += "0";
            newCard += String(masterCard[i], HEX);
          }

          // Check if card already exists
          bool cardExists = false;
          int cardIndex = -1;
          for (int i = 0; i < cardCount; i++)
          {
            if (allowedCards[i] == newCard)
            {
              cardExists = true;
              cardIndex = i;
              break;
            }
          }

          if (cardExists)
          {
            // Remove the card
            for (int i = cardIndex; i < cardCount - 1; i++)
            {
              allowedCards[i] = allowedCards[i + 1];
            }
            cardCount--;
            saveCardsToEEPROM();
            Serial.print("Karta odstraněna: ");
            printUID(masterCard);
            Serial.println();
            indicateCardRemoved();
          }
          else
          {
            // Add new card
            if (cardCount < MAX_CARDS)
            {
              allowedCards[cardCount++] = newCard;
              saveCardsToEEPROM();
              Serial.print("Nová karta přidána: ");
              printUID(masterCard);
              Serial.println();
              indicateNewCardSet();
            }
            else
            {
              Serial.println("Maximální počet karet dosažen, nelze přidat další.");
            }
          }

          rfid.PICC_HaltA();
          break;
        }

        if (digitalRead(BUTTON_PIN) == HIGH)
        {
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
}

// Handle web-triggered learning mode
void handleWebLearningMode()
{
  static unsigned long webLearningStartTime = 0;
  const unsigned long webLearningTimeout = 10000; // 10 seconds timeout

  if (webLearningMode)
  {
    // Initialize the timer when first entering learning mode
    if (webLearningStartTime == 0)
    {
      webLearningStartTime = millis();
      Serial.println("WEB REŽIM NASTAVENÍ KARTY: Přiložte kartu k čtečce");
      indicateLearningMode();
    }

    // Check for timeout
    if (millis() - webLearningStartTime > webLearningTimeout)
    {
      webLearningMode = false;
      webLearningStartTime = 0;
      clearLEDs();
      Serial.println("WEB režim nastavení karty: Časový limit vypršel");
      return;
    }

    // Only process cards if we're in learning mode
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial())
    {
      for (byte i = 0; i < 4; i++)
        masterCard[i] = rfid.uid.uidByte[i];

      String newCard = "";
      for (byte i = 0; i < 4; i++)
      {
        if (masterCard[i] < 0x10)
          newCard += "0";
        newCard += String(masterCard[i], HEX);
      }

      // Check if card already exists
      bool cardExists = false;
      int cardIndex = -1;
      for (int i = 0; i < cardCount; i++)
      {
        if (allowedCards[i] == newCard)
        {
          cardExists = true;
          cardIndex = i;
          break;
        }
      }

      if (cardExists)
      {
        // Remove the card
        for (int i = cardIndex; i < cardCount - 1; i++)
        {
          allowedCards[i] = allowedCards[i + 1];
        }
        cardCount--;
        saveCardsToEEPROM();
        Serial.print("Karta odstraněna přes web: ");
        printUID(masterCard);
        Serial.println();
        indicateCardRemoved();
      }
      else
      {
        // Add new card
        if (cardCount < MAX_CARDS)
        {
          allowedCards[cardCount++] = newCard;
          saveCardsToEEPROM();
          Serial.print("Nová karta přidána přes web: ");
          printUID(masterCard);
          Serial.println();
          indicateNewCardSet();
        }
        else
        {
          Serial.println("Maximální počet karet dosažen, nelze přidat další.");
          indicateError();
        }
      }

      rfid.PICC_HaltA();
      webLearningMode = false;
      webLearningStartTime = 0;
      clearLEDs();
    }
  }
  else
  {
    webLearningStartTime = 0; // Reset timer if not in learning mode
  }
}

// ==================== Funkce pro ovládání zámku ====================
void handleAuthorizedCard(byte *uid, byte uidLength)
{
  String uidStr = "";
  for (byte i = 0; i < uidLength; i++)
  {
    if (uid[i] < 0x10)
      uidStr += "0";
    uidStr += String(uid[i], HEX);
  }

  bool authorized = false;
  for (int i = 0; i < cardCount; i++)
  {
    if (allowedCards[i].equalsIgnoreCase(uidStr))
    {
      authorized = true;
      break;
    }
  }

  // Add to access history
  addToHistory(uidStr, authorized);

  if (authorized)
  {
    Serial.println("Autorizovaná karta: " + uidStr);
    toggleLock();
  }
  else
  {
    Serial.println("Nepovolená karta: " + uidStr);
    indicateAccessDenied();
    buzzer.tone(1000, 500);
    delay(500);
    buzzer.noTone();
  }
}

void toggleLock()
{
  if (!status)
  {
    pulsePin();
    myservo.write(133);
    indicateAccessGranted();
    status = true;
  }
  else
  {
    pulsePin();
    myservo.write(180);
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
void addToHistory(String cardId, bool granted)
{
  if (historyCount >= MAX_HISTORY)
  {
    // Shift all entries down to make room
    for (int i = 0; i < MAX_HISTORY - 1; i++)
    {
      accessHistory[i] = accessHistory[i + 1];
    }
    historyCount = MAX_HISTORY - 1;
  }

  accessHistory[historyCount].cardId = cardId;
  accessHistory[historyCount].timestamp = millis();
  accessHistory[historyCount].accessGranted = granted;
  historyCount++;
}

String getFormattedTime(unsigned long timestamp)
{
  unsigned long seconds = timestamp / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;

  seconds %= 60;
  minutes %= 60;
  hours %= 24;

  String result = "";
  if (days > 0)
    result += String(days) + "d ";
  if (hours > 0 || !result.isEmpty())
    result += String(hours) + "h ";
  if (minutes > 0 || !result.isEmpty())
    result += String(minutes) + "m ";
  result += String(seconds) + "s ago";

  return result;
}

// ==================== Pomocné funkce ====================
void pulsePin()
{
  digitalWrite(LED_PIN, HIGH);
  delay(20);
  digitalWrite(LED_PIN, LOW);
}

void clearLEDs()
{
  for (int i = 0; i < NUMPIXELS; i++)
  {
    pixels.setPixelColor(i, 0);
  }
  pixels.show();
  ledsAreOn = false;
}

void indicateLearningMode()
{
  for (int i = 0; i < NUMPIXELS; i++)
  {
    pixels.setPixelColor(i, pixels.Color(255 / BRIGHT, 75 / BRIGHT, 0));
    pixels.show();
    delay(DELAYVAL);
  }
}

void indicateNewCardSet()
{
  for (int i = 0; i < NUMPIXELS; i++)
  {
    pixels.setPixelColor(i, pixels.Color(250 / BRIGHT, 0, 175 / BRIGHT));
    pixels.show();
    delay(DELAYVAL);
  }
  delay(500);
  clearLEDs();
}

void indicateAccessGranted()
{
  for (int i = 0; i < NUMPIXELS; i++)
  {
    pixels.setPixelColor(i, pixels.Color(0, 255 / BRIGHT, 0));
    pixels.show();
    delay(DELAYVAL);
  }
  ledsAreOn = true;
}

void indicateAccessDenied()
{
  for (int i = 0; i < NUMPIXELS; i++)
  {
    pixels.setPixelColor(i, pixels.Color(255 / BRIGHT, 0, 0));
    pixels.show();
    delay(DELAYVAL);
  }
  ledsAreOn = true;
}

void printUID(byte *uid)
{
  for (byte i = 0; i < 4; i++)
  {
    if (uid[i] < 0x10)
      Serial.print("0");
    Serial.print(uid[i], HEX);
    if (i < 3)
      Serial.print(" ");
  }
}

// ==================== EEPROM Funkce ====================
void loadCardsFromEEPROM()
{
  cardCount = EEPROM.read(0);
  if (cardCount > MAX_CARDS)
    cardCount = 0;

  for (int i = 0; i < cardCount; i++)
  {
    String card = "";
    int address = 1 + (i * CARD_LENGTH);

    for (int j = 0; j < CARD_LENGTH - 1; j++)
    {
      char c = EEPROM.read(address + j);
      if (c != 0xFF && c != 0)
        card += c;
    }

    if (card.length() >= 8)
    {
      allowedCards[i] = card;
    }
    else
    {
      for (int k = i; k < cardCount - 1; k++)
      {
        allowedCards[k] = allowedCards[k + 1];
      }
      cardCount--;
      i--;
    }
  }
  Serial.printf("Načteno %d karet z EEPROM\n", cardCount);
}

void saveCardsToEEPROM()
{
  EEPROM.write(0, cardCount);

  for (int i = 0; i < cardCount; i++)
  {
    int address = 1 + (i * CARD_LENGTH);
    String card = allowedCards[i];

    for (int j = 0; j < CARD_LENGTH - 1; j++)
    {
      if (j < card.length())
      {
        EEPROM.write(address + j, card[j]);
      }
      else
      {
        EEPROM.write(address + j, 0);
      }
    }
  }

  EEPROM.commit();
  Serial.printf("Uloženo %d karet do EEPROM\n", cardCount);
}

// ==================== Web Server Funkce ====================
void initWiFi()
{
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Připojování k WiFi...");
  }
  Serial.println("Připojeno k WiFi");
  Serial.print("IP adresa: ");
  Serial.println(WiFi.localIP());
}

void handleLock()
{
  if (!status)
  {
    server.send(200, "text/plain", "Zámek již je zamčený");
    return;
  }
  toggleLock();
  server.send(200, "text/plain", "Zámek zamčen");
}

void handleUnlock()
{
  if (status)
  {
    server.send(200, "text/plain", "Zámek již je odemčený");
    return;
  }
  toggleLock();
  server.send(200, "text/plain", "Zámek odemčen");
}

void handleAddCard()
{
  if (!server.hasArg("card"))
  {
    server.send(400, "text/plain", "Chybějící ID karty");
    return;
  }

  String cardId = server.arg("card");
  cardId.trim();

  if (cardId.length() < 8)
  {
    server.send(400, "text/plain", "Neplatný formát karty (min 8 hex znaků)");
    return;
  }

  for (int i = 0; i < cardCount; i++)
  {
    if (allowedCards[i].equalsIgnoreCase(cardId))
    {
      server.send(200, "text/plain", "Karta již existuje: " + cardId);
      return;
    }
  }

  if (cardCount >= MAX_CARDS)
  {
    server.send(200, "text/plain", "Maximální počet karet (" + String(MAX_CARDS) + ") dosažen");
    return;
  }

  allowedCards[cardCount++] = cardId;
  saveCardsToEEPROM();
  server.send(200, "text/plain", "Karta přidána: " + cardId);
}

void handleRemoveCard()
{
  if (!server.hasArg("card"))
  {
    server.send(400, "text/plain", "Chybějící ID karty");
    return;
  }

  String cardId = server.arg("card");
  bool found = false;

  for (int i = 0; i < cardCount; i++)
  {
    if (allowedCards[i].equalsIgnoreCase(cardId))
    {
      for (int j = i; j < cardCount - 1; j++)
      {
        allowedCards[j] = allowedCards[j + 1];
      }
      cardCount--;
      found = true;
      break;
    }
  }

  if (found)
  {
    saveCardsToEEPROM();
    server.send(200, "text/plain", "Karta odebrána: " + cardId);
  }
  else
  {
    server.send(200, "text/plain", "Karta nenalezena");
  }
}

void handleListCards()
{
  String json = "[";
  for (int i = 0; i < cardCount; i++)
  {
    if (i != 0)
      json += ",";
    json += "\"" + allowedCards[i] + "\"";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleStatus()
{
  String json = "{\"status\":" + String(status ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleStartLearning()
{
  if (webLearningMode)
  {
    server.send(200, "text/plain", "Již čekám na kartu");
    return;
  }

  webLearningMode = true;
  indicateLearningMode();
  ledsAreOn = true;
  ledOnTime = millis();

  server.send(200, "text/plain", "Přiložte kartu ke čtečce");
}

void handleHistory()
{
  String json = "[";
  for (int i = 0; i < historyCount; i++)
  {
    if (i != 0)
      json += ",";
    json += "{\"card\":\"" + accessHistory[i].cardId + "\",";
    json += "\"granted\":";
    json += accessHistory[i].accessGranted ? "true" : "false";
    json += ",";
    json += "\"time\":\"" + getFormattedTime(millis() - accessHistory[i].timestamp) + "\"}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void loadThemeSetting()
{
  darkTheme = EEPROM.read(THEME_EEPROM_ADDR);
  if (darkTheme > 1)
    darkTheme = 0; // Default to light if invalid
}

void saveThemeSetting()
{
  EEPROM.write(THEME_EEPROM_ADDR, darkTheme);
  EEPROM.commit();
}

void handleToggleTheme()
{
  darkTheme = !darkTheme;
  saveThemeSetting();
  server.send(200, "text/plain", "Theme changed");
}

void indicateCardRemoved()
{
  for (int i = 0; i < NUMPIXELS; i++)
  {
    pixels.setPixelColor(i, pixels.Color(250 / BRIGHT, 0, 175 / BRIGHT));
    pixels.show();
    delay(DELAYVAL);
  }
  delay(500);
  clearLEDs();
}

void indicateError()
{
  for (int i = 0; i < NUMPIXELS; i++)
  {
    pixels.setPixelColor(i, pixels.Color(255 / BRIGHT, 0, 0));
    pixels.show();
    delay(2);
  }
  ledsAreOn = true;
}

void handleLogin()
{
  String html = R"(
    <!DOCTYPE html>
<html lang="en">

<head>
    <meta charset="UTF-8">
    <title>Login</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        /* Základní styly */
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            margin: 0;
            padding: 20px;
        }

        .container {
            max-width: 600px;
        }

        input[type="text"] {
            padding: 10px;
            width: 200px;
            border-radius: 5px;
            margin-right: 10px;
        }

        h1 {
            color: #e0e0e0;
            font-size: 40px;
        }

        h2 {
            color: #e0e0e0;
            font-size: 40px;
            margin: 0px;
            margin-bottom: 20px;
        }

    </style>
</head>

<body style="background-color:#242424">
    <h1>ESPLock</h1>
    <div style="display: grid; place-items: center;">
        <h2>Login</h2>
    <form action="/login" method="post">
        <input type="password" name="password" placeholder="Password" style="padding: 6px 12px; border: none; border-radius: 15px; cursor: pointer; font-size: 17px;">
        <input type="submit" value="Login" style="background-color:#3cc719; padding: 6px 12px; border: none; border-radius: 15px; cursor: pointer; font-size: 17px; margin-left: 5px;">
    </form>
    </div>
</body>

</html>
  )";
  server.send(200, "text/html", html);
}

void handleLoginSubmit()
{
  if (server.hasArg("password") && server.arg("password").equals(rootpassword))
  {
    isUserLoggedIn = true;
    loginTime = millis(); // Uložíme čas přihlášení
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    Serial.println("Uživatel přihlášen");
  }
  else
  {
    server.send(401, "text/plain", "Neplatné heslo");
  }
}

void handleLogout()
{
  isUserLoggedIn = false;
  server.sendHeader("Location", "/login");
  server.send(302, "text/plain", "");
  Serial.println("Uživatel odhlášen");
}

void loadButtonSetting()
{
  buttonEnabled = EEPROM.read(BUTTON_ENABLED_EEPROM_ADDR);
  if (buttonEnabled > 1)
    buttonEnabled = 1; // Default to enabled if invalid
}

void saveButtonSetting()
{
  EEPROM.write(BUTTON_ENABLED_EEPROM_ADDR, buttonEnabled);
  EEPROM.commit();
}

void handleToggleButton()
{
  buttonEnabled = !buttonEnabled;
  saveButtonSetting();
  server.send(200, "text/plain", buttonEnabled ? "Button enabled" : "Button disabled");
}