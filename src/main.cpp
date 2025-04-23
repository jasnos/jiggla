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

// Define a simpler debug approach for ESP32-S2/S3 in USB mode
// In this mode, we don't have Serial, so we use a no-op debug for now
#define DEBUG(x) do {} while(0)
#define DEBUGF(x, ...) do {} while(0)

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
int movement_size = 5; // Default movement size (replaces separate X and Y)
int movement_speed = 2000; // Total milliseconds for a complete movement pattern (1-3000 range)
bool jiggler_enabled = true; // Default state is enabled
char* movement_pattern = strdup("linear"); // Default movement pattern
bool random_delay = false; // Randomize delay between movements
bool movement_trail = false; // Create a movement trail
const int CIRCLE_STEPS = 100; // Number of steps to complete a circle (increased for smoothness)
const int LINE_STEPS = 50;    // Number of steps for a straight line
const int RECT_STEPS = 200;   // Number of steps for rectangle (50 per side)
const int TRIANGLE_STEPS = 150; // Number of steps for triangle (50 per side)
const int ZIGZAG_STEPS = 150;  // Number of steps for zig-zag

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

// Tracking variables for mouse position
int totalDisplacementX = 0;
int totalDisplacementY = 0;

// Add this near the top with other global variables
bool auth_enabled = true; // Default to true for backward compatibility

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
void moveMouseRectangle();
void moveMouseTriangle();
void moveMouseZigzag();
unsigned long calculateMoveInterval();
void moveMouse();
void saveSessions();
void loadSessions();
void cleanupMemory();
void resetCursorPosition();

// Function to scale movement size based on slider value
int scaleMovementSize(int rawSize) {
  // Scale the raw size (1-200) to appropriate pixel values
  if (rawSize <= 33) {
    // Small movements: 20-50 pixels
    return 20 + (rawSize * 30 / 33);
  } else if (rawSize <= 66) {
    // Medium movements: 100-200 pixels
    return 100 + ((rawSize - 33) * 100 / 33);
  } else {
    // Large movements: 250-500 pixels
    return 250 + ((rawSize - 66) * 250 / 134);
  }
}

void setup() {
  // Initialize USB for both mouse and Serial
  USB.VID(0x046d);
  USB.PID(0xc077);
  USB.manufacturerName("Logitech");
  USB.productName("M105 Optical Mouse");
  USB.begin();
  
  delay(500);
  DEBUG("\nStarting jiggla");

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
  
  DEBUG("jiggla ready");
}

void loop() {
  // Check if it's time to move the mouse and if jiggler is enabled
  if (jiggler_enabled && millis() - last_move_time >= calculateMoveInterval()) {
    DEBUG("Moving mouse");
    
    // Reset cursor position to original position before starting new movement
    resetCursorPosition();
    
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
      if (strcmp(wifi_mode, "apsta") == 0) {
        // In AP+STA mode, only turn off the AP part, keep STA running
        DEBUG("AP timeout reached, turning off AP but keeping station mode");
        WiFi.mode(WIFI_STA);
        ap_active = false;
        DEBUG("AP turned off, device will continue in station mode");
      } else {
        // In AP-only mode, turn off WiFi completely
        DEBUG("AP timeout reached, turning off WiFi completely");
        WiFi.mode(WIFI_OFF);
        ap_active = false;
        DEBUG("WiFi turned off, device will continue as USB mouse jiggler only");
      }
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
  // Track actual movement
  int totalDeltaX = 0;
  int totalDeltaY = 0;
  
  // Calculate delay between steps to achieve the desired total movement time
  int stepDelay = movement_speed / (LINE_STEPS * 2); // * 2 for round trip
  
  // Scale the movement size
  int scaledSize = scaleMovementSize(movement_size);
  
  // Move from origin to end point smoothly
  for (int i = 0; i < LINE_STEPS; i++) {
    // Calculate the step size for this iteration
    float progress = (float)i / (LINE_STEPS - 1); // 0.0 to 1.0
    int targetX = round(scaledSize * progress);
    int targetY = 0; // Linear movement is horizontal by default
    
    // Calculate delta from last position
    int deltaX = targetX - totalDeltaX;
    int deltaY = targetY - totalDeltaY;
    
    // Move to this position
    if (deltaX != 0 || deltaY != 0) {
      Mouse.move(deltaX, deltaY);
      
      // Update tracking
      totalDeltaX += deltaX;
      totalDeltaY += deltaY;
      
      // Update global tracking
      totalDisplacementX += deltaX;
      totalDisplacementY += deltaY;
    }
    
    delay(stepDelay);
  }
  
  // Move back to origin smoothly
  for (int i = LINE_STEPS - 1; i >= 0; i--) {
    // Calculate the step size for this iteration
    float progress = (float)i / (LINE_STEPS - 1); // 1.0 to 0.0
    int targetX = round(scaledSize * progress);
    int targetY = 0;
    
    // Calculate delta from last position
    int deltaX = targetX - totalDeltaX;
    int deltaY = targetY - totalDeltaY;
    
    // Move to this position
    if (deltaX != 0 || deltaY != 0) {
      Mouse.move(deltaX, deltaY);
      
      // Update tracking
      totalDeltaX += deltaX;
      totalDeltaY += deltaY;
      
      // Update global tracking
      totalDisplacementX += deltaX;
      totalDisplacementY += deltaY;
    }
    
    delay(stepDelay);
  }
  
  // Final compensation if there's any drift
  if (totalDeltaX != 0 || totalDeltaY != 0) {
    Mouse.move(-totalDeltaX, -totalDeltaY);
    
    // Update global tracking
    totalDisplacementX -= totalDeltaX;
    totalDisplacementY -= totalDeltaY;
    
    DEBUGF("Linear compensation: (%d, %d)", -totalDeltaX, -totalDeltaY);
  }
}

void moveMouseCircular() {
  // Calculate radius based on scaled movement size
  float radius = scaleMovementSize(movement_size);
  
  // Calculate delay between steps to achieve the desired total movement time
  int stepDelay = movement_speed / CIRCLE_STEPS;
  
  // Track total movement to ensure we return to start position
  int totalDeltaX = 0;
  int totalDeltaY = 0;
  int lastX = 0;
  int lastY = 0;
  
  // Draw a circle by moving in small increments
  for (int i = 0; i <= CIRCLE_STEPS; i++) {
    // Calculate position on the circle
    float angle = 2 * PI * i / CIRCLE_STEPS;
    int x = round(radius * cos(angle));
    int y = round(radius * sin(angle));
    
    // Calculate the delta movement from last position
    int deltaX = x - lastX;
    int deltaY = y - lastY;
    
    // Only move if there's an actual change to avoid unnecessary moves
    if (deltaX != 0 || deltaY != 0) {
      // Move to this position
      Mouse.move(deltaX, deltaY);
      
      // Keep track of our movement
      totalDeltaX += deltaX;
      totalDeltaY += deltaY;
      
      // Update global tracking
      totalDisplacementX += deltaX;
      totalDisplacementY += deltaY;
      
      // Update last position
      lastX = x;
      lastY = y;
    }
    
    delay(stepDelay);
  }
  
  // Return to exact starting position by inverting all accumulated movement
  if (totalDeltaX != 0 || totalDeltaY != 0) {
    Mouse.move(-totalDeltaX, -totalDeltaY);
    
    // Update global tracking
    totalDisplacementX -= totalDeltaX;
    totalDisplacementY -= totalDeltaY;
    
    DEBUGF("Circular compensation: (%d, %d)", -totalDeltaX, -totalDeltaY);
  }
}

void moveMouseRectangle() {
  // Track total movement
  int totalDeltaX = 0;
  int totalDeltaY = 0;
  
  // Scale the movement size
  int scaledSize = scaleMovementSize(movement_size);
  
  // Define rectangle dimensions
  int width = scaledSize;
  int height = scaledSize * 0.75; // Make height 75% of width for better appearance
  
  // Calculate delay between steps to achieve the desired total movement time
  int stepsPerSide = RECT_STEPS / 4; // 4 sides
  int stepDelay = movement_speed / RECT_STEPS;
  
  // Current position
  int currentX = 0;
  int currentY = 0;
  
  // Draw a rectangle with smooth edges
  
  // Side 1: Move right
  for (int i = 0; i <= stepsPerSide; i++) {
    float progress = (float)i / stepsPerSide;
    int targetX = round(width * progress);
    int targetY = 0;
    
    // Calculate delta from last position
    int deltaX = targetX - currentX;
    int deltaY = targetY - currentY;
    
    if (deltaX != 0 || deltaY != 0) {
      Mouse.move(deltaX, deltaY);
      
      // Update tracking
      totalDeltaX += deltaX;
      totalDeltaY += deltaY;
      
      // Update global tracking
      totalDisplacementX += deltaX;
      totalDisplacementY += deltaY;
      
      // Update current position
      currentX = targetX;
      currentY = targetY;
    }
    
    delay(stepDelay);
  }
  
  // Side 2: Move down
  for (int i = 0; i <= stepsPerSide; i++) {
    float progress = (float)i / stepsPerSide;
    int targetX = width;
    int targetY = round(height * progress);
    
    // Calculate delta from last position
    int deltaX = targetX - currentX;
    int deltaY = targetY - currentY;
    
    if (deltaX != 0 || deltaY != 0) {
      Mouse.move(deltaX, deltaY);
      
      // Update tracking
      totalDeltaX += deltaX;
      totalDeltaY += deltaY;
      
      // Update global tracking
      totalDisplacementX += deltaX;
      totalDisplacementY += deltaY;
      
      // Update current position
      currentX = targetX;
      currentY = targetY;
    }
    
    delay(stepDelay);
  }
  
  // Side 3: Move left
  for (int i = 0; i <= stepsPerSide; i++) {
    float progress = (float)i / stepsPerSide;
    int targetX = width - round(width * progress);
    int targetY = height;
    
    // Calculate delta from last position
    int deltaX = targetX - currentX;
    int deltaY = targetY - currentY;
    
    if (deltaX != 0 || deltaY != 0) {
      Mouse.move(deltaX, deltaY);
      
      // Update tracking
      totalDeltaX += deltaX;
      totalDeltaY += deltaY;
      
      // Update global tracking
      totalDisplacementX += deltaX;
      totalDisplacementY += deltaY;
      
      // Update current position
      currentX = targetX;
      currentY = targetY;
    }
    
    delay(stepDelay);
  }
  
  // Side 4: Move up (completing the rectangle)
  for (int i = 0; i <= stepsPerSide; i++) {
    float progress = (float)i / stepsPerSide;
    int targetX = 0;
    int targetY = height - round(height * progress);
    
    // Calculate delta from last position
    int deltaX = targetX - currentX;
    int deltaY = targetY - currentY;
    
    if (deltaX != 0 || deltaY != 0) {
      Mouse.move(deltaX, deltaY);
      
      // Update tracking
      totalDeltaX += deltaX;
      totalDeltaY += deltaY;
      
      // Update global tracking
      totalDisplacementX += deltaX;
      totalDisplacementY += deltaY;
      
      // Update current position
      currentX = targetX;
      currentY = targetY;
    }
    
    delay(stepDelay);
  }
  
  // Return to exact starting position if there's any drift
  if (totalDeltaX != 0 || totalDeltaY != 0) {
    Mouse.move(-totalDeltaX, -totalDeltaY);
    totalDisplacementX -= totalDeltaX;
    totalDisplacementY -= totalDeltaY;
    DEBUGF("Rectangle compensation: (%d, %d)", -totalDeltaX, -totalDeltaY);
  }
}

void moveMouseTriangle() {
  // Track total movement
  int totalDeltaX = 0;
  int totalDeltaY = 0;
  
  // Scale the movement size
  int scaledSize = scaleMovementSize(movement_size);
  
  // Triangle dimensions based on scaled movement size
  int side = scaledSize;
  float height = side * 0.866; // Height of equilateral triangle (side * sin(60°))
  
  // Calculate delay between steps to achieve the desired total movement time
  int stepsPerSide = TRIANGLE_STEPS / 3; // 3 sides
  int stepDelay = movement_speed / TRIANGLE_STEPS;
  
  // Current position
  int currentX = 0;
  int currentY = 0;
  
  // Draw a triangle with smooth edges
  
  // Side 1: Move down-right
  for (int i = 0; i <= stepsPerSide; i++) {
    float progress = (float)i / stepsPerSide;
    int targetX = round((side/2) * progress);
    int targetY = round(height * progress);
    
    // Calculate delta from last position
    int deltaX = targetX - currentX;
    int deltaY = targetY - currentY;
    
    if (deltaX != 0 || deltaY != 0) {
      Mouse.move(deltaX, deltaY);
      
      // Update tracking
      totalDeltaX += deltaX;
      totalDeltaY += deltaY;
      
      // Update global tracking
      totalDisplacementX += deltaX;
      totalDisplacementY += deltaY;
      
      // Update current position
      currentX = targetX;
      currentY = targetY;
    }
    
    delay(stepDelay);
  }
  
  // Side 2: Move left
  for (int i = 0; i <= stepsPerSide; i++) {
    float progress = (float)i / stepsPerSide;
    int targetX = round(side/2 - side * progress);
    int targetY = round(height);
    
    // Calculate delta from last position
    int deltaX = targetX - currentX;
    int deltaY = targetY - currentY;
    
    if (deltaX != 0 || deltaY != 0) {
      Mouse.move(deltaX, deltaY);
      
      // Update tracking
      totalDeltaX += deltaX;
      totalDeltaY += deltaY;
      
      // Update global tracking
      totalDisplacementX += deltaX;
      totalDisplacementY += deltaY;
      
      // Update current position
      currentX = targetX;
      currentY = targetY;
    }
    
    delay(stepDelay);
  }
  
  // Side 3: Move up-right (back to start)
  for (int i = 0; i <= stepsPerSide; i++) {
    float progress = (float)i / stepsPerSide;
    int targetX = round(-side/2 + (side/2) * progress);
    int targetY = round(height - height * progress);
    
    // Calculate delta from last position
    int deltaX = targetX - currentX;
    int deltaY = targetY - currentY;
    
    if (deltaX != 0 || deltaY != 0) {
      Mouse.move(deltaX, deltaY);
      
      // Update tracking
      totalDeltaX += deltaX;
      totalDeltaY += deltaY;
      
      // Update global tracking
      totalDisplacementX += deltaX;
      totalDisplacementY += deltaY;
      
      // Update current position
      currentX = targetX;
      currentY = targetY;
    }
    
    delay(stepDelay);
  }
  
  // Return to exact starting position if there's any drift
  if (totalDeltaX != 0 || totalDeltaY != 0) {
    Mouse.move(-totalDeltaX, -totalDeltaY);
    totalDisplacementX -= totalDeltaX;
    totalDisplacementY -= totalDeltaY;
    DEBUGF("Triangle compensation: (%d, %d)", -totalDeltaX, -totalDeltaY);
  }
}

void moveMouseZigzag() {
  // Track total movement
  int totalDeltaX = 0;
  int totalDeltaY = 0;
  
  // Scale the movement size
  int scaledSize = scaleMovementSize(movement_size);
  
  // Define zigzag dimensions
  int width = scaledSize / 2;
  int height = scaledSize / 3;
  int zigCount = 3; // Number of zigs and zags
  
  // Calculate delay between steps to achieve the desired total movement time
  int stepsPerZig = ZIGZAG_STEPS / (zigCount * 2); // Each zig and zag
  int stepDelay = movement_speed / ZIGZAG_STEPS;
  
  // Current position
  int currentX = 0;
  int currentY = 0;
  
  // Draw a zigzag pattern
  for (int z = 0; z < zigCount; z++) {
    // Zig - move diagonally down and right
    for (int i = 0; i <= stepsPerZig; i++) {
      float progress = (float)i / stepsPerZig;
      int targetX = currentX + round(width * progress);
      int targetY = currentY + round(height * progress);
      
      // Calculate delta from last position
      int deltaX = targetX - currentX;
      int deltaY = targetY - currentY;
      
      if (deltaX != 0 || deltaY != 0) {
        Mouse.move(deltaX, deltaY);
        
        // Update tracking
        totalDeltaX += deltaX;
        totalDeltaY += deltaY;
        
        // Update global tracking
        totalDisplacementX += deltaX;
        totalDisplacementY += deltaY;
        
        // Update current position
        currentX = targetX;
        currentY = targetY;
      }
      
      delay(stepDelay);
    }
    
    // Zag - move diagonally up and right
    for (int i = 0; i <= stepsPerZig; i++) {
      float progress = (float)i / stepsPerZig;
      int targetX = currentX + round(width * progress);
      int targetY = currentY - round(height * progress);
      
      // Calculate delta from last position
      int deltaX = targetX - currentX;
      int deltaY = targetY - currentY;
      
      if (deltaX != 0 || deltaY != 0) {
        Mouse.move(deltaX, deltaY);
        
        // Update tracking
        totalDeltaX += deltaX;
        totalDeltaY += deltaY;
        
        // Update global tracking
        totalDisplacementX += deltaX;
        totalDisplacementY += deltaY;
        
        // Update current position
        currentX = targetX;
        currentY = targetY;
      }
      
      delay(stepDelay);
    }
  }
  
  // Return to start position
  if (totalDeltaX != 0 || totalDeltaY != 0) {
    Mouse.move(-totalDeltaX, -totalDeltaY);
    totalDisplacementX -= totalDeltaX;
    totalDisplacementY -= totalDeltaY;
    DEBUGF("Zigzag compensation: (%d, %d)", -totalDeltaX, -totalDeltaY);
  }
}

void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    DEBUG("An error occurred while mounting SPIFFS");
    return;
  }
  DEBUG("SPIFFS mounted successfully");
}

void loadConfig() {
  DEBUG("Loading configuration");
  
  if (SPIFFS.exists(config_file)) {
    File file = SPIFFS.open(config_file, "r");
    if (file) {
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, file);
      
      if (!error) {
        move_interval = doc["move_interval"] | move_interval;
        
        // Handle movement pattern
        if (doc.containsKey("movement_pattern")) {
          if (movement_pattern != NULL) free(movement_pattern);
          movement_pattern = strdup(doc["movement_pattern"].as<const char*>());
        } else {
          // Legacy compatibility - use circular_movement to determine pattern
          bool circular = doc["circular_movement"] | false;
          if (movement_pattern != NULL) free(movement_pattern);
          movement_pattern = strdup(circular ? "circular" : "linear");
        }
        
        // Handle movement size (new) or fall back to X/Y values
        if (doc.containsKey("movement_size")) {
          movement_size = doc["movement_size"] | movement_size;
        } else {
          // Legacy - use max of X and Y for size
          int movement_x = doc["movement_x"] | 5;
          int movement_y = doc["movement_y"] | 5;
          movement_size = max(abs(movement_x), abs(movement_y));
        }
        
        movement_speed = doc["movement_speed"] | movement_speed;
        jiggler_enabled = doc["jiggler_enabled"] | jiggler_enabled;
        random_delay = doc["random_delay"] | random_delay;
        movement_trail = doc["movement_trail"] | movement_trail;
        
        DEBUG("Configuration loaded successfully");
      } else {
        DEBUG("Failed to deserialize config");
      }
      
      file.close();
    }
  } else {
    DEBUG("Config file doesn't exist, using defaults");
    saveConfig();
  }
}

void saveConfig() {
  DEBUG("Saving configuration");
  
  StaticJsonDocument<512> doc;
  doc["move_interval"] = move_interval;
  doc["movement_pattern"] = movement_pattern;
  doc["movement_size"] = movement_size;
  doc["movement_speed"] = movement_speed;
  doc["jiggler_enabled"] = jiggler_enabled;
  
  // Legacy compatibility
  doc["circular_movement"] = (strcmp(movement_pattern, "circular") == 0);
  doc["movement_x"] = movement_size;
  doc["movement_y"] = movement_size;
  
  doc["random_delay"] = random_delay;
  doc["movement_trail"] = movement_trail;
  
  File file = SPIFFS.open(config_file, "w");
  if (file) {
    if (serializeJson(doc, file) == 0) {
      DEBUG("Failed to write config");
    } else {
      DEBUG("Configuration saved successfully");
    }
    file.close();
  } else {
    DEBUG("Failed to open config file for writing");
  }
}

void loadSettings() {
  DEBUG("Loading settings");
  
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
          auth_enabled = doc["auth"].containsKey("enabled") ? doc["auth"]["enabled"].as<bool>() : true;
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
        
        DEBUG("Settings loaded successfully");
      } else {
        DEBUG("Failed to deserialize settings");
      }
      
      file.close();
    }
  } else {
    DEBUG("Settings file doesn't exist, using defaults");
    saveSettings();
  }
}

void saveSettings() {
  DEBUG("Saving settings");
  
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
  auth["enabled"] = auth_enabled;
  auth["username"] = current_username;
  auth["password"] = current_auth_password;
  
  // Web port
  doc["web_port"] = current_webport;
  
  File file = SPIFFS.open(settings_file, "w");
  if (file) {
    if (serializeJson(doc, file) == 0) {
      DEBUG("Failed to write settings");
    } else {
      DEBUG("Settings saved successfully");
    }
    file.close();
  } else {
    DEBUG("Failed to open settings file for writing");
  }
}

void setupWiFi() {
  DEBUG("Setting up WiFi");
  ap_start_time = millis(); // Record the time AP is started
  ap_active = true;
  
  if (strcmp(wifi_mode, "apsta") == 0) {
    // AP+STA mode
    DEBUG("Setting up WiFi in AP+STA mode");
    
    // Start in AP+STA mode
    WiFi.mode(WIFI_AP_STA);
    
    // Configure and start AP
    WiFi.softAPConfig(default_ip, default_ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(current_ssid, current_password, 1, ap_hidden);
    
    DEBUGF("AP IP address: %s", WiFi.softAPIP().toString().c_str());
    
    // Set hostname before connecting to WiFi
    WiFi.setHostname(current_hostname);
    
    // Try to connect to the preferred STA network
    WiFi.begin(sta_ssid, sta_password);
    
    DEBUG("Connecting to WiFi network: ");
    DEBUG(sta_ssid);
    
    // Wait for connection with timeout
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifi_connect_timeout) {
      delay(500);
      DEBUG(".");
    }
    DEBUG("\n");
    
    if (WiFi.status() == WL_CONNECTED) {
      // Successfully connected to preferred network
      DEBUGF("Connected to WiFi network. IP address: %s", WiFi.localIP().toString().c_str());
      isAPMode = false;
    } else {
      DEBUG("Failed to connect to preferred network, but AP is still active.");
      isAPMode = true;
    }
  } else {
    // AP Only mode
    DEBUG("Setting up WiFi in AP Only mode");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(default_ip, default_ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(current_ssid, current_password, 1, ap_hidden);
    
    DEBUGF("AP IP address: %s", WiFi.softAPIP().toString().c_str());
    
    isAPMode = true;
  }
  
  // Setup mDNS
  if (!MDNS.begin(current_hostname)) {
    DEBUG("Error setting up mDNS responder!");
  } else {
    DEBUG("mDNS responder started");
    MDNS.addService("http", "tcp", current_webport);
    MDNS.addService("jiggla", "tcp", current_webport);
  }
}

void setupAccessPoint() {
  DEBUG("Setting up Access Point");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(default_ip, default_ip, IPAddress(255, 255, 255, 0));
  
  // Use the hidden parameter if enabled
  WiFi.softAP(current_ssid, current_password, 1, ap_hidden);
  
  DEBUGF("AP IP address: %s", WiFi.softAPIP().toString().c_str());
  
  if (!MDNS.begin(current_hostname)) {
    DEBUG("Error setting up mDNS responder!");
  } else {
    DEBUG("mDNS responder started");
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
  // If authentication is disabled, always return true
  if (!auth_enabled) {
    return true;
  }
  
  DEBUG("Validating session");
  DEBUGF("Request URL: %s, Host: %s", request->url().c_str(), request->host().c_str());
  
  // Check if we're being accessed on a port other than the configured one
  String hostWithPort = request->host();
  int colonPos = hostWithPort.indexOf(':');
  if (colonPos != -1) {
    String portStr = hostWithPort.substring(colonPos + 1);
    int port = portStr.toInt();
    DEBUGF("Request port: %d, Configured port: %d", port, current_webport);
    
    // If the port doesn't match our configured port, we might need to save it
    if (port != current_webport && port > 0) {
      DEBUGF("Detected access on non-standard port %d, updating config", port);
      current_webport = port;
      saveSettings();
    }
  }
  
  if (request->hasHeader("Cookie")) {
    String cookie = request->header("Cookie");
    DEBUGF("Cookie header: %s", cookie.c_str());
    
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
      
      DEBUGF("Found session ID: %s", sessionId.c_str());
      
      // Find session
      for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active && sessions[i].id == sessionId) {
          // Check if session is expired
          // Handle millis() overflow by using subtraction which works correctly even across overflow
          if ((long)(sessions[i].expiry - millis()) > 0) {
            // Update expiry time
            sessions[i].expiry = millis() + session_timeout;
            
            // Save session changes periodically (only every 5 minutes to reduce flash wear)
            static unsigned long last_save = 0;
            if (millis() - last_save > 5 * 60 * 1000) {
              saveSessions();
              last_save = millis();
            }
            
            DEBUGF("Session validated: id=%s, expiry=%lu, current=%lu", 
                         sessionId.c_str(), sessions[i].expiry, millis());
            return true;
          } else {
            // Session expired
            DEBUGF("Session expired: id=%s, expiry=%lu, current=%lu", 
                         sessionId.c_str(), sessions[i].expiry, millis());
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
  DEBUG("Setting up web server");
  
  // Route to serve static files
  server->serveStatic("/", SPIFFS, "/");
  
  // Redirect all requests to login page if not authenticated
  server->onNotFound([](AsyncWebServerRequest *request) {
    DEBUGF("Unhandled request for URL: %s", request->url().c_str());
    
    if (!validateSession(request)) {
      request->redirect("/login");
    } else {
      request->send(404, "text/plain", "Not Found");
    }
  });
  
  // Serve main page (only if authenticated)
  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    DEBUG("Received request for main page");
    
    if (validateSession(request)) {
      DEBUG("Session is valid, serving index.html from SPIFFS");
      request->send(SPIFFS, "/index.html", "text/html");
    } else {
      DEBUG("Invalid session, redirecting to login");
      request->redirect("/login");
    }
  });
  
  // Serve login page
  server->on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (validateSession(request)) {
      request->redirect("/");
    } else {
      DEBUG("Serving login.html from SPIFFS");
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
    DEBUG("Auth check request received");
    
    // Debug: Print all headers to see if the cookie is present
    int headers = request->headers();
    for(int i=0; i<headers; i++) {
      DEBUGF("Header[%s]: %s", request->headerName(i).c_str(), request->header(i).c_str());
    }
    
    if (validateSession(request)) {
      DEBUG("Auth check passed");
      request->send(200, "application/json", "{\"status\":\"authenticated\"}");
    } else {
      DEBUG("Auth check failed");
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
          
          // Construct a more robust cookie - use the host from the request
          String host = request->host();
          int colonPos = host.indexOf(':');
          if (colonPos != -1) {
            host = host.substring(0, colonPos); // Remove port if present
          }
          
          // Set the cookie with improved attributes
          String cookieHeader = "session=" + sessionId + 
                               "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" + String(session_timeout / 1000);
          
          response->addHeader("Set-Cookie", cookieHeader);
          request->send(response);
          DEBUG("Login successful for user: " + username);
        } else {
          request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"No session slots available\"}");
        }
      } else {
        DEBUG("Login failed: Invalid credentials");
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
            DEBUG("Session invalidated: " + sessionId);
            
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
    doc["movement_pattern"] = movement_pattern;
    doc["movement_size"] = movement_size;
    doc["movement_speed"] = movement_speed;
    doc["jiggler_enabled"] = jiggler_enabled;
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
      
      if (doc.containsKey("movement_pattern")) {
        // Free old pattern string and allocate new one
        if (movement_pattern != NULL) free(movement_pattern);
        movement_pattern = strdup(doc["movement_pattern"].as<const char*>());
      } else if (doc.containsKey("circular_movement")) {
        // Handle legacy parameter
        bool circular = doc["circular_movement"].as<bool>();
        if (movement_pattern != NULL) free(movement_pattern);
        movement_pattern = strdup(circular ? "circular" : "linear");
      }
      
      if (doc.containsKey("move_interval")) {
        move_interval = doc["move_interval"].as<int>() * 1000; // Convert from seconds to milliseconds
      }
      
      if (doc.containsKey("movement_size")) {
        movement_size = doc["movement_size"].as<int>();
      } else {
        // For backwards compatibility
        if (doc.containsKey("movement_x")) {
          movement_size = abs(doc["movement_x"].as<int>());
        }
        if (doc.containsKey("movement_y")) {
          // Use the larger of X or Y
          movement_size = max(movement_size, abs(doc["movement_y"].as<int>()));
        }
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
      
      DEBUG("Configuration updated via API");
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
    auth["enabled"] = auth_enabled;
    auth["username"] = current_username;
    auth["password"] = current_auth_password;
    
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
        if (doc["auth"].containsKey("enabled")) {
          auth_enabled = doc["auth"]["enabled"].as<bool>();
          changed = true;
        }
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
      DEBUGF("OTA Update started: %s (Type: %s)", filename.c_str(), updateType.c_str());
      
      // Start update with appropriate command based on type
      int cmd = (updateType == "filesystem") ? U_SPIFFS : U_FLASH;
      
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
        DEBUG(String("OTA error: ") + Update.errorString());
      }
    }
    
    // Write chunk to flash
    if (Update.write(data, len) != len) {
      DEBUG(String("OTA error: ") + Update.errorString());
    }
    
    // Final chunk - finish update
    if (final) {
      if (Update.end(true)) {
        DEBUG("OTA update successful. Rebooting...");
      } else {
        DEBUG(String("OTA error: ") + Update.errorString());
      }
    }
  });
  
  // API endpoint for touchpad mouse movement
  server->on("/api/touchpad/move", HTTP_POST, [](AsyncWebServerRequest *request) {
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
    
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (!error) {
      int x = doc["x"].as<int>();
      int y = doc["y"].as<int>();
      
      // Move mouse by the specified amount
      Mouse.move(x, y);
      
      // Update last move time
      last_move_time = millis();
      next_move_time = millis() + calculateMoveInterval();
      
      request->send(200, "application/json", "{\"status\":\"success\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    }
  });
  
  // API endpoint for touchpad mouse clicks
  server->on("/api/touchpad/click", HTTP_POST, [](AsyncWebServerRequest *request) {
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
    
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (!error) {
      String button = doc["button"].as<String>();
      String clickType = doc["clickType"].as<String>();
      
      if (button == "left") {
        if (clickType == "double") {
          // Double click (press-release-press-release)
          DEBUG("Mouse double-click");
          Mouse.press(MOUSE_LEFT);
          delay(8);
          Mouse.release(MOUSE_LEFT);
          delay(8);
          Mouse.press(MOUSE_LEFT);
          delay(8);
          Mouse.release(MOUSE_LEFT);
        } else {
          // Normal click (press and release)
          DEBUG("Mouse single-click");
          Mouse.press(MOUSE_LEFT);
          delay(8);
          Mouse.release(MOUSE_LEFT);
        }
      } else if (button == "right") {
        // Right click (press and release)
        DEBUG("Mouse right-click");
        Mouse.press(MOUSE_RIGHT);
        delay(8);
        Mouse.release(MOUSE_RIGHT);
      }
      
      // Update last move time
      last_move_time = millis();
      next_move_time = millis() + calculateMoveInterval();
      
      request->send(200, "application/json", "{\"status\":\"success\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    }
  });
  
  // API endpoint for touchpad button state (pressed/released)
  server->on("/api/touchpad/button", HTTP_POST, [](AsyncWebServerRequest *request) {
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
    
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (!error) {
      String button = doc["button"].as<String>();
      String state = doc["state"].as<String>();
      
      if (button == "left") {
        if (state == "press") {
          // Press left button without releasing
          DEBUG("Mouse left button pressed");
          Mouse.press(MOUSE_LEFT);
        } else if (state == "release") {
          // Release left button
          DEBUG("Mouse left button released");
          Mouse.release(MOUSE_LEFT);
        }
      } else if (button == "right") {
        if (state == "press") {
          // Press right button without releasing
          DEBUG("Mouse right button pressed");
          Mouse.press(MOUSE_RIGHT);
        } else if (state == "release") {
          // Release right button
          DEBUG("Mouse right button released");
          Mouse.release(MOUSE_RIGHT);
        }
      }
      
      // Update last move time
      last_move_time = millis();
      next_move_time = millis() + calculateMoveInterval();
      
      request->send(200, "application/json", "{\"status\":\"success\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    }
  });
  
  // API endpoint for touchpad mouse scroll
  server->on("/api/touchpad/scroll", HTTP_POST, [](AsyncWebServerRequest *request) {
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
    
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (!error) {
      int amount = doc["amount"].as<int>();
      
      // Further amplify scroll amount for large screens
      int scrollMultiplier = 10; // Additional server-side multiplier (increased from 5 to 10)
      int scaledAmount = amount * scrollMultiplier;
      
      // Scroll the mouse wheel (positive = down, negative = up)
      Mouse.move(0, 0, scaledAmount);
      
      // Update last move time
      last_move_time = millis();
      next_move_time = millis() + calculateMoveInterval();
      
      request->send(200, "application/json", "{\"status\":\"success\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    }
  });
  
  // Start server
  server->begin();
  DEBUG("Web server started");
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
  // For tracking the overall displacement from the starting position
  static int totalDisplacementX = 0;
  static int totalDisplacementY = 0;
  
  if (movement_trail) {
    // Create a movement trail with multiple movements
    int trailMovementX = 0;
    int trailMovementY = 0;
    
    // Perform multiple movements to create a trail
    for (int i = 0; i < 3; i++) {
      // Store current displacement before this movement
      int beforeX = totalDisplacementX;
      int beforeY = totalDisplacementY;
      
      // Capture the current values of movement_size
      int originalMovementSize = movement_size;
      
      // Adjust for this iteration of the trail (making each step smaller)
      movement_size = movement_size / 2;
      
      // Perform the movement based on pattern
      if (strcmp(movement_pattern, "circular") == 0) {
        moveMouseCircular();
      } else if (strcmp(movement_pattern, "rectangle") == 0) {
        moveMouseRectangle();
      } else if (strcmp(movement_pattern, "triangle") == 0) {
        moveMouseTriangle();
      } else if (strcmp(movement_pattern, "zigzag") == 0) {
        moveMouseZigzag();
      } else {
        // Default to linear
        moveMouseLinear();
      }
      
      // Restore original movement values
      movement_size = originalMovementSize;
      
      // Track any change in displacement that didn't perfectly return to start
      trailMovementX += (totalDisplacementX - beforeX);
      trailMovementY += (totalDisplacementY - beforeY);
      
      // Ensure cursor returns to initial position after each movement
      resetCursorPosition();
      
      delay(100); // Brief pause between trail movements
    }
  } else {
    // Single movement based on selected pattern
    if (strcmp(movement_pattern, "circular") == 0) {
      moveMouseCircular();
    } else if (strcmp(movement_pattern, "rectangle") == 0) {
      moveMouseRectangle();
    } else if (strcmp(movement_pattern, "triangle") == 0) {
      moveMouseTriangle();
    } else if (strcmp(movement_pattern, "zigzag") == 0) {
      moveMouseZigzag();
    } else {
      // Default to linear
      moveMouseLinear();
    }
    
    // Always reset cursor position after movement
    resetCursorPosition();
  }
  
  // Add a delay based on the movement_speed setting
  delay(movement_speed);
}

// Save sessions to flash
void saveSessions() {
  DEBUG("Saving sessions");
  
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
      DEBUG("Failed to write sessions");
    } else {
      DEBUG("Sessions saved successfully");
    }
    file.close();
  } else {
    DEBUG("Failed to open sessions file for writing");
  }
}

// Load sessions from flash
void loadSessions() {
  DEBUG("Loading sessions");
  
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
        
        DEBUGF("Loaded %d sessions", i);
        
        // Debug: list all active sessions
        for (int j = 0; j < MAX_SESSIONS; j++) {
          if (sessions[j].active) {
            DEBUGF("Active session: id=%s, expiry=%lu, current=%lu, diff=%ld", 
                          sessions[j].id.c_str(), sessions[j].expiry, millis(), 
                          (long)(sessions[j].expiry - millis()));
          }
        }
      } else {
        DEBUG("Failed to deserialize sessions");
      }
      
      file.close();
    }
  } else {
    DEBUG("Sessions file doesn't exist");
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

// Reset cursor to initial position
void resetCursorPosition() {
  if (totalDisplacementX != 0 || totalDisplacementY != 0) {
    // Move back to the original position
    Mouse.move(-totalDisplacementX, -totalDisplacementY);
    DEBUGF("Reset cursor to initial position: (%d, %d)", -totalDisplacementX, -totalDisplacementY);
    
    // Reset displacement tracking
    totalDisplacementX = 0;
    totalDisplacementY = 0;
  }
}