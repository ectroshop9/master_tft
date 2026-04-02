#include <esp_now.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <vector>
#include <Preferences.h>
#include <algorithm>

// --- الأرجل ---
#define TFT_CS    10
#define TFT_DC    11
#define TFT_SCK   12
#define TFT_MOSI  13
#define TFT_MISO  14
#define TFT_RST   15
#define JOY_VERT  3 
#define JOY_SEL   1 

// --- الهيكل (يجب أن يطابق السلايف 100%) ---
typedef struct struct_message {
  int id;
  float weight;
  float temp;
  float hum;
  float dsTemp[3];
  int light;
  bool motion;
  int battery;
  int gas;
  int uv;
  bool bees;
} struct_message;

typedef struct Hive {
  struct_message data;
  unsigned long lastSeen;
} Hive;

// --- المتغيرات ---
std::vector<Hive> activeHives;
struct_message incomingReadings;
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
Preferences prefs;
int selectedIndex = 0;
bool joyReady = true;
unsigned long lastRefresh = 0;

// --- إدارة الذاكرة ---
void saveHivesToMemory() {
  prefs.putInt("count", activeHives.size());
  for(int i = 0; i < (int)activeHives.size(); i++) {
    char key[12]; sprintf(key, "id_%d", i);
    prefs.putInt(key, activeHives[i].data.id);
  }
}

// --- دالة الاستقبال (التوقيع المتوافق مع v3.3.7) ---
// ملاحظة: تم إزالة const من المعطى الثاني لتوافق أدق مع بعض إصدارات Compiler
void OnDataRecv(const esp_now_recv_info_t * recv_info, const uint8_t *incomingData, int len) {
  if (len == sizeof(struct_message)) {
    memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));
    
    bool found = false;
    for (auto &h : activeHives) {
      if (h.data.id == incomingReadings.id) {
        h.data = incomingReadings;
        h.lastSeen = millis();
        found = true; 
        break;
      }
    }
    
    if (!found) {
      Hive newHive;
      newHive.data = incomingReadings;
      newHive.lastSeen = millis();
      activeHives.push_back(newHive);
      saveHivesToMemory();
    }
  }
}

// --- وظائف الرسم ---
void drawHeader() {
  tft.fillRect(0, 0, 320, 35, 0x01E0);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(15, 8);
  tft.print("SMART HIVE PRO V1.1");
}

void updateDisplay() {
  if (activeHives.empty()) {
    tft.setCursor(50, 120);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.print("SEARCHING FOR SLAVES...");
    return;
  }

  int startIdx = (selectedIndex / 5) * 5;
  for (int i = 0; i < 5; i++) {
    int current = startIdx + i;
    int y = 40 + (i * 38);
    if (current >= (int)activeHives.size()) {
      tft.fillRect(10, y, 300, 34, ILI9341_BLACK);
      continue;
    }
    
    bool isSelected = (current == selectedIndex);
    uint16_t boxColor = isSelected ? ILI9341_YELLOW : 0x2104;
    uint16_t textColor = isSelected ? ILI9341_BLACK : ILI9341_WHITE;
    unsigned long diff = (activeHives[current].lastSeen == 0) ? 999 : (millis() - activeHives[current].lastSeen) / 1000;
    
    tft.fillRoundRect(10, y, 300, 34, 5, boxColor);
    tft.setTextColor(textColor);
    tft.setTextSize(2);
    tft.setCursor(15, y + 8);
    tft.printf("#%02d", activeHives[current].data.id);

    tft.setTextSize(1);
    tft.setCursor(85, y + 6);
    tft.printf("W:%.1fkg | B:%d%% | G:%d%%", activeHives[current].data.weight, activeHives[current].data.battery, activeHives[current].data.gas);
    
    tft.setCursor(85, y + 18);
    if (diff > 60) {
      tft.setTextColor(isSelected ? 0x8000 : ILI9341_RED);
      tft.print("OFFLINE");
    } else {
      tft.setTextColor(isSelected ? 0x03E0 : ILI9341_GREEN);
      tft.printf("ON: %lus | T:%.1fC | UV:%d", diff, activeHives[current].data.temp, activeHives[current].data.uv);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(JOY_SEL, INPUT_PULLUP);
  analogReadResolution(12);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error ESP-NOW");
    return;
  }

  // تسجيل الدالة مع Cast صريح لنوع البيانات المعتمد في 3.x
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);

  prefs.begin("hive-storage", false);
  int count = prefs.getInt("count", 0);
  for(int i = 0; i < count; i++) {
    char key[12]; sprintf(key, "id_%d", i);
    int id = prefs.getInt(key, 0);
    if(id != 0) { 
      Hive h; 
      memset(&h.data, 0, sizeof(h.data)); 
      h.data.id = id; 
      h.lastSeen = 0; 
      activeHives.push_back(h); 
    }
  }

  drawHeader();
}

void loop() {
  int yVal = analogRead(JOY_VERT);
  if (yVal > 1800 && yVal < 2200) joyReady = true;
  if (joyReady && !activeHives.empty()) {
    if (yVal < 500) { selectedIndex = (selectedIndex <= 0) ? (int)activeHives.size() - 1 : selectedIndex - 1; joyReady = false; updateDisplay(); }
    else if (yVal > 3500) { selectedIndex = (selectedIndex >= (int)activeHives.size() - 1) ? 0 : selectedIndex + 1; joyReady = false; updateDisplay(); }
  }
  if (millis() - lastRefresh > 1000) { updateDisplay(); lastRefresh = millis(); }
}