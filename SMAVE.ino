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
#define HARDRESET 2


#define FORMAT_LITTLEFS_IF_FAILED true

const char *ssid = "asli";
const char *password = "123412345";
const char *urlSendData = "https://0764m0hj-8080.asse.devtunnels.ms/api/sv1/input-data";
const char *urlBacaRef = "https://0764m0hj-8080.asse.devtunnels.ms/api/sv1/baca-ref";
const char *urlReqToken = "https://0764m0hj-8080.asse.devtunnels.ms/api/sv1/req-token";
const int serverPort = 8080;

String valve = "";
String reqToken = "";
boolean dataSent = true;
// double roundedFlowRate = round(static_cast<double>(flowRate) * 100.0) / 100.0;

int countSendToServer;

/// WATERFLOW
long currentMillis = 0;
long previousMillis = 0;
int interval = 1000;
boolean ledState = LOW;
float calibrationFactor = 596.0;
volatile int pulseCount;
byte pulse1Sec = 0;
float flowRate;
unsigned int flowMilliLitres;
// unsigned long volume;

int volume;

// Simpan nilai-nilai sebelumnya
int previousVolume;

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

  pinMode(SENSOR, INPUT_PULLUP);
  // pinMode(HARDRESET, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(SENSOR), pulseCounter, FALLING);
  sei();

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

  // baca ref
  bacaRef();

  bacaFs();

  delay(1000);
}

void loop() {
  // Memperbarui waktu dari server NTP
  timeClient.update();

  if (valve.equals("on")) {
    waterFlow();
    // Mengirim data jika waktu saat ini adalah kelipatan 5 menit dan data belum dikirim
    if (timeClient.getMinutes() % 5 == 0) {
      if (dataSent == true) {
        reqTokenFromServer();
        sendToServer();
        dataSent = false;  // Set flag bahwa data telah dikirim
        Serial.println("Send data to server");
      }
    } else {
      dataSent = true;  // Set flag bahwa data telah dikirim
    }
  }else{
    bacaRef();
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
      sendToServer();
    }
    if (data.equals("bacaref")) {
      bacaRef();
    }
    if (data.equals("req")) {
      reqTokenFromServer();
    }
  }
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
    flowRate = (float)pulseCount / calibrationFactor;
    // flowRate = (volume * 1000.0 * 33.93) / 60.0;

    pulseCount = 0;

    volume += flowRate / 60;
    // flowMilliLitres = 0;
    // flowRate = 0;
    // Print the flow rate for this second in litres / minute
    Serial.print("Flow rate: ");
    // Serial.print(int(flowRate));  // Print the integer part of the variable
    Serial.print(flowRate);  // Print the integer part of the variable
    Serial.print("L/min");
    Serial.print("\t");  // Print tab space

    // Print the cumulative total of litres flowed since starting
    Serial.print("Output Liquid Quantity: ");
    Serial.print(volume);
    Serial.println("L");

    // Check if 10 seconds have passed since the last save
    if (currentMillis - lastSaveTime > 10000) {
      // Check if data is different from previous data before saving to LittleFS
      if (volume != previousVolume) {
        // Simpan data ke LittleFS
        writeFile(LittleFS, "/volume.txt", String(volume).c_str());

        // Update nilai-nilai sebelumnya
        previousVolume = volume;

        // Update waktu terakhir penyimpanan
        lastSaveTime = currentMillis;
      }
    }
    previousMillis = millis();
  }
}

void reqTokenFromServer() {
  HTTPClient http;

  DynamicJsonDocument doc(1024);
  doc["secret"] = "secret";
  doc["id"] = 1;
  doc["random"] = "TPjnu0HO";

  // Mengonversi objek JSON ke string
  String jsonString = "";
  serializeJson(doc, jsonString);

  // Kirim permintaan POST
  http.begin(urlReqToken);
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

    String token = doc["token"];
    reqToken = token;
    Serial.print("Token anda : ");
    Serial.println(reqToken);
  } else {
    Serial.print("Error in HTTP request. HTTP Response code: ");
    Serial.println(httpResponseCode);
    Serial.print("httpResponseCode : ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void sendToServer() {
  HTTPClient http;

  DynamicJsonDocument doc(1024);

  doc["token"] = reqToken;
  doc["volume"] = volume;
  doc["debit"] = flowRate;

  // Mengonversi objek JSON ke string
  String jsonString = "";
  serializeJson(doc, jsonString);

  // Kirim permintaan POST
  http.begin(urlSendData);
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

    String msg = doc["msg"];
    Serial.print("Keterangan : ");
    Serial.println(msg);
    if (msg.equals("success")) {
      //reset
      writeFile(LittleFS, "/volume.txt", "0");
      bacaFs();
      bacaRef();
    } else {
      if (countSendToServer > 4) {
        //HARDRESET
        // digitalWrite(HARDRESET, HIGH);
        
      }
      countSendToServer++;
      Serial.print("Loop ke - ");
      Serial.println(countSendToServer);
      sendToServer();
    }
  } else {
    Serial.print("Error in HTTP request. HTTP Response code: ");
    Serial.println(httpResponseCode);
    Serial.print("httpResponseCode : ");
    Serial.println(httpResponseCode);
    if (httpResponseCode == -11) {
      Serial.println("Resend");
      sendToServer();
    }
  }
  http.end();
}

void bacaFs() {
  File file = LittleFS.open("/volume.txt", "r");

  // Periksa apakah file berhasil dibuka
  if (file) {
    // Baca nilai dari file dan konversi ke integer
    volume = file.readString().toInt();

    // Tampilkan nilai di Serial Monitor (opsional)
    Serial.println(volume);

    // Tutup file setelah selesai membaca
    file.close();
  } else {
    // Gagal membuka file, lakukan sesuatu di sini
    Serial.println("Gagal membuka file");
  }
}
