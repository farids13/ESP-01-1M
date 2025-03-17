#include <ESP8266WiFi.h>
#include <EEPROM.h>

#define RELAY 0
#define EEPROM_SIZE 128  // Tambah ukuran untuk SSID trigger
#define TRIGGER_SSID_ADDR 96  // Alamat untuk menyimpan SSID trigger (30 byte)
#define EEPROM_COUNTER_ADDR 126  // Pindahkan counter ke alamat baru
#define EEPROM_MAX_WRITES 100000  // Batas maksimum siklus tulis EEPROM

WiFiServer server(80);
String ssid, password;
String triggerMACs;  // Format: "AA:BB:CC:DD:EE:FF,11:22:33:44:55:66"
String triggerSSIDs;  // Format: "SSID1,SSID2,SSID3"
bool scanMode = true;  // Mode scan SSID
unsigned int eepromWriteCount = 0;

unsigned long lastScanTime = 0;
const unsigned long scanInterval = 1000;
// Fungsi untuk membaca counter penggunaan EEPROM
void readEepromCounter() {
  EEPROM.begin(EEPROM_SIZE);
  // Baca 2 byte counter (LSB dan MSB)
  eepromWriteCount = EEPROM.read(EEPROM_COUNTER_ADDR) + (EEPROM.read(EEPROM_COUNTER_ADDR + 1) << 8);
  EEPROM.end();
  
  Serial.print("EEPROM write count: ");
  Serial.println(eepromWriteCount);
}


void saveEepromCounter() {
	EEPROM.begin(EEPROM_SIZE);
	// Simpan 2 byte counter (LSB dan MSB)
	EEPROM.write(EEPROM_COUNTER_ADDR, eepromWriteCount & 0xFF);
	EEPROM.write(EEPROM_COUNTER_ADDR + 1, (eepromWriteCount >> 8) & 0xFF);
	EEPROM.commit();
	EEPROM.end();
}

// Fungsi untuk menambah counter penggunaan EEPROM
void incrementEepromCounter() {
  eepromWriteCount++;
  saveEepromCounter();
}

// Fungsi untuk mendapatkan ukuran EEPROM yang terpakai (dalam bytes)
int getUsedEepromSize() {
  int usedSize = 0;
  EEPROM.begin(EEPROM_SIZE);
  
  // Cek SSID (0-31)
  for (int i = 0; i < 32; i++) {
    if (EEPROM.read(i) != 0) {
      usedSize++;
    }
  }
  
  // Cek Password (32-93)
  for (int i = 32; i < 94; i++) {
    if (EEPROM.read(i) != 0) {
      usedSize++;
    }
  }
  
  // Counter selalu terpakai (94-95)
  usedSize += 2;
  
  EEPROM.end();
  return usedSize;
}

void saveTriggerSSIDs(String newTriggerSSIDs) {
    EEPROM.begin(EEPROM_SIZE);
    for (unsigned int i = TRIGGER_SSID_ADDR; i < TRIGGER_SSID_ADDR + 30; i++) {
        EEPROM.write(i, i < newTriggerSSIDs.length() + TRIGGER_SSID_ADDR ? 
                    newTriggerSSIDs[i - TRIGGER_SSID_ADDR] : 0);
    }
    EEPROM.commit();
    EEPROM.end();
    
    // Tambah counter penggunaan EEPROM
    incrementEepromCounter();
}


byte calculateChecksum(String data) {
    byte checksum = 0;
    for (unsigned int i = 0; i < data.length(); i++) {
        checksum ^= data[i];
    }
    return checksum;
}

void saveTriggerMACs(String newTriggerMACs) {
    EEPROM.begin(EEPROM_SIZE);
    
    // Bersihkan area EEPROM
    for (int i = TRIGGER_SSID_ADDR; i < TRIGGER_SSID_ADDR + 30; i++) {
        EEPROM.write(i, 0);
    }
    
    // Tulis data
    for (unsigned int i = 0; i < newTriggerMACs.length() && i < 29; i++) {
        EEPROM.write(TRIGGER_SSID_ADDR + i, newTriggerMACs[i]);
    }
    
    // Tulis checksum di byte terakhir
    byte checksum = calculateChecksum(newTriggerMACs);
    EEPROM.write(TRIGGER_SSID_ADDR + 29, checksum);
    
    EEPROM.commit();
    EEPROM.end();
    
    // Tambah counter penggunaan EEPROM
    incrementEepromCounter();
}



void scanWiFiForTrigger() {
if (triggerMACs.length() == 0) {
        Serial.println("No trigger MAC addresses set");
        return;
    }
    
    // Bersihkan MAC address dari karakter yang tidak diinginkan
    String cleanMACs = "";
    for (unsigned int i = 0; i < triggerMACs.length(); i++) {
        char c = triggerMACs[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') || c == ':' || c == ',') {
            cleanMACs += c;
        }
    }
    
    if (cleanMACs != triggerMACs) {
        Serial.println("Cleaned MAC addresses: " + cleanMACs);
        triggerMACs = cleanMACs;
        saveTriggerMACs(cleanMACs); // Simpan versi yang sudah dibersihkan
    }
    
    Serial.println("Scanning for trigger MAC addresses: " + triggerMACs);
    
    int networksFound = WiFi.scanNetworks();
    bool triggerFound = false;
    
    // Pisahkan string triggerMACs menjadi array
    unsigned int startIndex = 0;
    int commaIndex;
    
    // Periksa setiap MAC address dalam daftar trigger
    while (startIndex < triggerMACs.length()) {
        commaIndex = triggerMACs.indexOf(',', startIndex);
        if (commaIndex == -1) commaIndex = triggerMACs.length();
        
        String currentMAC = triggerMACs.substring(startIndex, commaIndex);
        currentMAC.trim();
        currentMAC.toUpperCase(); // MAC address biasanya uppercase
        
        // Periksa apakah MAC address ini ada dalam jaringan yang ditemukan
        for (int i = 0; i < networksFound; i++) {
            String bssidStr = WiFi.BSSIDstr(i);
            bssidStr.toUpperCase();
			Serial.println("info scan : " + bssidStr);
            
            if (bssidStr == currentMAC) {
                Serial.println("Trigger MAC found: " + currentMAC + " (" + WiFi.SSID(i) + ")! Turning relay ON");
                digitalWrite(RELAY, LOW); // Turn ON
                triggerFound = true;
                break;
            }
        }
        
        if (triggerFound) break;
        startIndex = commaIndex + 1;
    }
    
    if (!triggerFound) {
        Serial.println("No trigger MAC addresses found, relay remains OFF");
        digitalWrite(RELAY, HIGH); // Turn OFF
    }
    
    WiFi.scanDelete();
}

bool commitEEPROM() {
    bool success = false;
    int retries = 3;
    
    while (retries > 0 && !success) {
        success = EEPROM.commit();
        if (!success) {
            Serial.println("EEPROM commit failed, retrying...");
            delay(100);
            retries--;
        }
    }
    
    return success;
}

// Fungsi untuk membaca SSID & Password dari EEPROM
void readWiFiConfig() {
    EEPROM.begin(EEPROM_SIZE);
    ssid = "";
    password = "";
    triggerMACs = "";

	// Baca Trigger MAC addresses
	String tempMACs = "";
	for (int i = TRIGGER_SSID_ADDR; i < TRIGGER_SSID_ADDR + 29 && EEPROM.read(i) != 0; i++) {
		tempMACs += char(EEPROM.read(i));
	}
	
	// Baca checksum
	byte storedChecksum = EEPROM.read(TRIGGER_SSID_ADDR + 29);
	byte calculatedChecksum = calculateChecksum(tempMACs);
	
	if (storedChecksum == calculatedChecksum) {
		triggerMACs = tempMACs;
	} else {
		Serial.println("MAC address checksum failed, data might be corrupted");
		triggerMACs = "";
	}
    
    // Baca SSID dan password seperti biasa
    for (int i = 0; i < 32 && EEPROM.read(i) != 0; i++) {
        ssid += char(EEPROM.read(i));
    }
    
    for (int i = 32; i < 94 && EEPROM.read(i) != 0; i++) {
        password += char(EEPROM.read(i));
    }
    
    // Baca Trigger MAC addresses
    for (int i = TRIGGER_SSID_ADDR; i < TRIGGER_SSID_ADDR + 30 && EEPROM.read(i) != 0; i++) {
        triggerMACs += char(EEPROM.read(i));
    }
    
    ssid.trim();
    password.trim();
    triggerMACs.trim();
    
    // Validasi MAC address format
    if (triggerMACs.length() > 0) {
        // Hapus karakter null jika ada
        int nullPos = triggerMACs.indexOf('\0');
        if (nullPos != -1) {
            triggerMACs = triggerMACs.substring(0, nullPos);
        }
        
        // Validasi format MAC address (hanya contoh sederhana)
        if (triggerMACs.indexOf(':') == -1) {
            Serial.println("Invalid MAC address format, resetting");
            triggerMACs = "";
        }
    }
    
    EEPROM.end();
    
    Serial.println("Read WiFi config:");
    Serial.println("SSID: " + ssid);
    Serial.println("Password length: " + String(password.length()));
    Serial.println("Trigger MACs: [" + triggerMACs + "]");
}



// Fungsi untuk menyimpan SSID & Password ke EEPROM
void saveWiFiConfig(String newSSID, String newPass) {
    EEPROM.begin(EEPROM_SIZE);
    for (unsigned int i = 0; i < 32; i++) {
        EEPROM.write(i, i < newSSID.length() ? newSSID[i] : 0);
    }
    for (unsigned int i = 32; i < 96; i++) {
        EEPROM.write(i, i < newPass.length() + 32 ? newPass[i - 32] : 0);
    }
    EEPROM.commit();
    EEPROM.end();
}

// Fungsi untuk menghapus konfigurasi WiFi dari EEPROM
void clearWiFiConfig() {
    EEPROM.begin(EEPROM_SIZE);
    
    // Hapus semua data dengan menulis 0
    for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
    }
    
    // Pastikan commit berhasil
    bool success = EEPROM.commit();
    EEPROM.end();
    
    if (success) {
        Serial.println("WiFi configuration cleared successfully!");
    } else {
        Serial.println("Failed to clear WiFi configuration!");
    }
    
    // Reset variabel global
    ssid = "";
    password = "";
    triggerMACs = "";
}


// Fungsi untuk reset counter EEPROM
void resetEepromCounter() {
  eepromWriteCount = 0;
  saveEepromCounter();
  Serial.println("EEPROM counter reset to 0");
}

// Fungsi untuk memulai Access Point jika WiFi gagal terhubung
void startAccessPoint() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP-Config", "12345678");  // Hotspot dengan password
    Serial.println("AP Mode: Connect to ESP-Config (password: 12345678)");
}

// Fungsi untuk menyambungkan ke WiFi
void connectToWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    
    Serial.print("Connecting to WiFi...");
    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 20) {  // Tunggu 10 detik max
        delay(500);
        Serial.print(".");
        attempt++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nFailed to connect! Starting AP Mode...");
        startAccessPoint();
    }
}

// Fungsi untuk scan jaringan WiFi yang tersedia
String scanWiFiNetworks() {
    Serial.println("Scanning WiFi networks...");
    
    int networksFound = WiFi.scanNetworks();
    String wifiList = "";
    
    if (networksFound == 0) {
        wifiList = "No WiFi networks found";
    } else {
        wifiList = "<select name='ssid' id='ssid'>";
        for (int i = 0; i < networksFound; i++) {
            // Tambahkan setiap SSID ke dropdown list
            wifiList += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (";
            wifiList += WiFi.RSSI(i);
            wifiList += "dBm) ";
            wifiList += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "Open" : "Secured";
            wifiList += "</option>";
        }
        wifiList += "</select>";
    }
    
    return wifiList;
}

// Fungsi untuk menampilkan halaman konfigurasi WiFi
void sendWiFiConfigPage(WiFiClient client) {
    String wifiList = scanWiFiNetworks();
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("");
    client.println("<!DOCTYPE HTML><html>");
    client.println("<head><title>ESP8266 WiFi Setup</title>");
    client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
    client.println("<style>");
    client.println("body { font-family: Arial; margin: 20px; }");
    client.println("select, input { width: 100%; padding: 8px; margin: 8px 0; }");
    client.println("button { background-color: #4CAF50; color: white; padding: 10px; border: none; cursor: pointer; width: 100%; margin-bottom: 10px; }");
    client.println(".danger { background-color: #f44336; }");
    client.println("</style></head><body>");
    client.println("<h2>WiFi Configuration</h2>");
    client.println("<div class='tab'>");
    client.println("<button class='tablinks active' onclick='openTab(event, \"connect\")'>Connect Mode</button>");
    client.println("<button class='tablinks' onclick='openTab(event, \"scan\")'>Scan Mode</button>");
    client.println("</div>");

    // Tab koneksi WiFi
    client.println("<div id='connect' class='tabcontent' style='display:block;'>");
    client.println("<h3>Connect to WiFi</h3>");
    client.println("<form action='/wifi' method='get'>");
    client.println("<p>Select WiFi Network:</p>");
    client.println(wifiList);
    client.println("<p>Password:</p>");
    client.println("<input type='password' name='pass' placeholder='WiFi Password'>");
    client.println("<br><br>");
    client.println("<button type='submit'>Connect</button>");
    client.println("</form>");
    client.println("</div>");

	// Tab scan mode
	client.println("<div id='scan' class='tabcontent'>");
	client.println("<h3>SSID Trigger Mode</h3>");
	client.println("<p>Select WiFi networks that will trigger the relay to turn ON when detected:</p>");
	client.println("<form action='/trigger' method='get'>");
	client.println("<select name='triggermacs' id='triggermacs' multiple size='5'>");

	// Dapatkan daftar jaringan WiFi yang tersedia
	int networksFound = WiFi.scanNetworks();

	// Pisahkan string triggerMACs menjadi array untuk pengecekan
	String currentTriggers[10]; // Maksimal 10 MAC trigger
	int triggerCount = 0;

	unsigned int startIndex = 0;
	int commaIndex;
	while (startIndex < triggerMACs.length() && triggerCount < 10) {
		commaIndex = triggerMACs.indexOf(',', startIndex);
		if (commaIndex == -1) commaIndex = triggerMACs.length();
		
		currentTriggers[triggerCount] = triggerMACs.substring(startIndex, commaIndex);
		currentTriggers[triggerCount].trim();
		currentTriggers[triggerCount].toUpperCase();
		triggerCount++;
		
		startIndex = commaIndex + 1;
	}

	// Tambahkan opsi untuk jaringan WiFi yang tersedia (tampilkan SSID, simpan MAC)
	for (int i = 0; i < networksFound; i++) {
		String macAddress = WiFi.BSSIDstr(i);
		String selected = "";
		
		// Periksa apakah MAC address ini ada dalam daftar trigger
		for (int j = 0; j < triggerCount; j++) {
			if (macAddress.equalsIgnoreCase(currentTriggers[j])) {
				selected = "selected";
				break;
			}
		}
		
		// Tampilkan SSID tapi value-nya adalah MAC address
		client.println("<option value='" + macAddress + "' " + selected + ">" + 
					WiFi.SSID(i) + " (" + WiFi.RSSI(i) + "dBm)</option>");
	}
	client.println("</select>");
	client.println("<p><small>Hold Ctrl (or Cmd on Mac) to select multiple networks</small></p>");
	client.println("<br>");
	client.println("<button type='submit' class='info'>Set Trigger Networks</button>");
	client.println("</form>");

	// Tampilkan daftar jaringan trigger yang aktif
	client.println("<p>Current trigger networks:</p>");
	client.println("<ul>");
	if (triggerMACs.length() > 0) {
		for (int i = 0; i < triggerCount; i++) {
			// Cari SSID yang sesuai dengan MAC address ini (jika masih terdeteksi)
			String ssidName = "Unknown (MAC: " + currentTriggers[i] + ")";
			for (int j = 0; j < networksFound; j++) {
				if (WiFi.BSSIDstr(j).equalsIgnoreCase(currentTriggers[i])) {
					ssidName = WiFi.SSID(j);
					break;
				}
			}
			client.println("<li><b>" + ssidName + "</b></li>");
		}
	} else {
		client.println("<li>None</li>");
	}
	client.println("</ul>");
	client.println("<p>When any of these networks is detected during scanning, the relay will turn ON automatically.</p>");
	client.println("</div>");
    client.println("<form action='/clearwifi' method='get'>");
    client.println("<button type='submit' class='danger'>Reset WiFi Settings</button>");
    client.println("</form>");
    client.println("<p><a href='/'>Back to Control Panel</a></p>");
    client.println("</body></html>");
}

// Fungsi untuk menangani konfigurasi WiFi via Web
void processWiFiSettings(String request) {
    if (request.indexOf("ssid=") != -1 && request.indexOf("pass=") != -1) {
        int ssidStart = request.indexOf("ssid=") + 5;
        int ssidEnd = request.indexOf("&", ssidStart);
        int passStart = request.indexOf("pass=") + 5;
        int passEnd = request.indexOf(" ", passStart);
        
        String newSSID = request.substring(ssidStart, ssidEnd);
        String newPass = request.substring(passStart, passEnd);
        
        // URL decode the SSID and password
        newSSID.replace("%20", " "); // Replace space
        newPass.replace("%20", " ");
        
        Serial.println("New WiFi Credentials Received!");
        Serial.println("SSID: " + newSSID);
        Serial.println("PASS: " + newPass);
        
        saveWiFiConfig(newSSID, newPass);
        ESP.restart();
    }
}

void safeRestart() {
    Serial.println("Preparing for safe restart...");
    
    // Tutup semua koneksi WiFi
    WiFi.disconnect(true);
    
    // Tunggu sebentar
    delay(1000);
    
    // Restart ESP
    ESP.restart();
}


// Fungsi untuk menangani relay
void handleRelayRequest(WiFiClient client, String request) {
    if (request.indexOf("/RELAY=ON") != -1) {
        Serial.println("RELAY=ON");
        digitalWrite(RELAY, LOW);
    }
    if (request.indexOf("/RELAY=OFF") != -1) {
        Serial.println("RELAY=OFF");
        digitalWrite(RELAY, HIGH);
    }
    if (request.indexOf("/manualreset") != -1) {
        Serial.println("Manual Reset Requested");
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println("");
        client.println("<!DOCTYPE HTML><html>");
        client.println("<head><meta http-equiv='refresh' content='5;url=/' /></head>");
        client.println("<body style='font-family: Arial; text-align: center; margin: 20px;'>");
        client.println("<h2>Manual Reset in Progress</h2>");
        client.println("<p>Device is resetting to factory settings...</p>");
        client.println("<p>You will be redirected to the home page in 5 seconds.</p>");
        client.println("<div style='background-color: #f2f2f2; padding: 20px; border-radius: 8px; margin: 20px auto; max-width: 400px;'>");
        client.println("<p>Please reconnect to the ESP-Config network after reset.</p>");
        client.println("<p><b>SSID:</b> ESP-Config</p>");
        client.println("<p><b>Password:</b> 12345678</p>");
        client.println("</div>");
        client.println("</body></html>");
        
        // Tunggu sampai client menerima respons
        delay(2000);
        
        // Tutup koneksi client
        client.stop();
        
        // Reset konfigurasi
        clearWiFiConfig();
        
        // Restart dengan aman
        safeRestart();
        return;
    }

    // Kirim respons ke browser
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("");
    client.println("<!DOCTYPE HTML><html>");
    client.println("<head><title>ESP8266 RELAY Control</title>");
    client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
    client.println("<style>");
    client.println("body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 20px; background-color: #f5f5f5; color: #333; }");
    client.println(".container { max-width: 600px; margin: 0 auto; background-color: white; border-radius: 10px; padding: 20px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }");
    client.println("h2 { color: #2c3e50; margin-bottom: 20px; }");
    client.println(".button { display: inline-block; background-color: #4CAF50; color: white; padding: 12px 30px; ");
    client.println("text-decoration: none; font-size: 16px; margin: 10px 5px; cursor: pointer; border-radius: 8px; border: none; transition: all 0.3s; }");
    client.println(".button:hover { transform: translateY(-2px); box-shadow: 0 4px 8px rgba(0,0,0,0.1); }");
    client.println(".off { background-color: #f44336; }");
    client.println(".settings { background-color: #2196F3; }");
    client.println(".warning { background-color: #ff9800; }");
    client.println(".info { background-color: #f8f9fa; padding: 15px; border-radius: 8px; margin-bottom: 20px; border-left: 4px solid #2196F3; text-align: left; }");
    client.println(".eeprom-meter { height: 20px; background-color: #ddd; border-radius: 10px; margin: 10px 0; width: 100%; max-width: 300px; display: inline-block; }");
    client.println(".eeprom-value { height: 20px; background-color: #4CAF50; border-radius: 10px; text-align: center; line-height: 20px; color: white; }");
    client.println(".eeprom-warning { background-color: #ff9800; }");
    client.println(".eeprom-danger { background-color: #f44336; }");
    client.println(".status { font-size: 18px; font-weight: bold; margin: 15px 0; }");
    client.println(".footer { margin-top: 30px; font-size: 12px; color: #777; }");
    client.println(".button-group { margin: 15px 0; }");
    client.println("</style></head>");
    client.println("<body>");
    client.println("<div class='container'>");
    client.println("<h2>ESP8266 Control Panel</h2>");
    
    // Informasi koneksi WiFi
    client.println("<div class='info'>");
    if (WiFi.status() == WL_CONNECTED) {
        client.println("<p><b>Connected to:</b> " + WiFi.SSID() + "</p>");
        client.println("<p><b>IP Address:</b> " + WiFi.localIP().toString() + "</p>");
        client.println("<p><b>Signal Strength:</b> " + String(WiFi.RSSI()) + " dBm</p>");
    } else {
        client.println("<p><b>Mode:</b> Access Point</p>");
        client.println("<p><b>SSID:</b> ESP-Config</p>");
        client.println("<p><b>Password:</b> 12345678</p>");
        client.println("<p><b>IP Address:</b> 192.168.4.1</p>");
    }
    
    // EEPROM Usage Information
    int usedEepromSize = getUsedEepromSize();
    float eepromPercentage = (float)eepromWriteCount / EEPROM_MAX_WRITES * 100;
    float spacePercentage = (float)usedEepromSize / EEPROM_SIZE * 100;
    
    client.println("<p><b>EEPROM Health:</b></p>");
    client.println("<div class='eeprom-meter'>");
    String eepromClass = "eeprom-value";
    if (eepromPercentage > 80) eepromClass += " eeprom-danger";
    else if (eepromPercentage > 50) eepromClass += " eeprom-warning";
    client.println("<div class='" + eepromClass + "' style='width: " + String(eepromPercentage) + "%;'>" + 
                  String(eepromPercentage, 1) + "%</div>");
    client.println("</div>");
    client.println("<p>Write Cycles: " + String(eepromWriteCount) + " / " + String(EEPROM_MAX_WRITES) + "</p>");
    
    client.println("<div class='eeprom-meter'>");
    String spaceClass = "eeprom-value";
    if (spacePercentage > 80) spaceClass += " eeprom-danger";
    else if (spacePercentage > 50) spaceClass += " eeprom-warning";
    client.println("<div class='" + spaceClass + "' style='width: " + String(spacePercentage) + "%;'>" + 
                  String(spacePercentage, 1) + "%</div>");
    client.println("</div>");
    client.println("<p>Storage: " + String(usedEepromSize) + " / " + String(EEPROM_SIZE) + " bytes</p>");
    client.println("</div>");
    
    client.println("<div class='status'>");
    client.println("Relay is now: <span style='color: " + String(digitalRead(RELAY) == HIGH ? "#f44336" : "#4CAF50") + ";'>");
    client.println(digitalRead(RELAY) == HIGH ? "OFF" : "ON");
    client.println("</span>");
    client.println("</div>");
    
    client.println("<div class='button-group'>");
    client.println("<a href=\"/RELAY=ON\" class=\"button\">Turn ON</a>");
    client.println("<a href=\"/RELAY=OFF\" class=\"button off\">Turn OFF</a>");
    client.println("</div>");
    
    client.println("<div class='button-group'>");
    client.println("<a href=\"/wifisetup\" class=\"button settings\">WiFi Settings</a>");
    
    if (scanMode) {
        client.println("<a href=\"/scanmode=OFF\" class=\"button off\">Disable Scan Mode</a>");
    } else {
        client.println("<a href=\"/scanmode=ON\" class=\"button settings\">Enable Scan Mode</a>");
    }
    client.println("</div>");
    
    // Tambahkan tombol manual reset
    client.println("<div class='button-group'>");
    client.println("<a href=\"/manualreset\" class=\"button warning\" onclick=\"return confirm('Are you sure you want to reset all settings? This cannot be undone.');\">Manual Reset</a>");
    client.println("</div>");
    
    client.println("<div class='footer'>");
    client.println("ESP8266 WiFi Relay Controller v1.0");
    client.println("</div>");
    
    client.println("</div>"); // container
    
    // Tambahkan script JavaScript untuk konfirmasi
    client.println("<script>");
    client.println("function confirmReset() {");
    client.println("  return confirm('Are you sure you want to reset all settings? This cannot be undone.');");
    client.println("}");
    client.println("</script>");
    
    client.println("</body></html>");
}

void setup() {
    Serial.begin(9600);
	lastScanTime = millis();
    pinMode(RELAY, OUTPUT);
    digitalWrite(RELAY, LOW);
    
    // Baca counter EEPROM
    readEepromCounter();

    
    readWiFiConfig();
    if (ssid.length() > 0) {
        connectToWiFi();
    } else {
        startAccessPoint();
    }
    
    server.begin();
    Serial.println("Server started!");
}

void loop() {
	unsigned long currentMillis = millis();

	if ((currentMillis < lastScanTime) || (currentMillis - lastScanTime > scanInterval)) {
        lastScanTime = currentMillis;
        if (scanMode) {
            Serial.println("Performing scheduled WiFi scan...");
            scanWiFiForTrigger();
        }
    }


    WiFiClient client = server.accept();
    if (!client) return;

    Serial.println("New client connected!");
    while (!client.available()) {
        delay(1);
		Serial.println("Waiting for client...");
    }

    String request = client.readStringUntil('\r');
    Serial.println(request);
    client.flush();

    if (request.indexOf("/wifi?") != -1) {
        processWiFiSettings(request);
    } else if (request.indexOf("/wifisetup") != -1) {
        sendWiFiConfigPage(client);
    } else if (request.indexOf("/clearwifi") != -1) {
		clearWiFiConfig();
		
		client.println("HTTP/1.1 200 OK");
		client.println("Content-Type: text/html");
		client.println("");
		client.println("<!DOCTYPE HTML><html>");
		client.println("<head>");
		client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
		client.println("<meta http-equiv='refresh' content='10;url=/' />");
		client.println("<style>");
		client.println("body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 20px; background-color: #f5f5f5; color: #333; }");
		client.println(".container { max-width: 600px; margin: 0 auto; background-color: white; border-radius: 10px; padding: 20px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }");
		client.println("h2 { color: #2c3e50; margin-bottom: 20px; }");
		client.println(".info-box { background-color: #f8f9fa; padding: 15px; border-radius: 8px; margin: 20px 0; border-left: 4px solid #2196F3; text-align: left; }");
		client.println(".countdown { font-size: 24px; font-weight: bold; margin: 20px 0; color: #f44336; }");
		client.println(".button { display: inline-block; background-color: #2196F3; color: white; padding: 12px 30px; ");
		client.println("text-decoration: none; font-size: 16px; margin: 10px 5px; cursor: pointer; border-radius: 8px; border: none; transition: all 0.3s; }");
		client.println(".button:hover { transform: translateY(-2px); box-shadow: 0 4px 8px rgba(0,0,0,0.1); }");
		client.println("</style>");
		client.println("</head>");
		client.println("<body>");
		client.println("<div class='container'>");
		client.println("<h2>WiFi Settings Cleared!</h2>");
		
		client.println("<div class='info-box'>");
		client.println("<p><b>Status:</b> Device will restart in Access Point mode.</p>");
		client.println("<p><b>Next Steps:</b></p>");
		client.println("<ul>");
		client.println("<li>Wait for the device to restart</li>");
		client.println("<li>Connect to the <b>ESP-Config</b> network</li>");
		client.println("<li>Password: <b>12345678</b></li>");
		client.println("<li>Navigate to <b>192.168.4.1</b> in your browser</li>");
		client.println("</ul>");
		client.println("</div>");
		
		client.println("<div class='countdown'>");
		client.println("Restarting in <span id='seconds'>10</span> seconds...");
		client.println("</div>");
		
		client.println("<a href='/' class='button'>Back to Home</a>");
		
		// JavaScript untuk countdown
		client.println("<script>");
		client.println("var seconds = 10;");
		client.println("var countdown = setInterval(function() {");
		client.println("  seconds--;");
		client.println("  document.getElementById('seconds').textContent = seconds;");
		client.println("  if (seconds <= 0) clearInterval(countdown);");
		client.println("}, 1000);");
		client.println("</script>");
		
		client.println("</div>"); // container
		client.println("</body></html>");
		
		// Tunggu sampai client menerima respons
		delay(2000);
		
		// Tutup koneksi client sebelum restart
		client.stop();
		
		// Tunggu sebentar lagi untuk memastikan semua operasi selesai
		delay(1000);
		
		// Restart ESP
		safeRestart();
	}else if (request.indexOf("/reseteeprom") != -1) {
        resetEepromCounter();
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println("");
        client.println("<!DOCTYPE HTML><html>");
        client.println("<head><meta http-equiv='refresh' content='5;url=/wifisetup' /></head>");
        client.println("<body><h2>EEPROM counter reset!</h2>");
        client.println("<p>The EEPROM write cycle counter has been reset to 0.</p>");
        client.println("<p>Redirecting in 5 seconds...</p></body></html>");
    } else if (request.indexOf("/trigger?triggermacs=") != -1) {
		String requestStr = request;
		String newTriggerMACs = "";
		
		// Ekstrak semua parameter triggermacs
		int startPos = 0;
		while (true) {
			int triggerStart = requestStr.indexOf("triggermacs=", startPos);
			if (triggerStart == -1) break;
			
			triggerStart += 12; // Panjang "triggermacs="
			int triggerEnd = requestStr.indexOf("&", triggerStart);
			if (triggerEnd == -1) triggerEnd = requestStr.indexOf(" ", triggerStart);
			
			String macValue = requestStr.substring(triggerStart, triggerEnd);
			macValue.replace("%3A", ":"); // Replace URL-encoded colon
			
			if (newTriggerMACs.length() > 0) {
				newTriggerMACs += ",";
			}
			newTriggerMACs += macValue;
			
			startPos = triggerEnd;
		}
		
		Serial.println("New Trigger MAC addresses: " + newTriggerMACs);
		saveTriggerMACs(newTriggerMACs);
		triggerMACs = newTriggerMACs;
		
		client.println("HTTP/1.1 200 OK");
		client.println("Content-Type: text/html");
		client.println("");
		client.println("<!DOCTYPE HTML><html>");
		client.println("<head><meta http-equiv='refresh' content='3;url=/wifisetup' /></head>");
		client.println("<body><h2>Trigger Networks Updated!</h2>");
		client.println("<p>New trigger networks have been set.</p>");
		client.println("<p>Redirecting in 3 seconds...</p></body></html>");
	}else if (request.indexOf("/scanmode=ON") != -1) {
		scanMode = true;
		Serial.println("Scan mode activated");
		handleRelayRequest(client, request);
	} else if (request.indexOf("/scanmode=OFF") != -1) {
		scanMode = false;
		Serial.println("Scan mode deactivated");
		handleRelayRequest(client, request);
	} else {
        handleRelayRequest(client, request);
    }

    Serial.println("Client disconnected.");
}