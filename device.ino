#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Adafruit_SSD1306.h>
#include <FirebaseArduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "DHT.h"
#include "heartRate.h"
#include <DallasTemperature.h>
#include <OneWire.h>

#define WIFI_SSID "Galaxy S20"
#define WIFI_PASSWORD "aaaaaaaa"
#define FIREBASE_HOST "health-band-cb631-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "4sIQt6ncxeXiTf6GDM4Rq7Ll8YZIPjHG124wd6u7"



MAX30105 particleSensor;

const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred

#define DHTPIN 14
#define DHTTYPE DHT11
#define ONE_WIRE_BUS 12

float beatsPerMinute;
int beatAvg;
DHT dht(DHTPIN, DHTTYPE);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
long endD, startD, datetime;
String epochTime, mac, f, codef;
unsigned long lastTime = 0;
unsigned long timerDelay = 15000;
unsigned long lastTime1 = 0;
unsigned long timerDelay1 = 30000;

int b_temp, h, t;
void setup() {

  Serial.begin(9600);
  dht.begin();
  sensors.begin();
  Serial.println("Initializing...");

  // Initialize sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
  {
    Serial.println("MAX30105 was not found. Please check wiring/power. ");
    while (1);
  }
  Serial.println("Place your index finger on the sensor with steady pressure.");

  particleSensor.setup(); //Configure sensor with default settings
  particleSensor.setPulseAmplitudeRed(0x0A); //Turn Red LED to low to indicate sensor is running
  particleSensor.setPulseAmplitudeGreen(0); //Turn off Green LED

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  timeClient.begin();
  timeClient.setTimeOffset(0);
  timeClient.update();
  epochTime = timeClient.getEpochTime();
  Serial.println(epochTime);
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);

  const ArduinoJson::JsonObject& device = Firebase.get("/devices").getJsonVariant().asObject();
  for (ArduinoJson::Internals::ListConstIterator<JsonPair> jpi = device.begin(); jpi != device.end(); ++jpi) {
    JsonPair jp = *jpi;
    JsonObject& o = jp.value.asObject();
    codef = o.get<String>("code");

    if (codef == "band123456") {
      mac = o.get<String>("datakey");
      f = o.get<String>("user");
      Serial.println(mac);
      Serial.println(f);
      break;
    }
  }
}

void loop() {

  long irValue = particleSensor.getIR();
  if (checkForBeat(irValue) == true) {
    //We sensed a beat!
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0);

    if (beatsPerMinute < 255 && beatsPerMinute > 20)
    {
      rates[rateSpot++] = (byte)beatsPerMinute; //Store this reading in the array
      rateSpot %= RATE_SIZE; //Wrap variable

      //Take average of readings
      beatAvg = 0;
      for (byte x = 0 ; x < RATE_SIZE ; x++)
        beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }

  Serial.print("IR=");
  Serial.print(irValue);
  Serial.print(", BPM=");
  Serial.print(beatsPerMinute);
  Serial.print(", Avg BPM=");
  Serial.print(beatAvg);

  if (irValue < 50000) {
    Serial.print(" No finger?");
    Serial.println();
    dhtRead();
    bodytemp();
    if ((millis() - lastTime) > timerDelay) {
      liveData();
      lastTime = millis();
    } else if ((millis() - lastTime1) > timerDelay1) {
      pushData();
      lastTime1 = millis();
    }
  }


  Serial.println();
}
void dhtRead() {

  h = dht.readHumidity();
  t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }
  Serial.print(F("Humidity: "));
  Serial.print(h);
  Serial.print(F("%  Temperature: "));
  Serial.print(t);
  Serial.println(F("°C "));
}
void bodytemp() {
  sensors.requestTemperatures();
  Serial.print("Body Temperature is: ");
  Serial.println(sensors.getTempCByIndex(0));
  b_temp = sensors.getTempCByIndex(0);
  delay(500);
}
void liveData() {
  Firebase.setString("prediction_input/Heartrate", (String)beatAvg);
  Firebase.setString("prediction_input/body_temp", (String)b_temp);
  Firebase.setString("prediction_input/Temp", (String)t);
  Firebase.setString("prediction_input/Humidity", (String)h);
  Firebase.setString("prediction_input/Spo2", "98");
  Firebase.set("/users/" + f + "/devices/" + mac + "/live/heartrate", beatAvg);
  Firebase.set("/users/" + f + "/devices/" + mac + "/live/spo2", 98);
  Firebase.set("/users/" + f + "/devices/" + mac + "/live/temperature", t);
  Firebase.set("/users/" + f + "/devices/" + mac + "/live/humidity", h);
  Firebase.set("/users/" + f + "/devices/" + mac + "/live/body_temperature", b_temp);
}
void pushData() {
  StaticJsonBuffer<200> jsonBuffer2;
  JsonObject& obj2 = jsonBuffer2.createObject();
  obj2["date"] = epochTime + "000";
  obj2["heartrate"] = beatAvg;
  obj2["spo2"] = 98;
  obj2["temperature"] = t;
  obj2["humidity"] = h;
  obj2["body_temperature"] = b_temp;
  Firebase.push("/users/" + f + "/devices/" + mac + "/statistics", obj2);
}
