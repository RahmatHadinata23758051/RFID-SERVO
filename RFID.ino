#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>
#include <EEPROM.h>

// Ganti dengan kredensial Wi-Fi Anda
const char* ssid = "POPO";
const char* password = "12345678";

// URL Google Apps Script Anda
const char* serverUrl = "https://script.google.com/macros/s/AKfycbx2b4tjBGNkx6OSz29GJ9YLKKuDqFDC4DtEgDaCsFTn-yJ4AZZQmRJQ1DDCTy7_BxaGEA/exec";

// Pin untuk koneksi RFID
const uint8_t RST_PIN = D3;
const uint8_t SS_PIN = D4;

// Pin untuk servo
const int SERVO_PIN = D2;

// Objek untuk RFID dan servo
MFRC522 mfrc522(SS_PIN, RST_PIN);
Servo servo;

// EEPROM konfigurasi
const int EEPROM_SIZE = 512; // Kapasitas EEPROM
const int UID_SIZE = 4;      // Panjang UID kartu (4 byte)
const int NAME_SIZE = 10;    // Panjang maksimal nama
const int NIM_SIZE = 8;      // Panjang maksimal NIM
const int PRODI_SIZE = 5;    // Panjang maksimal prodi
const int PLAT_SIZE = 10;    // Panjang maksimal plat kendaraan
const int RECORD_SIZE = UID_SIZE + NAME_SIZE + NIM_SIZE + PRODI_SIZE + PLAT_SIZE; // Ukuran per record
const int MAX_RECORDS = EEPROM_SIZE / RECORD_SIZE;

// Inisialisasi objek HTTP dan WiFiClientSecure
WiFiClientSecure client;
HTTPClient http;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();
  servo.attach(SERVO_PIN);
  servo.write(0); // Pastikan palang servo dalam posisi tertutup

  // Menghubungkan ke Wi-Fi
  connectToWiFi();

  // Memulai koneksi HTTPS
  client.setInsecure();  // Untuk menghindari masalah sertifikat SSL pada ESP8266

  EEPROM.begin(EEPROM_SIZE);
  Serial.println("Scan kartu RFID untuk membuka atau mendaftarkan palang parkir...");

  // Jika ada perintah reset via serial, reset daftar kartu
  Serial.println("Tekan 'r' untuk mereset seluruh daftar kartu.");
}

void loop() {
  // Cek apakah ada perintah dari serial untuk mereset daftar kartu
  if (Serial.available()) {
    char input = Serial.read();
    if (input == 'r' || input == 'R') {
      Serial.println("Mereset seluruh daftar kartu...");
      resetCardList();
      Serial.println("Daftar kartu telah direset.");
    }
  }

  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  Serial.print(F("UID Kartu: "));
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.println();

  if (isCardRegistered(mfrc522.uid.uidByte)) {
    Serial.println("Kartu terdaftar. Membuka palang...");
    openGate();
    sendToGoogleSheets(mfrc522.uid.uidByte); // Kirim data ke Google Sheets
  } else {
    Serial.println("Kartu tidak terdaftar. Masukkan data pengguna:");
    registerCard(mfrc522.uid.uidByte);
    Serial.println("Kartu berhasil didaftarkan!");
  }

  mfrc522.PICC_HaltA();
}

bool isCardRegistered(byte *uid) {
  for (int i = 0; i < MAX_RECORDS; i++) {
    byte storedUID[UID_SIZE];
    EEPROM.get(i * RECORD_SIZE, storedUID);
    bool match = true;
    for (int j = 0; j < UID_SIZE; j++) {
      if (storedUID[j] != uid[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

void registerCard(byte *uid) {
  for (int i = 0; i < MAX_RECORDS; i++) {
    byte storedUID[UID_SIZE];
    EEPROM.get(i * RECORD_SIZE, storedUID);

    if (storedUID[0] == 0xFF) {
      // Simpan UID
      for (int j = 0; j < UID_SIZE; j++) {
        EEPROM.write(i * RECORD_SIZE + j, uid[j]);
      }

      // Masukkan data pengguna secara bertahap
      String name = getInput("Masukkan Nama (maks. 10 karakter): ", 10);
      String nim = getInput("Masukkan NIM (maks. 8 karakter): ", 8);
      String prodi = getInput("Masukkan Prodi (maks. 5 karakter): ", 5);
      String plat = getInput("Masukkan Plat Kendaraan (maks. 10 karakter): ", 10);

      // Simpan data ke EEPROM
      saveUserData(i, name, nim, prodi, plat);
      EEPROM.commit();
      return;
    }
  }
  Serial.println("Memori EEPROM penuh! Tidak dapat mendaftarkan kartu baru.");
}

String getInput(String prompt, int maxLength) {
  String input = "";
  Serial.println(prompt);
  while (input.length() == 0) {
    if (Serial.available() > 0) {
      input = Serial.readStringUntil('\n');
      input.trim();
      if (input.length() > maxLength) input = input.substring(0, maxLength);
    }
  }
  Serial.print(prompt);
  Serial.println(input);
  return input;
}

void saveUserData(int index, String name, String nim, String prodi, String plat) {
  // Simpan data ke EEPROM
  for (int j = 0; j < NAME_SIZE; j++) {
    EEPROM.write(index * RECORD_SIZE + UID_SIZE + j, name[j]);
  }
  for (int j = 0; j < NIM_SIZE; j++) {
    EEPROM.write(index * RECORD_SIZE + UID_SIZE + NAME_SIZE + j, nim[j]);
  }
  for (int j = 0; j < PRODI_SIZE; j++) {
    EEPROM.write(index * RECORD_SIZE + UID_SIZE + NAME_SIZE + NIM_SIZE + j, prodi[j]);
  }
  for (int j = 0; j < PLAT_SIZE; j++) {
    EEPROM.write(index * RECORD_SIZE + UID_SIZE + NAME_SIZE + NIM_SIZE + PRODI_SIZE + j, plat[j]);
  }
}

void openGate() {
  servo.write(90);   // Buka palang
  delay(5000);        // Tunggu 5 detik
  servo.write(0);     // Tutup palang
}

void sendToGoogleSheets(byte *uid) {
  String uidStr = "";
  for (byte i = 0; i < UID_SIZE; i++) {
    uidStr += String(uid[i], HEX);
  }

  String name = "", nim = "", prodi = "", plat = "";
  for (int i = 0; i < MAX_RECORDS; i++) {
    byte storedUID[UID_SIZE];
    EEPROM.get(i * RECORD_SIZE, storedUID);

    if (storedUID[0] != 0xFF) {
      char nameChar[NAME_SIZE + 1] = {0};
      char nimChar[NIM_SIZE + 1] = {0};
      char prodiChar[PRODI_SIZE + 1] = {0};
      char platChar[PLAT_SIZE + 1] = {0};

      for (int j = 0; j < NAME_SIZE; j++) nameChar[j] = EEPROM.read(i * RECORD_SIZE + UID_SIZE + j);
      for (int j = 0; j < NIM_SIZE; j++) nimChar[j] = EEPROM.read(i * RECORD_SIZE + UID_SIZE + NAME_SIZE + j);
      for (int j = 0; j < PRODI_SIZE; j++) prodiChar[j] = EEPROM.read(i * RECORD_SIZE + UID_SIZE + NAME_SIZE + NIM_SIZE + j);
      for (int j = 0; j < PLAT_SIZE; j++) platChar[j] = EEPROM.read(i * RECORD_SIZE + UID_SIZE + NAME_SIZE + NIM_SIZE + PRODI_SIZE + j);

      name = String(nameChar);
      nim = String(nimChar);
      prodi = String(prodiChar);
      plat = String(platChar);
      break;
    }
  }

  String url = serverUrl;
  http.begin(client, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Mendapatkan waktu saat ini
  String waktu = String(millis() / 1000); // Menyimpan waktu dalam detik sejak program dimulai (gunakan waktu real-time jika diperlukan)

  // Kirim data ke Google Sheets, termasuk waktu
  String postData = "uid=" + uidStr + "&name=" + name + "&nim=" + nim + "&prodi=" + prodi + "&plat=" + plat + "&waktu=" + waktu;
  int httpCode = http.POST(postData);

  if (httpCode > 0) {
    Serial.println("Data berhasil dikirim ke Google Sheets.");
  } else {
    Serial.println("Error saat mengirim data ke Google Sheets.");
  }

  http.end();
}


void resetCardList() {
  for (int i = 0; i < MAX_RECORDS; i++) {
    for (int j = 0; j < RECORD_SIZE; j++) {
      EEPROM.write(i * RECORD_SIZE + j, 0xFF);
    }
  }
  EEPROM.commit();
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Menyambungkan ke WiFi...");
  }
  Serial.println("Terhubung ke WiFi!");
}
