#include <M5Core2.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <AXP192.h>
#include "AudioFileSourceSD.h"
#include "AudioFileSourceID3.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include <pt.h>

// Wi-FiのSSID
char *ssid = "YourSSID";
// Wi-Fiのパスワード
char *password = "YourPassword";
// MQTTの接続先のIP
const char *endpoint = "ServerIP";
// MQTTのポート
const int port = 1883;
char *deviceID = "DeviceID";
char *subTopic = "TopicName";

enum actionMode {ALERT, STAMP}; // MODEの定義
int modeNumber; // 現在のMODEを保持
bool vibFlag = false;

AudioGeneratorMP3 *mp3;
AudioFileSourceSD *file;
AudioOutputI2S *out;
AudioFileSourceID3 *id3;

#define OUTPUT_GAIN 10

static struct pt pt1, pt2;

// 天気取得に必要な変数
const String weatherPlace = "Matsumoto";
const String key = "YourAPIKey";
const String URL = "http://api.openweathermap.org/data/2.5/weather?q=" + weatherPlace + ",jp&APPID=" + key;
String weather;
double temperature;

///////////////////////////////////////////////////////////
//--- SET UP ---///
WiFiClient httpsClient;
PubSubClient mqttClient(httpsClient);


void setupAudio(){
  M5.Axp.SetSpkEnable(true);
  mp3 = new AudioGeneratorMP3();
  out = new AudioOutputI2S(0, 0); // Output to ExternalDAC
  out->SetPinout(12, 0, 2);
  out->SetOutputModeMono(true);
  out->SetGain((float)OUTPUT_GAIN/10.0);
}


void setup() {
  Serial.begin(115200);
    
  // Initialize the M5Stack object
  M5.begin(true, true, false, true);

  setupAudio();

  // START
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(3);
  M5.Lcd.printf("START");
  PT_INIT(&pt1);
  PT_INIT(&pt2);
    
  // Start WiFi
  Serial.println("Connecting to ");
  Serial.print(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // WiFi Connected
  Serial.println("\nWiFi Connected.");
  M5.Lcd.setCursor(10, 40);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(3);
  M5.Lcd.printf("WiFi Connected.");
    
  mqttClient.setServer(endpoint, port);
  mqttClient.setCallback(mqttCallback);
  
  connectMQTT();

  getWeatherDate();
  modeNumber = ALERT;
  initAlertMonitor();

  
}

////////////////////////////////////////////
//--- 天気情報を取得 ---//
void getWeatherDate(){
    if ((WiFi.status() == WL_CONNECTED)) {
    HTTPClient http;
 
    http.begin(URL); //URLを指定
    int httpCode = http.GET();  //GETリクエストを送信
 
    if (httpCode > 0) { //返答がある場合
      String payload = http.getString();  //返答（JSON形式）を取得
      Serial.println(httpCode);
      Serial.println(payload);

      // メモリを確保してjsonに変換
      StaticJsonDocument<1024> weatherdata;
      DeserializationError error = deserializeJson(weatherdata, payload);

      if (error) {
      // エラーの場合
      M5.Lcd.print(F("deserializeJson() failed: "));
      M5.Lcd.println(error.f_str());
      }

      //各データを抜き出し
      weather = weatherdata["weather"][0]["main"].as<char*>();
      temperature = weatherdata["main"]["temp"].as<double>();
    } else {
      Serial.println("Error on HTTP request");
    }
 
    http.end(); //Free the resources
  }
}

////////////////////////////////////////////
//--- MQTT通信の設定 ---//

void connectMQTT() {
  while (!mqttClient.connected()) {
    if (mqttClient.connect(deviceID)) {
      Serial.println("Connected.");
      int qos = 0;
      mqttClient.subscribe(subTopic, qos);
      Serial.println("Subscribed.");
    } else {
      Serial.print("Failed. Error state=");
      Serial.print(mqttClient.state());
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

long messageSentAt = 0;
int count = 0;
char pubMessage[128];

void mqttCallback (char* topic, byte* payload, unsigned int length) { 
  String str = "";
  Serial.print("Received. topic=");
  Serial.println(topic);
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    str += (char)payload[i];
  }
  Serial.print("\n");

  StaticJsonDocument<200> doc;
    
  DeserializationError error = deserializeJson(doc, str);
  
  // パースが成功したか確認。できなきゃ終了
  if (error) {
    return;
  }
  
  // JSONデータを割りあて
  int color = doc["color"];
  Serial.println(color);
  if( color == 1){
    if(modeNumber != ALERT) initAlertMonitor();
    modeNumber = ALERT;
    makeAlert();    // アラートする
    initAlertMonitor();
  }
  M5.update();
}
  
void mqttLoop() {
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();
}


////////////////////////////////////////////
//--- Audioに関わる処理 ---///

void playAudio()
{
  file = new AudioFileSourceSD("/Sound/Alert.mp3");
  id3 = new AudioFileSourceID3(file);
  mp3->begin(id3, out);
}

static int audioThread(struct pt *pt) {
//  static unsigned long timestamp = 0;
  PT_BEGIN(pt);
  if (mp3->isRunning()) {
    if (!mp3->loop()){
      mp3->stop();
      Serial.printf("Stop()");
    }
  }
  PT_END(pt);
}


////////////////////////////////////////////
//--- UIに関わる処理 ---///
AXP192 power;

void initAlertMonitor(){
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.fillCircle(160, 50, 40, 0xef1a);
  M5.Lcd.fillCircle(120, 110, 40, 0xef1a);
  M5.Lcd.fillCircle(200, 110, 40, 0xef1a);

  // ロゴの表示
  M5.Lcd.setTextColor(0xfac7);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setCursor(80, 70);
  M5.Lcd.println("Protect");
  M5.Lcd.setCursor(20, 100);
  M5.Lcd.println("YG!!");

  // 天気の表示
  M5.Lcd.setTextColor(0x4edf);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(20, 170);
  M5.Lcd.print(weatherPlace);
  M5.Lcd.setCursor(160, 160);
  M5.Lcd.print(weather);
  M5.Lcd.setCursor(160, 180);
  M5.Lcd.print(temperature - 273.15);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(250, 180);
  M5.Lcd.print("deg.");

  // フッターの表示
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(20, 220);
  M5.Lcd.printf("Stamp");
  M5.Lcd.setCursor(140, 220);
  M5.Lcd.printf("Mode");
  M5.Lcd.setCursor(240, 220);
  M5.Lcd.printf("Reset");
}

void initStampMonitor(){
  M5.Lcd.fillScreen(BLACK);

  //  フッターの表示
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(2);

  M5.Lcd.setCursor(20, 220);
  M5.Lcd.printf("Stamp");
  M5.Lcd.setCursor(140, 220);
  M5.Lcd.printf("Mode");
  M5.Lcd.setCursor(240, 220);
  M5.Lcd.printf("Reset");

  // 枠の表示
  for(int i = 0; i < 6; i++){
    M5.Lcd.drawRect(10 + (i % 3) * 102, 10 + (i / 3) * 102, 100, 100, 0x4248);
  }
}

void makeAlert(){
  M5.Lcd.fillRect(0, 80, 320, 80, YELLOW);
  M5.Lcd.fillRect(40, 100, 240, 40, BLACK);

  // 文字の表示
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(45, 110);
  M5.Lcd.setTextColor(RED, BLACK);
  M5.Lcd.printf("!! CAUTION !!");

  // バイブレーションとサウンド
  playAudio();
  vibFlag = true;
}

int vibCount = 0;
static int vibThread(struct pt *pt) {
  static unsigned long timestamp = 0;
  PT_BEGIN(pt);
  while(vibFlag) {
    power.SetLDOEnable(3, true);
    PT_WAIT_UNTIL(pt, millis() - timestamp > 500);
    power.SetLDOEnable(3, false);
    PT_WAIT_UNTIL(pt, millis() - timestamp > 1000);
    timestamp = millis();
    vibCount += 1;
    Serial.println(vibCount);
    if (2 < vibCount) {
      vibFlag = false;
      vibCount = 0;
    }
  }
  PT_END(pt);
}


////////////////////////////////////////////////////
//--- スタンプに関する処理 ---//
int counter = 0;
void countStamp(){
  counter++;
}

void resetStamp(){
  counter = 0;
}

void drawStampImage(){
  if(counter % 6 == 1) initStampMonitor();
  for(int count = 0; count <= (counter - 1) % 6; count++){
    switch((counter - 1) / 6){
      case 0:
        M5.Lcd.drawJpgFile(SD, "/image/stampCard1.jpg", 10 + (count % 3) * 102, 10 + (count / 3) * 102, 100, 100, 10 + (count % 3) * 100, 20 + (count / 3) * 100, JPEG_DIV_NONE);
        break;
      case 1:
        M5.Lcd.drawJpgFile(SD, "/image/stampCard2.jpg", 10 + (count % 3) * 102, 10 + (count / 3) * 102, 100, 100, 10 + (count % 3) * 100, 20 + (count / 3) * 100, JPEG_DIV_NONE);
        break;
      case 2:
        M5.Lcd.drawJpgFile(SD, "/image/stampCard3.jpg", 10 + (count % 3) * 102, 10 + (count / 3) * 102, 100, 100, 10 + (count % 3) * 100, 20 + (count / 3) * 100, JPEG_DIV_NONE);
        break;
    }
  }
  if(counter == 18) counter = 0;
}

/////////////////////////////////////////////////////
int num = 0;
void loop(){
  // mqttLoop(); // 常にチェックして切断されたら復帰できるように
  audioThread(&pt1);
  vibThread(&pt2);
  
    // ボタン操作に関する処理
    M5.update();
    if (M5.BtnA.wasPressed()) {
      countStamp();
      if(modeNumber == STAMP) drawStampImage();
      Serial.printf("%d\n", counter);
    } else if (M5.BtnB.wasPressed()) {
      if(modeNumber == STAMP){
        modeNumber = ALERT;
        initAlertMonitor();
        makeAlert();
      }
      else if(modeNumber == ALERT){
        modeNumber = STAMP;
        initStampMonitor();
        drawStampImage();
      }
    } else if (M5.BtnC.wasPressed()) {
      resetStamp();
      if(modeNumber == STAMP) initStampMonitor();
      Serial.printf("%d\n", counter);
    }

  /* DEBUG
  num++;
  if(num == 200000){
   num = 0;
   if(modeNumber != ALERT) initAlertMonitor();
   modeNumber = ALERT;
   makeAlert();    // アラートする
   initAlertMonitor();
  }
  */
  
}
