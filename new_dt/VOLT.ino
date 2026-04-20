#include <WiFi.h>
#include <WebServer.h>
#include <PZEM004Tv30.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <time.h>

// WIFI
const char* ssid = "harish";
const char* password = "123456789";

WebServer server(80);

// TFT
#define TFT_CS 5
#define TFT_DC 21
#define TFT_RST 22
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// PZEM
#define RX 16
#define TX 17
PZEM004Tv30 pzem(Serial2, RX, TX);

// PINS
#define RELAY 25
#define BUTTON 32

// VARIABLES
float voltage=0,current=0,power=0,energy=0,lastEnergy=0;
float balance=100, tariff=10;
float dailyEnergy=0,startEnergy=0;

float maxVoltage=250, maxCurrent=10;

bool relayState=false;
bool lastButton=HIGH;
bool pzemOK=true,fault=false;

// ================= SENSOR =================
void readSensor() {
  float v=pzem.voltage();
  float c=pzem.current();
  float p=pzem.power();
  float e=pzem.energy();

  if(isnan(v) || v==0){
    pzemOK=false;
    voltage=0; current=0; power=0;
    return;
  }

  pzemOK=true;

  voltage=v;
  current=isnan(c)?0:c;
  power=isnan(p)?0:p;
  if(!isnan(e)) energy=e;
}

// ================= BILLING =================
void updateBilling(){
  if(!pzemOK){
    lastEnergy=energy;
    return;
  }

  float delta=energy-lastEnergy;

  if(delta>0){
    balance -= delta*tariff;
  }

  lastEnergy=energy;

  if(balance<=0){
    balance=0;
    relayState=false;
  }

  dailyEnergy = energy - startEnergy;
}

// ================= TIME =================
void initTime(){
  configTime(19800,0,"pool.ntp.org");
}

int lastDay=-1;

void checkMidnightReset(){
  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){
    if(timeinfo.tm_mday!=lastDay && timeinfo.tm_hour==0){
      startEnergy=energy;
      lastDay=timeinfo.tm_mday;
    }
  }
}

// ================= PROTECTION =================
void checkProtection(){
  if(voltage>maxVoltage || current>maxCurrent){
    fault=true;
    relayState=false;
  } else fault=false;
}

// ================= RELAY =================
void applyRelay(){
  if(!fault && balance>0){
    digitalWrite(RELAY, relayState ? LOW : HIGH);  // ACTIVE LOW
  } else {
    relayState=false;
    digitalWrite(RELAY, HIGH);
  }
}

// ================= BUTTON =================
void checkButton(){
  static unsigned long lastDebounceTime = 0;
  bool s=digitalRead(BUTTON);
  if(lastButton==HIGH && s==LOW && (millis() - lastDebounceTime) > 200){
    if(!fault && balance>0){
      relayState=!relayState;
      Serial.println("🔘 Button toggled relay");
    }
    lastDebounceTime = millis();
  }
  lastButton=s;
}

// ================= UI =================
void drawBox(int x,int y,String title,String value){
  tft.drawRect(x,y,75,30,ST77XX_WHITE);

  tft.setCursor(x+3,y+3);
  tft.setTextColor(ST77XX_CYAN);
  tft.print(title);

  tft.setCursor(x+3,y+15);
  tft.setTextColor(ST77XX_WHITE);
  tft.print(value);
}

void updateDisplay(){
  tft.fillScreen(ST77XX_BLACK);

  tft.setCursor(5,0);
  tft.setTextColor(ST77XX_GREEN);
  tft.print("PREPAID METER");

  drawBox(0,12,"VOLT", pzemOK?String(voltage,1):"--");
  drawBox(80,12,"CURR", pzemOK?String(current,2):"--");

  drawBox(0,45,"POWER", pzemOK?String(power,1):"--");
  drawBox(80,45,"ENERGY", pzemOK?String(energy,3):"--");

  drawBox(0,78,"TODAY", pzemOK?String(dailyEnergy,4):"--");

  String status;
  if(!pzemOK) status="PZEM OFF";
  else if(fault) status="FAULT";
  else status=relayState?"ON":"OFF";

  drawBox(80,78,"STATUS",status);

  tft.setCursor(5,110);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print("Bal:");
  tft.print(balance,2);

  tft.setCursor(5,120);
  tft.setTextColor(ST77XX_BLUE);
  tft.print(WiFi.localIP());
}

// ================= API =================
void handleData(){
  readSensor();
  updateBilling();

  String json="{";
  json+="\"voltage\":"+String(voltage)+",";
  json+="\"current\":"+String(current)+",";
  json+="\"power\":"+String(power)+",";
  json+="\"energy\":"+String(energy)+",";
  json+="\"dailyEnergy\":"+String(dailyEnergy)+",";
  json+="\"balance\":"+String(balance)+",";
  json+="\"relay\":"+String(relayState)+",";
  json+="\"fault\":"+String(fault)+",";
  json+="\"pzem\":"+String(pzemOK)+",";
  json+="\"tariff\":"+String(tariff)+",";
  json+="\"maxVoltage\":"+String(maxVoltage)+",";
  json+="\"maxCurrent\":"+String(maxCurrent);
  json+="}";
  server.send(200,"application/json",json);
}

// 🔥 RELAY CONTROL WITH SERIAL LOG
void handleRelay(){
  if(!server.hasArg("state")){
    Serial.println("❌ Relay request missing state");
    server.send(400,"application/json","{\"error\":\"Missing state\"}");
    return;
  }

  String state=server.arg("state");
  state.toLowerCase();

  Serial.print("📡 Relay request received: ");
  Serial.println(state);

  if(fault || balance<=0){
    Serial.println("⚠️ Relay blocked (fault or low balance)");
    server.send(403,"application/json","{\"error\":\"Blocked\"}");
    return;
  }

  if(state=="on"){
    relayState=true;
    Serial.println("✅ Relay turned ON");
  } 
  else if(state=="off"){
    relayState=false;
    Serial.println("🛑 Relay turned OFF");
  } 
  else {
    Serial.println("❌ Invalid relay command");
    server.send(400,"application/json","{\"error\":\"Invalid\"}");
    return;
  }

  applyRelay();

  String json="{\"relay\":"+String(relayState?"true":"false")+"}";
  server.send(200,"application/json",json);
}

void handleRecharge(){
  if(server.hasArg("amount")){
    float a=server.arg("amount").toFloat();
    balance+=a;
    Serial.print("💰 Recharge: ");
    Serial.println(a);
  }
  server.send(200,"text/plain","OK");
}

void handleThreshold(){
  if(server.hasArg("voltage")) maxVoltage=server.arg("voltage").toFloat();
  if(server.hasArg("current")) maxCurrent=server.arg("current").toFloat();
  if(server.hasArg("tariff")) tariff=server.arg("tariff").toFloat();

  Serial.println("⚙️ Threshold updated");

  server.send(200,"text/plain","UPDATED");
}

void getThreshold(){
  String json="{";
  json+="\"maxVoltage\":"+String(maxVoltage)+",";
  json+="\"maxCurrent\":"+String(maxCurrent)+",";
  json+="\"tariff\":"+String(tariff);
  json+="}";
  server.send(200,"application/json",json);
}

// ================= SETUP =================
void setup(){
  Serial.begin(115200);

  pinMode(RELAY, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  digitalWrite(RELAY, HIGH);

  Serial2.begin(9600,SERIAL_8N1,RX,TX);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);

  WiFi.begin(ssid,password);

  Serial.print("Connecting WiFi");
  while(WiFi.status()!=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ Connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  initTime();

  startEnergy=pzem.energy();
  lastEnergy=startEnergy;

  server.on("/data",handleData);
  server.on("/recharge",HTTP_POST,handleRecharge);
  server.on("/relay",HTTP_PUT,handleRelay);
  server.on("/relay",HTTP_GET,handleRelay);
  server.on("/threshold",HTTP_GET,getThreshold);
  server.on("/threshold",HTTP_PUT,handleThreshold);

  server.begin();
  Serial.println("🚀 Server started");
}

// ================= LOOP =================
unsigned long lastUpdate=0;

void loop(){
  server.handleClient();
  checkButton();
  applyRelay();

  if(millis()-lastUpdate>2000){
    lastUpdate=millis();

    readSensor();
    checkProtection();
    updateBilling();
    checkMidnightReset();
    updateDisplay();

    Serial.print("⚡ Relay:");
    Serial.print(relayState);
    Serial.print(" | V:");
    Serial.print(voltage);
    Serial.print(" | I:");
    Serial.println(current);
  }
}