#include <esp_now.h>
#include <WiFi.h>
#include "DHT.h"
#include "HX711.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// --- تعريف الدبابيس (مطابقة لـ diagram.json) ---
#define DHTPIN 18 
#define DHTTYPE DHT22
#define DOUT_PIN 5 
#define SCK_PIN  6 
#define ONE_WIRE_BUS 15 
#define LIGHT_PIN 8 
#define PIR_MOTION_PIN 11 
#define BATTERY_PIN 1 
#define GAS_PIN 9 
#define UV_PIN 10 
#define BEE_COUNT_PIN 21 
#define RELAY_PIN 7 
#define BUTTON_PIN 14 
#define LED_PIN 38 

// --- الهيكل (Struct) الموحد ---
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

struct_message myData;

// عنوان البث العام (لضمان الربط الفوري في المحاكي)
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; 

// --- الكائنات ---
DHT dht(DHTPIN, DHTTYPE);
HX711 scale;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

unsigned long lastSendTime = 0;
const unsigned long sendInterval = 5000; 

// دالة تقرير حالة الإرسال
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\n[📡 ESP-NOW] Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS ✅" : "FAILED ❌");
}

void setup() {
  Serial.begin(115200);
  
  // تهيئة المنافذ
  pinMode(PIR_MOTION_PIN, INPUT);
  pinMode(BEE_COUNT_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  // تشغيل الحساسات
  dht.begin();
  scale.begin(DOUT_PIN, SCK_PIN);
  scale.set_scale(420.f); 
  scale.tare();
  sensors.begin();

  // إعدادات الواي فاي و ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); 

  if (esp_now_init() != ESP_OK) {
    Serial.println("❗ Error ESP-NOW Init");
    return;
  }

  // تسجيل Callback الإرسال (توافق 3.3.7)
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
  
  // تعريف الماستر كـ Peer
  esp_now_peer_info_t peerInfo = {}; 
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("❗ Failed to add Master");
    return;
  }

  myData.id = 10; // معرف هذه الخلية
  Serial.println("System Initialized. Broadcasting on ID 10...");
}

void loop() {
  if (millis() - lastSendTime >= sendInterval) {
    lastSendTime = millis();

    // تثبيت الهوية
    myData.id = 10; 

    // 1. قراءة الميزان
    if (scale.is_ready()) {
      myData.weight = scale.get_units(5) / 1000.0;
      if (myData.weight < 0) myData.weight = 0;
    }

    // 2. قراءة الجو (DHT22)
    myData.temp = dht.readTemperature();
    myData.hum = dht.readHumidity();

    // 3. قراءة الحساسات الداخلية (DS18B20)
    sensors.requestTemperatures();
    for(int i=0; i<3; i++) {
      float t = sensors.getTempCByIndex(i);
      myData.dsTemp[i] = (t == -127.0 || isnan(t)) ? 0 : t;
    }

    // 4. قراءة الحساسات التناظرية (ADC)
    myData.light = map(analogRead(LIGHT_PIN), 0, 4095, 0, 100);
    myData.battery = map(analogRead(BATTERY_PIN), 0, 4095, 0, 100);
    myData.gas = map(analogRead(GAS_PIN), 0, 4095, 0, 100);
    myData.uv = map(analogRead(UV_PIN), 0, 4095, 0, 15);

    // 5. قراءة الحساسات الرقمية (PIR)
    myData.motion = (digitalRead(PIR_MOTION_PIN) == HIGH);
    myData.bees = (digitalRead(BEE_COUNT_PIN) == HIGH);

    // --- الإرسال الفعلي ---
    esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));

    // وميض تأكيدي
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    
    // طباعة للمراقبة (Serial Monitor)
    Serial.printf("ID:10 | W:%.2fkg | T:%.1fC | Bat:%d%%\n", myData.weight, myData.temp, myData.battery);
  }
}