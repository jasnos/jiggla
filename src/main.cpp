// ESP32-S2 Mouse Jiggler
// Features:
// - USB Mouse HID emulation
// - WiFi Access Point with configurable settings
// - Web server for configuration
// - Configurable movement pattern and intervals

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <USB.h>
#include <USBHID.h>
#include <USBHIDMouse.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <FS.h>
#include <math.h>
#include <Update.h>

// Include credentials (not tracked by git)
#include "../credentials.h"

// Default configuration - will be overridden by settings.json if it exists
const char* default_ssid = DEFAULT_AP_SSID;
const char* default_password = DEFAULT_AP_PASSWORD;
const char* default_hostname = DEFAULT_HOSTNAME;
const char* default_username = DEFAULT_WEB_USERNAME;
const char* default_auth_password = DEFAULT_WEB_PASSWORD;
const int default_webport = DEFAULT_WEB_PORT; // Keep as const for default value

// Current settings that can be modified at runtime
char* current_ssid = strdup(default_ssid);
char* current_password = strdup(default_password);
char* current_hostname = strdup(default_hostname);
char* current_username = strdup(default_username);
char* current_auth_password = strdup(default_auth_password);
int current_webport = default_webport;
const IPAddress default_ip(192, 168, 4, 1);

// Preferred WiFi network
const char* preferred_ssid = WIFI_SSID;
const char* preferred_password = WIFI_PASSWORD;
const int wifi_connect_timeout = 10000; // 10 seconds timeout for WiFi connection

// STA mode customizable credentials (loaded from settings)
char* sta_ssid = strdup(preferred_ssid);
char* sta_password = strdup(preferred_password);

// Default mouse movement settings
int move_interval = 4 * 60 * 1000; // 4 minutes in milliseconds
int movement_x = 5;
int movement_y = 5;
int movement_speed = 10; // Default movement speed in milliseconds
bool jiggler_enabled = true; // Default state is enabled
bool circular_movement = false; // Default to linear movement
bool random_delay = false; // Randomize delay between movements
bool movement_trail = false; // Create a movement trail
const int CIRCLE_STEPS = 36; // Number of steps to complete a circle

// File paths
const char* config_file = "/config.json";
const char* settings_file = "/settings.json";

// USB Mouse
USBHIDMouse Mouse;

// Web server
AsyncWebServer* server;

// Timestamp for last movement
unsigned long last_move_time = 0;
unsigned long next_move_time = 0; // Next scheduled movement

// Session management
const int MAX_SESSIONS = 10;
struct Session {
  String id;
  unsigned long expiry;
  bool active;
};
Session sessions[MAX_SESSIONS];
unsigned long session_timeout = 30 * 60 * 1000; // 30 minutes in milliseconds

// WiFi mode
bool isAPMode = false;

// Add a new variable for hidden AP option
bool ap_hidden = false;

// WiFi mode settings
char* wifi_mode = strdup("ap"); // "ap" or "apsta"
char* ap_availability = strdup("always"); // "always" or "timeout"
int ap_timeout = 5; // minutes
unsigned long ap_start_time = 0; // When AP was started
bool ap_active = true; // Is AP currently active

// Function prototypes
void loadConfig();
void saveConfig();
void loadSettings();
void saveSettings();
void setupWiFi();
void setupAccessPoint();
void setupWebServer();
String generateSessionId();
bool validateSession(AsyncWebServerRequest *request);
void initSPIFFS();
void cleanupExpiredSessions();
void moveMouseLinear();
void moveMouseCircular();
unsigned long calculateMoveInterval();
void moveMouse();
void saveSessions();
void loadSessions();
void cleanupMemory();

void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(500);
  Serial.println("\nStarting jiggla");

  // Initialize USB with custom VID/PID to appear as a branded mouse
  // Logitech VID: 0x046d, Mouse PID: 0xc077 (M105 Optical Mouse)
  USB.VID(0x046d);
  USB.PID(0xc077);
  USB.manufacturerName("Logitech");
  USB.productName("M105 Optical Mouse");
  USB.begin();
  
  // Initialize file system
  initSPIFFS();
  
  // Load settings (auth, AP details)
  loadSettings();
  
  // Load configuration (mouse movement settings)
  loadConfig();
  
  // Initialize sessions
  for (int i = 0; i < MAX_SESSIONS; i++) {
    sessions[i].active = false;
  }
  
  // Load saved sessions
  loadSessions();
  
  // Setup WiFi (STA mode with fallback to AP mode)
  setupWiFi();
  
  // Initialize web server with current port
  server = new AsyncWebServer(current_webport);
  
  // Setup web server
  setupWebServer();
  
  // Initialize mouse
  Mouse.begin();
  delay(1000);
  
  // Setup RNG for session IDs
  randomSeed(micros());
  
  Serial.println("jiggla ready");
}

void loop() {
  // Check if it's time to move the mouse and if jiggler is enabled
  if (jiggler_enabled && millis() - last_move_time >= calculateMoveInterval()) {
    Serial.println("Moving mouse");
    
    // Perform the movement
    moveMouse();
    
    // Update last move time
    last_move_time = millis();
    
    // Calculate and store next scheduled movement time
    next_move_time = millis() + calculateMoveInterval();
  }
  
  // Handle AP timeout if configured
  if (strcmp(ap_availability, "timeout") == 0 && ap_active) {
    unsigned long ap_elapsed_minutes = (millis() - ap_start_time) / 60000;
    
    if (ap_elapsed_minutes >= ap_timeout) {
      Serial.println("AP timeout reached, turning off WiFi");
      
      // Turn off WiFi
      WiFi.mode(WIFI_OFF);
      ap_active = false;
      
      Serial.println("WiFi turned off, device will continue as USB mouse jiggler only");
    }
  }
  
  // Cleanup expired sessions periodically
  static unsigned long last_cleanup = 0;
  if (millis() - last_cleanup >= 60000) { // Check every minute
    cleanupExpiredSessions();
    last_cleanup = millis();
  }
}

void moveMouseLinear() {
  // Move to point B
  Mouse.move(movement_x, movement_y);
  delay(movement_speed);
  
  // Move back to point A
  Mouse.move(-movement_x, -movement_y);
}

void moveMouseCircular() {
  // Calculate radius based on X and Y values
  float radius = sqrt(movement_x * movement_x + movement_y * movement_y);
  
  // Draw a circle by moving in small increments
  for (int i = 0; i < CIRCLE_STEPS; i++) {
    // Calculate position on the circle
    float angle = 2 * PI * i / CIRCLE_STEPS;
    int x = round(radius * cos(angle));
    int y = round(radius * sin(angle));
    
    // Move to this position
    Mouse.move(x, y);
    delay(movement_speed); // Use configured movement speed
  }
  
  // Return to starting position
  Mouse.move(-movement_x, -movement_y);
}

void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error occurred while mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully");
}

void loadConfig() {
  Serial.println("Loading configuration");
  
  if (SPIFFS.exists(config_file)) {
    File file = SPIFFS.open(config_file, "r");
    if (file) {
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, file);
      
      if (!error) {
        move_interval = doc["move_interval"] | move_interval;
        movement_x = doc["movement_x"] | movement_x;
        movement_y = doc["movement_y"] | movement_y;
        movement_speed = doc["movement_speed"] | movement_speed;
        jiggler_enabled = doc["jiggler_enabled"] | jiggler_enabled;
        circular_movement = doc["circular_movement"] | circular_movement;
        random_delay = doc["random_delay"] | random_delay;
        movement_trail = doc["movement_trail"] | movement_trail;
        
        Serial.println("Configuration loaded successfully");
      } else {
        Serial.println("Failed to deserialize config");
      }
      
      file.close();
    }
  } else {
    Serial.println("Config file doesn't exist, using defaults");
    saveConfig();
  }
}

void saveConfig() {
  Serial.println("Saving configuration");
  
  StaticJsonDocument<512> doc;
  doc["move_interval"] = move_interval;
  doc["movement_x"] = movement_x;
  doc["movement_y"] = movement_y;
  doc["movement_speed"] = movement_speed;
  doc["jiggler_enabled"] = jiggler_enabled;
  doc["circular_movement"] = circular_movement;
  doc["random_delay"] = random_delay;
  doc["movement_trail"] = movement_trail;
  
  File file = SPIFFS.open(config_file, "w");
  if (file) {
    if (serializeJson(doc, file) == 0) {
      Serial.println("Failed to write config");
    } else {
      Serial.println("Configuration saved successfully");
    }
    file.close();
  } else {
    Serial.println("Failed to open config file for writing");
  }
}

void loadSettings() {
  Serial.println("Loading settings");
  
  if (SPIFFS.exists(settings_file)) {
    File file = SPIFFS.open(settings_file, "r");
    if (file) {
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, file);
      
      if (!error) {
        // Load AP settings
        if (doc.containsKey("ap")) {
          if (doc["ap"].containsKey("ssid")) {
            // Free the old string if it exists
            if (current_ssid != NULL) free(current_ssid);
            current_ssid = strdup(doc["ap"]["ssid"].as<const char*>());
          }
          if (doc["ap"].containsKey("password")) {
            if (current_password != NULL) free(current_password);
            current_password = strdup(doc["ap"]["password"].as<const char*>());
          }
          if (doc["ap"].containsKey("hidden")) {
            ap_hidden = doc["ap"]["hidden"].as<bool>();
          }
        }
        
        // Load hostname (now at root level)
        if (doc.containsKey("hostname")) {
          if (current_hostname != NULL) free(current_hostname);
          current_hostname = strdup(doc["hostname"].as<const char*>());
        }
        
        // Load WiFi mode settings
        if (doc.containsKey("wifi_mode")) {
          if (wifi_mode != NULL) free(wifi_mode);
          wifi_mode = strdup(doc["wifi_mode"].as<const char*>());
        }
        
        if (doc.containsKey("ap_availability")) {
          if (ap_availability != NULL) free(ap_availability);
          ap_availability = strdup(doc["ap_availability"].as<const char*>());
        }
        
        if (doc.containsKey("ap_timeout")) {
          ap_timeout = doc["ap_timeout"].as<int>();
        }
        
        // Load STA settings
        if (doc.containsKey("sta")) {
          if (doc["sta"].containsKey("ssid")) {
            if (sta_ssid != NULL) free(sta_ssid);
            sta_ssid = strdup(doc["sta"]["ssid"].as<const char*>());
          }
          if (doc["sta"].containsKey("password")) {
            if (sta_password != NULL) free(sta_password);
            sta_password = strdup(doc["sta"]["password"].as<const char*>());
          }
        }
        
        // Load auth settings
        if (doc.containsKey("auth")) {
          if (doc["auth"].containsKey("username")) {
            if (current_username != NULL) free(current_username);
            current_username = strdup(doc["auth"]["username"].as<const char*>());
          }
          if (doc["auth"].containsKey("password")) {
            if (current_auth_password != NULL) free(current_auth_password);
            current_auth_password = strdup(doc["auth"]["password"].as<const char*>());
          }
        }
        
        // Load web port
        if (doc.containsKey("web_port")) {
          current_webport = doc["web_port"].as<int>();
        }
        
        Serial.println("Settings loaded successfully");
      } else {
        Serial.println("Failed to deserialize settings");
      }
      
      file.close();
    }
  } else {
    Serial.println("Settings file doesn't exist, using defaults");
    saveSettings();
  }
}

void saveSettings() {
  Serial.println("Saving settings");
  
  StaticJsonDocument<512> doc;
  
  // AP settings
  JsonObject ap = doc.createNestedObject("ap");
  ap["ssid"] = current_ssid;
  ap["password"] = current_password;
  ap["hidden"] = ap_hidden;
  
  // Hostname at root level
  doc["hostname"] = current_hostname;
  
  // WiFi mode settings
  doc["wifi_mode"] = wifi_mode;
  doc["ap_availability"] = ap_availability;
  doc["ap_timeout"] = ap_timeout;
  
  // STA settings
  JsonObject sta = doc.createNestedObject("sta");
  sta["ssid"] = sta_ssid;
  sta["password"] = sta_password;
  
  // Auth settings
  JsonObject auth = doc.createNestedObject("auth");
  auth["username"] = current_username;
  auth["password"] = current_auth_password;
  
  // Web port
  doc["web_port"] = current_webport;
  
  File file = SPIFFS.open(settings_file, "w");
  if (file) {
    if (serializeJson(doc, file) == 0) {
      Serial.println("Failed to write settings");
    } else {
      Serial.println("Settings saved successfully");
    }
    file.close();
  } else {
    Serial.println("Failed to open settings file for writing");
  }
}

void setupWiFi() {
  Serial.println("Setting up WiFi");
  ap_start_time = millis(); // Record the time AP is started
  ap_active = true;
  
  if (strcmp(wifi_mode, "apsta") == 0) {
    // AP+STA mode
    Serial.println("Setting up WiFi in AP+STA mode");
    
    // Start in AP+STA mode
    WiFi.mode(WIFI_AP_STA);
    
    // Configure and start AP
    WiFi.softAPConfig(default_ip, default_ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(current_ssid, current_password, 1, ap_hidden);
    
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    
    // Try to connect to the preferred STA network
    WiFi.begin(sta_ssid, sta_password);
    
    Serial.print("Connecting to WiFi network: ");
    Serial.println(sta_ssid);
    
    // Wait for connection with timeout
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifi_connect_timeout) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      // Successfully connected to preferred network
      Serial.print("Connected to WiFi network. IP address: ");
      Serial.println(WiFi.localIP());
      isAPMode = false;
    } else {
      Serial.println("Failed to connect to preferred network, but AP is still active.");
      isAPMode = true;
    }
  } else {
    // AP Only mode
    Serial.println("Setting up WiFi in AP Only mode");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(default_ip, default_ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(current_ssid, current_password, 1, ap_hidden);
    
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    
    isAPMode = true;
  }
  
  // Setup mDNS
  if (!MDNS.begin(current_hostname)) {
    Serial.println("Error setting up mDNS responder!");
  } else {
    Serial.println("mDNS responder started");
    MDNS.addService("http", "tcp", current_webport);
    MDNS.addService("jiggla", "tcp", current_webport);
  }
}

void setupAccessPoint() {
  Serial.println("Setting up Access Point");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(default_ip, default_ip, IPAddress(255, 255, 255, 0));
  
  // Use the hidden parameter if enabled
  WiFi.softAP(current_ssid, current_password, 1, ap_hidden);
  
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  if (!MDNS.begin(current_hostname)) {
    Serial.println("Error setting up mDNS responder!");
  } else {
    Serial.println("mDNS responder started");
    MDNS.addService("http", "tcp", current_webport);
    MDNS.addService("jiggla", "tcp", current_webport);
  }
  
  isAPMode = true;
}

String generateSessionId() {
  const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  String sessionId = "";
  
  for (int i = 0; i < 32; i++) {
    sessionId += charset[random(0, sizeof(charset) - 1)];
  }
  
  return sessionId;
}

bool validateSession(AsyncWebServerRequest *request) {
  if (request->hasHeader("Cookie")) {
    String cookie = request->header("Cookie");
    int sessionIndex = cookie.indexOf("session=");
    
    if (sessionIndex != -1) {
      sessionIndex += 8; // Move past "session="
      int endIndex = cookie.indexOf(";", sessionIndex);
      String sessionId;
      
      if (endIndex == -1) {
        sessionId = cookie.substring(sessionIndex);
      } else {
        sessionId = cookie.substring(sessionIndex, endIndex);
      }
      
      // Find session
      for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active && sessions[i].id == sessionId) {
          // Check if session is expired
          // Handle millis() overflow by using subtraction which works correctly even across overflow
          if ((long)(millis() - sessions[i].expiry) < 0) {
            // Update expiry time
            sessions[i].expiry = millis() + session_timeout;
            
            // Save session changes periodically (only every 5 minutes to reduce flash wear)
            static unsigned long last_save = 0;
            if (millis() - last_save > 5 * 60 * 1000) {
              saveSessions();
              last_save = millis();
            }
            
            return true;
          } else {
            // Session expired
            sessions[i].active = false;
            saveSessions(); // Save the change
            return false;
          }
        }
      }
    }
  }
  
  return false;
}

void cleanupExpiredSessions() {
  unsigned long now = millis();
  
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].active && now >= sessions[i].expiry) {
      sessions[i].active = false;
    }
  }
}

void setupWebServer() {
  Serial.println("Setting up web server");
  
  // Route to serve static files
  server->serveStatic("/", SPIFFS, "/");
  
  // Redirect all requests to login page if not authenticated
  server->onNotFound([](AsyncWebServerRequest *request) {
    Serial.print("Unhandled request for URL: ");
    Serial.println(request->url());
    
    if (!validateSession(request)) {
      request->redirect("/login");
    } else {
      request->send(404, "text/plain", "Not Found");
    }
  });
  
  // Serve main page (only if authenticated)
  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Received request for main page");
    
    if (validateSession(request)) {
      Serial.println("Session is valid, serving index.html from SPIFFS");
      request->send(SPIFFS, "/index.html", "text/html");
    } else {
      Serial.println("Invalid session, redirecting to login");
      request->redirect("/login");
    }
  });
  
  // Serve login page
  server->on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (validateSession(request)) {
      request->redirect("/");
    } else {
      Serial.println("Serving login.html from SPIFFS");
      request->send(SPIFFS, "/login.html", "text/html");
    }
  });
  
  // Block direct access to index.html
  server->on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  
  // Block direct access to login.html
  server->on("/login.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/login");
  });
  
  // API endpoint to check authentication
  server->on("/api/auth/check", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (validateSession(request)) {
      request->send(200, "application/json", "{\"status\":\"authenticated\"}");
    } else {
      request->send(401, "application/json", "{\"status\":\"unauthorized\"}");
    }
  });
  
  // API endpoint to login
  server->on("/api/auth/login", HTTP_POST, 
    [](AsyncWebServerRequest *request) {
      // Empty handler for request
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Process login request
    if (request->_tempObject != NULL) {
      // Already processed
      return;
    }
    
    // Mark as processed to prevent multiple responses
    request->_tempObject = (void*)1;
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (!error) {
      String username = doc["username"].as<String>();
      String password = doc["password"].as<String>();
      
      if (username == current_username && password == current_auth_password) {
        // Create new session
        String sessionId = generateSessionId();
        
        // Find free slot
        int slot = -1;
        for (int i = 0; i < MAX_SESSIONS; i++) {
          if (!sessions[i].active) {
            slot = i;
            break;
          }
        }
        
        if (slot != -1) {
          sessions[slot].id = sessionId;
          sessions[slot].expiry = millis() + session_timeout;
          sessions[slot].active = true;
          
          // Save the sessions
          saveSessions();
          
          // Set cookie
          AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"status\":\"success\"}");
          response->addHeader("Set-Cookie", "session=" + sessionId + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + String(session_timeout / 1000));
          request->send(response);
          Serial.println("Login successful for user: " + username);
        } else {
          request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"No session slots available\"}");
        }
      } else {
        Serial.println("Login failed: Invalid credentials");
        request->send(401, "application/json", "{\"status\":\"error\",\"message\":\"Invalid credentials\"}");
      }
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    }
  });
  
  // API endpoint to logout
  server->on("/api/auth/logout", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool sessionFound = false;
    String sessionId = "";
    
    if (request->hasHeader("Cookie")) {
      String cookie = request->header("Cookie");
      int sessionIndex = cookie.indexOf("session=");
      
      if (sessionIndex != -1) {
        sessionIndex += 8; // Move past "session="
        int endIndex = cookie.indexOf(";", sessionIndex);
        
        if (endIndex == -1) {
          sessionId = cookie.substring(sessionIndex);
        } else {
          sessionId = cookie.substring(sessionIndex, endIndex);
        }
        
        // Find and invalidate session
        for (int i = 0; i < MAX_SESSIONS; i++) {
          if (sessions[i].active && sessions[i].id == sessionId) {
            sessions[i].active = false;
            sessionFound = true;
            Serial.println("Session invalidated: " + sessionId);
            
            // Save the sessions
            saveSessions();
            
            break;
          }
        }
      }
    }
    
    // Clear cookie with more secure parameters
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", 
      sessionFound ? "{\"status\":\"success\",\"message\":\"Logged out successfully\"}" : 
                    "{\"status\":\"warning\",\"message\":\"No valid session found\"}");
    
    response->addHeader("Set-Cookie", "session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
    request->send(response);
  });
  
  // API endpoint to get current configuration
  server->on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!validateSession(request)) {
      request->send(401, "application/json", "{\"status\":\"unauthorized\"}");
      return;
    }
    
    StaticJsonDocument<512> doc;
    doc["move_interval"] = move_interval / 1000; // Convert to seconds for readability
    doc["movement_x"] = movement_x;
    doc["movement_y"] = movement_y;
    doc["movement_speed"] = movement_speed;
    doc["jiggler_enabled"] = jiggler_enabled;
    doc["circular_movement"] = circular_movement;
    doc["random_delay"] = random_delay;
    doc["movement_trail"] = movement_trail;
    
    String response;
    serializeJson(doc, response);
    
    request->send(200, "application/json", response);
  });
  
  // API endpoint to get device status information
  server->on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!validateSession(request)) {
      request->send(401, "application/json", "{\"status\":\"unauthorized\"}");
      return;
    }
    
    StaticJsonDocument<512> doc;
    doc["jiggler_enabled"] = jiggler_enabled;
    doc["last_move_time"] = last_move_time;
    doc["next_move_time"] = next_move_time;
    doc["uptime_seconds"] = millis() / 1000;
    doc["in_ap_mode"] = isAPMode;
    
    String response;
    serializeJson(doc, response);
    
    request->send(200, "application/json", response);
  });
  
  // API endpoint to update configuration
  server->on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Only validate the session in the first handler, 
    // but don't send a response here
    if (!validateSession(request)) {
      request->send(401, "application/json", "{\"status\":\"unauthorized\"}");
      request->_tempObject = (void*)1; // Mark as processed
      return;
    }
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Check if response was already sent
    if (request->_tempObject != NULL) {
      return;
    }
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (!error) {
      // Update configuration
      if (doc.containsKey("jiggler_enabled")) {
        jiggler_enabled = doc["jiggler_enabled"].as<bool>();
      }
      
      if (doc.containsKey("circular_movement")) {
        circular_movement = doc["circular_movement"].as<bool>();
      }
      
      if (doc.containsKey("move_interval")) {
        move_interval = doc["move_interval"].as<int>() * 1000; // Convert from seconds to milliseconds
      }
      
      if (doc.containsKey("movement_x")) {
        movement_x = doc["movement_x"].as<int>();
      }
      
      if (doc.containsKey("movement_y")) {
        movement_y = doc["movement_y"].as<int>();
      }
      
      if (doc.containsKey("movement_speed")) {
        movement_speed = doc["movement_speed"].as<int>();
      }
      
      if (doc.containsKey("random_delay")) {
        random_delay = doc["random_delay"].as<bool>();
      }
      
      if (doc.containsKey("movement_trail")) {
        movement_trail = doc["movement_trail"].as<bool>();
      }
      
      // Save configuration
      saveConfig();
      
      // Reset the timer
      last_move_time = millis();
      next_move_time = millis() + calculateMoveInterval();
      
      Serial.println("Configuration updated via API");
      request->send(200, "application/json", "{\"status\":\"success\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    }
  });
  
  // API endpoint to trigger mouse movement immediately
  server->on("/api/move", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!validateSession(request)) {
      request->send(401, "application/json", "{\"status\":\"unauthorized\"}");
      return;
    }
    
    // Move mouse based on current movement pattern
    moveMouse();
    
    // Update last move time
    last_move_time = millis();
    next_move_time = millis() + calculateMoveInterval();
    
    request->send(200, "application/json", "{\"status\":\"success\"}");
  });
  
  // API endpoint to get settings
  server->on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!validateSession(request)) {
      request->send(401, "application/json", "{\"status\":\"unauthorized\"}");
      return;
    }
    
    StaticJsonDocument<512> doc;
    
    // AP settings
    JsonObject ap = doc.createNestedObject("ap");
    ap["ssid"] = current_ssid;
    ap["password"] = current_password;
    ap["hidden"] = ap_hidden;
    
    // Hostname at root level
    doc["hostname"] = current_hostname;
    
    // WiFi mode settings
    doc["wifi_mode"] = wifi_mode;
    doc["ap_availability"] = ap_availability;
    doc["ap_timeout"] = ap_timeout;
    
    // STA settings
    JsonObject sta = doc.createNestedObject("sta");
    sta["ssid"] = sta_ssid;
    sta["password"] = sta_password;
    
    // Auth settings
    JsonObject auth = doc.createNestedObject("auth");
    auth["username"] = current_username;
    // Don't send the actual password for security
    auth["password"] = "";
    
    // Web port
    doc["web_port"] = current_webport;
    
    String response;
    serializeJson(doc, response);
    
    request->send(200, "application/json", response);
  });
  
  // API endpoint to update settings
  server->on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Only validate the session in the first handler
    if (!validateSession(request)) {
      request->send(401, "application/json", "{\"status\":\"unauthorized\"}");
      request->_tempObject = (void*)1; // Mark as processed
      return;
    }
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Check if response was already sent
    if (request->_tempObject != NULL) {
      return;
    }
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (!error) {
      bool changed = false;
      
      // Update AP settings
      if (doc.containsKey("ap")) {
        if (doc["ap"].containsKey("ssid")) {
          if (current_ssid != NULL) free(current_ssid);
          current_ssid = strdup(doc["ap"]["ssid"].as<const char*>());
          changed = true;
        }
        if (doc["ap"].containsKey("password")) {
          if (current_password != NULL) free(current_password);
          current_password = strdup(doc["ap"]["password"].as<const char*>());
          changed = true;
        }
        if (doc["ap"].containsKey("hidden")) {
          ap_hidden = doc["ap"]["hidden"].as<bool>();
          changed = true;
        }
      }
      
      // Update hostname (now at root level)
      if (doc.containsKey("hostname")) {
        if (current_hostname != NULL) free(current_hostname);
        current_hostname = strdup(doc["hostname"].as<const char*>());
        changed = true;
      }
      
      // Update WiFi mode settings
      if (doc.containsKey("wifi_mode")) {
        if (wifi_mode != NULL) free(wifi_mode);
        wifi_mode = strdup(doc["wifi_mode"].as<const char*>());
        changed = true;
      }
      
      if (doc.containsKey("ap_availability")) {
        if (ap_availability != NULL) free(ap_availability);
        ap_availability = strdup(doc["ap_availability"].as<const char*>());
        changed = true;
      }
      
      if (doc.containsKey("ap_timeout")) {
        ap_timeout = doc["ap_timeout"].as<int>();
        changed = true;
      }
      
      // Update STA settings
      if (doc.containsKey("sta")) {
        if (doc["sta"].containsKey("ssid")) {
          if (sta_ssid != NULL) free(sta_ssid);
          sta_ssid = strdup(doc["sta"]["ssid"].as<const char*>());
          changed = true;
        }
        if (doc["sta"].containsKey("password")) {
          if (sta_password != NULL) free(sta_password);
          sta_password = strdup(doc["sta"]["password"].as<const char*>());
          changed = true;
        }
      }
      
      // Update Auth settings
      if (doc.containsKey("auth")) {
        if (doc["auth"].containsKey("username")) {
          if (current_username != NULL) free(current_username);
          current_username = strdup(doc["auth"]["username"].as<const char*>());
          changed = true;
        }
        if (doc["auth"].containsKey("password") && doc["auth"]["password"].as<String>().length() > 0) {
          if (current_auth_password != NULL) free(current_auth_password);
          current_auth_password = strdup(doc["auth"]["password"].as<const char*>());
          changed = true;
        }
      }
      
      // Update web port
      if (doc.containsKey("web_port")) {
        current_webport = doc["web_port"].as<int>();
        changed = true;
      }
      
      if (changed) {
        // Save settings
        saveSettings();
        
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Settings updated successfully\"}");
      } else {
        request->send(200, "application/json", "{\"status\":\"warning\",\"message\":\"No changes were made\"}");
      }
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    }
  });
  
  // API endpoint to reboot device
  server->on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!validateSession(request)) {
      request->send(401, "application/json", "{\"status\":\"unauthorized\"}");
      return;
    }
    
    // Send response before rebooting
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Rebooting device\"}");
    
    // Schedule reboot after sending response
    delay(500);
    ESP.restart();
  });
  
  // Serve the OTA update page with authentication
  server->on("/ota.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!validateSession(request)) {
      request->redirect("/login");
      return;
    }
    request->send(SPIFFS, "/ota.html", "text/html");
  });
  
  // Handle redirect from /update to /ota.html
  server->on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!validateSession(request)) {
      request->redirect("/login");
      return;
    }
    request->redirect("/ota.html");
  });
  
  // Handle OTA update file upload
  server->on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Send the response first
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", 
      (Update.hasError()) ? "Update failed!" : "Update success! Rebooting...");
    response->addHeader("Connection", "close");
    request->send(response);
    
    // Wait a bit and then restart
    delay(500);
    ESP.restart();
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    // Check authentication
    if (!validateSession(request)) {
      return;
    }
    
    // Get update type parameter (firmware or filesystem)
    String updateType = "firmware"; // Default to firmware
    if (request->hasParam("update_type", true)) {
      updateType = request->getParam("update_type", true)->value();
    }
    
    // First chunk received
    if (index == 0) {
      Serial.printf("OTA Update started: %s (Type: %s)\n", filename.c_str(), updateType.c_str());
      
      // Start update with appropriate command based on type
      int cmd = (updateType == "filesystem") ? U_SPIFFS : U_FLASH;
      
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
        Serial.println("OTA error: " + String(Update.errorString()));
      }
    }
    
    // Write chunk to flash
    if (Update.write(data, len) != len) {
      Serial.println("OTA error: " + String(Update.errorString()));
    }
    
    // Final chunk - finish update
    if (final) {
      if (Update.end(true)) {
        Serial.println("OTA update successful. Rebooting...");
      } else {
        Serial.println("OTA error: " + String(Update.errorString()));
      }
    }
  });
  
  // Start server
  server->begin();
  Serial.println("Web server started");
}

// Calculate movement interval, applying randomization if enabled
unsigned long calculateMoveInterval() {
  if (!random_delay) {
    return move_interval;
  }
  
  // Add random variation of ±30%
  float variation = 0.3; // 30%
  float randomFactor = 1.0 - variation + (random(0, 2000) / 1000.0 * variation * 2);
  
  return (unsigned long)(move_interval * randomFactor);
}

// Perform mouse movement based on settings
void moveMouse() {
  if (movement_trail) {
    // Create a movement trail with multiple movements
    for (int i = 0; i < 3; i++) {
      if (circular_movement) {
        moveMouseCircular();
      } else {
        moveMouseLinear();
      }
      delay(100); // Brief pause between trail movements
    }
  } else {
    // Single movement
    if (circular_movement) {
      moveMouseCircular();
    } else {
      moveMouseLinear();
    }
  }
}

// Save sessions to flash
void saveSessions() {
  Serial.println("Saving sessions");
  
  StaticJsonDocument<2048> doc;
  JsonArray sessionsArray = doc.createNestedArray("sessions");
  
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].active) {
      JsonObject sessionObj = sessionsArray.createNestedObject();
      sessionObj["id"] = sessions[i].id;
      sessionObj["expiry"] = sessions[i].expiry;
      sessionObj["active"] = sessions[i].active;
    }
  }
  
  File file = SPIFFS.open("/sessions.json", "w");
  if (file) {
    if (serializeJson(doc, file) == 0) {
      Serial.println("Failed to write sessions");
    } else {
      Serial.println("Sessions saved successfully");
    }
    file.close();
  } else {
    Serial.println("Failed to open sessions file for writing");
  }
}

// Load sessions from flash
void loadSessions() {
  Serial.println("Loading sessions");
  
  if (SPIFFS.exists("/sessions.json")) {
    File file = SPIFFS.open("/sessions.json", "r");
    if (file) {
      StaticJsonDocument<2048> doc;
      DeserializationError error = deserializeJson(doc, file);
      
      if (!error) {
        // Clear existing sessions
        for (int i = 0; i < MAX_SESSIONS; i++) {
          sessions[i].active = false;
        }
        
        // Load saved sessions
        JsonArray sessionsArray = doc["sessions"].as<JsonArray>();
        int i = 0;
        
        for (JsonObject sessionObj : sessionsArray) {
          if (i < MAX_SESSIONS) {
            sessions[i].id = sessionObj["id"].as<String>();
            sessions[i].expiry = sessionObj["expiry"].as<unsigned long>();
            
            // Add an extra day to the expiry to ensure sessions remain valid after reboot
            // This prevents immediate session expiry after reboot
            sessions[i].expiry = millis() + 86400000; // 24 hours in milliseconds
            
            sessions[i].active = true;
            i++;
          }
        }
        
        Serial.printf("Loaded %d sessions\n", i);
      } else {
        Serial.println("Failed to deserialize sessions");
      }
      
      file.close();
    }
  } else {
    Serial.println("Sessions file doesn't exist");
  }
}

// Function to free allocated memory
void cleanupMemory() {
  // Free memory
  if (current_ssid != NULL) free(current_ssid);
  if (current_password != NULL) free(current_password);
  if (current_hostname != NULL) free(current_hostname);
  if (current_username != NULL) free(current_username);
  if (current_auth_password != NULL) free(current_auth_password);
  if (wifi_mode != NULL) free(wifi_mode);
  if (ap_availability != NULL) free(ap_availability);
  if (sta_ssid != NULL) free(sta_ssid);
  if (sta_password != NULL) free(sta_password);
}