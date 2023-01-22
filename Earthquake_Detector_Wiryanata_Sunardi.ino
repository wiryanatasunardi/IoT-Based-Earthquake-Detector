#include <Arduino.h>
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>
#include <PubSubClient.h> //Library MQTT
#include <WiFi.h> //Library WiFi
#include <WiFiClient.h> //Library WiFi
#include "time.h" //Library Timestamp UNIX
#include "Wire.h" //I2C Wire Library
#include <SPI.h> //SPI Communication Library
#include <Adafruit_GFX.h> //OLED GFX Library
#include <Adafruit_SSD1306.h> //OLED GFX Library
#include <MPU6050_light.h> //Accelerometer MPU 6050 Library
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); //Declare Screen Width and Height

//Provide the token generation process info.
#include "addons/TokenHelper.h"
//Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

// Insert your network credentials
#define WIFI_SSID "SSID"
#define WIFI_PASSWORD "PASSWORD"

// Insert Firebase project API Key
#define API_KEY "YOUR-KEY-API"

// Insert RTDB URLefine the RTDB URL */
#define DATABASE_URL "URL DATABASE" 

//Initialize Jumlah Sampel, Nilai Max dan Min Pendeteksi serta Waktu Buzzer
#define samples 50
#define maxVal 50
#define minVal -50 
#define buzTime 100

// NTP server to request epoch time
const char* ntpServer = "pool.ntp.org"; //Server Timestamp UNIX

// Variable to save current epoch time
unsigned long epochTime; //Variable untuk menyimpan timestamp UNIX

// Function that gets current epoch time
unsigned long getTime() { //Function untuk mendapatkan timestamp UNIX saat ini
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}

//Define Firebase Data object
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
int count = 0;
bool signupOK = false;
const char* mqttServer = "broker.mqttdashboard.com"; //MQTT Broker URL
const int mqttPort = 1883; //MQTT Broker Port
const char* mqttUser = "username"; //MQTT Username
const char* mqttPassword = "password"; //MQTT Username Password
int Vibration_pin = 13; //Pin MQTT
int buzz_pin = 12; //Pin Buzzer
int Sensor_State = 1; //Initialize Keadaan Sensor
int LED_pin = 14; //Initialize LED Penanda Alat Menyala

WiFiClient espClient; //Initialize Client untuk WiFi
PubSubClient client(espClient); //Initialize Client untuk MQTT
MPU6050 mpu(Wire); //Initialize Sensor melalui Library Wire

int xsample = 0; //Initialize Parameter Sampel X,Y,Z untuk Deteksi Gempa
int ysample = 0;
int zsample = 0;
long start;
int buz = 0; //Initialize Keadaan Buzzer Mati

//Initialize String untuk Dikirim Melalui MQTT
String xStr;
String yStr;
String zStr;
//Initialize Array Char untuk Dikirim Melalui MQTT
char xArr[4];
char yArr[4];
char zArr[4];
int value1, value2, value3, xValue, yValue, zValue;

void setup() {
  Serial.begin(115200);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) { //Error Handling OLED
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  Wire.begin(); //Begin Wire
  Serial.println("==================== EARTHQUAKE DETECTOR ====================");
  Serial.println("Calibrating....");
  Serial.println("Please Wait....");
  byte status = mpu.begin(); //Kalibrasi Awal Sensor MPU6050
  Serial.print(F("MPU6050 status: "));
  Serial.println(status);
  while(status!=0){ }
  Serial.println(F("Calculating offsets, do not move MPU6050"));
  delay(1000);
  mpu.calcOffsets(); // gyro and accelero
  Serial.println("Done!\n");
  pinMode(buzz_pin, OUTPUT);
  pinMode(Vibration_pin, INPUT);
  digitalWrite(buzz_pin, LOW);

  for(int i = 0; i < samples; i++){ //Update Posisi X,Y,Z Accelerometer
    mpu.update();
    xsample += mpu.getAngleX();
    ysample += mpu.getAngleY();
    zsample += mpu.getAngleZ();
  }

  //Perhitungan Posisi X,Y,Z Accelerometer Berdasarkan Sampel
  xsample /= samples;
  ysample /= samples;
  zsample /= samples;

  //Pengaturan Display OLED
  display.display();
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  Serial.println("Calibrated");
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  /* Sign up */
  if (Firebase.signUp(&config, &auth, "", "")){
    Serial.println("ok");
    signupOK = true;
  }
  else{
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  //Connect MQTT
  client.setServer(mqttServer, mqttPort);

  while (!client.connected()) {
    Serial.println("Connecting to MQTT..."); //Error Handling MQTT
 
    if (client.connect("ESP32Client", mqttUser, mqttPassword )) {
 
      Serial.println("connected");
 
    } else {
 
      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);
 
    }
  }
  configTime(0, 0, ntpServer); //Initialize NTPServer

  xTaskCreatePinnedToCore(mqttUpload, "Upload MQTT", 5000, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(readSensor, "Read Sensor", 5000, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(buzzOn, "Buzzer Task", 5000, NULL, 2, NULL, 0);

}

void loop() {
  // put your main code here, to run repeatedly:
  display.setCursor(0,0); //Setting Posisi Kursor OLED
  display.setTextSize(1);
  display.println("  Earthquake Status");
  display.drawFastHLine(0, 10, 128, 1); //Menggambar Garis
  display.setCursor(0,17);
  display.setTextSize(1);
  // put your main code here, to run repeatedly:
  //Serial.print("Vibration Status : ");
  Sensor_State = digitalRead(Vibration_pin); //Membaca Keadaan Getaran
  if (Sensor_State == 1){
    Serial.println("Sensing Vibration");
  }
  else{
    Serial.println("No Vibration");
  }
  
  epochTime = getTime(); //Mendapatkan Timestamp UNIX pembacaan

  //Penggabungan String untuk Data Logging
  xStr = String(epochTime) + "," + String(buz) + "," + String(xValue) + "," + String(yValue) + "," + String(zValue);
  xStr.toCharArray(xArr, xStr.length()+1);

  delay(1000);
  display.display();
  display.clearDisplay();
  
  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 15000 || sendDataPrevMillis == 0)){
    sendDataPrevMillis = millis();
    // Write an Int number on the database path test/int
    if (Firebase.RTDB.setInt(&fbdo, "test/xValue", xValue)){
      Serial.println("PASSED");
      Serial.println("PATH: " + fbdo.dataPath());
      Serial.println("TYPE: " + fbdo.dataType());
    }
    else {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }
    
    if (Firebase.RTDB.setInt(&fbdo, "test/yValue", yValue)){
      Serial.println("PASSED");
      Serial.println("PATH: " + fbdo.dataPath());
      Serial.println("TYPE: " + fbdo.dataType());
    }
    else {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }
    
    if (Firebase.RTDB.setInt(&fbdo, "test/zValue", zValue)){
      Serial.println("PASSED");
      Serial.println("PATH: " + fbdo.dataPath());
      Serial.println("TYPE: " + fbdo.dataType());
    }
    else {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }
  }

}

void mqttUpload(void *pvParameters) {
  const TickType_t xTicksToWait = pdMS_TO_TICKS(1000);
  for (;;) {
    vTaskDelay(xTicksToWait);
    client.loop();
    client.publish("esp32/quakedet/value", xArr);
  }
}

void readSensor(void *pvParameters) {
  const TickType_t xTicksToWait = pdMS_TO_TICKS(1000);
  for (;;) {
    vTaskDelay(xTicksToWait);
    mpu.update(); //Update Keadaan Sensor
    
    //Mendapatkan Keadaan Sudut X,Y,Z
    value1 = mpu.getAngleX();
    value2 = mpu.getAngleY();
    value3 = mpu.getAngleZ();

    //Update Nilai X,Y,Z Sesungguhnya
    xValue = xsample-value1;
    yValue = ysample-value2;
    zValue = zsample-value3;

    Serial.print("x = ");
    Serial.println(xValue);
    Serial.print("y = ");
    Serial.println(yValue);
    Serial.print("z = ");
    Serial.println(zValue);
  }
}

void buzzOn(void *pvParameters) {
  const TickType_t xTicksToWait = pdMS_TO_TICKS(500);
  for (;;) {
    vTaskDelay(xTicksToWait);
    //Alarm Gempa dan Menyalakan/Mematikan Buzzer
    if(xValue < minVal || xValue > maxVal || yValue < minVal || yValue > maxVal || zValue < minVal || zValue > maxVal){
      if(buz == 0){
        start = millis();
        display.println(" Earthquake Alert !!!");
        buz = 1;
      }
    }
  
    else if(buz == 1){
      Serial.println("Earthquake Alert !!!");
      display.println(" Earthquake Alert !!!");
      if(millis() >= start + buzTime){
        buz = 0;
        display.println("Everything Is Fine");
      }
    }
    else{
      buz = 0;
      display.println(" Everything Is Fine");
    }
    //digitalWrite(LED_pin, buz);

    Serial.print("Buzz = ");
    Serial.println(buz);
    digitalWrite(buzz_pin, buz);
    }
  }
