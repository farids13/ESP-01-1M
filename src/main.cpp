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

// Fungsi untuk menyimpan counter penggunaan EEPROM
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
    for (int i = TRIGGER_SSID_ADDR; i < TRIGGER_SSID_ADDR + 30; i++) {
        EEPROM.write(i, i < newTriggerSSIDs.length() + TRIGGER_SSID_ADDR ? 
                    newTriggerSSIDs[i - TRIGGER_SSID_ADDR] : 0);
    }
    EEPROM.commit();
    EEPROM.end();
    
    // Tambah counter penggunaan EEPROM
    incrementEepromCounter();
}

void saveTriggerMACs(String newTriggerMACs) {
    EEPROM.begin(EEPROM_SIZE);
    for (int i = TRIGGER_SSID_ADDR; i < TRIGGER_SSID_ADDR + 30; i++) {
        EEPROM.write(i, i < newTriggerMACs.length() + TRIGGER_SSID_ADDR ? 
                    newTriggerMACs[i - TRIGGER_SSID_ADDR] : 0);
    }
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
    
    Serial.println("Scanning for trigger MAC addresses: " + triggerMACs);
    
    int networksFound = WiFi.scanNetworks();
    bool triggerFound = false;
    
    // Pisahkan string triggerMACs menjadi array
    int startIndex = 0;
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


// Fungsi untuk membaca SSID & Password dari EEPROM
void readWiFiConfig() {
    EEPROM.begin(EEPROM_SIZE);
    ssid = "";
    password = "";
    triggerMACs = "";
    
    // Baca SSID dan password seperti biasa
    for (int i = 0; i < 32; i++) {
        ssid += char(EEPROM.read(i));
    }
    for (int i = 32; i < 94; i++) {
        password += char(EEPROM.read(i));
    }
    
    // Baca Trigger MAC addresses
    for (int i = TRIGGER_SSID_ADDR; i < TRIGGER_SSID_ADDR + 30; i++) {
        triggerMACs += char(EEPROM.read(i));
    }
    
    ssid.trim();
    password.trim();
    triggerMACs.trim();
    
    EEPROM.end();
}


// Fungsi untuk menyimpan SSID & Password ke EEPROM
void saveWiFiConfig(String newSSID, String newPass) {
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < 32; i++) {
        EEPROM.write(i, i < newSSID.length() ? newSSID[i] : 0);
    }
    for (int i = 32; i < 96; i++) {
        EEPROM.write(i, i < newPass.length() + 32 ? newPass[i - 32] : 0);
    }
    EEPROM.commit();
    EEPROM.end();
}

// Fungsi untuk menghapus konfigurasi WiFi dari EEPROM
void clearWiFiConfig() {
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
    EEPROM.end();
    Serial.println("WiFi configuration cleared!");
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
	client.println("<h3>MAC Address Trigger Mode</h3>");
	client.println("<p>Select MAC addresses that will trigger the relay to turn ON when detected:</p>");
	client.println("<form action='/trigger' method='get'>");
	client.println("<select name='triggermacs' id='triggermacs' multiple size='5'>");

	// Dapatkan daftar jaringan WiFi yang tersedia
	int networksFound = WiFi.scanNetworks();

	// Pisahkan string triggerMACs menjadi array untuk pengecekan
	String currentTriggers[10]; // Maksimal 10 MAC trigger
	int triggerCount = 0;

	int startIndex = 0;
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

	// Tambahkan opsi untuk MAC address yang tersedia
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
		
		client.println("<option value='" + macAddress + "' " + selected + ">" + 
					macAddress + " - " + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + "dBm)</option>");
	}
	client.println("</select>");
	client.println("<p><small>Hold Ctrl (or Cmd on Mac) to select multiple MAC addresses</small></p>");
	client.println("<br>");
	client.println("<button type='submit' class='info'>Set Trigger MAC Addresses</button>");
	client.println("</form>");

	// Tampilkan daftar MAC address trigger yang aktif
	client.println("<p>Current trigger MAC addresses:</p>");
	client.println("<ul>");
	if (triggerMACs.length() > 0) {
		for (int i = 0; i < triggerCount; i++) {
			client.println("<li><b>" + currentTriggers[i] + "</b></li>");
		}
	} else {
		client.println("<li>None</li>");
	}
	client.println("</ul>");
	client.println("<p>When any of these MAC addresses is detected during scanning, the relay will turn ON automatically.</p>");
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

    // Kirim respons ke browser
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("");
    client.println("<!DOCTYPE HTML><html>");
    client.println("<head><title>ESP8266 RELAY Control</title>");
    client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
    client.println("<style>");
    client.println("body { font-family: Arial; text-align: center; margin: 20px; }");
    client.println(".button { display: inline-block; background-color: #4CAF50; color: white; padding: 15px 32px; ");
    client.println("text-decoration: none; font-size: 16px; margin: 10px; cursor: pointer; border-radius: 8px; }");
    client.println(".off { background-color: #f44336; }");
    client.println(".settings { background-color: #2196F3; }");
    client.println(".info { background-color: #f2f2f2; padding: 10px; border-radius: 8px; margin-bottom: 20px; }");
    client.println(".eeprom-meter { height: 20px; background-color: #ddd; border-radius: 10px; margin: 10px 0; width: 100%; max-width: 300px; display: inline-block; }");
    client.println(".eeprom-value { height: 20px; background-color: #4CAF50; border-radius: 10px; text-align: center; line-height: 20px; color: white; }");
    client.println(".eeprom-warning { background-color: #ff9800; }");
    client.println(".eeprom-danger { background-color: #f44336; }");
    client.println("</style></head>");
    client.println("<body>");
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
    
    client.print("<p>Relay is now: <b>");
    client.print(digitalRead(RELAY) == HIGH ? "OFF" : "ON");
    client.println("</b></p>");
    client.println("<a href=\"/RELAY=ON\" class=\"button\">Turn ON</a>");
    client.println("<a href=\"/RELAY=OFF\" class=\"button off\">Turn OFF</a>");
    client.println("<br><br>");
    client.println("<a href=\"/wifisetup\" class=\"button settings\">WiFi Settings</a>");

	client.println("<br><br>");
	client.println("<a href=\"/scanmode=" + String(scanMode ? "OFF" : "ON") + "\" class=\"button " + 
              (scanMode ? "off" : "settings") + "\">Scan Mode: " + 
              (scanMode ? "ON" : "OFF") + "</a>");
    client.println("</body></html>");
}

void setup() {
    Serial.begin(9600);
    pinMode(RELAY, OUTPUT);
    digitalWrite(RELAY, LOW);
    
    // Baca counter EEPROM
    readEepromCounter();


	lastScanTime = millis();
    
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


    WiFiClient client = server.available();
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
        client.println("<head><meta http-equiv='refresh' content='5;url=/' /></head>");
        client.println("<body><h2>WiFi settings cleared!</h2>");
        client.println("<p>Device will restart in Access Point mode...</p>");
        client.println("<p>Redirecting in 5 seconds...</p></body></html>");
        delay(1000);
        ESP.restart();
    } else if (request.indexOf("/reseteeprom") != -1) {
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
		client.println("<body><h2>Trigger MAC Addresses Updated!</h2>");
		client.println("<p>New trigger MAC addresses: " + newTriggerMACs + "</p>");
		client.println("<p>Redirecting in 3 seconds...</p></body></html>");
	} else if (request.indexOf("/scanmode=ON") != -1) {
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