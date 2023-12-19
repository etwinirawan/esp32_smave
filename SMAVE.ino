#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <Wire.h>
#include <Arduino.h>
#include "FS.h"
#include <LittleFS.h>
#define SENSOR 27

#define FORMAT_LITTLEFS_IF_FAILED true

const char *ssid = "asli";
const char *password = "123412345";
const char *serverAddress = "https://0764m0hj-8080.asse.devtunnels.ms";
const char *urlBacaRef = "https://0764m0hj-8080.asse.devtunnels.ms/api/sv1/baca-ref";
const int serverPort = 8080;

String valve = "";
boolean dataSent = true;

/// WATERFLOW
long currentMillis = 0;
long previousMillis = 0;
int interval = 1000;
boolean ledState = LOW;
float calibrationFactor = 4.5;
volatile byte pulseCount;
byte pulse1Sec = 0;
float flowRate;
unsigned int flowMilliLitres;
// unsigned long totalMilliLitres;
float totalMilliLitres;

// Simpan nilai-nilai sebelumnya
float previousFlowRate = 0.0;
float previousTotalMilliLitres = 0.0;

// Inisialisasi waktu terakhir penyimpanan
unsigned long lastSaveTime = 0;  


WiFiUDP ntpUDP;
//////////////waktu(wib)
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200);



void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

void readFile(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return;
  }

  Serial.println("- read from file:");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
  file.close();
}

void setup() {
  Serial.begin(115200);

  // Menghubungkan ke WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  ///FS
  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
    Serial.println("LittleFS Mount Failed");
    return;
  }
  Serial.println("Test complete");

  // Inisialisasi NTPClient
  timeClient.begin();

  pinMode(SENSOR, INPUT_PULLUP);

  pulseCount = 0;
  flowRate = 0.0;
  flowMilliLitres = 0;
  totalMilliLitres = 0;
  previousMillis = 0;

  attachInterrupt(digitalPinToInterrupt(SENSOR), pulseCounter, FALLING);

  delay(1000);
}

void loop() {
  // Memperbarui waktu dari server NTP
  timeClient.update();

  // Mengirim data jika waktu saat ini adalah kelipatan 5 menit dan data belum dikirim
  if (timeClient.getMinutes() % 5 == 0) {
    if (dataSent == true) {
      sendToNodeJS();
      dataSent = false;  // Set flag bahwa data telah dikirim
      Serial.println("Send data to server");
    }
  } else {
    dataSent = true;  // Set flag bahwa data telah dikirim
  }

  // Serial.println(dataSent);
  if (Serial.available() > 0) {
    String data = Serial.readString();
    if (data.equals("debit")) {
      Serial.print("Debit saat ini : ");
      readFile(LittleFS, "/debit.txt");  // Read the complete file
      Serial.println("");
    }
    if (data.equals("volume")) {
      Serial.print("Volume saat ini : ");
      readFile(LittleFS, "/volume.txt");  // Read the complete file
      Serial.println("");
    }
    if (data.equals("kirim")) {
      sendToNodeJS();
    }
    if (data.equals("bacaref")) {
      bacaRef();
    }
  }
  if (valve == "on") {
    waterFlow();
  }
  if (Serial.available() > 0) {
    String data = Serial.readString();
  }
  // Serial.println(timeClient.getMinutes() % 5 == 0);
  delay(1000);  // Tunggu 1 detik sebelum membaca waktu lagi
}

void sendToNodeJS() {
  // Membuat objek HTTPClient
  HTTPClient http;

  // Menyiapkan URL server Node.js
  String url = "http://" + String(serverAddress) + ":" + String(serverPort) + "/data";

  // Menyiapkan data yang akan dikirim
  String data = "debit=8&volume=12";  // Ganti dengan data sesuai kebutuhan

  // Membuat permintaan HTTP POST
  http.begin(serverAddress);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Mengirim data
  int httpResponseCode = http.GET();

  // Menampilkan hasil respons server
  Serial.print("HTTP Response Code: ");
  Serial.println(httpResponseCode);
  String response = http.getString();
  Serial.println(response);
  // Menutup koneksi
  http.end();
}

void bacaRef() {
  HTTPClient http;

  DynamicJsonDocument doc(1024);
  doc["id_user"] = 1;
  doc["random_user"] = "TPjnu0HO";

  // Mengonversi objek JSON ke string
  String jsonString = "";
  serializeJson(doc, jsonString);

  // Kirim permintaan POST
  http.begin(urlBacaRef);
  http.addHeader("Content-Type", "application/json");
  Serial.print("jsonString : ");
  Serial.println(jsonString);
  int httpResponseCode = http.POST(jsonString);

  // Jika permintaan berhasil
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    String response = http.getString();
    Serial.println(response);
    //Deserialize dari json
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);

    String status = doc["status"];
    valve = status;
    Serial.print("Hasil Status : ");
    Serial.println(valve);
  } else {
    Serial.print("Error in HTTP request. HTTP Response code: ");
    Serial.println(httpResponseCode);
    Serial.print("httpResponseCode : ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void waterFlow() {
  currentMillis = millis();
  if (currentMillis - previousMillis > interval) {

    pulse1Sec = pulseCount;
    pulseCount = 0;

    // Because this loop may not complete in exactly 1 second intervals we calculate
    // the number of milliseconds that have passed since the last execution and use
    // that to scale the output. We also apply the calibrationFactor to scale the output
    // based on the number of pulses per second per units of measure (litres/minute in
    // this case) coming from the sensor.
    flowRate = ((1000.0 / (millis() - previousMillis)) * pulse1Sec) / calibrationFactor;
    previousMillis = millis();

    // Divide the flow rate in litres/minute by 60 to determine how many litres have
    // passed through the sensor in this 1 second interval, then multiply by 1000 to
    // convert to millilitres.
    flowMilliLitres = (flowRate / 60) * 1000;

    // Add the millilitres passed in this second to the cumulative total
    totalMilliLitres += flowMilliLitres;

    // Print the flow rate for this second in litres / minute
    Serial.print("Flow rate: ");
    Serial.print(int(flowRate));  // Print the integer part of the variable
    Serial.print("L/min");
    Serial.print("\t");  // Print tab space

    // Print the cumulative total of litres flowed since starting
    Serial.print("Output Liquid Quantity: ");
    Serial.print(totalMilliLitres);
    Serial.print("mL / ");
    Serial.print(totalMilliLitres / 1000);
    Serial.println("L");

    // Check if 10 seconds have passed since the last save
    if (currentMillis - lastSaveTime > 10000) {
      // Check if data is different from previous data before saving to LittleFS
      if (flowRate != previousFlowRate || totalMilliLitres != previousTotalMilliLitres) {
        // Simpan data ke LittleFS
        writeFile(LittleFS, "/debit.txt", String(flowRate).c_str());
        writeFile(LittleFS, "/volume.txt", String(totalMilliLitres / 1000).c_str());

        // Update nilai-nilai sebelumnya
        previousFlowRate = flowRate;
        previousTotalMilliLitres = totalMilliLitres;
        // Update waktu terakhir penyimpanan
        lastSaveTime = currentMillis;
      }
    }
  }
}
