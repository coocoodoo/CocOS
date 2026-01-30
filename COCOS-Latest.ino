/**
 * M5Stack Cardputer Terminal & COC UI System
 * Retro Pixel Art UI Edition v4.5.0
 * 
 * v4.5.0 Changes:
 * - Added: WiFi Chat - Local chat with other Cardputers on same network
 * - Chat uses UDP broadcast for peer-to-peer messaging
 * - Web Server moved to terminal command only (type 'sdshare on/off') <- It was working, until I kept adding features. Low on Memory?
 * 
 * v4.4.0 Changes:
 * - Added: MP3 file playback in File Browser (ESP32-audioI2S)
 * - Added: Bluetooth Audio menu (UI only - ESP32-S3 lacks Classic BT) <- 2nd Attempt, I fail.. *Sad Face*
 * - MP3 player shows time, progress bar, volume control
 * 
 * v4.3.1 Changes:
 * - Fixed: Audio system conflict between WAV playback and mic recorder
 * - Added: Global volume control (Fn+[ down, Fn+] up)
 * - Added: Terminal "sound" command (-m mute, -v <0-100> volume)
 * - Improved: Voice Recorder UI with centered design and better VU meter
 * 
 * COC Menu System (type 'cocui'):
 * - File Browser: Browse SD, play WAV/MP3 files (Enter to play, MP3 Doesnt Work On Old Cardputer!!)
 * - WiFi Chat: Chat with other Cardputers on local network
 * - Mic Recorder: Record audio to SD card
 * - BT Audio: Bluetooth speaker pairing (not available on ESP32-S3, Cant Figure this out! *Sad Face* )
 * - WiFi: Scan, connect, save credentials
 * - Settings: Brightness, Sound, Screen Timeout, Timezone
 * - Games: Tetris, Breakout, Synth
 * 
 * Terminal commands for Web Server:
 * - sdshare on: Start SD card web server (requires WiFi)
 * - sdshare off: Stop SD card web server
 * - sdshare ap: Start AP mode with SD sharing
 * 
 * Required Libraries: 
 * - NimBLE-Arduino
 * - ESP32-audioI2S by schreibfaul1 (for MP3 playback) <-- :(
 * - Lost Track...
 *
 * Note: ESP32-S3 (Cardputer) does NOT support Classic Bluetooth/A2DP
 * 
 * AP Default: SSID=M5SDSHARE, Pass=m5cardputer
 * 
 * Config File (/usrconfig.cfg):
 * - terminal_hide, screen_off, brightness, timezone, sound, volume
 * - ap_ssid, ap_password (hotspot settings)
 * - wifi_ssid, wifi_password (saved WiFi) <-- Buggy
 */

#include <M5Cardputer.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <time.h>
#include <vector>
#include <algorithm>
#include <esp_system.h>
#include <esp_sleep.h>
#include <NimBLEDevice.h>

// USB Mass Storage - requires ESP32 Arduino Core 2.x with native USB support
// Set Tools > USB Mode: "USB-OTG (TinyUSB)" in Arduino IDE
#define ENABLE_USB_MSC
#ifdef ENABLE_USB_MSC
#include "USB.h"
#include "USBMSC.h"
#endif

// Optional: Uncomment if you have these libraries installed
#define ENABLE_IR_FEATURES
// #define ENABLE_BT_KEYBOARD
// #define ENABLE_BT_AUDIO  // NOTE: Requires Classic BT - NOT supported on ESP32-S3 (Cardputer)
#define ENABLE_MP3  // Requires ESP32-audioI2S library by schreibfaul1
#define ENABLE_SID  // Requires arduino-audio-tools + SIDPlayer libraries

#ifdef ENABLE_IR_FEATURES
#include <IRremote.hpp>
#endif

#ifdef ENABLE_BT_KEYBOARD
#include <BleKeyboard.h>
#endif

#ifdef ENABLE_BT_AUDIO
#include "BluetoothA2DPSource.h"
#endif

#ifdef ENABLE_MP3
#include "Audio.h"
#endif

#ifdef ENABLE_SID
#include "AudioTools.h"
#include "SIDStream.h"
#endif

// Smooth UI animations - provides rubber-banding menu selection
#define ENABLE_SMOOTH_UI
#ifdef ENABLE_SMOOTH_UI
#include <smooth_ui_toolkit.hpp> //Please make sure you use .hpp 
#endif

// Mic recorder icons (Mic1 = red, Mic2 = green) I will include the python converter to make sprites. Im proud of that one!
#include "micr.h"
#include "micg.h"

// --- Cardputer Key Definitions ---
#define KEY_BTN_UP      ';' 
#define KEY_BTN_DOWN    '.'
#define KEY_BTN_LEFT    ','
#define KEY_BTN_RIGHT   '/'
#define KEY_BTN_ESC     '`'
#define KEY_BTN_TAB     '\t'

// Fn key combinations
#define KEY_FN_2        '@'
#define KEY_FN_C        '['
#define KEY_FN_V        ']'
#define KEY_FN_M        ':'
#define KEY_FN_S        '!'
#define KEY_FN_T        '%'  // Fn+T to open terminal
#define KEY_FN_P        '='  // Fn+P to take screenshot

// --- Retro Color Palette (RGB565) ---
#define COL_YELLOW_BG     0xFFE0
#define COL_BLACK         0x0000
#define COL_WHITE         0xFFFF
#define COL_GRAY_LIGHT    0xD69A
#define COL_GRAY_MID      0xAD55
#define COL_GRAY_DARK     0x6B4D
#define COL_SELECTION     0x83A2  // Olive #827717
#define COL_TITLE_BAR     0x6B4D
#define COL_RED           0xF800
#define COL_GREEN         0x07E0
#define COL_BLUE          0x001F

// --- Menu Theme Colors (from spectral palette) ---
#define COL_THEME_0       0xB00B  // Dark magenta - File Browser
#define COL_THEME_1       0xDA28  // Red/coral - WiFi Chat
#define COL_THEME_2       0xEB45  // Orange - Mic Recorder
#define COL_THEME_3       0xFD4A  // Peach - BT Audio
#define COL_THEME_4       0xFE86  // Yellow - WiFi
#define COL_THEME_5       0xA6A5  // Lime green - USB Transfer
#define COL_THEME_6       0xB6D3  // Light green - Settings
#define COL_THEME_7       0x7E34  // Teal - Games
#define COL_THEME_8       0x3CD8  // Blue - Exit

// Fish-style terminal colors
#define TERM_BG_COLOR     0x2104    // Dark warm brown/maroon
#define TERM_TEXT_COLOR   0xFFFF    // White for commands
#define CURSOR_COLOR      0xFFFF    // White cursor
#define COL_CYAN          0x07FF    // Cyan for user/git
#define COL_MAGENTA       0xF81F    // Magenta for hostname
#define COL_YELLOW        0xFFE0    // Yellow for path/time
#define COL_ORANGE        0xFD20    // Orange for errors
#define COL_FISH_GREEN    0x87F0    // Light green for success 

// --- Configuration ---
#define FONT_SIZE         1    
#define MAX_HISTORY       20     
#define CURSOR_BLINK_MS   500    
#define WEB_SERVER_PORT   80

// --- Enums & Structs ---
enum AppMode { 
    MODE_TERMINAL, 
    MODE_FILE_UI, 
    MODE_DIALOG_YESNO,
    MODE_DIALOG_INPUT,
    MODE_TEXT_EDITOR,
    MODE_DIALOG_SAVE,
    MODE_QR_DISPLAY,
    MODE_HEX_VIEW,
    MODE_SERIAL_MONITOR,
    MODE_BT_KEYBOARD,
    MODE_SYNTH,
    MODE_COC_MENU,
    MODE_COC_WEBSERVER,
    MODE_COC_WIFI,
    MODE_COC_USB,
    MODE_COC_SETTINGS,
    MODE_COC_GAMES,
    MODE_GAME_TETRIS,
    MODE_GAME_BREAKOUT,
    MODE_BT_SEND,
    MODE_MIC_RECORDER,
    MODE_BT_AUDIO,
    MODE_COC_CHAT,
    MODE_ROKU_REMOTE
};

enum ClipboardMode { CLIP_NONE = 0, CLIP_COPY = 1, CLIP_MOVE = 2 };
enum DialogAction { ACTION_NONE = 0, ACTION_DELETE = 1, ACTION_RENAME = 2, ACTION_SAVE_AND_EXIT = 3, ACTION_SAVE_NEW_FILE = 4 };

struct FileInfo {
    String name;
    bool isDir;
    int size;
};

// --- Globals ---
M5Canvas sprite(&M5Cardputer.Display); 
AppMode currentMode = MODE_TERMINAL;
AppMode returnMode = MODE_TERMINAL;

// Terminal State
String currentPath = "/";       
String inputBuffer = "";        
std::vector<String> terminalHistory; 
bool cursorVisible = true;
unsigned long lastBlinkTime = 0;
bool needsRedraw = true;

// File UI State
std::vector<FileInfo> fileList;
int uiSelectedIndex = 0;
int uiScrollOffset = 0;
const int uiItemHeight = 18;

// Pagination settings for file browser
#define FILES_PER_PAGE 100
int fileCurrentPage = 0;
int fileTotalPages = 1;
int fileTotalCount = 0;
const int titleBarHeight = 14;

// Clipboard State
String clipboardPath = "";
ClipboardMode clipboardMode = CLIP_NONE;

// Dialog State
DialogAction pendingAction = ACTION_NONE;
String dialogMessage = "";
String dialogInput = "";
String targetFilePath = "";
bool dialogYesSelected = true;

// Text Editor State
std::vector<String> editorLines;
String editorFilePath = "";
String editorFileName = "untitled.txt";
int editorCursorX = 0;
int editorCursorY = 0;
int editorScrollY = 0;
bool editorDirty = false;
bool editorCursorVisible = true;
unsigned long editorLastBlink = 0;

// Command History State
std::vector<String> cmdHistory;
int cmdHistoryIndex = -1;
String cmdHistoryFile = "/.cmdhistory.log";
String savedInputBuffer = "";
const int MAX_CMD_HISTORY = 50;

// Battery State
unsigned long lastBatteryUpdate = 0;
int batteryLevel = 100;
bool batteryCharging = false;
int chargingAnimFrame = 0;
unsigned long lastChargingAnim = 0;

// Command execution timing
unsigned long cmdStartTime = 0;
unsigned long lastCmdDuration = 0;

// Network traffic visualization
#define TRAFFIC_HISTORY_SIZE 30  // Number of data points to show (30 seconds of history)
int trafficIn[TRAFFIC_HISTORY_SIZE];   // Incoming bytes history
int trafficOut[TRAFFIC_HISTORY_SIZE];  // Outgoing bytes history
int trafficIdx = 0;
unsigned long lastTrafficUpdate = 0;
unsigned long totalBytesIn = 0;
unsigned long totalBytesOut = 0;
unsigned long lastBytesIn = 0;
unsigned long lastBytesOut = 0;

// Web Server State
WebServer *webServer = nullptr;
bool sdShareEnabled = false;
String wifiSSID = "";
String wifiPassword = "";

// WiFi Chat State
#define CHAT_UDP_PORT 4210
#define CHAT_MAX_MESSAGES 20
#define CHAT_MAX_USERS 4
#define CHAT_NUM_ROOMS 4
#define CHAT_AP_PREFIX "M5Chat_"
WiFiUDP chatUdp;
bool chatActive = false;
bool chatInRoom = false;           // True when in a chat room, false when selecting room
int chatSelectedRoom = 0;          // 0-3 for 2x2 grid selection
int chatCurrentRoom = -1;          // -1 = no room, 0-3 = room number
String chatNickname = "User";
String chatInputBuffer = "";
std::vector<String> chatMessages;
int chatScrollOffset = 0;
unsigned long lastChatCheck = 0;
unsigned long lastRoomBroadcast = 0;
int chatRoomUsers[CHAT_NUM_ROOMS] = {0, 0, 0, 0};  // User count per room
smooth_ui_toolkit::AnimateValue chatSelectorX = 0;
smooth_ui_toolkit::AnimateValue chatSelectorY = 0;
smooth_ui_toolkit::AnimateValue chatModeSelectorY = 0;  // For mode selection screen

// Direct Connect (Ad-hoc) Chat State
enum ChatConnectMode { CHAT_MODE_SELECT, CHAT_MODE_WIFI, CHAT_MODE_HOST, CHAT_MODE_JOIN, CHAT_MODE_SCANNING };
ChatConnectMode chatConnectMode = CHAT_MODE_SELECT;
bool chatIsHost = false;           // True if hosting AP
bool chatDirectMode = false;       // True if in direct connect mode
int chatModeSelection = 0;         // 0=WiFi, 1=Host, 2=Join for mode select screen
std::vector<String> chatFoundAPs;  // Found chat APs when scanning
int chatAPSelection = 0;           // Selected AP in join list
unsigned long chatScanStart = 0;   // When scan started

// Screen Timeout State
String configFile = "/usrconfig.cfg";
unsigned long terminalHideTimeoutMs = 10000;   // Default 10 seconds - hide terminal, show plots
unsigned long screenOffTimeoutMs = 300000;     // Default 5 minutes (300 seconds) - turn off screen
unsigned long lastActivityTime = 0;
bool terminalHidden = false;
bool screenOff = false;
int configBrightness = 100;  // Configured brightness level (1-100)

// Time/Date State
int configTimezoneOffset = 0;  // GMT offset in hours (-12 to +14)
bool timeSynced = false;
const char* ntpServer = "pool.ntp.org";

// Hex Viewer State
String hexViewFilePath = "";
int hexViewOffset = 0;
int hexViewFileSize = 0;

// Serial Monitor State
int serialBaudRate = 115200;
std::vector<String> serialBuffer;
bool serialMonitorActive = false;

// BT Keyboard State
#ifdef ENABLE_BT_KEYBOARD
BleKeyboard bleKeyboard("M5Cardputer", "M5Stack", 100);
#endif
bool btKeyboardActive = false;

// IR State
#ifdef ENABLE_IR_FEATURES
const int IR_SEND_PIN = 44;  // Cardputer IR pin
const int IR_RECV_PIN = 46;

// TCL Roku TV IR Codes (NEC Protocol, 32-bit) My secret Remote (enter "roku" in Terminal). My wife loved this one! Planning to do Sony TVs to turn them off at work... 
// Device address: 0x57E3
const uint32_t ROKU_HOME       = 0x57E3C03F;
const uint32_t ROKU_BACK       = 0x57E36699;
const uint32_t ROKU_OK         = 0x57E354AB;
const uint32_t ROKU_UP         = 0x57E39867;
const uint32_t ROKU_DOWN       = 0x57E3CC33;
const uint32_t ROKU_LEFT       = 0x57E37887;
const uint32_t ROKU_RIGHT      = 0x57E3B44B;
const uint32_t ROKU_REWIND     = 0x57E32CD3;
const uint32_t ROKU_FORWARD    = 0x57E3AA55;
const uint32_t ROKU_PLAY_PAUSE = 0x57E31EE1;
const uint32_t ROKU_REPLAY     = 0x57E3E01F;
const uint32_t ROKU_OPTIONS    = 0x57E300FF;
const uint32_t ROKU_POWER      = 0x57E3E817;
const uint32_t ROKU_MUTE       = 0x57E304FB;
const uint32_t ROKU_VOL_UP     = 0x57E3F00F;
const uint32_t ROKU_VOL_DOWN   = 0x57E308F7;

// Roku remote state
int rokuSelectedBtn = 2;  // Start with OK button selected (center)
#endif

// Weather API (free tier: wttr.in requires no API key)
String weatherLocation = "";

// Uptime tracking
unsigned long bootTime = 0;

// ==========================================
//     Grid Synth State (PixiTracker-style)
// ==========================================
#define SYNTH_ROWS 8        // Pitch rows (high to low)
#define SYNTH_COLS 16       // Time steps (left to right)

// Grid cells: 0=empty, 1-8=note colors
uint8_t synthGrid[SYNTH_ROWS][SYNTH_COLS];
int synthCursorX = 0;       // Cursor column (time)
int synthCursorY = 0;       // Cursor row (pitch)
int synthPlayCol = 0;       // Playhead position
bool synthPlaying = false;
int synthBPM = 120;
unsigned long synthLastTick = 0;
String synthFileName = "beat.syx";
int synthCurrentColor = 1;  // Current paint color (1-8)
bool synthMenuOpen = false; // Sound menu state
int synthMenuCursor = 0;    // Selected color in menu (0-7)

// Sound types for each color (0-7)
// 0=Square, 1=Bass, 2=Lead, 3=Pluck, 4=Kick, 5=Snare, 6=HiHat, 7=Bell
uint8_t synthColorSound[8] = {0, 1, 2, 3, 4, 5, 6, 7};

// Sound type names
const char* synthSoundNames[] = {
    "Square", "Bass", "Lead", "Pluck", 
    "Kick", "Snare", "HiHat", "Bell"
};

// Sound modifiers: frequency multiplier and duration
const float synthSoundFreqMult[8] = {
    1.0,    // Square: normal
    0.5,    // Bass: octave down
    2.0,    // Lead: octave up
    1.5,    // Pluck: fifth up
    0.25,   // Kick: very low
    4.0,    // Snare: high noise-ish
    8.0,    // HiHat: very high
    3.0     // Bell: high overtone
};

const int synthSoundDuration[8] = {
    0,      // Square: sustain (0 = continuous)
    80,     // Bass: medium
    0,      // Lead: sustain
    50,     // Pluck: short
    60,     // Kick: medium-short
    30,     // Snare: short
    20,     // HiHat: very short
    100     // Bell: longer decay
};

// Note frequencies for each row (high C to low C, pentatonic-friendly)
const float synthRowFreq[SYNTH_ROWS] = {
    1046.50,  // C6
    880.00,   // A5
    783.99,   // G5
    659.25,   // E5
    523.25,   // C5
    440.00,   // A4
    392.00,   // G4
    329.63    // E4
};

// Rainbow colors for grid cells (like PixiTracker)
const uint16_t synthColors[9] = {
    0x2104,  // 0: Empty (dark gray)
    0xF800,  // 1: Red
    0xFBE0,  // 2: Orange  
    0xFFE0,  // 3: Yellow
    0x07E0,  // 4: Green
    0x07FF,  // 5: Cyan
    0x001F,  // 6: Blue
    0xF81F,  // 7: Magenta
    0xFFFF   // 8: White
};

// ==========================================
//     COC UI State (Main Menu System)
// ==========================================
int cocMenuIndex = 0;           // Main menu selection
int cocMenuScroll = 0;          // Menu scroll offset
const int COC_MENU_VISIBLE = 5; // Number of visible menu items
int cocSubMenuIndex = 0;        // Submenu selection
int cocWifiListIndex = 0;       // WiFi network list selection
int cocGameMenuIndex = 0;       // Games menu selection

// Smooth UI animated selection (rubber-banding effect)
#ifdef ENABLE_SMOOTH_UI
smooth_ui_toolkit::AnimateValue cocMenuSelectorY = 0;
smooth_ui_toolkit::AnimateValue cocMenuSelectorW = 100;
smooth_ui_toolkit::AnimateValue fileUiSelectorY = 0;
smooth_ui_toolkit::AnimateValue fileUiSelectorW = 100;
smooth_ui_toolkit::AnimateValue settingsSelectorY = 0;
smooth_ui_toolkit::AnimateValue settingsSelectorW = 100;
smooth_ui_toolkit::AnimateValue webServerSelectorY = 0;
smooth_ui_toolkit::AnimateValue webServerSelectorW = 100;
smooth_ui_toolkit::AnimateValue wifiSelectorY = 0;
smooth_ui_toolkit::AnimateValue wifiSelectorW = 100;
smooth_ui_toolkit::AnimateValue btAudioSelectorY = 0;
smooth_ui_toolkit::AnimateValue btAudioSelectorW = 100;
smooth_ui_toolkit::AnimateValue gamesSelectorY = 0;
smooth_ui_toolkit::AnimateValue gamesSelectorW = 100;
bool smoothUiInitialized = false;
#endif
int cocSettingsIndex = 0;       // Settings menu selection
int cocSettingsScroll = 0;      // Settings scroll offset

// COC Menu items - easily expandable
const char* cocMenuItems[] = {
    "File Browser",
    "WiFi Chat",
    "Mic Recorder",
    "BT Audio",
    "WiFi",
    "USB Transfer",
    "Settings",
    "Games",
    "Exit"
};
const int COC_MENU_COUNT = 9;  // Update this when adding items

// Menu theme colors - one per menu item
const uint16_t cocMenuColors[] = {
    COL_THEME_0,  // File Browser - Dark magenta
    COL_THEME_1,  // WiFi Chat - Red/coral
    COL_THEME_2,  // Mic Recorder - Orange
    COL_THEME_3,  // BT Audio - Peach
    COL_THEME_4,  // WiFi - Yellow
    COL_THEME_5,  // USB Transfer - Lime green
    COL_THEME_6,  // Settings - Light green
    COL_THEME_7,  // Games - Teal
    COL_THEME_8   // Exit - Blue
};
uint16_t currentThemeColor = COL_THEME_0;  // Current theme color for submenus

// Settings items - easily expandable
const char* cocSettingsItems[] = {
    "Brightness",
    "Sound",
    "Screen Timeout",
    "Timezone",
    "Nickname",
    "SID Duration",
    "Save & Exit"
};
const int COC_SETTINGS_COUNT = 7;
const int COC_SETTINGS_VISIBLE = 4;
bool nicknameEditMode = false;  // Track if we're editing the nickname

// Settings (use existing config variables where possible)
// settingBrightness uses configBrightness (converted to 0-255)
// settingScreenTimeout uses screenOffTimeoutMs (converted to seconds)
bool settingSoundEnabled = true;
int configSidDuration = 180;  // SID song duration in seconds (default 3 minutes)
int globalVolume = 80;           // Global volume 0-100
bool globalMuted = false;        // Mute state

// USB Transfer mode
bool usbModeActive = false;

// Mic Recorder State
bool micRecording = false;
String micRecordingFile = "";
unsigned long micRecordStartTime = 0;
unsigned long micRecordDuration = 0;
int micRecordingCount = 0;
#define MIC_SAMPLE_RATE 16000

// Bluetooth Audio State
#ifdef ENABLE_BT_AUDIO
BluetoothA2DPSource *a2dpSource = nullptr;
#endif
bool btAudioEnabled = false;
bool btAudioConnected = false;
bool btAudioScanning = false;
String btAudioDeviceName = "";
String btAudioDeviceAddr = "";
int btAudioScanIndex = 0;
std::vector<String> btAudioDevices;
std::vector<String> btAudioAddresses;

// Audio playback state for BT streaming
bool audioStreamingToBT = false;
File audioStreamFile;
uint32_t audioStreamSampleRate = 44100;
uint8_t audioStreamChannels = 2;
uint8_t audioStreamBits = 16;

// MP3 Playback (ESP32-audioI2S)
#ifdef ENABLE_MP3
// M5Cardputer I2S pins for speaker
#define I2S_BCLK      41
#define I2S_LRC       43
#define I2S_DOUT      42
Audio *mp3Audio = nullptr;
bool mp3Playing = false;
String mp3PlayingFile = "";
unsigned long mp3LastUpdate = 0;
#endif

#define MIC_BUFFER_SIZE 512
int16_t micBuffer[MIC_BUFFER_SIZE];
File micWavFile;
uint32_t micDataSize = 0;
int micVuLevel = 0;           // Current VU meter level (0-100)
int micVuPeak = 0;            // Peak hold for VU meter
unsigned long micLastVuUpdate = 0;

#ifdef ENABLE_USB_MSC
USBMSC msc;
static uint32_t usbSdSectorCount = 0;

/*
 * USB Mass Storage for SD Card
 * 
 * Uses SD.readRAW() and SD.writeRAW() for raw sector access.
 * bufsize can be >512 bytes, so we loop through sectors.
 * 
 * Requirements:
 * - ESP32 Arduino Core 2.x or later
 * - Tools > USB Mode: "USB-OTG (TinyUSB)"
 */

// USB MSC read callback - reads raw sectors from SD
static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    (void)offset;
    uint32_t sectorCount = bufsize / 512;
    uint8_t* buf = (uint8_t*)buffer;
    
    for (uint32_t i = 0; i < sectorCount; i++) {
        if (!SD.readRAW(buf + (i * 512), lba + i)) {
            return -1;
        }
    }
    return bufsize;
}

// USB MSC write callback - writes raw sectors to SD
static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    (void)offset;
    uint32_t sectorCount = bufsize / 512;
    
    for (uint32_t i = 0; i < sectorCount; i++) {
        if (!SD.writeRAW(buffer + (i * 512), lba + i)) {
            return -1;
        }
    }
    return bufsize;
}

// USB MSC start/stop (eject) callback
static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    if (load_eject && !start) {
        // User ejected the drive safely
        usbModeActive = false;
    }
    return true;
}

void startUSBMSC() {
    if (usbModeActive) return;
    
    // Get sector count
    usbSdSectorCount = SD.numSectors();
    if (usbSdSectorCount == 0) {
        // Fallback calculation
        usbSdSectorCount = SD.cardSize() / 512;
    }
    if (usbSdSectorCount == 0) return;
    
    // Configure USB MSC device
    msc.vendorID("M5Stack");
    msc.productID("Cardputer");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    msc.mediaPresent(true);
    msc.begin(usbSdSectorCount, 512);
    
    // Start USB
    USB.begin();
    usbModeActive = true;
}

void stopUSBMSC() {
    if (!usbModeActive) return;
    msc.end();
    usbModeActive = false;
}
#else
// Stub functions when USB MSC is disabled
void startUSBMSC() { usbModeActive = true; }
void stopUSBMSC() { usbModeActive = false; }
#endif

// ==========================================
//     Screenshot Function (Fn+P)
// ==========================================

String getTimestampFilename(const char* prefix, const char* ext) {
    // Get current time or use millis if no RTC
    unsigned long ms = millis();
    unsigned long sec = ms / 1000;
    unsigned long min = sec / 60;
    unsigned long hr = min / 60;
    
    // Format: PREFIX_HHMMSSmmm.ext
    char buf[32];
    snprintf(buf, sizeof(buf), "%s_%02lu%02lu%02lu%03lu.%s", 
             prefix, hr % 24, min % 60, sec % 60, ms % 1000, ext);
    return String(buf);
}

bool takeScreenshot() {
    // Create Screenshots folder if needed
    if (!SD.exists("/Screenshots")) {
        SD.mkdir("/Screenshots");
    }
    
    String filename = "/Screenshots/" + getTimestampFilename("SCR", "bmp");
    
    int w = M5Cardputer.Display.width();
    int h = M5Cardputer.Display.height();
    
    File file = SD.open(filename, FILE_WRITE);
    if (!file) return false;
    
    // BMP Header (54 bytes)
    uint32_t fileSize = 54 + (w * h * 3);
    uint32_t dataOffset = 54;
    uint32_t headerSize = 40;
    uint16_t planes = 1;
    uint16_t bpp = 24;
    uint32_t compression = 0;
    uint32_t imageSize = w * h * 3;
    
    // File header (14 bytes)
    file.write('B'); file.write('M');
    file.write((uint8_t*)&fileSize, 4);
    uint32_t reserved = 0;
    file.write((uint8_t*)&reserved, 4);
    file.write((uint8_t*)&dataOffset, 4);
    
    // Info header (40 bytes)
    file.write((uint8_t*)&headerSize, 4);
    file.write((uint8_t*)&w, 4);
    int32_t hNeg = -h;  // Negative for top-down
    file.write((uint8_t*)&hNeg, 4);
    file.write((uint8_t*)&planes, 2);
    file.write((uint8_t*)&bpp, 2);
    file.write((uint8_t*)&compression, 4);
    file.write((uint8_t*)&imageSize, 4);
    uint32_t ppm = 2835;  // 72 DPI
    file.write((uint8_t*)&ppm, 4);
    file.write((uint8_t*)&ppm, 4);
    uint32_t colors = 0;
    file.write((uint8_t*)&colors, 4);
    file.write((uint8_t*)&colors, 4);
    
    // Pixel data (RGB, top-down)
    uint8_t lineBuf[720];  // Max 240 pixels * 3 bytes
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t color = M5Cardputer.Display.readPixel(x, y);
            // Convert RGB565 to RGB888
            uint8_t r = ((color >> 11) & 0x1F) << 3;
            uint8_t g = ((color >> 5) & 0x3F) << 2;
            uint8_t b = (color & 0x1F) << 3;
            lineBuf[x * 3] = b;      // BMP is BGR
            lineBuf[x * 3 + 1] = g;
            lineBuf[x * 3 + 2] = r;
        }
        // Pad row to 4-byte boundary
        int padding = (4 - (w * 3) % 4) % 4;
        file.write(lineBuf, w * 3);
        for (int p = 0; p < padding; p++) file.write((uint8_t)0);
    }
    
    file.close();
    
    // Flash screen white briefly as feedback
    M5Cardputer.Display.fillScreen(TFT_WHITE);
    delay(50);
    
    return true;
}

// WebServer settings
bool cocApModeEnabled = false;  // AP mode for SD share
String cocApSSID = "M5SDSHARE";
String cocApPassword = "m5cardputer";

// WiFi saved credentials
String cocSavedWifiSSID = "";
String cocSavedWifiPass = "";

// BLE File Transfer
#define BLE_SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_UUID           "beb5483e-36e1-4688-b7f5-ea07361b26a8"

NimBLEServer *pServer = nullptr;
NimBLECharacteristic *pCharacteristic = nullptr;
bool cocBtInitialized = false;
bool cocBtDeviceConnected = false;
String cocBtDeviceName = "M5Cardputer";
String cocBtSendFile = "";
int cocBtSendProgress = 0;
bool cocBtSending = false;

// WiFi Direct File Transfer
bool wifiDirectEnabled = false;
bool wifiDirectSending = false;
String wifiDirectFilePath = "";
bool wifiDirectClientConnected = false;
WebServer *wifiDirectServer = nullptr;
String wifiDirectAPSSID = "M5Share";
String wifiDirectAPPass = "12345678";

// BLE Server Callbacks
class MyServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        cocBtDeviceConnected = true;
    }
    void onDisconnect(NimBLEServer* pServer) {
        cocBtDeviceConnected = false;
        // Restart advertising when disconnected
        NimBLEDevice::startAdvertising();
    }
};

// Helper function for menu sounds
void playMenuSound(int freq = 1000, int duration = 30) {
    if (settingSoundEnabled && !globalMuted && globalVolume > 0) {
        M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
        M5Cardputer.Speaker.tone(freq, duration);
    }
}

void playClickSound() {
    playMenuSound(1200, 20);
}

void playSelectSound() {
    playMenuSound(1000, 40);
}

void playBackSound() {
    playMenuSound(800, 30);
}

// Apply global volume setting to speaker
void applyVolume() {
    if (globalMuted || globalVolume == 0) {
        M5Cardputer.Speaker.setVolume(0);
    } else {
        M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
    }
}

// Reset audio system (call after WAV playback or mic recording)
void resetAudioSystem() {
    M5Cardputer.Speaker.stop();
    M5Cardputer.Speaker.end();
    M5Cardputer.Mic.end();
    delay(50);
    M5Cardputer.Speaker.begin();
    applyVolume();
}

// Show volume overlay briefly
// Volume overlay timer
unsigned long volumeOverlayShownTime = 0;
bool volumeOverlayVisible = false;

void showVolumeOverlay() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    
    // Draw overlay in corner
    int ovX = screenW - 70, ovY = 5, ovW = 65, ovH = 22;
    sprite.fillRect(ovX, ovY, ovW, ovH, COL_BLACK);
    sprite.drawRect(ovX, ovY, ovW, ovH, COL_WHITE);
    
    sprite.setTextColor(COL_WHITE, COL_BLACK);
    sprite.setCursor(ovX + 4, ovY + 4);
    if (globalMuted) {
        sprite.print("MUTED");
    } else {
        sprite.print("Vol:" + String(globalVolume) + "%");
    }
    
    // Volume bar
    int barX = ovX + 4, barY = ovY + 14, barW = ovW - 8, barH = 5;
    sprite.fillRect(barX, barY, barW, barH, COL_GRAY_DARK);
    if (!globalMuted) {
        int fillW = (barW * globalVolume) / 100;
        sprite.fillRect(barX, barY, fillW, barH, COL_GREEN);
    }
    
    sprite.pushSprite(0, 0);
    
    // Start timer
    volumeOverlayShownTime = millis();
    volumeOverlayVisible = true;
}

void hideVolumeOverlay() {
    if (!volumeOverlayVisible) return;
    volumeOverlayVisible = false;
    needsRedraw = true;  // Will trigger redraw on next loop iteration
}

// WiFi scan results
struct WifiNetwork {
    String ssid;
    int rssi;
    bool encrypted;
};
std::vector<WifiNetwork> cocWifiNetworks;
bool cocWifiScanning = false;
String cocWifiInputPass = "";
bool cocWifiEnteringPass = false;

// Tetris state
#define TETRIS_WIDTH 10
#define TETRIS_HEIGHT 20
uint8_t tetrisBoard[TETRIS_HEIGHT][TETRIS_WIDTH];
int tetrisPieceX, tetrisPieceY;
int tetrisPieceType, tetrisPieceRot;
int tetrisNextPiece = 0;  // Next piece to spawn
int tetrisScore = 0;
int tetrisLevel = 1;
int tetrisLines = 0;
bool tetrisGameOver = false;
unsigned long tetrisLastDrop = 0;

// Breakout state
#define BREAKOUT_ROWS 5
#define BREAKOUT_COLS 10
bool breakoutBricks[BREAKOUT_ROWS][BREAKOUT_COLS];
float breakoutPaddleX;
float breakoutBallX, breakoutBallY;
float breakoutBallVX, breakoutBallVY;
int breakoutScore = 0;
int breakoutLives = 3;
bool breakoutGameOver = false;
bool breakoutBallLaunched = false;

const int SD_CS_PIN = 12; 

// ==========================================
//     Forward Declarations
// ==========================================
void loadFileList();
void loadFileListPage(int page);
void renderFileUI();
String getFullPath(String filename);
void addToHistory(String text);

// COC UI forward declarations
void renderCocMenu();
void renderCocWebServer();
void renderMicRecorder();
void renderCocWifi();
void renderCocUSB();
void handleCocUSBInput();
void renderCocSettings();
void handleCocSettingsInput();
void renderCocGames();
void tetrisInit();
void tetrisSpawnPiece();
void breakoutInit();
void startWebServer();
void stopWebServer();
void saveUserConfig();
bool takeScreenshot();
void playWavFile(String filepath);
void playMp3File(String filepath);
void renderWifiDirectSend();
void startWifiDirectTransfer(String filepath);
void renderBtAudio();
void handleBtAudioInput();
void btAudioStartScan();
void btAudioConnect(int index);
void btAudioDisconnect();

// ==========================================
//     Config File Management
// ==========================================

void createDefaultConfig() {
    File file = SD.open(configFile, FILE_WRITE);
    if (file) {
        file.println("# M5Shell User Configuration");
        file.println("# Edit values and reboot to apply");
        file.println("#");
        file.println("# terminal_hide: seconds until terminal hides");
        file.println("#   Shows network traffic plot when hidden");
        file.println("#   Set to 0 to disable");
        file.println("terminal_hide=10");
        file.println("");
        file.println("# screen_off: seconds until screen turns off");
        file.println("#   Saves power when idle");
        file.println("#   Set to 0 to disable");
        file.println("screen_off=300");
        file.println("");
        file.println("# brightness: display brightness (1-100)");
        file.println("brightness=100");
        file.println("");
        file.println("# timezone: GMT offset (-12 to +14)");
        file.println("#   Example: -6 for CST, -5 for EST, 0 for GMT");
        file.println("timezone=-6");
        file.println("");
        file.println("# AP Mode Settings (SD Share hotspot)");
        file.println("ap_ssid=M5SDSHARE");
        file.println("ap_password=m5cardputer");
        file.println("");
        file.println("# Saved WiFi credentials");
        file.println("wifi_ssid=");
        file.println("wifi_password=");
        file.close();
    }
}

void loadUserConfig() {
    // Set defaults
    terminalHideTimeoutMs = 10000;    // 10 seconds
    screenOffTimeoutMs = 300000;      // 5 minutes
    configBrightness = 100;           // Full brightness
    configTimezoneOffset = -6;        // CST default
    settingSoundEnabled = true;       // Sound enabled by default
    globalVolume = 80;                // Default volume 80%
    globalMuted = false;              // Not muted by default
    
    if (!SD.exists(configFile)) {
        createDefaultConfig();
        return;
    }
    
    File file = SD.open(configFile, FILE_READ);
    if (!file) return;
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        // Skip comments and empty lines
        if (line.length() == 0 || line.startsWith("#")) continue;
        
        int eqPos = line.indexOf('=');
        if (eqPos == -1) continue;
        
        String key = line.substring(0, eqPos);
        String value = line.substring(eqPos + 1);
        key.trim();
        value.trim();
        
        if (key == "terminal_hide") {
            int secs = value.toInt();
            if (secs <= 0) {
                terminalHideTimeoutMs = 0;  // Disabled
            } else {
                terminalHideTimeoutMs = (unsigned long)secs * 1000;
            }
        }
        else if (key == "screen_off") {
            int secs = value.toInt();
            if (secs <= 0) {
                screenOffTimeoutMs = 0;  // Disabled
            } else {
                screenOffTimeoutMs = (unsigned long)secs * 1000;
            }
        }
        else if (key == "brightness") {
            int bright = value.toInt();
            if (bright >= 1 && bright <= 100) {
                configBrightness = bright;
                M5Cardputer.Display.setBrightness(configBrightness);
            }
        }
        else if (key == "timezone") {
            int tz = value.toInt();
            if (tz >= -12 && tz <= 14) {
                configTimezoneOffset = tz;
            }
        }
        else if (key == "sound") {
            settingSoundEnabled = (value == "1" || value == "true" || value == "on");
        }
        else if (key == "volume") {
            int vol = value.toInt();
            if (vol >= 0 && vol <= 100) {
                globalVolume = vol;
            }
        }
        else if (key == "sid_duration") {
            int dur = value.toInt();
            if (dur >= 30 && dur <= 600) {  // 30 seconds to 10 minutes
                configSidDuration = dur;
            }
        }
        else if (key == "ap_ssid") {
            if (value.length() > 0) cocApSSID = value;
        }
        else if (key == "ap_password") {
            if (value.length() > 0) cocApPassword = value;
        }
        else if (key == "wifi_ssid") {
            cocSavedWifiSSID = value;
        }
        else if (key == "wifi_password") {
            cocSavedWifiPass = value;
        }
        else if (key == "nickname") {
            if (value.length() > 0 && value.length() <= 15) {
                chatNickname = value;
            }
        }
    }
    file.close();
}

// Save current configuration to file
void saveUserConfig() {
    File file = SD.open(configFile, FILE_WRITE);
    if (file) {
        file.println("# M5Shell User Configuration");
        file.println("# Edit values and reboot to apply");
        file.println("#");
        file.println("# terminal_hide: seconds until terminal hides");
        file.println("#   Shows network traffic plot when hidden");
        file.println("#   Set to 0 to disable");
        if (terminalHideTimeoutMs == 0) {
            file.println("terminal_hide=0");
        } else {
            file.println("terminal_hide=" + String(terminalHideTimeoutMs / 1000));
        }
        file.println("");
        file.println("# screen_off: seconds until screen turns off");
        file.println("#   Saves power when idle");
        file.println("#   Set to 0 to disable");
        if (screenOffTimeoutMs == 0) {
            file.println("screen_off=0");
        } else {
            file.println("screen_off=" + String(screenOffTimeoutMs / 1000));
        }
        file.println("");
        file.println("# brightness: display brightness (1-100)");
        file.println("brightness=" + String(configBrightness));
        file.println("");
        file.println("# timezone: GMT offset (-12 to +14)");
        file.println("#   Example: -6 for CST, -5 for EST, 0 for GMT");
        file.println("timezone=" + String(configTimezoneOffset));
        file.println("");
        file.println("# sound: menu sounds (1=on, 0=off)");
        file.println("sound=" + String(settingSoundEnabled ? "1" : "0"));
        file.println("");
        file.println("# volume: speaker volume (0-100)");
        file.println("volume=" + String(globalVolume));
        file.println("");
        file.println("# sid_duration: SID song duration in seconds (30-600)");
        file.println("#   Auto-advances to next song after this time");
        file.println("sid_duration=" + String(configSidDuration));
        file.println("");
        file.println("# AP Mode Settings (SD Share hotspot)");
        file.println("ap_ssid=" + cocApSSID);
        file.println("ap_password=" + cocApPassword);
        file.println("");
        file.println("# Saved WiFi credentials");
        file.println("wifi_ssid=" + cocSavedWifiSSID);
        file.println("wifi_password=" + cocSavedWifiPass);
        file.println("");
        file.println("# Chat nickname (max 15 characters)");
        file.println("nickname=" + chatNickname);
        file.close();
    }
}

// Sync time from NTP server
bool syncTimeFromNTP() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    
    // Configure NTP with timezone offset
    long gmtOffset_sec = configTimezoneOffset * 3600;
    configTime(gmtOffset_sec, 0, ntpServer);
    
    // Wait for time to sync (max 10 seconds)
    struct tm timeinfo;
    int retries = 20;
    while (!getLocalTime(&timeinfo) && retries > 0) {
        delay(500);
        retries--;
    }
    
    if (retries > 0) {
        timeSynced = true;
        return true;
    }
    return false;
}

// Get formatted date string (MM/DD/YYYY)
String getDateString() {
    if (!timeSynced) return "--/--/----";
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return "--/--/----";
    
    char buf[12];
    sprintf(buf, "%02d/%02d/%04d", 
            timeinfo.tm_mon + 1,  // Month is 0-based
            timeinfo.tm_mday,
            timeinfo.tm_year + 1900);
    return String(buf);
}

// Get formatted time string (HH:MM:SS AM/PM)
String getTimeString() {
    if (!timeSynced) return "--:--:-- --";
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return "--:--:-- --";
    
    int hour = timeinfo.tm_hour;
    String ampm = "AM";
    
    if (hour >= 12) {
        ampm = "PM";
        if (hour > 12) hour -= 12;
    }
    if (hour == 0) hour = 12;
    
    char buf[12];
    sprintf(buf, "%02d:%02d:%02d %s", 
            hour,
            timeinfo.tm_min,
            timeinfo.tm_sec,
            ampm.c_str());
    return String(buf);
}

// Get timezone name string
String getTimezoneString() {
    String tz = "GMT";
    if (configTimezoneOffset >= 0) {
        tz += "+" + String(configTimezoneOffset);
    } else {
        tz += String(configTimezoneOffset);
    }
    return tz;
}

void resetActivityTimer() {
    lastActivityTime = millis();
    
    // Wake from screen off
    if (screenOff) {
        screenOff = false;
        terminalHidden = false;
        M5Cardputer.Display.wakeup();
        M5Cardputer.Display.setBrightness(configBrightness);
        needsRedraw = true;
    }
    // Wake from terminal hidden
    else if (terminalHidden) {
        terminalHidden = false;
        needsRedraw = true;
    }
}

void renderTrafficPlots() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    
    sprite.fillSprite(TERM_BG_COLOR);
    
    // Title
    sprite.setTextColor(COL_CYAN, TERM_BG_COLOR);
    sprite.setCursor(4, 2);
    sprite.print("Network Traffic Monitor");
    
    // Show connection status
    if (WiFi.status() == WL_CONNECTED) {
        sprite.setTextColor(COL_FISH_GREEN, TERM_BG_COLOR);
        sprite.setCursor(screenW - 85, 2);
        sprite.print(WiFi.localIP().toString());
    } else {
        sprite.setTextColor(COL_ORANGE, TERM_BG_COLOR);
        sprite.setCursor(screenW - 75, 2);
        sprite.print("No WiFi");
    }
    
    // Plot area
    int plotX = 4;
    int plotY = 14;
    int plotW = screenW - 8;
    int plotH = screenH - 30;
    int plotBottom = plotY + plotH - 1;
    
    // Fill plot background slightly lighter
    sprite.fillRect(plotX + 1, plotY + 1, plotW - 2, plotH - 2, 0x1082);
    
    // Draw border
    sprite.drawRect(plotX, plotY, plotW, plotH, COL_GRAY_MID);
    
    // Draw baseline
    sprite.drawLine(plotX + 1, plotBottom, plotX + plotW - 2, plotBottom, COL_GRAY_DARK);
    
    // Find max value for scaling
    int maxVal = 100;  // Minimum scale of 100 bytes
    bool hasData = false;
    for (int i = 0; i < TRAFFIC_HISTORY_SIZE; i++) {
        if (trafficIn[i] > 0 || trafficOut[i] > 0) hasData = true;
        if (trafficIn[i] > maxVal) maxVal = trafficIn[i];
        if (trafficOut[i] > maxVal) maxVal = trafficOut[i];
    }
    
    // Draw horizontal grid lines with scale labels
    sprite.setTextColor(COL_GRAY_DARK, 0x1082);
    for (int i = 1; i <= 3; i++) {
        int y = plotBottom - (plotH - 2) * i / 4;
        sprite.drawLine(plotX + 1, y, plotX + plotW - 2, y, 0x2945);
    }
    
    // Calculate bar positions to fill entire width
    int numBars = TRAFFIC_HISTORY_SIZE;
    int usableWidth = plotW - 4;
    
    // Draw traffic bars - oldest on left, newest on right (at edge)
    for (int i = 0; i < numBars; i++) {
        // Data index: oldest (trafficIdx) to newest (trafficIdx-1)
        int dataIdx = (trafficIdx + i) % TRAFFIC_HISTORY_SIZE;
        
        // Calculate bar position to fill entire width evenly
        int barX = plotX + 2 + (i * usableWidth) / numBars;
        int nextBarX = plotX + 2 + ((i + 1) * usableWidth) / numBars;
        int barW = nextBarX - barX;
        if (barW < 2) barW = 2;
        
        // Draw incoming traffic (cyan) - left half of bar
        if (trafficIn[dataIdx] > 0) {
            int h = (trafficIn[dataIdx] * (plotH - 4)) / maxVal;
            if (h < 1) h = 1;
            if (h > plotH - 4) h = plotH - 4;
            sprite.fillRect(barX, plotBottom - h, barW / 2, h, COL_CYAN);
        }
        
        // Draw outgoing traffic (magenta) - right half of bar
        if (trafficOut[dataIdx] > 0) {
            int h = (trafficOut[dataIdx] * (plotH - 4)) / maxVal;
            if (h < 1) h = 1;
            if (h > plotH - 4) h = plotH - 4;
            sprite.fillRect(barX + barW / 2, plotBottom - h, barW / 2, h, COL_MAGENTA);
        }
    }
    
    // Show "waiting for data" message if no traffic yet
    if (!hasData) {
        sprite.setTextColor(COL_GRAY_MID, 0x1082);
        sprite.setCursor(plotX + plotW/2 - 50, plotY + plotH/2 - 4);
        sprite.print("Waiting for traffic...");
    }
    
    // Legend and stats at bottom
    int legendY = screenH - 12;
    
    // In stats with colored box
    sprite.fillRect(4, legendY + 1, 8, 8, COL_CYAN);
    sprite.setTextColor(COL_CYAN, TERM_BG_COLOR);
    sprite.setCursor(14, legendY);
    String inStr = "In:";
    if (totalBytesIn < 1024) inStr += String(totalBytesIn) + "B";
    else if (totalBytesIn < 1024*1024) inStr += String(totalBytesIn/1024) + "KB";
    else inStr += String(totalBytesIn/(1024*1024)) + "MB";
    sprite.print(inStr);
    
    // Out stats with colored box
    sprite.fillRect(screenW/2 - 10, legendY + 1, 8, 8, COL_MAGENTA);
    sprite.setTextColor(COL_MAGENTA, TERM_BG_COLOR);
    sprite.setCursor(screenW/2, legendY);
    String outStr = "Out:";
    if (totalBytesOut < 1024) outStr += String(totalBytesOut) + "B";
    else if (totalBytesOut < 1024*1024) outStr += String(totalBytesOut/1024) + "KB";
    else outStr += String(totalBytesOut/(1024*1024)) + "MB";
    sprite.print(outStr);
    
    // Web server status indicator
    if (sdShareEnabled) {
        sprite.setTextColor(COL_FISH_GREEN, TERM_BG_COLOR);
        sprite.setCursor(screenW - 40, legendY);
        sprite.print("[WEB]");
    }
    
    sprite.pushSprite(0, 0);
}

void updateTrafficData() {
    // Update traffic history every second
    if (millis() - lastTrafficUpdate < 1000) return;
    lastTrafficUpdate = millis();
    
    // Calculate bytes since last update
    int bytesInDelta = totalBytesIn - lastBytesIn;
    int bytesOutDelta = totalBytesOut - lastBytesOut;
    
    lastBytesIn = totalBytesIn;
    lastBytesOut = totalBytesOut;
    
    // Store in circular buffer
    trafficIn[trafficIdx] = bytesInDelta;
    trafficOut[trafficIdx] = bytesOutDelta;
    trafficIdx = (trafficIdx + 1) % TRAFFIC_HISTORY_SIZE;
}

// QR Code display timing
unsigned long lastQRRedraw = 0;

void renderQRCode() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    
    // Clear the display
    M5Cardputer.Display.fillScreen(currentThemeColor);
    
    // Draw window frame
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    M5Cardputer.Display.fillRect(winX, winY, winW, winH, COL_GRAY_LIGHT);
    
    // 3D raised border
    M5Cardputer.Display.drawLine(winX, winY, winX + winW - 1, winY, COL_WHITE);
    M5Cardputer.Display.drawLine(winX, winY, winX, winY + winH - 1, COL_WHITE);
    M5Cardputer.Display.drawLine(winX, winY + winH - 1, winX + winW - 1, winY + winH - 1, COL_BLACK);
    M5Cardputer.Display.drawLine(winX + winW - 1, winY, winX + winW - 1, winY + winH - 1, COL_BLACK);
    
    // Title bar
    M5Cardputer.Display.fillRect(winX + 3, winY + 3, winW - 6, 14, COL_TITLE_BAR);
    M5Cardputer.Display.setTextColor(COL_WHITE, COL_TITLE_BAR);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(winX + 6, winY + 6);
    M5Cardputer.Display.print("SD Web Share - QR Code");
    
    // Get URL
    String url = "http://" + WiFi.localIP().toString();
    
    // QR code size and position (centered)
    int qrSize = 80;  // QR code size
    int qrX = (screenW - qrSize) / 2;
    int qrY = winY + 22;
    
    // Draw white background for QR code
    M5Cardputer.Display.fillRect(qrX - 4, qrY - 4, qrSize + 8, qrSize + 8, COL_WHITE);
    
    // Draw QR code
    M5Cardputer.Display.qrcode(url, qrX, qrY, qrSize, 3);
    
    // Draw URL below QR code
    M5Cardputer.Display.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    int urlWidth = url.length() * 6;
    M5Cardputer.Display.setCursor((screenW - urlWidth) / 2, qrY + qrSize + 8);
    M5Cardputer.Display.print(url);
    
    // Instructions at bottom
    M5Cardputer.Display.setTextColor(COL_GRAY_DARK, COL_GRAY_LIGHT);
    M5Cardputer.Display.setCursor(winX + 10, winY + winH - 14);
    M5Cardputer.Display.print("Scan with phone - [ESC] to exit");
    
    lastQRRedraw = millis();
}

void handleQRInput() {
    // Redraw QR code every 2 seconds to keep display fresh
    if (millis() - lastQRRedraw > 2000) {
        renderQRCode();
    }
    
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        // Check for escape key
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            currentMode = MODE_TERMINAL;
            needsRedraw = true;
            delay(200);
        }
    }
}

// ==========================================
//     Hex Viewer
// ==========================================

void renderHexView() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    
    sprite.fillSprite(currentThemeColor);
    
    // Window frame
    int winX = 2, winY = 2, winW = screenW - 4, winH = screenH - 4;
    sprite.fillRect(winX, winY, winW, winH, COL_GRAY_LIGHT);
    draw3DRaised(winX, winY, winW, winH);
    
    // Title bar
    sprite.fillRect(winX + 3, winY + 3, winW - 6, 12, COL_TITLE_BAR);
    sprite.setTextColor(COL_WHITE, COL_TITLE_BAR);
    sprite.setCursor(winX + 6, winY + 5);
    String title = "Hex: " + hexViewFilePath.substring(hexViewFilePath.lastIndexOf('/') + 1);
    if (title.length() > 30) title = title.substring(0, 27) + "...";
    sprite.print(title);
    
    // Content area
    int contentY = winY + 18;
    int lineHeight = 9;
    int bytesPerLine = 8;
    int linesVisible = (screenH - 40) / lineHeight;
    
    sprite.fillRect(winX + 3, contentY, winW - 6, screenH - 38, COL_WHITE);
    
    // Read and display hex data
    File f = SD.open(hexViewFilePath, FILE_READ);
    if (f) {
        f.seek(hexViewOffset);
        
        sprite.setTextColor(COL_BLACK, COL_WHITE);
        int y = contentY + 2;
        
        for (int line = 0; line < linesVisible && f.available(); line++) {
            // Offset column
            sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
            sprite.setCursor(winX + 5, y);
            char offsetStr[8];
            sprintf(offsetStr, "%04X:", hexViewOffset + line * bytesPerLine);
            sprite.print(offsetStr);
            
            // Hex bytes
            sprite.setTextColor(COL_BLACK, COL_WHITE);
            uint8_t bytes[8];
            int bytesRead = 0;
            
            for (int i = 0; i < bytesPerLine && f.available(); i++) {
                bytes[i] = f.read();
                bytesRead++;
                
                char hexStr[4];
                sprintf(hexStr, "%02X ", bytes[i]);
                sprite.setCursor(winX + 38 + i * 18, y);
                sprite.print(hexStr);
            }
            
            // ASCII representation
            sprite.setTextColor(COL_SELECTION, COL_WHITE);
            sprite.setCursor(winX + 185, y);
            for (int i = 0; i < bytesRead; i++) {
                char c = (bytes[i] >= 32 && bytes[i] < 127) ? bytes[i] : '.';
                sprite.print(c);
            }
            
            y += lineHeight;
        }
        f.close();
    }
    
    // Status bar
    sprite.fillRect(winX + 3, screenH - 16, winW - 6, 12, COL_GRAY_LIGHT);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 6, screenH - 14);
    sprite.print("Offset:" + String(hexViewOffset) + "/" + String(hexViewFileSize));
    sprite.setCursor(winX + winW - 80, screenH - 14);
    sprite.print("[;/.] ESC");
    
    sprite.pushSprite(0, 0);
}

void handleHexViewInput() {
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        int bytesPerLine = 8;
        int linesVisible = 10;
        int pageSize = bytesPerLine * linesVisible;
        
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
            if (hexViewOffset >= bytesPerLine) {
                hexViewOffset -= bytesPerLine;
                renderHexView();
            }
            delay(100);
        }
        else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
            if (hexViewOffset + pageSize < hexViewFileSize) {
                hexViewOffset += bytesPerLine;
                renderHexView();
            }
            delay(100);
        }
        else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_LEFT)) {
            // Page up
            hexViewOffset -= pageSize;
            if (hexViewOffset < 0) hexViewOffset = 0;
            renderHexView();
            delay(150);
        }
        else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_RIGHT)) {
            // Page down
            hexViewOffset += pageSize;
            if (hexViewOffset >= hexViewFileSize) hexViewOffset = hexViewFileSize - pageSize;
            if (hexViewOffset < 0) hexViewOffset = 0;
            renderHexView();
            delay(150);
        }
        else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            currentMode = MODE_TERMINAL;
            needsRedraw = true;
            delay(200);
        }
    }
}

// ==========================================
//     Serial Monitor
// ==========================================

void renderSerialMonitor() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    
    sprite.fillSprite(TERM_BG_COLOR);
    
    // Title
    sprite.setTextColor(COL_CYAN, TERM_BG_COLOR);
    sprite.setCursor(4, 2);
    sprite.print("Serial Monitor @ " + String(serialBaudRate) + " baud");
    
    // Serial buffer display
    int lineHeight = 10;
    int maxLines = (screenH - 30) / lineHeight;
    int startLine = 0;
    if ((int)serialBuffer.size() > maxLines) {
        startLine = serialBuffer.size() - maxLines;
    }
    
    sprite.setTextColor(COL_WHITE, TERM_BG_COLOR);
    int y = 14;
    for (int i = startLine; i < (int)serialBuffer.size(); i++) {
        sprite.setCursor(4, y);
        String line = serialBuffer[i];
        if (line.length() > 40) line = line.substring(0, 40);
        sprite.print(line);
        y += lineHeight;
    }
    
    // Status bar
    sprite.setTextColor(COL_YELLOW, TERM_BG_COLOR);
    sprite.setCursor(4, screenH - 12);
    sprite.print("RX:" + String(serialBuffer.size()) + " lines  [ESC]=Exit");
    
    sprite.pushSprite(0, 0);
}

void handleSerialMonitorInput() {
    // Check for incoming serial data
    while (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            serialBuffer.push_back(line);
            // Keep buffer limited
            while (serialBuffer.size() > 100) {
                serialBuffer.erase(serialBuffer.begin());
            }
            renderSerialMonitor();
        }
    }
    
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        
        // Send typed characters to serial
        for (auto c : status.word) {
            Serial.print(c);
        }
        
        if (status.enter) {
            Serial.println();
        }
        
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            serialMonitorActive = false;
            currentMode = MODE_TERMINAL;
            needsRedraw = true;
            delay(200);
        }
    }
    
    // Refresh display periodically
    static unsigned long lastRefresh = 0;
    if (millis() - lastRefresh > 500) {
        lastRefresh = millis();
        renderSerialMonitor();
    }
}

// ==========================================
//     BT Keyboard Mode
// ==========================================

void renderBTKeyboard() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    
    sprite.fillSprite(currentThemeColor);
    
    // Window
    int winX = 10, winY = 10, winW = screenW - 20, winH = screenH - 20;
    sprite.fillRect(winX, winY, winW, winH, COL_GRAY_LIGHT);
    draw3DRaised(winX, winY, winW, winH);
    
    // Title bar
    sprite.fillRect(winX + 3, winY + 3, winW - 6, 14, COL_TITLE_BAR);
    sprite.setTextColor(COL_WHITE, COL_TITLE_BAR);
    sprite.setCursor(winX + 6, winY + 5);
    sprite.print("Bluetooth Keyboard");
    
    // Content
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 10, winY + 25);
    sprite.print("Device: M5Cardputer");
    
    sprite.setCursor(winX + 10, winY + 40);
#ifdef ENABLE_BT_KEYBOARD
    if (bleKeyboard.isConnected()) {
        sprite.setTextColor(COL_GREEN, COL_GRAY_LIGHT);
        sprite.print("Status: Connected!");
    } else {
        sprite.setTextColor(COL_RED, COL_GRAY_LIGHT);
        sprite.print("Status: Waiting...");
    }
#else
    sprite.setTextColor(COL_RED, COL_GRAY_LIGHT);
    sprite.print("BT not enabled in build");
#endif
    
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 10, winY + 60);
    sprite.print("Type to send keystrokes");
    
    sprite.setCursor(winX + 10, winY + winH - 20);
    sprite.setTextColor(COL_GRAY_DARK, COL_GRAY_LIGHT);
    sprite.print("[ESC] Exit");
    
    sprite.pushSprite(0, 0);
}

void handleBTKeyboardInput() {
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        
#ifdef ENABLE_BT_KEYBOARD
        if (bleKeyboard.isConnected()) {
            // Send typed characters via BT
            for (auto c : status.word) {
                bleKeyboard.print(c);
            }
            
            if (status.enter) {
                bleKeyboard.write(KEY_RETURN);
            }
            if (status.del) {
                bleKeyboard.write(KEY_BACKSPACE);
            }
        }
#endif
        
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            btKeyboardActive = false;
            currentMode = MODE_TERMINAL;
            needsRedraw = true;
            delay(200);
        }
    }
    
    // Refresh display periodically to update connection status
    static unsigned long lastRefresh = 0;
    if (millis() - lastRefresh > 1000) {
        lastRefresh = millis();
        renderBTKeyboard();
    }
}

// ==========================================
//     Grid Synth (PixiTracker-style)
// ==========================================

void synthInit() {
    // Clear grid
    for (int r = 0; r < SYNTH_ROWS; r++) {
        for (int c = 0; c < SYNTH_COLS; c++) {
            synthGrid[r][c] = 0;
        }
    }
    synthCursorX = 0;
    synthCursorY = 0;
    synthPlayCol = 0;
    synthPlaying = false;
    synthBPM = 120;
    synthCurrentColor = 1;
    synthMenuOpen = false;
    synthMenuCursor = 0;
    
    // Reset sounds to defaults
    for (int i = 0; i < 8; i++) {
        synthColorSound[i] = i;
    }
}

void synthPlayColumn(int col) {
    // Find all active notes in this column and play them
    float mixFreq = 0;
    int count = 0;
    int shortestDuration = 0;  // Track shortest duration for timed sounds
    
    for (int r = 0; r < SYNTH_ROWS; r++) {
        uint8_t cellColor = synthGrid[r][col];
        if (cellColor > 0) {
            // Get sound type for this color
            uint8_t soundType = synthColorSound[cellColor - 1];
            float freqMult = synthSoundFreqMult[soundType];
            int duration = synthSoundDuration[soundType];
            
            // Apply frequency modifier
            mixFreq += synthRowFreq[r] * freqMult;
            count++;
            
            // Track duration (use shortest non-zero, or 0 for sustain)
            if (duration > 0 && (shortestDuration == 0 || duration < shortestDuration)) {
                shortestDuration = duration;
            }
        }
    }
    
    if (count > 0) {
        mixFreq /= count;
        // Clamp frequency to audible range
        if (mixFreq < 20) mixFreq = 20;
        if (mixFreq > 15000) mixFreq = 15000;
        M5Cardputer.Speaker.tone(mixFreq, shortestDuration);
    } else {
        M5Cardputer.Speaker.stop();
    }
}

void synthTick() {
    if (!synthPlaying) return;
    
    // Calculate step duration from BPM (4 steps per beat)
    unsigned long stepMs = 60000 / synthBPM / 4;
    
    if (millis() - synthLastTick < stepMs) return;
    synthLastTick = millis();
    
    // Play current column
    synthPlayColumn(synthPlayCol);
    
    // Advance playhead
    synthPlayCol++;
    if (synthPlayCol >= SYNTH_COLS) {
        synthPlayCol = 0;  // Loop
    }
}

void renderSynth() {
    int W = M5Cardputer.Display.width();   // 240
    int H = M5Cardputer.Display.height();  // 135
    
    // Grid layout
    int cellW = 13;
    int cellH = 13;
    int gridX = 20;   // Left margin for row labels
    int gridY = 14;   // Top margin for header
    
    sprite.fillSprite(0x0000);
    
    // === Header Bar ===
    sprite.fillRect(0, 0, W, 12, 0x39E7);
    sprite.setTextColor(COL_WHITE, 0x39E7);
    sprite.setCursor(2, 2);
    sprite.print("SYNTH ");
    
    // Play/Stop indicator
    sprite.setTextColor(synthPlaying ? COL_GREEN : COL_GRAY_LIGHT, 0x39E7);
    sprite.print(synthPlaying ? ">>" : "[]");
    
    // BPM
    sprite.setTextColor(COL_WHITE, 0x39E7);
    sprite.setCursor(60, 2);
    sprite.print("BPM:");
    sprite.setTextColor(COL_YELLOW, 0x39E7);
    sprite.print(synthBPM);
    
    // Current color indicator
    sprite.setTextColor(COL_WHITE, 0x39E7);
    sprite.setCursor(115, 2);
    sprite.print("Col:");
    sprite.fillRect(145, 2, 14, 8, synthColors[synthCurrentColor]);
    sprite.drawRect(145, 2, 14, 8, COL_WHITE);
    
    // === Draw Grid ===
    for (int r = 0; r < SYNTH_ROWS; r++) {
        int y = gridY + r * cellH;
        
        // Row labels (pitch markers)
        sprite.setTextColor(COL_GRAY_MID, 0x0000);
        sprite.setCursor(2, y + 3);
        const char* labels[] = {"C6", "A5", "G5", "E5", "C5", "A4", "G4", "E4"};
        sprite.print(labels[r]);
        
        for (int c = 0; c < SYNTH_COLS; c++) {
            int x = gridX + c * cellW;
            
            uint8_t cellVal = synthGrid[r][c];
            uint16_t cellColor = synthColors[cellVal];
            
            // Fill cell
            sprite.fillRect(x + 1, y + 1, cellW - 2, cellH - 2, cellColor);
            
            // Grid lines (darker on beat boundaries)
            uint16_t lineColor = (c % 4 == 0) ? 0x4A69 : 0x2945;
            sprite.drawRect(x, y, cellW, cellH, lineColor);
            
            // Playhead highlight (white column)
            if (synthPlaying && c == synthPlayCol) {
                sprite.drawRect(x, y, cellW, cellH, COL_WHITE);
            }
            
            // Cursor highlight (yellow)
            if (c == synthCursorX && r == synthCursorY) {
                sprite.drawRect(x, y, cellW, cellH, COL_YELLOW);
                sprite.drawRect(x + 1, y + 1, cellW - 2, cellH - 2, COL_YELLOW);
            }
        }
    }
    
    // === Beat markers ===
    int beatY = gridY + SYNTH_ROWS * cellH + 2;
    for (int c = 0; c < SYNTH_COLS; c += 4) {
        int x = gridX + c * cellW;
        sprite.setTextColor(COL_WHITE, 0x0000);
        sprite.setCursor(x + 4, beatY);
        sprite.print(c / 4 + 1);
    }
    
    // === Status Bar ===
    int statusY = H - 10;
    sprite.fillRect(0, statusY, W, 10, 0x2104);
    sprite.setTextColor(COL_GRAY_LIGHT, 0x2104);
    sprite.setCursor(3, statusY + 1);
    sprite.print("Spc:Play 1-8:Note Tab:Sounds Esc:Exit");
    
    // === Sound Menu Overlay ===
    if (synthMenuOpen) {
        // Darken background
        int menuW = 160;
        int menuH = 110;
        int menuX = (W - menuW) / 2;
        int menuY = (H - menuH) / 2;
        
        // Menu background with border
        sprite.fillRect(menuX, menuY, menuW, menuH, 0x0000);
        sprite.drawRect(menuX, menuY, menuW, menuH, COL_WHITE);
        sprite.drawRect(menuX + 1, menuY + 1, menuW - 2, menuH - 2, COL_GRAY_DARK);
        
        // Title bar
        sprite.fillRect(menuX + 2, menuY + 2, menuW - 4, 12, 0x39E7);
        sprite.setTextColor(COL_WHITE, 0x39E7);
        sprite.setCursor(menuX + 40, menuY + 4);
        sprite.print("COLOR SOUNDS");
        
        // List colors and their sounds
        int itemY = menuY + 18;
        for (int i = 0; i < 8; i++) {
            bool selected = (i == synthMenuCursor);
            
            // Highlight selected row
            if (selected) {
                sprite.fillRect(menuX + 4, itemY, menuW - 8, 10, 0x2945);
            }
            
            // Color swatch
            sprite.fillRect(menuX + 8, itemY + 1, 12, 8, synthColors[i + 1]);
            sprite.drawRect(menuX + 8, itemY + 1, 12, 8, COL_WHITE);
            
            // Color number
            sprite.setTextColor(selected ? COL_YELLOW : COL_WHITE, selected ? 0x2945 : 0x0000);
            sprite.setCursor(menuX + 24, itemY + 1);
            sprite.print(i + 1);
            sprite.print(":");
            
            // Sound name
            sprite.setTextColor(selected ? COL_CYAN : COL_GRAY_LIGHT, selected ? 0x2945 : 0x0000);
            sprite.setCursor(menuX + 45, itemY + 1);
            sprite.print(synthSoundNames[synthColorSound[i]]);
            
            itemY += 11;
        }
        
        // Instructions
        sprite.setTextColor(COL_GRAY_MID, 0x0000);
        sprite.setCursor(menuX + 8, menuY + menuH - 12);
        sprite.print(";/.:Sel +/-:Snd Tab:Close");
    }
    
    sprite.pushSprite(0, 0);
}

void synthSave(String filename) {
    if (!filename.startsWith("/")) filename = "/" + filename;
    if (!filename.endsWith(".syx")) filename += ".syx";
    
    File f = SD.open(filename, FILE_WRITE);
    if (!f) return;
    
    f.println("SYX2");  // Version 2 with sounds
    f.println(synthBPM);
    
    // Save sound settings for each color
    for (int i = 0; i < 8; i++) {
        f.print(synthColorSound[i]);
        if (i < 7) f.print(",");
    }
    f.println();
    
    // Save grid
    for (int r = 0; r < SYNTH_ROWS; r++) {
        for (int c = 0; c < SYNTH_COLS; c++) {
            f.print(synthGrid[r][c]);
            if (c < SYNTH_COLS - 1) f.print(",");
        }
        f.println();
    }
    f.close();
    synthFileName = filename;
}

void synthLoad(String filename) {
    if (!filename.startsWith("/")) filename = "/" + filename;
    if (!filename.endsWith(".syx")) filename += ".syx";
    if (!SD.exists(filename)) return;
    
    File f = SD.open(filename, FILE_READ);
    if (!f) return;
    
    synthInit();
    
    String line = f.readStringUntil('\n');
    line.trim();
    bool version2 = line.startsWith("SYX2");
    if (!line.startsWith("SYX")) { f.close(); return; }
    
    synthBPM = f.readStringUntil('\n').toInt();
    if (synthBPM < 40 || synthBPM > 300) synthBPM = 120;
    
    // Load sound settings if version 2
    if (version2) {
        line = f.readStringUntil('\n');
        int idx = 0;
        int start = 0;
        for (int i = 0; i <= (int)line.length() && idx < 8; i++) {
            if (i == (int)line.length() || line.charAt(i) == ',') {
                int val = line.substring(start, i).toInt();
                if (val >= 0 && val <= 7) synthColorSound[idx] = val;
                start = i + 1;
                idx++;
            }
        }
    }
    
    // Load grid
    for (int r = 0; r < SYNTH_ROWS && f.available(); r++) {
        line = f.readStringUntil('\n');
        int c = 0;
        int start = 0;
        for (int i = 0; i <= (int)line.length() && c < SYNTH_COLS; i++) {
            if (i == (int)line.length() || line.charAt(i) == ',') {
                int val = line.substring(start, i).toInt();
                if (val >= 0 && val <= 8) synthGrid[r][c] = val;
                start = i + 1;
                c++;
            }
        }
    }
    f.close();
    synthFileName = filename;
}

void handleSynthInput() {
    // Update playback timing
    synthTick();
    
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        // Keep refreshing during playback
        if (synthPlaying || synthMenuOpen) {
            static unsigned long lastDraw = 0;
            if (millis() - lastDraw > 30) {
                lastDraw = millis();
                renderSynth();
            }
        }
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    bool fnPressed = status.fn;
    
    // === TAB - Toggle sound menu ===
    if (status.tab) {
        synthMenuOpen = !synthMenuOpen;
        renderSynth();
        delay(150);
        return;
    }
    
    // === MENU MODE ===
    if (synthMenuOpen) {
        // Navigate menu with up/down
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
            synthMenuCursor = (synthMenuCursor > 0) ? synthMenuCursor - 1 : 7;
            renderSynth();
            delay(100);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
            synthMenuCursor = (synthMenuCursor < 7) ? synthMenuCursor + 1 : 0;
            renderSynth();
            delay(100);
            return;
        }
        
        // Change sound with +/- or left/right
        if (M5Cardputer.Keyboard.isKeyPressed('+') || M5Cardputer.Keyboard.isKeyPressed('=') ||
            M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_RIGHT)) {
            synthColorSound[synthMenuCursor]++;
            if (synthColorSound[synthMenuCursor] > 7) synthColorSound[synthMenuCursor] = 0;
            // Preview sound
            float freq = synthRowFreq[4] * synthSoundFreqMult[synthColorSound[synthMenuCursor]];
            int dur = synthSoundDuration[synthColorSound[synthMenuCursor]];
            if (dur == 0) dur = 100;
            M5Cardputer.Speaker.tone(freq, dur);
            renderSynth();
            delay(100);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('-') || M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_LEFT)) {
            if (synthColorSound[synthMenuCursor] == 0) synthColorSound[synthMenuCursor] = 7;
            else synthColorSound[synthMenuCursor]--;
            // Preview sound
            float freq = synthRowFreq[4] * synthSoundFreqMult[synthColorSound[synthMenuCursor]];
            int dur = synthSoundDuration[synthColorSound[synthMenuCursor]];
            if (dur == 0) dur = 100;
            M5Cardputer.Speaker.tone(freq, dur);
            renderSynth();
            delay(100);
            return;
        }
        
        // ESC or Enter closes menu
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC) || status.enter) {
            synthMenuOpen = false;
            renderSynth();
            delay(150);
            return;
        }
        
        renderSynth();
        return;
    }
    
    // === NORMAL MODE ===
    
    // === ESC - Exit synth ===
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        M5Cardputer.Speaker.stop();
        synthPlaying = false;
        currentMode = MODE_TERMINAL;
        needsRedraw = true;
        delay(200);
        return;
    }
    
    // === Space - Play/Stop ===
    if (M5Cardputer.Keyboard.isKeyPressed(' ')) {
        synthPlaying = !synthPlaying;
        if (synthPlaying) {
            synthPlayCol = 0;
            synthLastTick = millis();
        } else {
            M5Cardputer.Speaker.stop();
        }
        renderSynth();
        delay(150);
        return;
    }
    
    // === Navigation ===
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
        synthCursorY = (synthCursorY > 0) ? synthCursorY - 1 : SYNTH_ROWS - 1;
        renderSynth();
        delay(80);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
        synthCursorY = (synthCursorY < SYNTH_ROWS - 1) ? synthCursorY + 1 : 0;
        renderSynth();
        delay(80);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_LEFT)) {
        synthCursorX = (synthCursorX > 0) ? synthCursorX - 1 : SYNTH_COLS - 1;
        renderSynth();
        delay(80);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_RIGHT)) {
        synthCursorX = (synthCursorX < SYNTH_COLS - 1) ? synthCursorX + 1 : 0;
        renderSynth();
        delay(80);
        return;
    }
    
    // === Enter - Toggle/Place note ===
    if (status.enter) {
        if (synthGrid[synthCursorY][synthCursorX] > 0) {
            synthGrid[synthCursorY][synthCursorX] = 0;
        } else {
            synthGrid[synthCursorY][synthCursorX] = synthCurrentColor;
            // Preview with sound type
            uint8_t sndType = synthColorSound[synthCurrentColor - 1];
            float freq = synthRowFreq[synthCursorY] * synthSoundFreqMult[sndType];
            int dur = synthSoundDuration[sndType];
            if (dur == 0) dur = 80;
            M5Cardputer.Speaker.tone(freq, dur);
        }
        renderSynth();
        delay(100);
        return;
    }
    
    // === Delete - Clear cell ===
    if (status.del) {
        synthGrid[synthCursorY][synthCursorX] = 0;
        renderSynth();
        delay(80);
        return;
    }
    
    // === +/- Change current color (when not in menu) ===
    if (!fnPressed) {
        if (M5Cardputer.Keyboard.isKeyPressed('+') || M5Cardputer.Keyboard.isKeyPressed('=')) {
            synthCurrentColor++;
            if (synthCurrentColor > 8) synthCurrentColor = 1;
            renderSynth();
            delay(100);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('-')) {
            synthCurrentColor--;
            if (synthCurrentColor < 1) synthCurrentColor = 8;
            renderSynth();
            delay(100);
            return;
        }
    }
    
    // === Number keys 1-8 for direct color & place ===
    for (auto c : status.word) {
        if (c >= '1' && c <= '8') {
            synthCurrentColor = c - '0';
            synthGrid[synthCursorY][synthCursorX] = synthCurrentColor;
            // Preview with sound type
            uint8_t sndType = synthColorSound[synthCurrentColor - 1];
            float freq = synthRowFreq[synthCursorY] * synthSoundFreqMult[sndType];
            int dur = synthSoundDuration[sndType];
            if (dur == 0) dur = 80;
            M5Cardputer.Speaker.tone(freq, dur);
            renderSynth();
            delay(80);
            return;
        }
        if (c == '0') {
            synthGrid[synthCursorY][synthCursorX] = 0;
            renderSynth();
            delay(80);
            return;
        }
    }
    
    // === Fn combinations ===
    if (fnPressed) {
        // Fn+= (Fn++) - BPM up
        if (M5Cardputer.Keyboard.isKeyPressed('+') || M5Cardputer.Keyboard.isKeyPressed('=')) {
            if (synthBPM < 300) synthBPM += 5;
            renderSynth();
            delay(100);
            return;
        }
        // Fn+- - BPM down
        if (M5Cardputer.Keyboard.isKeyPressed('-')) {
            if (synthBPM > 40) synthBPM -= 5;
            renderSynth();
            delay(100);
            return;
        }
        
        // Fn+S - Save
        if (M5Cardputer.Keyboard.isKeyPressed('s')) {
            synthSave(synthFileName);
            sprite.fillRect(80, 55, 80, 25, COL_GREEN);
            sprite.setTextColor(COL_WHITE, COL_GREEN);
            sprite.setCursor(95, 63);
            sprite.print("SAVED!");
            sprite.pushSprite(0, 0);
            delay(400);
            renderSynth();
            return;
        }
        
        // Fn+C - Clear all
        if (M5Cardputer.Keyboard.isKeyPressed('c')) {
            for (int r = 0; r < SYNTH_ROWS; r++) {
                for (int c = 0; c < SYNTH_COLS; c++) {
                    synthGrid[r][c] = 0;
                }
            }
            renderSynth();
            delay(150);
            return;
        }
    }
    
    renderSynth();
}

// ==========================================
//     Roku IR Remote
// ==========================================
#ifdef ENABLE_IR_FEATURES

void sendRokuCode(uint32_t code) {
    IrSender.begin(IR_SEND_PIN);
    IrSender.sendNEC(code, 32);
    M5Cardputer.Speaker.tone(1200, 30);  // Feedback beep
}

void renderRokuRemote() {
    int W = M5Cardputer.Display.width();   // 240
    int H = M5Cardputer.Display.height();  // 135
    
    sprite.fillSprite(COL_BLACK);
    
    // Title bar
    sprite.fillRect(0, 0, W, 18, COL_MAGENTA);
    sprite.setTextColor(COL_WHITE, COL_MAGENTA);
    sprite.setCursor(6, 5);
    sprite.print("TCL ROKU Remote");
    sprite.setCursor(W - 55, 5);
    sprite.setTextSize(1);
    sprite.print("[ESC]Exit");
    
    // Layout: Left side = D-pad, Right side = Power/Vol/Mute
    const int btnH = 20;
    const int padX = 70;   // D-pad center X
    const int padY = 68;   // D-pad center Y
    
    // D-pad buttons
    int dpadW = 44;
    int gap = 2;
    
    // Draw D-pad
    // Up
    sprite.fillRoundRect(padX - dpadW/2, padY - btnH - gap - btnH/2, dpadW, btnH, 3, 
        (rokuSelectedBtn == 0) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setTextColor(COL_WHITE, (rokuSelectedBtn == 0) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setCursor(padX - 6, padY - btnH - gap - btnH/2 + 6);
    sprite.print("UP");
    
    // Left
    sprite.fillRoundRect(padX - dpadW - gap - dpadW/2, padY - btnH/2, dpadW, btnH, 3,
        (rokuSelectedBtn == 1) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setTextColor(COL_WHITE, (rokuSelectedBtn == 1) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setCursor(padX - dpadW - gap - dpadW/2 + 8, padY - btnH/2 + 6);
    sprite.print("LEFT");
    
    // OK (center)
    sprite.fillRoundRect(padX - dpadW/2, padY - btnH/2, dpadW, btnH, 3,
        (rokuSelectedBtn == 2) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setTextColor(COL_WHITE, (rokuSelectedBtn == 2) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setCursor(padX - 6, padY - btnH/2 + 6);
    sprite.print("OK");
    
    // Right
    sprite.fillRoundRect(padX + dpadW/2 + gap, padY - btnH/2, dpadW, btnH, 3,
        (rokuSelectedBtn == 3) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setTextColor(COL_WHITE, (rokuSelectedBtn == 3) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setCursor(padX + dpadW/2 + gap + 5, padY - btnH/2 + 6);
    sprite.print("RIGHT");
    
    // Down
    sprite.fillRoundRect(padX - dpadW/2, padY + btnH/2 + gap, dpadW, btnH, 3,
        (rokuSelectedBtn == 4) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setTextColor(COL_WHITE, (rokuSelectedBtn == 4) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setCursor(padX - 12, padY + btnH/2 + gap + 6);
    sprite.print("DOWN");
    
    // Right side buttons - Power, Vol, Mute
    int rightX = 175;
    
    // Power (red)
    sprite.fillRoundRect(rightX, 22, 55, btnH, 3, 
        (rokuSelectedBtn == 5) ? COL_RED : COL_GRAY_DARK);
    sprite.setTextColor(COL_WHITE, (rokuSelectedBtn == 5) ? COL_RED : COL_GRAY_DARK);
    sprite.setCursor(rightX + 8, 28);
    sprite.print("POWER");
    
    // Vol+
    sprite.fillRoundRect(rightX, 45, 55, btnH, 3,
        (rokuSelectedBtn == 6) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setTextColor(COL_WHITE, (rokuSelectedBtn == 6) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setCursor(rightX + 12, 51);
    sprite.print("VOL+");
    
    // Vol-
    sprite.fillRoundRect(rightX, 68, 55, btnH, 3,
        (rokuSelectedBtn == 7) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setTextColor(COL_WHITE, (rokuSelectedBtn == 7) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setCursor(rightX + 12, 74);
    sprite.print("VOL-");
    
    // Mute
    sprite.fillRoundRect(rightX, 91, 55, btnH, 3,
        (rokuSelectedBtn == 8) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setTextColor(COL_WHITE, (rokuSelectedBtn == 8) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setCursor(rightX + 12, 97);
    sprite.print("MUTE");
    
    // Bottom row - Home, Back, Options, Play
    int bottomY = 115;
    sprite.fillRoundRect(5, bottomY, 38, 18, 3, (rokuSelectedBtn == 9) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setTextColor(COL_WHITE, (rokuSelectedBtn == 9) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setCursor(10, bottomY + 5);
    sprite.print("HOME");
    
    sprite.fillRoundRect(47, bottomY, 38, 18, 3, (rokuSelectedBtn == 10) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setTextColor(COL_WHITE, (rokuSelectedBtn == 10) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setCursor(52, bottomY + 5);
    sprite.print("BACK");
    
    sprite.fillRoundRect(89, bottomY, 30, 18, 3, (rokuSelectedBtn == 11) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setTextColor(COL_WHITE, (rokuSelectedBtn == 11) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setCursor(97, bottomY + 5);
    sprite.print("*");
    
    sprite.fillRoundRect(123, bottomY, 38, 18, 3, (rokuSelectedBtn == 12) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setTextColor(COL_WHITE, (rokuSelectedBtn == 12) ? COL_MAGENTA : COL_GRAY_DARK);
    sprite.setCursor(128, bottomY + 5);
    sprite.print("PLAY");
    
    // Key hints at very bottom
    sprite.setTextColor(COL_CYAN, COL_BLACK);
    sprite.setCursor(4, H - 8);
    sprite.print(";,./=Nav O=Pwr +=V+ -=V- M=Mute");
    
    sprite.pushSprite(0, 0);
}

void handleRokuRemoteInput() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    
    // ESC - Exit Roku remote
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        currentMode = MODE_TERMINAL;
        addToHistory("Roku remote closed");
        needsRedraw = true;
        delay(200);
        return;
    }
    
    // D-pad navigation: ; = Up, . = Down, , = Left, / = Right
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
        rokuSelectedBtn = 0;
        sendRokuCode(ROKU_UP);
        renderRokuRemote();
        delay(150);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
        rokuSelectedBtn = 4;
        sendRokuCode(ROKU_DOWN);
        renderRokuRemote();
        delay(150);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_LEFT)) {
        rokuSelectedBtn = 1;
        sendRokuCode(ROKU_LEFT);
        renderRokuRemote();
        delay(150);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_RIGHT)) {
        rokuSelectedBtn = 3;
        sendRokuCode(ROKU_RIGHT);
        renderRokuRemote();
        delay(150);
        return;
    }
    
    // Enter = OK/Select
    if (status.enter) {
        rokuSelectedBtn = 2;
        sendRokuCode(ROKU_OK);
        renderRokuRemote();
        delay(150);
        return;
    }
    
    // Backspace = Back
    if (status.del) {
        rokuSelectedBtn = 10;
        sendRokuCode(ROKU_BACK);
        renderRokuRemote();
        delay(150);
        return;
    }
    
    // Space = Options (*)
    if (M5Cardputer.Keyboard.isKeyPressed(' ')) {
        rokuSelectedBtn = 11;
        sendRokuCode(ROKU_OPTIONS);
        renderRokuRemote();
        delay(150);
        return;
    }
    
    // H = Home
    if (M5Cardputer.Keyboard.isKeyPressed('h')) {
        rokuSelectedBtn = 9;
        sendRokuCode(ROKU_HOME);
        renderRokuRemote();
        delay(150);
        return;
    }
    
    // O = Power
    if (M5Cardputer.Keyboard.isKeyPressed('o')) {
        rokuSelectedBtn = 5;
        sendRokuCode(ROKU_POWER);
        renderRokuRemote();
        delay(150);
        return;
    }
    
    // + or = = Volume Up
    if (M5Cardputer.Keyboard.isKeyPressed('+') || M5Cardputer.Keyboard.isKeyPressed('=')) {
        rokuSelectedBtn = 6;
        sendRokuCode(ROKU_VOL_UP);
        renderRokuRemote();
        delay(120);
        return;
    }
    
    // - = Volume Down
    if (M5Cardputer.Keyboard.isKeyPressed('-')) {
        rokuSelectedBtn = 7;
        sendRokuCode(ROKU_VOL_DOWN);
        renderRokuRemote();
        delay(120);
        return;
    }
    
    // M = Mute
    if (M5Cardputer.Keyboard.isKeyPressed('m')) {
        rokuSelectedBtn = 8;
        sendRokuCode(ROKU_MUTE);
        renderRokuRemote();
        delay(150);
        return;
    }
    
    // P = Play/Pause
    if (M5Cardputer.Keyboard.isKeyPressed('p')) {
        rokuSelectedBtn = 12;
        sendRokuCode(ROKU_PLAY_PAUSE);
        renderRokuRemote();
        delay(150);
        return;
    }
    
    // [ = Rewind
    if (M5Cardputer.Keyboard.isKeyPressed('[')) {
        sendRokuCode(ROKU_REWIND);
        renderRokuRemote();
        delay(150);
        return;
    }
    
    // ] = Forward
    if (M5Cardputer.Keyboard.isKeyPressed(']')) {
        sendRokuCode(ROKU_FORWARD);
        renderRokuRemote();
        delay(150);
        return;
    }
}

#endif // ENABLE_IR_FEATURES

// ==========================================
//     COC UI - Main Menu System
// ==========================================

// Icon drawing functions
void drawIconFolder(int x, int y, uint16_t color) {
    sprite.fillRect(x, y + 4, 8, 4, color);
    sprite.fillRect(x, y + 6, 20, 14, color);
    sprite.drawRect(x, y + 6, 20, 14, COL_WHITE);
}

void drawIconWifi(int x, int y, uint16_t color) {
    sprite.drawArc(x + 10, y + 18, 16, 14, 225, 315, color);
    sprite.drawArc(x + 10, y + 18, 12, 10, 225, 315, color);
    sprite.drawArc(x + 10, y + 18, 8, 6, 225, 315, color);
    sprite.fillCircle(x + 10, y + 16, 2, color);
}

void drawIconBluetooth(int x, int y, uint16_t color) {
    sprite.drawLine(x + 10, y + 2, x + 10, y + 18, color);
    sprite.drawLine(x + 10, y + 2, x + 16, y + 8, color);
    sprite.drawLine(x + 16, y + 8, x + 4, y + 14, color);
    sprite.drawLine(x + 4, y + 6, x + 16, y + 12, color);
    sprite.drawLine(x + 16, y + 12, x + 10, y + 18, color);
}

void drawIconUSB(int x, int y, uint16_t color) {
    // USB trident symbol
    sprite.drawLine(x + 10, y + 2, x + 10, y + 16, color);  // Main stem
    sprite.fillCircle(x + 10, y + 17, 2, color);            // Bottom circle
    sprite.drawLine(x + 10, y + 4, x + 5, y + 8, color);    // Left branch
    sprite.drawLine(x + 10, y + 4, x + 15, y + 8, color);   // Right branch
    sprite.fillRect(x + 3, y + 7, 4, 4, color);             // Left square
    sprite.fillCircle(x + 15, y + 9, 2, color);             // Right circle
    sprite.drawLine(x + 8, y + 2, x + 12, y + 2, color);    // Top bar
}

void drawIconSettings(int x, int y, uint16_t color) {
    // Gear/cog icon - simplified without trig
    int cx = x + 10, cy = y + 10;
    sprite.fillCircle(cx, cy, 5, color);
    sprite.fillCircle(cx, cy, 2, COL_WHITE);  // Center hole
    // Gear teeth at 8 positions (manual coords)
    sprite.fillRect(cx - 1, cy - 8, 3, 3, color);  // Top
    sprite.fillRect(cx - 1, cy + 5, 3, 3, color);  // Bottom
    sprite.fillRect(cx - 8, cy - 1, 3, 3, color);  // Left
    sprite.fillRect(cx + 5, cy - 1, 3, 3, color);  // Right
    sprite.fillRect(cx + 4, cy - 6, 3, 3, color);  // Top-right
    sprite.fillRect(cx - 6, cy - 6, 3, 3, color);  // Top-left
    sprite.fillRect(cx + 4, cy + 4, 3, 3, color);  // Bottom-right
    sprite.fillRect(cx - 6, cy + 4, 3, 3, color);  // Bottom-left
}

void drawIconServer(int x, int y, uint16_t color) {
    sprite.fillRect(x + 2, y + 2, 16, 5, color);
    sprite.fillRect(x + 2, y + 9, 16, 5, color);
    sprite.fillRect(x + 2, y + 16, 16, 5, color);
    sprite.fillCircle(x + 5, y + 4, 1, COL_GREEN);
    sprite.fillCircle(x + 5, y + 11, 1, COL_GREEN);
    sprite.fillCircle(x + 5, y + 18, 1, COL_YELLOW);
}

void drawIconGamepad(int x, int y, uint16_t color) {
    sprite.fillRoundRect(x + 2, y + 6, 18, 10, 3, color);
    sprite.fillRect(x + 5, y + 9, 4, 1, COL_BLACK);
    sprite.fillRect(x + 6, y + 8, 1, 3, COL_BLACK);
    sprite.fillCircle(x + 14, y + 10, 1, COL_BLACK);
    sprite.fillCircle(x + 16, y + 12, 1, COL_BLACK);
}

void drawIconExit(int x, int y, uint16_t color) {
    sprite.drawRect(x + 4, y + 4, 12, 12, color);
    sprite.drawLine(x + 10, y + 8, x + 18, y + 8, color);
    sprite.drawLine(x + 10, y + 12, x + 18, y + 12, color);
    sprite.drawLine(x + 18, y + 8, x + 18, y + 12, color);
    sprite.drawLine(x + 14, y + 6, x + 18, y + 10, color);
    sprite.drawLine(x + 14, y + 14, x + 18, y + 10, color);
}

void drawIconMic(int x, int y, uint16_t color) {
    // Microphone body (rounded rectangle)
    sprite.fillRoundRect(x + 7, y + 2, 6, 10, 2, color);
    // Stand/base
    sprite.drawArc(x + 10, y + 12, 6, 5, 180, 360, color);
    sprite.drawLine(x + 10, y + 17, x + 10, y + 20, color);
    sprite.drawLine(x + 6, y + 20, x + 14, y + 20, color);
}

void drawIconChat(int x, int y, uint16_t color) {
    // Speech bubble icon
    sprite.fillRoundRect(x + 2, y + 3, 14, 10, 3, color);
    // Tail of speech bubble
    sprite.fillTriangle(x + 4, y + 12, x + 8, y + 12, x + 4, y + 17, color);
    // Second bubble (smaller, offset) for "chat" effect
    sprite.fillRoundRect(x + 10, y + 10, 8, 6, 2, COL_WHITE);
    sprite.drawRoundRect(x + 10, y + 10, 8, 6, 2, color);
}

void renderCocMenu() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(cocMenuColors[cocMenuIndex]);  // Theme color based on selection
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "CocOS Menu");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 28;  // Leave room for status
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    int itemH = 18;
    int maxVisible = contentH / itemH;
    if (maxVisible > COC_MENU_VISIBLE) maxVisible = COC_MENU_VISIBLE;
    
    // Adjust scroll to keep selection visible
    if (cocMenuIndex < cocMenuScroll) {
        cocMenuScroll = cocMenuIndex;
    }
    if (cocMenuIndex >= cocMenuScroll + maxVisible) {
        cocMenuScroll = cocMenuIndex - maxVisible + 1;
    }
    
    // Calculate and draw animated selector
#ifdef ENABLE_SMOOTH_UI
    int visibleIndex = cocMenuIndex - cocMenuScroll;
    int targetY = contentY + 2 + visibleIndex * itemH;
    int targetW = 24 + sprite.textWidth(cocMenuItems[cocMenuIndex]) + 8;  // icon + text + padding
    cocMenuSelectorY = targetY;  // Set target - animation happens automatically
    cocMenuSelectorW = targetW;  // Animate width too
    int selectorY = (int)cocMenuSelectorY;  // Get current animated value
    int selectorW = (int)cocMenuSelectorW;  // Get current animated width
    uint16_t selColor = cocMenuColors[cocMenuIndex];  // Theme color for selection
    
    // Draw animated selector background
    sprite.fillRoundRect(contentX + 1, selectorY, selectorW, itemH - 1, 4, selColor);
#endif
    
    // Draw visible items
    for (int v = 0; v < maxVisible && (cocMenuScroll + v) < COC_MENU_COUNT; v++) {
        int i = cocMenuScroll + v;  // Actual menu item index
        int y = contentY + 2 + v * itemH;
        bool selected = (i == cocMenuIndex);
        
#ifdef ENABLE_SMOOTH_UI
        // Check if this item overlaps with animated selector
        int selTop = selectorY;
        int selBot = selectorY + itemH - 1;
        bool inSelector = (y >= selTop - itemH && y <= selBot);
        
        if (inSelector && selected) {
            sprite.setTextColor(COL_WHITE, selColor);
        } else {
            sprite.setTextColor(COL_BLACK, COL_WHITE);
        }
#else
        if (selected) {
            int selW = 24 + sprite.textWidth(cocMenuItems[i]) + 8;  // icon + text + padding
            sprite.fillRoundRect(contentX + 1, y, selW, itemH - 1, 4, cocMenuColors[i]);
            sprite.setTextColor(COL_WHITE, cocMenuColors[i]);
        } else {
            sprite.setTextColor(COL_BLACK, COL_WHITE);
        }
#endif
        
        // Draw icon
        int iconX = contentX + 4;
        int iconY = y + 1;
        uint16_t iconColor = selected ? COL_WHITE : COL_BLACK;
        
        switch (i) {
            case 0: drawFolderIcon(iconX, iconY); break;          // File Browser
            case 1: drawIconChat(iconX, iconY, iconColor); break;   // WiFi Chat
            case 2: drawIconMic(iconX, iconY, iconColor); break;    // Mic Recorder
            case 3: drawIconBluetooth(iconX, iconY, iconColor); break; // BT Audio
            case 4: drawIconWifi(iconX, iconY, iconColor); break;   // WiFi
            case 5: drawIconUSB(iconX, iconY, iconColor); break;    // USB Transfer
            case 6: drawIconSettings(iconX, iconY, iconColor); break; // Settings
            case 7: drawIconGamepad(iconX, iconY, iconColor); break; // Games
            case 8: drawIconExit(iconX, iconY, iconColor); break;   // Exit
            default: drawFileIcon(iconX, iconY); break;
        }
        
        // Menu text
        sprite.setCursor(contentX + 24, y + 5);
        sprite.print(cocMenuItems[i]);
        
        // Right side status (always on white background, not highlighted)
        sprite.setCursor(contentX + contentW - 80, y + 5);
        switch (i) {
            case 1:  // WiFi Chat
                if (chatActive && chatIsHost) {
                    sprite.setTextColor(COL_BLUE, COL_WHITE);
                    sprite.print("Hosting");
                } else if (chatActive && chatDirectMode) {
                    sprite.setTextColor(COL_GREEN, COL_WHITE);
                    sprite.print("Direct");
                } else if (chatActive) {
                    sprite.setTextColor(COL_GREEN, COL_WHITE);
                    sprite.print("[ON]");
                } else {
                    sprite.setTextColor(COL_GRAY_MID, COL_WHITE);
                    sprite.print("Ready");
                }
                break;
            case 2:  // Mic Recorder
                if (micRecording) {
                    sprite.setTextColor(COL_RED, COL_WHITE);
                    sprite.print("[REC]");
                }
                break;
            case 3:  // BT Audio
                if (btAudioConnected) {
                    sprite.setTextColor(COL_GREEN, COL_WHITE);
                    sprite.print("Paired");
                } else {
                    sprite.setTextColor(COL_GRAY_MID, COL_WHITE);
                    sprite.print("Off");
                }
                break;
            case 4:  // WiFi
                if (WiFi.status() == WL_CONNECTED) {
                    sprite.setTextColor(COL_GREEN, COL_WHITE);
                    sprite.print("Connected");
                } else {
                    sprite.setTextColor(COL_GRAY_MID, COL_WHITE);
                    sprite.print("Off");
                }
                break;
            case 5:  // USB Transfer
                if (usbModeActive) {
                    sprite.setTextColor(COL_GREEN, COL_WHITE);
                    sprite.print("[Active]");
                }
                break;
            case 6:  // Settings
                sprite.setTextColor(COL_GRAY_MID, COL_WHITE);
                sprite.print("Brt:" + String(configBrightness));
                break;
        }
    }
    
    // Draw scrollbar if needed
    if (COC_MENU_COUNT > maxVisible) {
        int scrollBarX = contentX + contentW - 10;
        int scrollBarH = contentH - 4;
        sprite.fillRect(scrollBarX, contentY + 2, 8, scrollBarH, COL_GRAY_LIGHT);
        draw3DSunken(scrollBarX, contentY + 2, 8, scrollBarH);
        
        // Thumb
        int thumbH = max(10, scrollBarH * maxVisible / COC_MENU_COUNT);
        int thumbY = contentY + 2 + (scrollBarH - thumbH) * cocMenuScroll / max(1, COC_MENU_COUNT - maxVisible);
        sprite.fillRect(scrollBarX + 1, thumbY, 6, thumbH, COL_GRAY_MID);
        draw3DRaised(scrollBarX + 1, thumbY, 6, thumbH);
    }
    
    // Item counter
    sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
    sprite.setCursor(contentX + 4, contentY + contentH + 10);
    sprite.print(String(cocMenuIndex + 1) + "/" + String(COC_MENU_COUNT));
    
    // Status bar
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    sprite.print(";/.:Nav Ent:Sel Fn+T:Term");
    
    sprite.pushSprite(0, 0);
}

void handleCocMenuInput() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    bool fnPressed = status.fn;
    
    // Fn+T - Open Terminal
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_T) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('t'))) {
        playSelectSound();
        currentMode = MODE_TERMINAL;
        needsRedraw = true;
        delay(150);
        return;
    }
    
    // Fn+P - Screenshot
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_P) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('p'))) {
        takeScreenshot();
        renderCocMenu();
        delay(150);
        return;
    }
    
    // Navigate
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
        playClickSound();
        cocMenuIndex = (cocMenuIndex > 0) ? cocMenuIndex - 1 : COC_MENU_COUNT - 1;
        renderCocMenu();
        delay(100);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
        playClickSound();
        cocMenuIndex = (cocMenuIndex < COC_MENU_COUNT - 1) ? cocMenuIndex + 1 : 0;
        renderCocMenu();
        delay(100);
        return;
    }
    
    // Select
    if (status.enter) {
        playSelectSound();
        currentThemeColor = cocMenuColors[cocMenuIndex];  // Set theme color for submenu
        switch (cocMenuIndex) {
            case 0:  // File Browser
                currentPath = "/";
                uiSelectedIndex = 0;
                uiScrollOffset = 0;
                fileCurrentPage = 0;
                loadFileList();
                currentMode = MODE_FILE_UI;
                renderFileUI();
                break;
            case 1:  // WiFi Chat
                chatInputBuffer = "";
                chatScrollOffset = 0;
                chatSelectedRoom = 0;
                chatInRoom = false;
                chatCurrentRoom = -1;
                chatConnectMode = CHAT_MODE_SELECT;
                chatModeSelection = 0;
                chatDirectMode = false;
                chatIsHost = false;
                currentMode = MODE_COC_CHAT;
                renderCocChat();
                break;
            case 2:  // Mic Recorder
                currentMode = MODE_MIC_RECORDER;
                renderMicRecorder();
                break;
            case 3:  // BT Audio
                btAudioScanIndex = 0;
                currentMode = MODE_BT_AUDIO;
                renderBtAudio();
                break;
            case 4:  // WiFi
                cocWifiScanning = false;
                cocWifiNetworks.clear();
                cocSubMenuIndex = 0;
                cocWifiEnteringPass = false;
                currentMode = MODE_COC_WIFI;
                renderCocWifi();
                break;
            case 5:  // USB Transfer
                currentMode = MODE_COC_USB;
                renderCocUSB();
                break;
            case 6:  // Settings
                cocSettingsIndex = 0;
                cocSettingsScroll = 0;
                currentMode = MODE_COC_SETTINGS;
                renderCocSettings();
                break;
            case 7:  // Games
                cocGameMenuIndex = 0;
                currentMode = MODE_COC_GAMES;
                renderCocGames();
                break;
            case 8:  // Exit to Terminal
                currentMode = MODE_TERMINAL;
                needsRedraw = true;
                break;
        }
        delay(150);
        return;
    }
    
    // ESC - Exit to terminal
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        playBackSound();
        currentMode = MODE_TERMINAL;
        needsRedraw = true;
        delay(150);
        return;
    }
}

// ==========================================
//     COC UI - Web Server Submenu
// ==========================================

void startApMode() {
    // Stop any existing connections
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    delay(200);
    
    // Set AP mode
    WiFi.mode(WIFI_AP);
    delay(200);
    
    // Configure and start AP
    // Parameters: ssid, password, channel, hidden, max_connections
    bool result;
    if (cocApPassword.length() >= 8) {
        // WPA2 with password (channel 6, not hidden, max 4 connections)
        result = WiFi.softAP(cocApSSID.c_str(), cocApPassword.c_str(), 6, false, 4);
    } else {
        // Open network (no password)
        result = WiFi.softAP(cocApSSID.c_str(), NULL, 6, false, 4);
    }
    
    delay(500);
    
    if (result) {
        // Configure IP
        IPAddress local_IP(192, 168, 4, 1);
        IPAddress gateway(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);
        WiFi.softAPConfig(local_IP, gateway, subnet);
        
        startWebServer();
        cocApModeEnabled = true;
    }
}

void stopApMode() {
    stopWebServer();
    WiFi.softAPdisconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    cocApModeEnabled = false;
}

void renderCocWebServer() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "Web Server");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 12;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    int itemH = 28;
    int baseY = contentY + 4;
    
    const char* webServerItems[] = {"SD Share (WiFi)", "SD Share (AP Hotspot)"};
    
#ifdef ENABLE_SMOOTH_UI
    // Calculate target position and draw animated selector
    int targetY = baseY + cocSubMenuIndex * itemH;
    int targetW = 6 + sprite.textWidth(webServerItems[cocSubMenuIndex]) + 8;
    webServerSelectorY = targetY;
    webServerSelectorW = targetW;
    int selectorY = (int)webServerSelectorY;
    int selectorW = (int)webServerSelectorW;
    
    // Draw animated selector
    sprite.fillRoundRect(contentX + 1, selectorY, selectorW, itemH - 2, 4, currentThemeColor);
#endif
    
    int y = baseY;
    
    // Option 1: SD Share via WiFi
    bool sel1 = (cocSubMenuIndex == 0);
#ifdef ENABLE_SMOOTH_UI
    int selTop = selectorY;
    int selBot = selectorY + itemH - 2;
    bool inSel1 = (y >= selTop - itemH && y <= selBot) && sel1;
    if (inSel1) {
        sprite.setTextColor(COL_WHITE, currentThemeColor);
    } else {
        sprite.setTextColor(COL_BLACK, COL_WHITE);
    }
#else
    if (sel1) {
        sprite.fillRoundRect(contentX + 1, y, 6 + sprite.textWidth(webServerItems[0]) + 8, itemH - 2, 4, currentThemeColor);
        sprite.setTextColor(COL_WHITE, currentThemeColor);
    } else {
        sprite.setTextColor(COL_BLACK, COL_WHITE);
    }
#endif
    sprite.setCursor(contentX + 6, y + 3);
    sprite.print(webServerItems[0]);
    
    // Toggle button
    bool wifiShareOn = sdShareEnabled && !cocApModeEnabled;
    int btnX = contentX + contentW - 45;
    sprite.fillRect(btnX, y + 2, 38, 12, wifiShareOn ? COL_GREEN : COL_GRAY_MID);
    draw3DRaised(btnX, y + 2, 38, 12);
    sprite.setTextColor(COL_WHITE, wifiShareOn ? COL_GREEN : COL_GRAY_MID);
    sprite.setCursor(btnX + (wifiShareOn ? 10 : 12), y + 4);
    sprite.print(wifiShareOn ? "ON" : "OFF");
    
    // Status line (always on white)
    sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
    sprite.setCursor(contentX + 10, y + 16);
    if (WiFi.status() != WL_CONNECTED && !cocApModeEnabled) {
        sprite.print("Requires WiFi");
    } else if (wifiShareOn) {
        sprite.print(WiFi.localIP().toString());
    }
    
    y += itemH;
    
    // Option 2: SD Share via AP Mode
    bool sel2 = (cocSubMenuIndex == 1);
#ifdef ENABLE_SMOOTH_UI
    bool inSel2 = (y >= selTop - itemH && y <= selBot) && sel2;
    if (inSel2) {
        sprite.setTextColor(COL_WHITE, currentThemeColor);
    } else {
        sprite.setTextColor(COL_BLACK, COL_WHITE);
    }
#else
    if (sel2) {
        sprite.fillRoundRect(contentX + 1, y, 6 + sprite.textWidth(webServerItems[1]) + 8, itemH - 2, 4, currentThemeColor);
        sprite.setTextColor(COL_WHITE, currentThemeColor);
    } else {
        sprite.setTextColor(COL_BLACK, COL_WHITE);
    }
#endif
    sprite.setCursor(contentX + 6, y + 3);
    sprite.print(webServerItems[1]);
    
    // Toggle button
    sprite.fillRect(btnX, y + 2, 38, 12, cocApModeEnabled ? COL_GREEN : COL_GRAY_MID);
    draw3DRaised(btnX, y + 2, 38, 12);
    sprite.setTextColor(COL_WHITE, cocApModeEnabled ? COL_GREEN : COL_GRAY_MID);
    sprite.setCursor(btnX + (cocApModeEnabled ? 10 : 12), y + 4);
    sprite.print(cocApModeEnabled ? "ON" : "OFF");
    
    // AP info (always on white)
    sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
    sprite.setCursor(contentX + 10, y + 16);
    if (cocApModeEnabled) {
        sprite.print("IP: 192.168.4.1");
    } else {
        sprite.print(cocApSSID + "/" + cocApPassword);
    }
    
    y += itemH + 4;
    
    // Info box
    sprite.fillRect(contentX + 2, y, contentW - 4, 32, COL_GRAY_LIGHT);
    draw3DSunken(contentX + 2, y, contentW - 4, 32);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(contentX + 6, y + 4);
    sprite.print("WiFi: Use existing network");
    sprite.setCursor(contentX + 6, y + 14);
    sprite.print("AP: Create own hotspot");
    sprite.setCursor(contentX + 6, y + 24);
    sprite.print("Edit: /usrconfig.cfg");
    
    // Status bar
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    sprite.print("Enter:Toggle  Esc:Back");
    
    sprite.pushSprite(0, 0);
}

void handleCocWebServerInput() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    bool fnPressed = status.fn;
    
    // Fn+T - Open Terminal
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_T) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('t'))) {
        playSelectSound();
        currentMode = MODE_TERMINAL;
        needsRedraw = true;
        delay(150);
        return;
    }
    
    // Fn+P - Screenshot
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_P) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('p'))) {
        takeScreenshot();
        renderCocWebServer();
        delay(150);
        return;
    }
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP) || M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
        playClickSound();
        cocSubMenuIndex = (cocSubMenuIndex == 0) ? 1 : 0;
        renderCocWebServer();
        delay(100);
        return;
    }
    
    if (status.enter) {
        playSelectSound();
        if (cocSubMenuIndex == 0) {
            // Toggle WiFi SD Share
            if (!sdShareEnabled && !cocApModeEnabled) {
                if (WiFi.status() == WL_CONNECTED) {
                    startWebServer();
                }
            } else if (sdShareEnabled && !cocApModeEnabled) {
                stopWebServer();
            }
        } else {
            // Toggle AP Mode
            if (!cocApModeEnabled) {
                if (sdShareEnabled) stopWebServer();
                startApMode();
            } else {
                stopApMode();
            }
        }
        renderCocWebServer();
        delay(150);
        return;
    }
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        playBackSound();
        currentMode = MODE_COC_MENU;
        renderCocMenu();
        delay(150);
        return;
    }
}

// ==========================================
//     COC UI - WiFi Chat with Rooms
// ==========================================

// Room colors for the 2x2 grid
const uint16_t chatRoomColors[] = {
    0xF800,  // Red - Room 1
    0x07E0,  // Green - Room 2
    0x001F,  // Blue - Room 3
    0xFFE0   // Yellow - Room 4
};

// Start hosting a direct chat AP
void startChatHost() {
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    delay(100);
    
    WiFi.mode(WIFI_AP);
    delay(100);
    
    // Create AP with nickname in SSID
    String apSSID = String(CHAT_AP_PREFIX) + chatNickname;
    WiFi.softAP(apSSID.c_str(), "cardputer", 6, false, 4);
    delay(500);
    
    IPAddress local_IP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(local_IP, gateway, subnet);
    
    chatIsHost = true;
    chatDirectMode = true;
    chatConnectMode = CHAT_MODE_WIFI;  // Move to room selection
    
    // Start UDP for chat
    chatUdp.begin(CHAT_UDP_PORT);
    chatActive = true;
    chatInRoom = true;
    chatCurrentRoom = 0;  // Direct mode uses room 0
    chatMessages.clear();
    chatMessages.push_back("[System] Hosting chat as " + chatNickname);
    chatMessages.push_back("[System] Others can join: " + apSSID);
    chatMessages.push_back("[System] Password: cardputer");
}

// Scan for chat APs
void startChatScan() {
    chatConnectMode = CHAT_MODE_SCANNING;
    chatFoundAPs.clear();
    chatScanStart = millis();
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.startsWith(CHAT_AP_PREFIX)) {
            chatFoundAPs.push_back(ssid);
        }
    }
    WiFi.scanDelete();
    
    chatAPSelection = 0;
    chatConnectMode = CHAT_MODE_JOIN;
}

// Connect to a chat AP
void joinChatAP(String ssid) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), "cardputer");
    
    // Wait for connection
    int timeout = 20;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
        delay(500);
        timeout--;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        chatIsHost = false;
        chatDirectMode = true;
        chatConnectMode = CHAT_MODE_WIFI;
        
        // Start UDP for chat
        chatUdp.begin(CHAT_UDP_PORT);
        chatActive = true;
        chatInRoom = true;
        chatCurrentRoom = 0;
        chatMessages.clear();
        String hostName = ssid.substring(String(CHAT_AP_PREFIX).length());
        chatMessages.push_back("[System] Connected to " + hostName);
        chatMessages.push_back("[System] IP: " + WiFi.localIP().toString());
    } else {
        chatConnectMode = CHAT_MODE_JOIN;  // Stay in join mode
    }
}

// Stop direct chat and cleanup
void stopDirectChat() {
    if (chatDirectMode) {
        chatUdp.stop();
        chatActive = false;
        chatInRoom = false;
        chatCurrentRoom = -1;
        
        if (chatIsHost) {
            WiFi.softAPdisconnect(true);
        } else {
            WiFi.disconnect();
        }
        WiFi.mode(WIFI_STA);
        
        chatIsHost = false;
        chatDirectMode = false;
        chatConnectMode = CHAT_MODE_SELECT;
    }
}

void startChatRoom(int room) {
    // Check if we have connection (either WiFi or direct mode)
    if (!chatDirectMode && WiFi.status() != WL_CONNECTED) return;
    if (room < 0 || room >= CHAT_NUM_ROOMS) return;
    
    chatUdp.begin(CHAT_UDP_PORT + room);  // Each room uses different port
    chatActive = true;
    chatInRoom = true;
    chatCurrentRoom = room;
    chatMessages.clear();
    chatMessages.push_back("[System] Joined Room " + String(room + 1));
    
    // Broadcast join message with room info
    String joinMsg = "R" + String(room) + ":" + chatNickname + " joined";
    broadcastChatMessage(joinMsg);
    
    // Broadcast presence for room count
    broadcastRoomPresence();
}

void stopChatRoom() {
    if (chatActive && chatCurrentRoom >= 0) {
        // Broadcast leave message
        String leaveMsg = "R" + String(chatCurrentRoom) + ":" + chatNickname + " left";
        broadcastChatMessage(leaveMsg);
        
        chatUdp.stop();
        chatActive = false;
        chatInRoom = false;
        chatCurrentRoom = -1;
    }
}

void broadcastChatMessage(String message) {
    if (!chatActive || chatCurrentRoom < 0) return;
    
    IPAddress broadcastIP;
    if (chatDirectMode) {
        // In direct mode, use the AP's subnet broadcast
        if (chatIsHost) {
            broadcastIP = IPAddress(192, 168, 4, 255);
        } else {
            broadcastIP = WiFi.localIP();
            broadcastIP[3] = 255;
        }
        chatUdp.beginPacket(broadcastIP, CHAT_UDP_PORT);
    } else {
        broadcastIP = WiFi.localIP();
        broadcastIP[3] = 255;
        chatUdp.beginPacket(broadcastIP, CHAT_UDP_PORT + chatCurrentRoom);
    }
    chatUdp.print(message);
    chatUdp.endPacket();
}

void broadcastRoomPresence() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    // Broadcast presence on discovery port
    IPAddress broadcastIP = WiFi.localIP();
    broadcastIP[3] = 255;
    
    WiFiUDP discoveryUdp;
    discoveryUdp.begin(CHAT_UDP_PORT + 10);
    discoveryUdp.beginPacket(broadcastIP, CHAT_UDP_PORT + 10);
    String presence = "P:" + String(chatCurrentRoom) + ":" + chatNickname;
    discoveryUdp.print(presence);
    discoveryUdp.endPacket();
    discoveryUdp.stop();
}

void checkRoomPresence() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    // Listen for presence broadcasts
    static WiFiUDP discoveryUdp;
    static bool discoveryStarted = false;
    
    if (!discoveryStarted) {
        discoveryUdp.begin(CHAT_UDP_PORT + 10);
        discoveryStarted = true;
    }
    
    int packetSize = discoveryUdp.parsePacket();
    if (packetSize > 0) {
        char buffer[64];
        int len = discoveryUdp.read(buffer, sizeof(buffer) - 1);
        if (len > 0) {
            buffer[len] = '\0';
            String msg = String(buffer);
            
            // Parse presence: "P:room:nickname"
            if (msg.startsWith("P:")) {
                int colonIdx = msg.indexOf(':', 2);
                if (colonIdx > 2) {
                    int room = msg.substring(2, colonIdx).toInt();
                    if (room >= 0 && room < CHAT_NUM_ROOMS) {
                        // Update room count (simplified - just increment)
                        // In real implementation, track unique users
                        chatRoomUsers[room] = min(CHAT_MAX_USERS, chatRoomUsers[room] + 1);
                    }
                }
            }
        }
    }
    
    // Decay room counts over time (reset every 5 seconds if not refreshed)
    static unsigned long lastDecay = 0;
    if (millis() - lastDecay > 5000) {
        lastDecay = millis();
        for (int i = 0; i < CHAT_NUM_ROOMS; i++) {
            if (i != chatCurrentRoom) {
                chatRoomUsers[i] = max(0, chatRoomUsers[i] - 1);
            }
        }
    }
}

void sendChatMessage(String message) {
    if (!chatActive || message.length() == 0) return;
    
    String fullMsg;
    if (chatDirectMode) {
        fullMsg = chatNickname + ": " + message;
    } else {
        if (chatCurrentRoom < 0) return;
        fullMsg = "R" + String(chatCurrentRoom) + ":" + chatNickname + ": " + message;
    }
    
    // Add to local messages
    addChatMessage(chatNickname + ": " + message);
    
    // Broadcast to others
    broadcastChatMessage(fullMsg);
}

void addChatMessage(String message) {
    chatMessages.push_back(message);
    
    // Limit message history
    while (chatMessages.size() > CHAT_MAX_MESSAGES) {
        chatMessages.erase(chatMessages.begin());
    }
    
    // Auto-scroll to bottom
    int visibleLines = 5;
    if ((int)chatMessages.size() > visibleLines) {
        chatScrollOffset = chatMessages.size() - visibleLines;
    }
}

void checkIncomingChat() {
    if (!chatActive) return;
    
    int packetSize = chatUdp.parsePacket();
    if (packetSize > 0) {
        char buffer[256];
        int len = chatUdp.read(buffer, sizeof(buffer) - 1);
        if (len > 0) {
            buffer[len] = '\0';
            String msg = String(buffer);
            
            IPAddress remoteIP = chatUdp.remoteIP();
            IPAddress myIP = chatDirectMode ? 
                (chatIsHost ? IPAddress(192, 168, 4, 1) : WiFi.localIP()) : 
                WiFi.localIP();
            
            if (remoteIP != myIP) {
                if (chatDirectMode) {
                    // Direct mode - messages don't have room prefix
                    addChatMessage(msg);
                    M5Cardputer.Speaker.tone(1000, 50);
                } else {
                    // WiFi mode - parse room message: "Rn:message"
                    if (chatCurrentRoom >= 0 && msg.startsWith("R" + String(chatCurrentRoom) + ":")) {
                        String content = msg.substring(3);  // Remove "Rn:"
                        addChatMessage(content);
                        M5Cardputer.Speaker.tone(1000, 50);
                    }
                }
            }
        }
    }
}

// Render mode selection screen (WiFi/Host/Join)
void renderChatModeSelect() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "WiFi Chat - Connect");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 5;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 20;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    // Connection options
    const char* options[] = {"Use WiFi Network", "Host Direct Chat", "Join Direct Chat"};
    const char* descriptions[] = {"Connect via router", "Others join you", "Find & join host"};
    uint16_t optColors[] = {COL_GREEN, COL_BLUE, COL_YELLOW};
    
    int btnH = 26;
    int btnW = contentW - 20;
    int btnGap = 4;
    int totalBtnH = btnH + btnGap;
    
    // Calculate scroll offset to keep selected item visible
    int visibleHeight = contentH - 4;  // Available space for buttons
    int totalHeight = 3 * totalBtnH - btnGap;  // Total height of all buttons
    int scrollOffset = 0;
    
    if (totalHeight > visibleHeight) {
        // Need scrolling - calculate offset based on selection
        int selectedTop = chatModeSelection * totalBtnH;
        int selectedBottom = selectedTop + btnH;
        
        if (selectedBottom > visibleHeight) {
            scrollOffset = selectedBottom - visibleHeight;
        }
    }
    
    int btnY = contentY + 2 - scrollOffset;
    
#ifdef ENABLE_SMOOTH_UI
    // Calculate animated selector position
    int targetY = btnY + chatModeSelection * totalBtnH;
    chatModeSelectorY = targetY;
    int animY = (int)chatModeSelectorY;
    
    // Clip animated selector to content area
    if (animY >= contentY && animY + btnH <= contentY + contentH) {
        sprite.fillRoundRect(contentX + 10, animY, btnW, btnH, 4, optColors[chatModeSelection]);
        draw3DSunken(contentX + 10, animY, btnW, btnH);
    }
#endif
    
    for (int i = 0; i < 3; i++) {
        int y = btnY + i * totalBtnH;
        bool sel = (chatModeSelection == i);
        
        // Skip if button is outside visible area
        if (y + btnH < contentY || y > contentY + contentH) continue;
        
#ifdef ENABLE_SMOOTH_UI
        // Check if this button overlaps with animated selector
        int selTop = animY;
        int selBot = animY + btnH;
        bool inSelector = (y >= selTop - btnH && y + btnH >= selTop && y <= selBot);
        
        if (!inSelector) {
            // Draw non-selected button
            sprite.fillRoundRect(contentX + 10, y, btnW, btnH, 4, COL_GRAY_LIGHT);
            draw3DRaised(contentX + 10, y, btnW, btnH);
        }
        
        // Determine colors based on animation position
        uint16_t bgColor = inSelector ? optColors[chatModeSelection] : COL_GRAY_LIGHT;
        uint16_t textColor = inSelector ? COL_WHITE : COL_BLACK;
        uint16_t descColor = inSelector ? COL_GRAY_LIGHT : COL_GRAY_DARK;
#else
        // Button background
        sprite.fillRoundRect(contentX + 10, y, btnW, btnH, 4, sel ? optColors[i] : COL_GRAY_LIGHT);
        if (sel) {
            draw3DSunken(contentX + 10, y, btnW, btnH);
        } else {
            draw3DRaised(contentX + 10, y, btnW, btnH);
        }
        
        uint16_t bgColor = sel ? optColors[i] : COL_GRAY_LIGHT;
        uint16_t textColor = sel ? COL_WHITE : COL_BLACK;
        uint16_t descColor = sel ? COL_GRAY_LIGHT : COL_GRAY_DARK;
#endif
        
        // Text
        sprite.setTextColor(textColor, bgColor);
        sprite.setCursor(contentX + 18, y + 4);
        sprite.print(options[i]);
        sprite.setCursor(contentX + 18, y + 15);
        sprite.setTextColor(descColor, bgColor);
        sprite.print(descriptions[i]);
        
        // Status indicator for WiFi option
        if (i == 0) {
            sprite.setCursor(contentX + btnW - 30, y + 10);
            if (WiFi.status() == WL_CONNECTED) {
                sprite.setTextColor(COL_GREEN, bgColor);
                sprite.print("OK");
            } else {
                sprite.setTextColor(COL_RED, bgColor);
                sprite.print("--");
            }
        }
    }
    
    // Status bar
    sprite.fillRect(winX + 3, winY + winH - 14, winW - 6, 11, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 14, winW - 6, 11);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 12);
    sprite.print("Up/Down:Select  Enter:Choose  Esc:Back");
    
    sprite.pushSprite(0, 0);
}

// Render AP scan/join screen
void renderChatJoinSelect() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "WiFi Chat - Join");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 5;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 20;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    if (chatConnectMode == CHAT_MODE_SCANNING) {
        sprite.setTextColor(COL_BLACK, COL_WHITE);
        sprite.setCursor(contentX + 40, contentY + 45);
        sprite.print("Scanning...");
        sprite.pushSprite(0, 0);
        return;
    }
    
    if (chatFoundAPs.empty()) {
        sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
        sprite.setCursor(contentX + 15, contentY + 35);
        sprite.print("No chat hosts found");
        sprite.setCursor(contentX + 15, contentY + 50);
        sprite.print("Press Enter to scan again");
    } else {
        sprite.setTextColor(COL_BLACK, COL_WHITE);
        sprite.setCursor(contentX + 10, contentY + 5);
        sprite.print("Found " + String(chatFoundAPs.size()) + " host(s):");
        
        int itemH = 22;
        int maxVisible = (contentH - 25) / itemH;
        
        for (int i = 0; i < min((int)chatFoundAPs.size(), maxVisible); i++) {
            int y = contentY + 20 + i * itemH;
            bool sel = (chatAPSelection == i);
            
            sprite.fillRoundRect(contentX + 5, y, contentW - 10, itemH - 2, 3, sel ? COL_BLUE : COL_GRAY_LIGHT);
            sprite.setTextColor(sel ? COL_WHITE : COL_BLACK, sel ? COL_BLUE : COL_GRAY_LIGHT);
            sprite.setCursor(contentX + 12, y + 6);
            
            // Show host name (remove prefix)
            String hostName = chatFoundAPs[i].substring(String(CHAT_AP_PREFIX).length());
            sprite.print(hostName);
        }
    }
    
    // Status bar
    sprite.fillRect(winX + 3, winY + winH - 14, winW - 6, 11, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 14, winW - 6, 11);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 12);
    if (chatFoundAPs.empty()) {
        sprite.print("Enter:Scan  Esc:Back");
    } else {
        sprite.print("Enter:Join  Tab:Rescan  Esc:Back");
    }
    
    sprite.pushSprite(0, 0);
}

void renderChatRoomSelect() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "WiFi Chat - Select Room");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 5;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 20;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    // Check connection status (WiFi or direct mode)
    if (!chatDirectMode && WiFi.status() != WL_CONNECTED) {
        sprite.setTextColor(COL_RED, COL_WHITE);
        sprite.setCursor(contentX + 20, contentY + 40);
        sprite.print("Not connected!");
        sprite.pushSprite(0, 0);
        return;
    }
    
    // 2x2 grid of room buttons
    int gridX = contentX + 15;
    int gridY = contentY + 8;
    int btnW = (contentW - 40) / 2;
    int btnH = (contentH - 30) / 2;
    int gap = 6;
    
    // Calculate animated selector position
#ifdef ENABLE_SMOOTH_UI
    int selCol = chatSelectedRoom % 2;
    int selRow = chatSelectedRoom / 2;
    int targetX = gridX + selCol * (btnW + gap);
    int targetY = gridY + selRow * (btnH + gap);
    chatSelectorX = targetX;
    chatSelectorY = targetY;
    int animX = (int)chatSelectorX;
    int animY = (int)chatSelectorY;
    
    // Draw animated selector highlight
    sprite.drawRoundRect(animX - 2, animY - 2, btnW + 4, btnH + 4, 6, currentThemeColor);
    sprite.drawRoundRect(animX - 3, animY - 3, btnW + 6, btnH + 6, 7, currentThemeColor);
#endif
    
    // Draw 4 room buttons
    for (int i = 0; i < CHAT_NUM_ROOMS; i++) {
        int col = i % 2;
        int row = i / 2;
        int bx = gridX + col * (btnW + gap);
        int by = gridY + row * (btnH + gap);
        
        bool selected = (i == chatSelectedRoom);
        uint16_t roomColor = chatRoomColors[i];
        
        // Button background
        sprite.fillRoundRect(bx, by, btnW, btnH, 5, roomColor);
        
        // 3D effect
        if (selected) {
            draw3DSunken(bx, by, btnW, btnH);
        } else {
            draw3DRaised(bx, by, btnW, btnH);
        }
        
        // Room number (top)
        sprite.setTextColor(COL_WHITE, roomColor);
        sprite.setCursor(bx + btnW/2 - 15, by - 1);
        sprite.print("Room ");
        sprite.print(i + 1);
        
        // User count in center (moved up 5px from old position)
        sprite.setCursor(bx + btnW/2 - 10, by + btnH/2 - 5);
        sprite.setTextColor(COL_WHITE, roomColor);
        int users = (chatCurrentRoom == i) ? chatRoomUsers[i] + 1 : chatRoomUsers[i];
        users = min(users, CHAT_MAX_USERS);
        sprite.print(String(users) + "/" + String(CHAT_MAX_USERS));
        
        // Status (bottom - moved down 5px from old position)
        sprite.setCursor(bx + btnW/2 - 15, by + btnH - 12);
        if (users >= CHAT_MAX_USERS) {
            sprite.setTextColor(COL_RED, roomColor);
            sprite.print("FULL");
        } else if (users > 0) {
            sprite.setTextColor(COL_YELLOW, roomColor);
            sprite.print("Active");
        } else {
            sprite.setTextColor(COL_GRAY_LIGHT, roomColor);
            sprite.print("Empty");
        }
    }
    
    // Nickname display
    sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
    sprite.setCursor(contentX + 4, contentY + contentH - 10);
    sprite.print("Nick: ");
    sprite.setTextColor(COL_BLACK, COL_WHITE);
    sprite.print(chatNickname);
    
    // Status bar
    sprite.fillRect(winX + 3, winY + winH - 14, winW - 6, 11, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 14, winW - 6, 11);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 12);
    sprite.print("Arrows:Select  Enter:Join  Esc:Back");
    
    sprite.pushSprite(0, 0);
}

void renderChatRoom() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    
    uint16_t roomColor = chatRoomColors[chatCurrentRoom];
    sprite.fillSprite(roomColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    String title = "Room " + String(chatCurrentRoom + 1) + " - " + chatNickname;
    drawWindowFrame(winX, winY, winW, winH, title.c_str());
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 3;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 36;
    
    // Message area
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    draw3DSunken(contentX, contentY, contentW, contentH);
    
    // Draw messages
    int lineH = 10;
    int visibleLines = (contentH - 4) / lineH;
    int startIdx = max(0, (int)chatMessages.size() - visibleLines);
    
    for (int i = startIdx; i < (int)chatMessages.size(); i++) {
        int y = contentY + 4 + (i - startIdx) * lineH;
        sprite.setCursor(contentX + 4, y);
        
        String msg = chatMessages[i];
        
        // Color based on message type
        if (msg.startsWith("[System]") || msg.endsWith("joined") || msg.endsWith("left")) {
            sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
        } else if (msg.startsWith(chatNickname + ":")) {
            sprite.setTextColor(COL_BLUE, COL_WHITE);
        } else {
            sprite.setTextColor(COL_BLACK, COL_WHITE);
        }
        
        // Truncate long messages
        if (msg.length() > 36) {
            msg = msg.substring(0, 33) + "...";
        }
        sprite.print(msg);
    }
    
    // Input area
    int inputY = winY + winH - 30;
    sprite.fillRect(contentX, inputY, contentW, 14, COL_WHITE);
    draw3DSunken(contentX, inputY, contentW, 14);
    sprite.setTextColor(COL_BLACK, COL_WHITE);
    sprite.setCursor(contentX + 4, inputY + 3);
    sprite.print("> ");
    
    String displayInput = chatInputBuffer;
    if (displayInput.length() > 32) {
        displayInput = displayInput.substring(displayInput.length() - 32);
    }
    sprite.print(displayInput);
    sprite.print("_");
    
    // Status bar
    sprite.fillRect(winX + 3, winY + winH - 14, winW - 6, 11, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 14, winW - 6, 11);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 12);
    sprite.print("Enter:Send  Esc:Leave Room");
    
    sprite.pushSprite(0, 0);
}

void renderCocChat() {
    // Direct mode active - show chat room directly
    if (chatDirectMode && chatInRoom) {
        renderChatRoom();
        return;
    }
    
    // In a room (WiFi mode)
    if (chatInRoom && chatCurrentRoom >= 0) {
        renderChatRoom();
        return;
    }
    
    // Mode selection screen
    if (chatConnectMode == CHAT_MODE_SELECT) {
        renderChatModeSelect();
        return;
    }
    
    // Join screen (scanning or selecting AP)
    if (chatConnectMode == CHAT_MODE_JOIN || chatConnectMode == CHAT_MODE_SCANNING) {
        renderChatJoinSelect();
        return;
    }
    
    // Room selection (WiFi mode)
    renderChatRoomSelect();
}

void handleCocChatInput() {
    // Check for room presence updates (only in WiFi mode)
    if (!chatDirectMode) {
        checkRoomPresence();
    }
    
    // Broadcast our presence periodically
    if (chatActive && !chatDirectMode && millis() - lastRoomBroadcast > 2000) {
        lastRoomBroadcast = millis();
        broadcastRoomPresence();
    }
    
    // Check for incoming messages if in room
    if (chatInRoom) {
        checkIncomingChat();
    }
    
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    bool fnPressed = status.fn;
    
    // Fn+T - Open Terminal (always available)
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_T) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('t'))) {
        if (chatDirectMode) stopDirectChat();
        else if (chatInRoom) stopChatRoom();
        playSelectSound();
        currentMode = MODE_TERMINAL;
        needsRedraw = true;
        delay(150);
        return;
    }
    
    // ===== MODE SELECT SCREEN =====
    if (chatConnectMode == CHAT_MODE_SELECT) {
        // Up/Down navigation
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
            playClickSound();
            chatModeSelection = (chatModeSelection > 0) ? chatModeSelection - 1 : 2;
            renderCocChat();
            delay(100);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
            playClickSound();
            chatModeSelection = (chatModeSelection < 2) ? chatModeSelection + 1 : 0;
            renderCocChat();
            delay(100);
            return;
        }
        
        // Enter - Select mode
        if (status.enter) {
            playSelectSound();
            switch (chatModeSelection) {
                case 0:  // WiFi mode
                    if (WiFi.status() == WL_CONNECTED) {
                        chatConnectMode = CHAT_MODE_WIFI;
                    } else {
                        M5Cardputer.Speaker.tone(300, 200);  // Error - not connected
                    }
                    break;
                case 1:  // Host direct chat
                    startChatHost();
                    break;
                case 2:  // Join direct chat
                    startChatScan();
                    break;
            }
            renderCocChat();
            delay(150);
            return;
        }
        
        // ESC - Back to menu
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            playBackSound();
            currentMode = MODE_COC_MENU;
            renderCocMenu();
            delay(150);
            return;
        }
        return;
    }
    
    // ===== JOIN/SCAN SCREEN =====
    if (chatConnectMode == CHAT_MODE_JOIN) {
        // Up/Down navigation
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP) && !chatFoundAPs.empty()) {
            playClickSound();
            chatAPSelection = (chatAPSelection > 0) ? chatAPSelection - 1 : chatFoundAPs.size() - 1;
            renderCocChat();
            delay(100);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN) && !chatFoundAPs.empty()) {
            playClickSound();
            chatAPSelection = (chatAPSelection < (int)chatFoundAPs.size() - 1) ? chatAPSelection + 1 : 0;
            renderCocChat();
            delay(100);
            return;
        }
        
        // Tab - Rescan
        if (status.tab || M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_TAB)) {
            playClickSound();
            startChatScan();
            renderCocChat();
            delay(150);
            return;
        }
        
        // Enter - Join or scan
        if (status.enter) {
            if (chatFoundAPs.empty()) {
                startChatScan();
            } else {
                playSelectSound();
                joinChatAP(chatFoundAPs[chatAPSelection]);
            }
            renderCocChat();
            delay(150);
            return;
        }
        
        // ESC - Back to mode select
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            playBackSound();
            chatConnectMode = CHAT_MODE_SELECT;
            renderCocChat();
            delay(150);
            return;
        }
        return;
    }
    
    // ===== IN CHAT ROOM (both WiFi and Direct modes) =====
    if (chatInRoom) {
        // Enter - Send message
        if (status.enter && chatInputBuffer.length() > 0) {
            sendChatMessage(chatInputBuffer);
            chatInputBuffer = "";
            playSelectSound();
            renderCocChat();
            delay(150);
            return;
        }
        
        // ESC - Leave room
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            if (chatDirectMode) {
                stopDirectChat();
            } else {
                stopChatRoom();
            }
            playBackSound();
            renderCocChat();
            delay(150);
            return;
        }
        
        // Backspace
        if (status.del && chatInputBuffer.length() > 0) {
            chatInputBuffer.remove(chatInputBuffer.length() - 1);
            playClickSound();
            renderCocChat();
            return;
        }
        
        // Typing
        bool typed = false;
        for (auto i : status.word) {
            if (i == '\t') continue;
            if (chatInputBuffer.length() < 100) {
                chatInputBuffer += i;
                typed = true;
            }
        }
        if (typed) {
            M5Cardputer.Speaker.tone(1500, 15);
            renderCocChat();
        }
        return;
    }
    
    // ===== ROOM SELECTION (WiFi mode only) =====
    if (chatConnectMode == CHAT_MODE_WIFI) {
        // Navigation (2x2 grid)
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_LEFT)) {
            playClickSound();
            if (chatSelectedRoom % 2 == 1) chatSelectedRoom--;
            renderCocChat();
            delay(100);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_RIGHT)) {
            playClickSound();
            if (chatSelectedRoom % 2 == 0) chatSelectedRoom++;
            renderCocChat();
            delay(100);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
            playClickSound();
            if (chatSelectedRoom >= 2) chatSelectedRoom -= 2;
            renderCocChat();
            delay(100);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
            playClickSound();
            if (chatSelectedRoom < 2) chatSelectedRoom += 2;
            renderCocChat();
            delay(100);
            return;
        }
        
        // Enter - Join selected room
        if (status.enter) {
            if (chatRoomUsers[chatSelectedRoom] < CHAT_MAX_USERS) {
                startChatRoom(chatSelectedRoom);
                playSelectSound();
            } else {
                M5Cardputer.Speaker.tone(300, 200);
            }
            renderCocChat();
            delay(150);
            return;
        }
        
        // ESC - Back to mode select
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            playBackSound();
            chatConnectMode = CHAT_MODE_SELECT;
            renderCocChat();
            delay(150);
            return;
        }
    }
}

// ==========================================
//     COC UI - Mic Recorder
// ==========================================

void micWriteWavHeader(File &file, uint32_t dataSize) {
    // Seek to beginning
    file.seek(0);
    
    uint32_t fileSize = dataSize + 36;
    uint16_t audioFormat = 1;  // PCM
    uint16_t numChannels = 1;  // Mono
    uint32_t sampleRate = MIC_SAMPLE_RATE;
    uint16_t bitsPerSample = 16;
    uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
    uint16_t blockAlign = numChannels * bitsPerSample / 8;
    
    // RIFF header
    file.write((const uint8_t*)"RIFF", 4);
    file.write((uint8_t*)&fileSize, 4);
    file.write((const uint8_t*)"WAVE", 4);
    
    // fmt chunk
    file.write((const uint8_t*)"fmt ", 4);
    uint32_t fmtSize = 16;
    file.write((uint8_t*)&fmtSize, 4);
    file.write((uint8_t*)&audioFormat, 2);
    file.write((uint8_t*)&numChannels, 2);
    file.write((uint8_t*)&sampleRate, 4);
    file.write((uint8_t*)&byteRate, 4);
    file.write((uint8_t*)&blockAlign, 2);
    file.write((uint8_t*)&bitsPerSample, 2);
    
    // data chunk
    file.write((const uint8_t*)"data", 4);
    file.write((uint8_t*)&dataSize, 4);
}

void micStartRecording() {
    if (micRecording) return;
    
    // Create Recordings folder if it doesn't exist
    if (!SD.exists("/Recordings")) {
        SD.mkdir("/Recordings");
    }
    
    // Generate filename with timestamp
    micRecordingCount++;
    micRecordingFile = "/Recordings/rec_" + String(millis()) + ".wav";
    
    // Stop speaker before starting mic
    M5Cardputer.Speaker.stop();
    M5Cardputer.Speaker.end();
    delay(10);
    
    // Initialize microphone
    auto mic_cfg = M5Cardputer.Mic.config();
    mic_cfg.sample_rate = MIC_SAMPLE_RATE;
    mic_cfg.dma_buf_len = 256;
    mic_cfg.dma_buf_count = 4;
    M5Cardputer.Mic.config(mic_cfg);
    M5Cardputer.Mic.begin();
    
    // Create WAV file with placeholder header
    micWavFile = SD.open(micRecordingFile, FILE_WRITE);
    if (!micWavFile) {
        M5Cardputer.Mic.end();
        resetAudioSystem();
        return;
    }
    
    // Write placeholder header (will update later)
    micDataSize = 0;
    micWriteWavHeader(micWavFile, 0);
    micWavFile.seek(44);  // Position after header
    
    micRecording = true;
    micRecordStartTime = millis();
    micVuLevel = 0;
    micVuPeak = 0;
}

void micStopRecording() {
    if (!micRecording) return;
    
    micRecording = false;
    M5Cardputer.Mic.end();
    
    // Update WAV header with actual data size
    micWriteWavHeader(micWavFile, micDataSize);
    micWavFile.close();
    
    micRecordDuration = (millis() - micRecordStartTime) / 1000;
    
    // Reset audio system and play stop sound
    resetAudioSystem();
    if (settingSoundEnabled && !globalMuted) {
        M5Cardputer.Speaker.tone(800, 50);
        delay(60);
        M5Cardputer.Speaker.tone(600, 50);
    }
}

void micProcessAudio() {
    if (!micRecording) return;
    
    // Read from microphone
    if (M5Cardputer.Mic.record(micBuffer, MIC_BUFFER_SIZE, MIC_SAMPLE_RATE)) {
        // Calculate VU level from samples
        int32_t sum = 0;
        int16_t maxVal = 0;
        for (int i = 0; i < MIC_BUFFER_SIZE; i++) {
            int16_t sample = abs(micBuffer[i]);
            sum += sample;
            if (sample > maxVal) maxVal = sample;
        }
        int avgLevel = sum / MIC_BUFFER_SIZE;
        micVuLevel = map(avgLevel, 0, 8000, 0, 100);
        if (micVuLevel > 100) micVuLevel = 100;
        
        // Peak hold
        if (micVuLevel > micVuPeak) micVuPeak = micVuLevel;
        
        // Write to file
        size_t written = micWavFile.write((uint8_t*)micBuffer, MIC_BUFFER_SIZE * 2);
        micDataSize += written;
    }
    
    // Decay peak hold
    if (millis() - micLastVuUpdate > 100) {
        micLastVuUpdate = millis();
        if (micVuPeak > micVuLevel) micVuPeak--;
    }
}

String micFormatTime(unsigned long ms) {
    unsigned long secs = ms / 1000;
    unsigned long mins = secs / 60;
    secs = secs % 60;
    char buf[10];
    sprintf(buf, "%02lu:%02lu", mins, secs);
    return String(buf);
}

void renderMicRecorder() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "Voice Recorder");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 28;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    // Center area for main display
    int centerX = contentX + contentW / 2;
    
    // Large mic icon at center
    int btnY = contentY + 8;
    int iconX = centerX - Mic2_width / 2;
    int iconY = btnY;
    
    // Icons are already RGB565_SWAPPED from png_to_cpp_array.py, no swap needed
    if (micRecording) {
        // Blink between green and red mic icons when recording
        bool showRed = (millis() / 500) % 2;  // Toggle every 500ms
        if (showRed) {
            sprite.pushImage(iconX, iconY, Mic1_width, Mic1_height, Mic1);  // Red
        } else {
            sprite.pushImage(iconX, iconY, Mic2_width, Mic2_height, Mic2);  // Green
        }
    } else {
        // Show green mic icon when not recording
        sprite.pushImage(iconX, iconY, Mic2_width, Mic2_height, Mic2);
    }
    
    int btnRadius = Mic2_height / 2;  // For status positioning
    
    // Status text below button
    int statusY = btnY + btnRadius * 2 + 10;
    sprite.setTextColor(COL_BLACK, COL_WHITE);
    
    if (micRecording) {
        // Time display (large, centered)
        String timeStr = micFormatTime(millis() - micRecordStartTime);
        sprite.setCursor(centerX - 18, statusY);
        sprite.print(timeStr);
        
        // File size
        float sizeMB = micDataSize / (1024.0 * 1024.0);
        String sizeStr = String(sizeMB, 2) + " MB";
        sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
        sprite.setCursor(centerX - (sizeStr.length() * 3), statusY + 12);
        sprite.print(sizeStr);
    } else {
        sprite.setCursor(centerX - 30, statusY - 5);
        sprite.print("Press SPACE");
        
        if (micRecordDuration > 0) {
            sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
            sprite.setCursor(contentX + 5, statusY + 7);
            String lastFile = micRecordingFile.substring(micRecordingFile.lastIndexOf('/') + 1);
            if (lastFile.length() > 22) lastFile = lastFile.substring(0, 19) + "...";
            sprite.print("Last: " + lastFile);
        }
    }
    
    // VU Meter (horizontal, full width)
    int vuX = contentX + 5;
    int vuY = contentY + contentH - 18;
    int vuW = contentW - 10;
    int vuH = 14;
    
    // VU background with 3D effect
    sprite.fillRect(vuX, vuY, vuW, vuH, COL_GRAY_DARK);
    draw3DSunken(vuX, vuY, vuW, vuH);
    
    // Scale markers
    sprite.setTextColor(COL_GRAY_LIGHT, COL_GRAY_DARK);
    for (int i = 0; i <= 10; i++) {
        int markerX = vuX + 2 + ((vuW - 4) * i / 10);
        sprite.drawPixel(markerX, vuY + vuH - 3, COL_GRAY_MID);
    }
    
    // VU level bar with gradient coloring
    int levelW = (vuW - 4) * micVuLevel / 100;
    if (levelW > 0) {
        // Draw segments with color gradient
        int greenEnd = (vuW - 4) * 50 / 100;
        int yellowEnd = (vuW - 4) * 75 / 100;
        
        for (int x = 0; x < levelW; x++) {
            uint16_t col = COL_GREEN;
            if (x > yellowEnd) col = COL_RED;
            else if (x > greenEnd) col = COL_YELLOW;
            sprite.drawFastVLine(vuX + 2 + x, vuY + 2, vuH - 4, col);
        }
    }
    
    // Peak marker (white line)
    if (micVuPeak > 0) {
        int peakX = vuX + 2 + (vuW - 4) * micVuPeak / 100;
        sprite.drawFastVLine(peakX, vuY + 1, vuH - 2, COL_WHITE);
    }
    
    // Instructions bar
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    if (micRecording) {
        sprite.print("SPACE:Stop  ESC:Menu");
    } else {
        sprite.print("SPACE:Record  ESC:Menu");
    }
    
    sprite.pushSprite(0, 0);
}

void handleMicRecorderInput() {
    // Process audio continuously while recording
    if (micRecording) {
        micProcessAudio();
        
        // Update display periodically
        static unsigned long lastRender = 0;
        if (millis() - lastRender > 100) {
            lastRender = millis();
            renderMicRecorder();
        }
    }
    
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    bool fnPressed = status.fn;
    
    // Fn+T - Open Terminal (stop recording first)
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_T) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('t'))) {
        if (micRecording) micStopRecording();
        currentMode = MODE_TERMINAL;
        needsRedraw = true;
        delay(150);
        return;
    }
    
    // Fn+P - Screenshot
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_P) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('p'))) {
        takeScreenshot();
        renderMicRecorder();
        delay(150);
        return;
    }
    
    // Space or Enter - Toggle recording
    if (M5Cardputer.Keyboard.isKeyPressed(' ') || status.enter) {
        if (micRecording) {
            micStopRecording();
        } else {
            micStartRecording();
        }
        renderMicRecorder();
        delay(200);
        return;
    }
    
    // ESC - Back to menu (stop recording first)
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        if (micRecording) micStopRecording();
        playBackSound();
        currentMode = MODE_COC_MENU;
        renderCocMenu();
        delay(150);
        return;
    }
}

// ==========================================
//     WAV File Playback
// ==========================================

bool wavPlaying = false;
String wavPlayingFile = "";

void playWavFile(String filepath) {
    File wavFile = SD.open(filepath, FILE_READ);
    if (!wavFile) return;
    
    // Read WAV header
    uint8_t header[44];
    if (wavFile.read(header, 44) != 44) {
        wavFile.close();
        return;
    }
    
    // Verify RIFF header
    if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F') {
        wavFile.close();
        return;
    }
    
    // Extract WAV parameters
    uint16_t audioFormat = header[20] | (header[21] << 8);
    uint16_t numChannels = header[22] | (header[23] << 8);
    uint32_t sampleRate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    uint16_t bitsPerSample = header[34] | (header[35] << 8);
    
    // Only support PCM format
    if (audioFormat != 1) {
        wavFile.close();
        return;
    }
    
    // Show playback screen
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "Playing WAV");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 28;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    // File info
    String fname = filepath.substring(filepath.lastIndexOf('/') + 1);
    sprite.setTextColor(COL_BLACK, COL_WHITE);
    sprite.setCursor(contentX + 10, contentY + 15);
    sprite.print("File: " + (fname.length() > 20 ? fname.substring(0, 17) + "..." : fname));
    
    sprite.setCursor(contentX + 10, contentY + 30);
    sprite.print("Rate: " + String(sampleRate) + " Hz");
    
    sprite.setCursor(contentX + 10, contentY + 45);
    sprite.print("Bits: " + String(bitsPerSample) + " Ch: " + String(numChannels));
    
    // Progress bar background
    int barX = contentX + 10, barY = contentY + 65, barW = contentW - 20, barH = 12;
    sprite.fillRect(barX, barY, barW, barH, COL_GRAY_MID);
    draw3DSunken(barX, barY, barW, barH);
    
    // Instructions
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    sprite.print("Esc: Stop playback");
    
    sprite.pushSprite(0, 0);
    
    // Configure speaker for playback
    M5Cardputer.Speaker.end();  // End any previous state
    delay(10);
    M5Cardputer.Speaker.begin();
    applyVolume();
    
    // Calculate file size for progress
    uint32_t dataSize = wavFile.size() - 44;
    uint32_t bytesPlayed = 0;
    
    // Play audio in chunks
    const int WAV_CHUNK_SIZE = 1024;
    uint8_t audioBuffer[WAV_CHUNK_SIZE];
    
    wavPlaying = true;
    wavPlayingFile = filepath;
    
    while (wavFile.available() && wavPlaying) {
        M5Cardputer.update();
        
        // Check for ESC to stop
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            wavPlaying = false;
            break;
        }
        
        int bytesRead = wavFile.read(audioBuffer, WAV_CHUNK_SIZE);
        if (bytesRead <= 0) break;
        
        bytesPlayed += bytesRead;
        
        // Play the chunk
        if (bitsPerSample == 16) {
            M5Cardputer.Speaker.playRaw((int16_t*)audioBuffer, bytesRead / 2, sampleRate, numChannels > 1);
        } else if (bitsPerSample == 8) {
            // Convert 8-bit to 16-bit
            int16_t converted[WAV_CHUNK_SIZE];
            for (int i = 0; i < bytesRead; i++) {
                converted[i] = (audioBuffer[i] - 128) * 256;
            }
            M5Cardputer.Speaker.playRaw(converted, bytesRead, sampleRate, numChannels > 1);
        }
        
        // Wait for buffer space
        while (M5Cardputer.Speaker.isPlaying() && wavPlaying) {
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
                wavPlaying = false;
                break;
            }
            delay(1);
        }
        
        // Update progress bar
        int progress = (bytesPlayed * 100) / dataSize;
        int fillW = (barW - 4) * progress / 100;
        sprite.fillRect(barX + 2, barY + 2, barW - 4, barH - 4, COL_GRAY_DARK);
        sprite.fillRect(barX + 2, barY + 2, fillW, barH - 4, COL_GREEN);
        sprite.pushSprite(0, 0);
    }
    
    wavFile.close();
    M5Cardputer.Speaker.stop();
    wavPlaying = false;
    wavPlayingFile = "";
    
    // Reset audio system for other uses
    resetAudioSystem();
}

// ==========================================
//     SID File Playback (C64 Emulation)
// ==========================================

#ifdef ENABLE_SID
bool sidPlaying = false;
String sidPlayingFile = "";

// Returns: 0=stopped, 1=next song, -1=previous song
int playSidFile(String filepath) {
    // Open SID file and read into memory
    File sidFile = SD.open(filepath, FILE_READ);
    if (!sidFile) return 0;
    
    size_t fileSize = sidFile.size();
    if (fileSize > 65536) {  // SID files are typically < 64KB
        sidFile.close();
        return 0;
    }
    
    // Allocate memory for SID data (prefer PSRAM if available)
    uint8_t* sidData = (uint8_t*)ps_malloc(fileSize);
    if (!sidData) {
        sidData = (uint8_t*)malloc(fileSize);
        if (!sidData) {
            sidFile.close();
            return 0;
        }
    }
    
    // Read SID file into memory
    size_t bytesRead = sidFile.read(sidData, fileSize);
    sidFile.close();
    
    if (bytesRead != fileSize) {
        free(sidData);
        return 0;
    }
    
    // Configure SID stream
    const uint32_t sampleRate = 11025;  // Lower rate for smoother emulation
    const uint8_t channels = 1;         // Mono for SID
    
    SIDStream sid(sidData, fileSize);
    auto cfg = sid.defaultConfig();
    cfg.sample_rate = sampleRate;
    cfg.channels = channels;
    cfg.bits_per_sample = 16;
    sid.begin(cfg);
    
    // Show playback screen
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "Playing SID");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 28;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    // File info
    String fname = filepath.substring(filepath.lastIndexOf('/') + 1);
    sprite.setTextColor(COL_BLACK, COL_WHITE);
    sprite.setCursor(contentX + 10, contentY + 15);
    sprite.print("File: " + (fname.length() > 20 ? fname.substring(0, 17) + "..." : fname));
    
    sprite.setCursor(contentX + 10, contentY + 30);
    sprite.print("C64 SID Emulation");
    
    sprite.setCursor(contentX + 10, contentY + 45);
    sprite.print("Rate: " + String(sampleRate) + " Hz");
    
    // Playback time display area
    int timeY = contentY + 65;
    sprite.setCursor(contentX + 10, timeY);
    sprite.print("Time: 00:00");
    
    // Instructions
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    sprite.print(",/:Nav []:Vol V:Viz Bksp:Stop");
    
    sprite.pushSprite(0, 0);
    
    // Configure speaker for playback (same as WAV player)
    M5Cardputer.Speaker.end();
    delay(10);
    M5Cardputer.Speaker.begin();
    applyVolume();
    
    // Playback buffer (matching WAV player size)
    const int SID_CHUNK_SIZE = 1024;
    int16_t audioBuffer[SID_CHUNK_SIZE / 2];  // 512 samples
    
    sidPlaying = true;
    sidPlayingFile = filepath;
    int sidNavResult = 0;  // 0=stop, 1=next, -1=prev
    
    // Visualizer toggle and data
    bool showVisualizer = false;
    const int VIS_BARS = 16;
    int visLevels[VIS_BARS] = {0};
    int visPeaks[VIS_BARS] = {0};
    unsigned long lastPeakDecay = 0;
    
    // Auto-advance timer (SID files loop forever, use configurable duration)
    unsigned long SID_SONG_DURATION = (unsigned long)configSidDuration * 1000;  // Convert seconds to ms
    
    unsigned long startTime = millis();
    unsigned long lastUpdate = 0;
    
    while (sidPlaying) {
        M5Cardputer.update();
        
        // Get keyboard state for special keys
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
            
            // Check for Backspace to stop and return to browser
            if (status.del) {
                sidPlaying = false;
                sidNavResult = 0;
                break;
            }
            
            // Check for / to go to next song
            if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_RIGHT)) {
                sidPlaying = false;
                sidNavResult = 1;
                break;
            }
            
            // Check for , to go to previous song
            if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_LEFT)) {
                sidPlaying = false;
                sidNavResult = -1;
                break;
            }
            
            // Toggle visualizer with 'v'
            if (M5Cardputer.Keyboard.isKeyPressed('v')) {
                showVisualizer = !showVisualizer;
                lastUpdate = 0;  // Force immediate redraw
            }
            
            // Volume control with Fn+[ and Fn+]
            if (M5Cardputer.Keyboard.isKeyPressed('[')) {
                globalVolume = max(0, globalVolume - 10);
                applyVolume();
            }
            if (M5Cardputer.Keyboard.isKeyPressed(']')) {
                globalVolume = min(100, globalVolume + 10);
                applyVolume();
            }
        }
        
        // Auto-advance to next song after duration
        if (millis() - startTime >= SID_SONG_DURATION) {
            sidPlaying = false;
            sidNavResult = 1;  // Next song
            break;
        }
        
        // Read samples from SID emulator
        size_t bytesRead = sid.readBytes((uint8_t*)audioBuffer, sizeof(audioBuffer));
        if (bytesRead <= 0) {
            // SID playback ended, go to next song
            sidNavResult = 1;
            break;
        }
        
        // Calculate audio levels for visualizer
        if (showVisualizer) {
            int samplesPerBar = (bytesRead / 2) / VIS_BARS;
            if (samplesPerBar < 1) samplesPerBar = 1;
            
            for (int b = 0; b < VIS_BARS; b++) {
                long sum = 0;
                int startSample = b * samplesPerBar;
                for (int s = 0; s < samplesPerBar && (startSample + s) < (int)(bytesRead / 2); s++) {
                    int16_t sample = audioBuffer[startSample + s];
                    sum += abs(sample);
                }
                int avg = sum / samplesPerBar;
                int level = map(avg, 0, 16000, 0, 8);
                if (level > 8) level = 8;
                visLevels[b] = level;
                if (level > visPeaks[b]) visPeaks[b] = level;
            }
            
            // Decay peaks
            if (millis() - lastPeakDecay > 100) {
                lastPeakDecay = millis();
                for (int b = 0; b < VIS_BARS; b++) {
                    if (visPeaks[b] > visLevels[b]) visPeaks[b]--;
                }
            }
        }
        
        // Play the chunk
        M5Cardputer.Speaker.playRaw(audioBuffer, bytesRead / 2, sampleRate, false);
        
        // Wait for buffer space (same pattern as WAV player)
        while (M5Cardputer.Speaker.isPlaying() && sidPlaying) {
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
                Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
                if (status.del) {
                    sidPlaying = false;
                    sidNavResult = 0;
                    break;
                }
                if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_RIGHT)) {
                    sidPlaying = false;
                    sidNavResult = 1;
                    break;
                }
                if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_LEFT)) {
                    sidPlaying = false;
                    sidNavResult = -1;
                    break;
                }
            }
            delay(1);
        }
        
        // Update display periodically
        unsigned long now = millis();
        int updateInterval = showVisualizer ? 80 : 500;  // Faster updates for visualizer
        
        if (now - lastUpdate > updateInterval) {
            lastUpdate = now;
            unsigned long elapsed = (now - startTime) / 1000;
            int minutes = elapsed / 60;
            int seconds = elapsed % 60;
            
            if (showVisualizer) {
                // Draw visualizer mode
                sprite.fillSprite(COL_BLACK);
                sprite.setTextColor(COL_GREEN, COL_BLACK);
                
                // Title
                sprite.setCursor(4, 4);
                sprite.print("SID: " + (fname.length() > 20 ? fname.substring(0, 17) + "..." : fname));
                
                // Time and volume (show elapsed / total)
                int totalSecs = SID_SONG_DURATION / 1000;
                int totalMin = totalSecs / 60;
                int totalSecRem = totalSecs % 60;
                char infoStr[48];
                sprintf(infoStr, "%02d:%02d/%02d:%02d Vol:%d%%", minutes, seconds, totalMin, totalSecRem, globalVolume);
                sprite.setCursor(4, 18);
                sprite.print(infoStr);
                
                // Draw visualizer bars
                int barWidth = (screenW - 8) / VIS_BARS;
                int visY = 36;
                int visHeight = 60;
                
                for (int b = 0; b < VIS_BARS; b++) {
                    int x = 4 + b * barWidth;
                    int peakY = visY + visHeight - (visPeaks[b] * visHeight) / 8;
                    
                    for (int seg = 0; seg < 8; seg++) {
                        int segY = visY + visHeight - (seg + 1) * (visHeight / 8);
                        if (seg < visLevels[b]) {
                            uint16_t color;
                            if (seg >= 6) color = COL_RED;
                            else if (seg >= 4) color = COL_YELLOW;
                            else color = COL_GREEN;
                            sprite.fillRect(x + 1, segY + 1, barWidth - 2, visHeight / 8 - 2, color);
                        }
                    }
                    
                    if (visPeaks[b] > 0) {
                        sprite.drawLine(x + 1, peakY, x + barWidth - 2, peakY, COL_WHITE);
                    }
                }
                
                // Instructions
                sprite.setTextColor(COL_GRAY_MID, COL_BLACK);
                sprite.setCursor(4, screenH - 12);
                sprite.print(",/:Nav []:Vol V:View Bksp:Stop");
            } else {
                // Draw normal window mode
                sprite.fillSprite(currentThemeColor);
                drawWindowFrame(winX, winY, winW, winH, "Playing SID");
                sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
                
                sprite.setTextColor(COL_BLACK, COL_WHITE);
                sprite.setCursor(contentX + 10, contentY + 15);
                sprite.print("File: " + (fname.length() > 20 ? fname.substring(0, 17) + "..." : fname));
                
                sprite.setCursor(contentX + 10, contentY + 30);
                sprite.print("C64 SID Emulation");
                
                sprite.setCursor(contentX + 10, contentY + 45);
                sprite.print("Rate: " + String(sampleRate) + " Hz");
                
                // Show elapsed / total time
                int totalSecs = SID_SONG_DURATION / 1000;
                int totalMin = totalSecs / 60;
                int totalSecRem = totalSecs % 60;
                char timeStr[48];
                sprintf(timeStr, "%02d:%02d/%02d:%02d Vol:%d%%", minutes, seconds, totalMin, totalSecRem, globalVolume);
                sprite.setCursor(contentX + 10, timeY);
                sprite.print(timeStr);
                
                // Instructions
                sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
                draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
                sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
                sprite.setCursor(winX + 8, winY + winH - 14);
                sprite.print(",/:Nav []:Vol V:Viz Bksp:Stop");
            }
            
            sprite.pushSprite(0, 0);
        }
    }
    
    // Cleanup
    free(sidData);
    M5Cardputer.Speaker.stop();
    sidPlaying = false;
    sidPlayingFile = "";
    
    // Reset audio system for other uses
    resetAudioSystem();
    
    return sidNavResult;
}

// Terminal-based SID player with ASCII visualizer
void playSidTerminal(String filepath) {
    // Open SID file and read into memory
    File sidFile = SD.open(filepath, FILE_READ);
    if (!sidFile) {
        addToHistory("Error: Cannot open file");
        return;
    }
    
    size_t fileSize = sidFile.size();
    if (fileSize > 65536) {
        sidFile.close();
        addToHistory("Error: File too large");
        return;
    }
    
    uint8_t* sidData = (uint8_t*)ps_malloc(fileSize);
    if (!sidData) {
        sidData = (uint8_t*)malloc(fileSize);
        if (!sidData) {
            sidFile.close();
            addToHistory("Error: Out of memory");
            return;
        }
    }
    
    size_t bytesRead = sidFile.read(sidData, fileSize);
    sidFile.close();
    
    if (bytesRead != fileSize) {
        free(sidData);
        addToHistory("Error: Read failed");
        return;
    }
    
    // Configure SID stream
    const uint32_t sampleRate = 11025;
    const uint8_t channels = 1;
    
    SIDStream sid(sidData, fileSize);
    auto cfg = sid.defaultConfig();
    cfg.sample_rate = sampleRate;
    cfg.channels = channels;
    cfg.bits_per_sample = 16;
    sid.begin(cfg);
    
    // Configure speaker
    M5Cardputer.Speaker.end();
    delay(10);
    M5Cardputer.Speaker.begin();
    applyVolume();
    
    // Playback buffer
    const int SID_CHUNK_SIZE = 1024;
    int16_t audioBuffer[SID_CHUNK_SIZE / 2];
    
    sidPlaying = true;
    sidPlayingFile = filepath;
    
    // Visualizer variables
    const int VIS_BARS = 16;
    int visLevels[VIS_BARS] = {0};
    int visPeaks[VIS_BARS] = {0};
    
    unsigned long startTime = millis();
    unsigned long lastUpdate = 0;
    
    String fname = filepath.substring(filepath.lastIndexOf('/') + 1);
    
    // Screen dimensions
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    
    while (sidPlaying) {
        M5Cardputer.update();
        
        // Check keyboard
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
            
            // ESC to stop
            if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
                sidPlaying = false;
                break;
            }
            
            // Volume control
            if (M5Cardputer.Keyboard.isKeyPressed('[')) {
                globalVolume = max(0, globalVolume - 10);
                applyVolume();
            }
            if (M5Cardputer.Keyboard.isKeyPressed(']')) {
                globalVolume = min(100, globalVolume + 10);
                applyVolume();
            }
        }
        
        // Read samples from SID emulator
        size_t bytesRead = sid.readBytes((uint8_t*)audioBuffer, sizeof(audioBuffer));
        if (bytesRead <= 0) break;
        
        // Calculate audio levels for visualizer (simple RMS per band)
        int samplesPerBar = (bytesRead / 2) / VIS_BARS;
        if (samplesPerBar < 1) samplesPerBar = 1;
        
        for (int b = 0; b < VIS_BARS; b++) {
            long sum = 0;
            int startSample = b * samplesPerBar;
            for (int s = 0; s < samplesPerBar && (startSample + s) < (int)(bytesRead / 2); s++) {
                int16_t sample = audioBuffer[startSample + s];
                sum += abs(sample);
            }
            int avg = sum / samplesPerBar;
            int level = map(avg, 0, 16000, 0, 8);  // 0-8 height
            if (level > 8) level = 8;
            visLevels[b] = level;
            if (level > visPeaks[b]) visPeaks[b] = level;
        }
        
        // Decay peaks
        static unsigned long lastPeakDecay = 0;
        if (millis() - lastPeakDecay > 100) {
            lastPeakDecay = millis();
            for (int b = 0; b < VIS_BARS; b++) {
                if (visPeaks[b] > visLevels[b]) visPeaks[b]--;
            }
        }
        
        // Play the chunk
        M5Cardputer.Speaker.playRaw(audioBuffer, bytesRead / 2, sampleRate, false);
        
        // Wait for buffer space
        while (M5Cardputer.Speaker.isPlaying() && sidPlaying) {
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
                sidPlaying = false;
                break;
            }
            delay(1);
        }
        
        // Update display periodically
        unsigned long now = millis();
        if (now - lastUpdate > 80) {  // ~12 FPS for smooth visualization
            lastUpdate = now;
            
            unsigned long elapsed = (now - startTime) / 1000;
            int minutes = elapsed / 60;
            int seconds = elapsed % 60;
            
            // Clear screen with terminal background
            sprite.fillSprite(COL_BLACK);
            sprite.setTextColor(COL_GREEN, COL_BLACK);
            
            // Title
            sprite.setCursor(4, 4);
            sprite.print("SID PLAYER - " + (fname.length() > 18 ? fname.substring(0, 15) + "..." : fname));
            
            // Time and volume
            char infoStr[40];
            sprintf(infoStr, "Time: %02d:%02d  Vol: %d%%", minutes, seconds, globalVolume);
            sprite.setCursor(4, 18);
            sprite.print(infoStr);
            
            // Draw ASCII-style visualizer bars
            int barWidth = (screenW - 8) / VIS_BARS;
            int visY = 38;
            int visHeight = 64;
            
            for (int b = 0; b < VIS_BARS; b++) {
                int x = 4 + b * barWidth;
                int barH = (visLevels[b] * visHeight) / 8;
                int peakY = visY + visHeight - (visPeaks[b] * visHeight) / 8;
                
                // Draw bar segments (ASCII block style)
                for (int seg = 0; seg < 8; seg++) {
                    int segY = visY + visHeight - (seg + 1) * (visHeight / 8);
                    uint16_t color;
                    
                    if (seg < visLevels[b]) {
                        // Color based on level
                        if (seg >= 6) color = COL_RED;
                        else if (seg >= 4) color = COL_YELLOW;
                        else color = COL_GREEN;
                        sprite.fillRect(x + 1, segY + 1, barWidth - 2, visHeight / 8 - 2, color);
                    }
                }
                
                // Peak marker
                if (visPeaks[b] > 0) {
                    sprite.drawLine(x + 1, peakY, x + barWidth - 2, peakY, COL_WHITE);
                }
            }
            
            // Instructions at bottom
            sprite.setTextColor(COL_GRAY_MID, COL_BLACK);
            sprite.setCursor(4, screenH - 12);
            sprite.print("ESC:Stop  []:Vol");
            
            sprite.pushSprite(0, 0);
        }
    }
    
    // Cleanup
    free(sidData);
    M5Cardputer.Speaker.stop();
    sidPlaying = false;
    sidPlayingFile = "";
    resetAudioSystem();
    
    addToHistory("Playback stopped: " + fname);
}
#endif

// ==========================================
//     MP3 File Playback (Simple decoder)
// ==========================================

// MP3 Playback using ESP32-audioI2S library
void playMp3File(String filepath) {
#ifdef ENABLE_MP3
    // Stop M5 speaker to free I2S
    M5Cardputer.Speaker.end();
    delay(50);
    
    // Initialize Audio object if needed
    if (mp3Audio == nullptr) {
        mp3Audio = new Audio();
        mp3Audio->setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    }
    
    // Set volume (0-21 range for this library)
    int vol = map(globalVolume, 0, 100, 0, 21);
    if (globalMuted) vol = 0;
    mp3Audio->setVolume(vol);
    
    // Open file
    if (!mp3Audio->connecttoFS(SD, filepath.c_str())) {
        // Failed to open
        M5Cardputer.Speaker.begin();
        applyVolume();
        return;
    }
    
    mp3Playing = true;
    mp3PlayingFile = filepath;
    
    // Get file info
    String fname = filepath.substring(filepath.lastIndexOf('/') + 1);
    File f = SD.open(filepath);
    size_t fileSize = f.size();
    f.close();
    
    // Show playback screen
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 28;
    int barX = contentX + 10, barY = contentY + 65, barW = contentW - 20, barH = 12;
    
    // Main playback loop
    unsigned long startTime = millis();
    while (mp3Playing && mp3Audio->isRunning()) {
        mp3Audio->loop();
        M5Cardputer.update();
        
        // Check for ESC to stop
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            mp3Audio->stopSong();
            break;
        }
        
        // Update display periodically
        if (millis() - mp3LastUpdate > 200) {
            mp3LastUpdate = millis();
            
            sprite.fillSprite(currentThemeColor);
            drawWindowFrame(winX, winY, winW, winH, "Playing MP3");
            sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
            
            // File info
            sprite.setTextColor(COL_BLACK, COL_WHITE);
            sprite.setCursor(contentX + 10, contentY + 10);
            sprite.print("File: ");
            sprite.print(fname.length() > 18 ? fname.substring(0, 15) + "..." : fname);
            
            sprite.setCursor(contentX + 10, contentY + 25);
            sprite.print("Size: " + String(fileSize / 1024) + " KB");
            
            // Time display
            unsigned long elapsed = (millis() - startTime) / 1000;
            uint32_t totalTime = mp3Audio->getAudioFileDuration();
            sprite.setCursor(contentX + 10, contentY + 40);
            char timeStr[32];
            sprintf(timeStr, "Time: %02d:%02d / %02d:%02d", 
                    (int)(elapsed / 60), (int)(elapsed % 60),
                    (int)(totalTime / 60), (int)(totalTime % 60));
            sprite.print(timeStr);
            
            // Volume
            sprite.setCursor(contentX + 10, contentY + 55);
            sprite.print("Volume: " + String(globalVolume) + "%");
            if (globalMuted) sprite.print(" (MUTED)");
            
            // Progress bar
            sprite.fillRect(barX, barY, barW, barH, COL_GRAY_MID);
            draw3DSunken(barX, barY, barW, barH);
            
            uint32_t currentPos = mp3Audio->getAudioCurrentTime();
            int progress = 0;
            if (totalTime > 0) {
                progress = (currentPos * 100) / totalTime;
            }
            int fillW = ((barW - 4) * progress) / 100;
            sprite.fillRect(barX + 2, barY + 2, fillW, barH - 4, COL_GREEN);
            
            // Instructions bar
            sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
            draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
            sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
            sprite.setCursor(winX + 8, winY + winH - 14);
            sprite.print("Esc:Stop  Fn+[/]:Volume");
            
            sprite.pushSprite(0, 0);
        }
        
        // Handle volume keys during playback
        if (M5Cardputer.Keyboard.isKeyPressed('[')) {
            globalVolume = max(0, globalVolume - 10);
            int v = map(globalVolume, 0, 100, 0, 21);
            if (globalMuted) v = 0;
            mp3Audio->setVolume(v);
            delay(150);
        }
        if (M5Cardputer.Keyboard.isKeyPressed(']')) {
            globalVolume = min(100, globalVolume + 10);
            int v = map(globalVolume, 0, 100, 0, 21);
            if (globalMuted) v = 0;
            mp3Audio->setVolume(v);
            delay(150);
        }
        
        delay(1);
    }
    
    mp3Playing = false;
    mp3PlayingFile = "";
    
    // Restore M5 speaker
    M5Cardputer.Speaker.begin();
    applyVolume();
    
#else
    // No MP3 support compiled - show message
    File mp3File = SD.open(filepath, FILE_READ);
    if (!mp3File) return;
    
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "MP3 Player");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 28;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    sprite.setTextColor(COL_RED, COL_WHITE);
    sprite.setCursor(contentX + 10, contentY + 15);
    sprite.print("MP3 Not Enabled");
    
    sprite.setTextColor(COL_BLACK, COL_WHITE);
    sprite.setCursor(contentX + 10, contentY + 35);
    sprite.print("Install ESP32-audioI2S");
    sprite.setCursor(contentX + 10, contentY + 50);
    sprite.print("library and enable");
    sprite.setCursor(contentX + 10, contentY + 65);
    sprite.print("ENABLE_MP3 in code");
    
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    sprite.print("Esc:Back");
    
    sprite.pushSprite(0, 0);
    
    while (true) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            break;
        }
        delay(50);
    }
    
    mp3File.close();
#endif
}

// ==========================================
//     Bluetooth Audio (A2DP Source)
// ==========================================

#ifdef ENABLE_BT_AUDIO
// A2DP callback for audio data
int32_t btAudioCallback(uint8_t *data, int32_t len) {
    if (!audioStreamingToBT || !audioStreamFile) {
        return 0;
    }
    
    int bytesRead = audioStreamFile.read(data, len);
    if (bytesRead <= 0) {
        audioStreamingToBT = false;
        return 0;
    }
    return bytesRead;
}
#endif

void btAudioStartScan() {
    btAudioScanning = true;
    btAudioDevices.clear();
    btAudioAddresses.clear();
    btAudioScanIndex = 0;
    
    // Show scanning status
    renderBtAudio();
    
#ifdef ENABLE_BT_AUDIO
    // Use ESP32 Bluetooth Classic discovery
    // Note: This is a simplified version - full implementation needs
    // BluetoothSerial or direct esp_bt_gap API
    
    // For now, show instructions to user
    btAudioScanning = false;
    
    // Add placeholder message
    btAudioDevices.push_back("[Manual Entry Required]");
    btAudioAddresses.push_back("");
    
    // The actual A2DP connection is done by device name
    // User can type the name of their speaker
#else
    // Simulated scan for UI testing
    delay(1500);
    
    // Add some example devices for testing UI
    btAudioDevices.push_back("Example Speaker");
    btAudioAddresses.push_back("00:00:00:00:00:01");
    btAudioDevices.push_back("BT Headphones");
    btAudioAddresses.push_back("00:00:00:00:00:02");
    
    btAudioScanning = false;
#endif
    renderBtAudio();
}

void btAudioConnect(int index) {
#ifdef ENABLE_BT_AUDIO
    if (index < 0 || index >= (int)btAudioDevices.size()) return;
    
    if (a2dpSource == nullptr) {
        a2dpSource = new BluetoothA2DPSource();
    }
    
    // Connect to selected device
    const char* name = btAudioDevices[index].c_str();
    a2dpSource->start(name, btAudioCallback);
    
    btAudioConnected = true;
    btAudioDeviceName = btAudioDevices[index];
    btAudioDeviceAddr = btAudioAddresses[index];
#else
    // Placeholder connection
    if (index >= 0 && index < (int)btAudioDevices.size()) {
        btAudioConnected = true;
        btAudioDeviceName = btAudioDevices[index];
    }
#endif
    renderBtAudio();
}

void btAudioDisconnect() {
#ifdef ENABLE_BT_AUDIO
    if (a2dpSource != nullptr) {
        a2dpSource->end();
    }
#endif
    btAudioConnected = false;
    btAudioDeviceName = "";
    btAudioDeviceAddr = "";
    renderBtAudio();
}

void renderBtAudio() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "Bluetooth Audio");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 28;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    sprite.setTextColor(COL_BLACK, COL_WHITE);
    
#ifndef ENABLE_BT_AUDIO
    // Show limitation message for ESP32-S3
    sprite.setCursor(contentX + 5, contentY + 10);
    sprite.setTextColor(COL_RED, COL_WHITE);
    sprite.print("Not Available");
    
    sprite.setTextColor(COL_BLACK, COL_WHITE);
    sprite.setCursor(contentX + 5, contentY + 30);
    sprite.print("ESP32-S3 does not support");
    sprite.setCursor(contentX + 5, contentY + 45);
    sprite.print("Classic Bluetooth (A2DP)");
    
    sprite.setCursor(contentX + 5, contentY + 65);
    sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
    sprite.print("BT Audio requires the");
    sprite.setCursor(contentX + 5, contentY + 80);
    sprite.print("original ESP32 chip.");
    
    // Instructions bar
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    sprite.print("Esc:Back");
    
    sprite.pushSprite(0, 0);
    return;
#endif
    
    // Connection status header
    sprite.setCursor(contentX + 5, contentY + 5);
    if (btAudioConnected) {
        sprite.setTextColor(COL_GREEN, COL_WHITE);
        sprite.print("Connected: ");
        String dispName = btAudioDeviceName;
        if (dispName.length() > 15) dispName = dispName.substring(0, 12) + "...";
        sprite.print(dispName);
    } else if (btAudioScanning) {
        sprite.setTextColor(COL_BLUE, COL_WHITE);
        sprite.print("Scanning for devices...");
    } else {
        sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
        sprite.print("Not connected");
    }
    
    // Device list or options
    int listY = contentY + 22;
    int itemH = 16;
    int maxVisible = (contentH - 30) / itemH;
    
    if (btAudioConnected) {
        // Show connected device info and disconnect option
        sprite.setTextColor(COL_BLACK, COL_WHITE);
        sprite.setCursor(contentX + 5, listY);
        sprite.print("Device: " + btAudioDeviceName);
        
        sprite.setCursor(contentX + 5, listY + 18);
        sprite.print("Audio output: Bluetooth");
        
        // Disconnect button
        int btnY = listY + 45;
        bool selected = (btAudioScanIndex == 0);
        if (selected) {
            int selW = sprite.textWidth("[ Disconnect ]") + 10;
            sprite.fillRoundRect(contentX + 2, btnY, selW, itemH, 4, currentThemeColor);
            sprite.setTextColor(COL_WHITE, currentThemeColor);
        } else {
            sprite.setTextColor(COL_BLACK, COL_WHITE);
        }
        sprite.setCursor(contentX + 5, btnY + 3);
        sprite.print("[ Disconnect ]");
    } else if (!btAudioScanning && btAudioDevices.empty()) {
        // Show scan option
        bool selected = (btAudioScanIndex == 0);
        if (selected) {
            int selW = sprite.textWidth("[ Scan for devices ]") + 10;
            sprite.fillRoundRect(contentX + 2, listY, selW, itemH, 4, currentThemeColor);
            sprite.setTextColor(COL_WHITE, currentThemeColor);
        } else {
            sprite.setTextColor(COL_BLACK, COL_WHITE);
        }
        sprite.setCursor(contentX + 5, listY + 3);
        sprite.print("[ Scan for devices ]");
        
        sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
        sprite.setCursor(contentX + 5, listY + 25);
        sprite.print("Pair with BT speakers");
        sprite.setCursor(contentX + 5, listY + 40);
        sprite.print("to stream audio");
    } else if (!btAudioScanning) {
        // Show found devices
#ifdef ENABLE_SMOOTH_UI
        // Calculate animated selector for BT device list
        if (btAudioScanIndex < (int)btAudioDevices.size()) {
            int targetY = listY + btAudioScanIndex * itemH;
            String name = btAudioDevices[btAudioScanIndex];
            if (name.length() > 20) name = name.substring(0, 17) + "...";
            int targetW = 22 + sprite.textWidth(name) + 8;
            btAudioSelectorY = targetY;
            btAudioSelectorW = targetW;
        }
        int btSelY = (int)btAudioSelectorY;
        int btSelW = (int)btAudioSelectorW;
        
        // Draw animated selector
        if (btAudioScanIndex < (int)btAudioDevices.size()) {
            sprite.fillRoundRect(contentX + 2, btSelY, btSelW, itemH, 4, currentThemeColor);
        }
#endif
        
        for (int i = 0; i < min((int)btAudioDevices.size(), maxVisible); i++) {
            int y = listY + i * itemH;
            bool selected = (i == btAudioScanIndex);
            
#ifdef ENABLE_SMOOTH_UI
            int selTop = btSelY;
            int selBot = btSelY + itemH;
            bool inSelector = (y >= selTop - itemH && y <= selBot) && selected;
            
            if (inSelector) {
                sprite.setTextColor(COL_WHITE, currentThemeColor);
            } else {
                sprite.setTextColor(COL_BLACK, COL_WHITE);
            }
#else
            if (selected) {
                String name = btAudioDevices[i];
                if (name.length() > 20) name = name.substring(0, 17) + "...";
                int selW = 22 + sprite.textWidth(name) + 8;
                sprite.fillRoundRect(contentX + 2, y, selW, itemH, 4, currentThemeColor);
                sprite.setTextColor(COL_WHITE, currentThemeColor);
            } else {
                sprite.setTextColor(COL_BLACK, COL_WHITE);
            }
#endif
            
            // Bluetooth icon
            drawIconBluetooth(contentX + 2, y - 1, selected ? COL_WHITE : COL_BLUE);
            
            // Device name
            sprite.setCursor(contentX + 22, y + 3);
            String name = btAudioDevices[i];
            if (name.length() > 20) name = name.substring(0, 17) + "...";
            sprite.print(name);
        }
        
        // Rescan option at bottom
        int rescanY = listY + min((int)btAudioDevices.size(), maxVisible) * itemH + 5;
        bool rescanSelected = (btAudioScanIndex == (int)btAudioDevices.size());
        if (rescanSelected) {
            int selW = sprite.textWidth("[ Rescan ]") + 10;
            sprite.fillRoundRect(contentX + 2, rescanY, selW, itemH, 4, currentThemeColor);
            sprite.setTextColor(COL_WHITE, currentThemeColor);
        } else {
            sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
        }
        sprite.setCursor(contentX + 5, rescanY + 3);
        sprite.print("[ Rescan ]");
    }
    
    // Instructions bar
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    if (btAudioConnected) {
        sprite.print("Enter:Disconnect  Esc:Back");
    } else if (btAudioScanning) {
        sprite.print("Scanning...");
    } else {
        sprite.print("Enter:Select  Esc:Back");
    }
    
    sprite.pushSprite(0, 0);
}

void handleBtAudioInput() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    bool fnPressed = status.fn;
    
    // Fn+T - Open Terminal
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_T) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('t'))) {
        playSelectSound();
        currentMode = MODE_TERMINAL;
        needsRedraw = true;
        delay(150);
        return;
    }
    
    // Fn+P - Screenshot
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_P) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('p'))) {
        takeScreenshot();
        renderBtAudio();
        delay(150);
        return;
    }
    
    if (btAudioScanning) {
        // Can't do anything while scanning
        return;
    }
    
    // Navigate
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
        playClickSound();
        if (btAudioScanIndex > 0) btAudioScanIndex--;
        renderBtAudio();
        delay(100);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
        playClickSound();
        int maxIdx = btAudioConnected ? 0 : (btAudioDevices.empty() ? 0 : (int)btAudioDevices.size());
        if (btAudioScanIndex < maxIdx) btAudioScanIndex++;
        renderBtAudio();
        delay(100);
        return;
    }
    
    // Select
    if (status.enter) {
        playSelectSound();
        if (btAudioConnected) {
            // Disconnect
            btAudioDisconnect();
        } else if (btAudioDevices.empty()) {
            // Start scan
            btAudioStartScan();
        } else if (btAudioScanIndex < (int)btAudioDevices.size()) {
            // Connect to selected device
            btAudioConnect(btAudioScanIndex);
        } else {
            // Rescan
            btAudioStartScan();
        }
        delay(150);
        return;
    }
    
    // ESC - Back to menu
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        playBackSound();
        currentMode = MODE_COC_MENU;
        renderCocMenu();
        delay(150);
        return;
    }
}

// ==========================================
//     WiFi Direct File Transfer
// ==========================================

void startWifiDirectAP() {
    // Stop any existing connections
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    delay(100);
    
    // Set AP mode
    WiFi.mode(WIFI_AP);
    delay(100);
    
    // Start AP
    WiFi.softAP(wifiDirectAPSSID.c_str(), wifiDirectAPPass.c_str(), 6, false, 1);
    delay(500);
    
    wifiDirectEnabled = true;
}

void stopWifiDirectAP() {
    if (wifiDirectServer != nullptr) {
        wifiDirectServer->stop();
        delete wifiDirectServer;
        wifiDirectServer = nullptr;
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    wifiDirectEnabled = false;
    wifiDirectSending = false;
}

void renderWifiDirectSend() {
    int W = M5Cardputer.Display.width();
    int H = M5Cardputer.Display.height();
    
    sprite.fillSprite(0x0000);  // Black background
    
    // Title
    sprite.setTextColor(COL_CYAN, 0x0000);
    sprite.setCursor(10, 5);
    sprite.print("WiFi Direct File Share");
    
    // Connection status
    sprite.setCursor(10, 22);
    int numClients = WiFi.softAPgetStationNum();
    if (numClients > 0) {
        wifiDirectClientConnected = true;
        sprite.setTextColor(COL_GREEN, 0x0000);
        sprite.print("Client connected!");
    } else {
        wifiDirectClientConnected = false;
        sprite.setTextColor(COL_YELLOW, 0x0000);
        sprite.print("Waiting for connection...");
    }
    
    // AP Info
    sprite.setTextColor(COL_WHITE, 0x0000);
    sprite.setCursor(10, 40);
    sprite.print("SSID: ");
    sprite.setTextColor(COL_CYAN, 0x0000);
    sprite.print(wifiDirectAPSSID);
    
    sprite.setTextColor(COL_WHITE, 0x0000);
    sprite.setCursor(10, 55);
    sprite.print("Pass: ");
    sprite.setTextColor(COL_CYAN, 0x0000);
    sprite.print(wifiDirectAPPass);
    
    // File info
    sprite.setTextColor(COL_WHITE, 0x0000);
    sprite.setCursor(10, 73);
    sprite.print("File: ");
    sprite.setTextColor(COL_CYAN, 0x0000);
    String fname = wifiDirectFilePath.substring(wifiDirectFilePath.lastIndexOf('/') + 1);
    if (fname.length() > 25) fname = fname.substring(0, 22) + "...";
    sprite.print(fname);
    
    // URL to download - make it prominent
    sprite.setTextColor(COL_WHITE, 0x0000);
    sprite.setCursor(10, 91);
    sprite.print("Open in browser:");
    sprite.setTextColor(COL_GREEN, 0x0000);
    sprite.setCursor(10, 105);
    sprite.print("http://" + WiFi.softAPIP().toString());
    
    // Status bar
    sprite.fillRect(0, H - 12, W, 12, 0x2104);
    sprite.setTextColor(COL_GRAY_LIGHT, 0x2104);
    sprite.setCursor(4, H - 10);
    sprite.print("Esc:Back | Connect to WiFi & browse URL");
    
    sprite.pushSprite(0, 0);
}

void wifiDirectHandleDownload() {
    if (!wifiDirectServer->hasArg("file")) {
        wifiDirectServer->send(200, "text/html", 
            "<html><body><h2>M5 File Share</h2>"
            "<p>File: " + wifiDirectFilePath.substring(wifiDirectFilePath.lastIndexOf('/') + 1) + "</p>"
            "<a href='/download?file=1'>Download File</a></body></html>");
        return;
    }
    
    File file = SD.open(wifiDirectFilePath, FILE_READ);
    if (!file) {
        wifiDirectServer->send(404, "text/plain", "File not found");
        return;
    }
    
    String filename = wifiDirectFilePath.substring(wifiDirectFilePath.lastIndexOf('/') + 1);
    size_t fileSize = file.size();
    
    wifiDirectServer->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    wifiDirectServer->sendHeader("Content-Length", String(fileSize));
    wifiDirectServer->setContentLength(fileSize);
    wifiDirectServer->send(200, "application/octet-stream", "");
    
    // Stream file to client
    uint8_t buf[1024];
    size_t sent = 0;
    
    wifiDirectSending = true;
    while (file.available() && wifiDirectSending) {
        int bytesRead = file.read(buf, sizeof(buf));
        if (bytesRead > 0) {
            wifiDirectServer->client().write(buf, bytesRead);
            sent += bytesRead;
        }
        
        // Check for cancel every few chunks
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            wifiDirectSending = false;
            break;
        }
    }
    
    file.close();
    wifiDirectSending = false;
}

void startWifiDirectTransfer(String filepath) {
    wifiDirectFilePath = filepath;
    wifiDirectSending = false;
    wifiDirectClientConnected = false;
    
    // Start WiFi AP
    startWifiDirectAP();
    
    // Create web server for file download
    if (wifiDirectServer != nullptr) {
        delete wifiDirectServer;
    }
    wifiDirectServer = new WebServer(80);
    wifiDirectServer->on("/", HTTP_GET, wifiDirectHandleDownload);
    wifiDirectServer->on("/download", HTTP_GET, wifiDirectHandleDownload);
    wifiDirectServer->begin();
    
    currentMode = MODE_BT_SEND;  // Reuse the same mode
    renderWifiDirectSend();
}

void handleWifiDirectInput() {
    // Handle web server
    if (wifiDirectServer != nullptr) {
        wifiDirectServer->handleClient();
    }
    
    // Update display periodically
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 500) {
        lastUpdate = millis();
        renderWifiDirectSend();
    }
    
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        stopWifiDirectAP();
        playBackSound();
        currentMode = MODE_FILE_UI;
        renderFileUI();
        delay(150);
        return;
    }
}

// ==========================================
//     COC UI - WiFi Submenu
// ==========================================

void cocScanWifi() {
    cocWifiScanning = true;
    cocWifiNetworks.clear();
    renderCocWifi();
    
    int n = WiFi.scanNetworks();
    
    for (int i = 0; i < n; i++) {
        WifiNetwork net;
        net.ssid = WiFi.SSID(i);
        net.rssi = WiFi.RSSI(i);
        net.encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        cocWifiNetworks.push_back(net);
    }
    
    // Sort by signal strength
    std::sort(cocWifiNetworks.begin(), cocWifiNetworks.end(), 
              [](const WifiNetwork &a, const WifiNetwork &b) { return a.rssi > b.rssi; });
    
    WiFi.scanDelete();
    cocWifiScanning = false;
    cocWifiListIndex = 0;
}

void renderCocWifi() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "WiFi");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 12;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    // Status bar at top
    sprite.fillRect(contentX, contentY, contentW, 14, COL_GRAY_LIGHT);
    draw3DSunken(contentX, contentY, contentW, 14);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(contentX + 4, contentY + 3);
    if (WiFi.status() == WL_CONNECTED) {
        sprite.print("Connected: " + WiFi.SSID());
    } else {
        sprite.print("Not connected");
    }
    
    // Buttons row
    int btnY = contentY + 16;
    int btnH = 14;
    
    // Scan button
    bool scanSel = (cocSubMenuIndex == 0);
    sprite.fillRoundRect(contentX + 2, btnY, 65, btnH, 4, scanSel ? currentThemeColor : COL_GRAY_LIGHT);
    draw3DRaised(contentX + 2, btnY, 65, btnH);
    sprite.setTextColor(scanSel ? COL_WHITE : COL_BLACK, scanSel ? currentThemeColor : COL_GRAY_LIGHT);
    sprite.setCursor(contentX + 8, btnY + 3);
    sprite.print(cocWifiScanning ? "Scanning" : "Scan");
    
    // Disconnect button (if connected)
    if (WiFi.status() == WL_CONNECTED) {
        bool discSel = (cocSubMenuIndex == 1);
        sprite.fillRoundRect(contentX + 72, btnY, 75, btnH, 4, discSel ? currentThemeColor : COL_GRAY_LIGHT);
        draw3DRaised(contentX + 72, btnY, 75, btnH);
        sprite.setTextColor(discSel ? COL_WHITE : COL_BLACK, discSel ? currentThemeColor : COL_GRAY_LIGHT);
        sprite.setCursor(contentX + 78, btnY + 3);
        sprite.print("Disconnect");
    }
    
    // Network list
    int listY = btnY + btnH + 4;
    int listH = contentH - (listY - contentY) - 2;
    sprite.fillRect(contentX + 1, listY, contentW - 2, listH, COL_WHITE);
    
    int itemH = 14;
    int maxItems = listH / itemH;
    int baseIdx = (WiFi.status() == WL_CONNECTED) ? 2 : 1;
    
    // Calculate scroll
    int listScroll = 0;
    int selectedNetIdx = cocSubMenuIndex - baseIdx;
    if (selectedNetIdx >= maxItems) {
        listScroll = selectedNetIdx - maxItems + 1;
    }
    
#ifdef ENABLE_SMOOTH_UI
    // Calculate animated selector for WiFi list
    int selectedNetIdx2 = cocSubMenuIndex - baseIdx;
    if (selectedNetIdx2 >= 0 && selectedNetIdx2 < (int)cocWifiNetworks.size()) {
        int visibleIdx = selectedNetIdx2 - listScroll;
        if (visibleIdx >= 0 && visibleIdx < maxItems) {
            int targetY = listY + visibleIdx * itemH;
            String ssid = cocWifiNetworks[selectedNetIdx2].ssid;
            if (ssid.length() > 22) ssid = ssid.substring(0, 19) + "...";
            int targetW = 32 + sprite.textWidth(ssid) + 8;
            wifiSelectorY = targetY;
            wifiSelectorW = targetW;
        }
    }
    int wifiSelY = (int)wifiSelectorY;
    int wifiSelW = (int)wifiSelectorW;
    
    // Draw animated selector if selecting a network
    if (cocSubMenuIndex >= baseIdx && cocSubMenuIndex < baseIdx + (int)cocWifiNetworks.size()) {
        sprite.fillRoundRect(contentX + 2, wifiSelY, wifiSelW, itemH - 1, 4, currentThemeColor);
    }
#endif
    
    int drawY = listY;
    for (int i = listScroll; i < (int)cocWifiNetworks.size() && (i - listScroll) < maxItems; i++) {
        bool sel = (cocSubMenuIndex == baseIdx + i);
        
#ifdef ENABLE_SMOOTH_UI
        int selTop = wifiSelY;
        int selBot = wifiSelY + itemH - 1;
        bool inSelector = (drawY >= selTop - itemH && drawY <= selBot) && sel;
        
        if (inSelector) {
            sprite.setTextColor(COL_WHITE, currentThemeColor);
        } else {
            sprite.setTextColor(COL_BLACK, COL_WHITE);
        }
#else
        if (sel) {
            String ssid = cocWifiNetworks[i].ssid;
            if (ssid.length() > 22) ssid = ssid.substring(0, 19) + "...";
            int selW = 32 + sprite.textWidth(ssid) + 8;
            sprite.fillRoundRect(contentX + 2, drawY, selW, itemH - 1, 4, currentThemeColor);
            sprite.setTextColor(COL_WHITE, currentThemeColor);
        } else {
            sprite.setTextColor(COL_BLACK, COL_WHITE);
        }
#endif
        
        // Signal bars
        int bars = 0;
        if (cocWifiNetworks[i].rssi >= -50) bars = 4;
        else if (cocWifiNetworks[i].rssi >= -60) bars = 3;
        else if (cocWifiNetworks[i].rssi >= -70) bars = 2;
        else if (cocWifiNetworks[i].rssi >= -80) bars = 1;
        
        for (int b = 0; b < 4; b++) {
            uint16_t barCol = (b < bars) ? COL_GREEN : COL_GRAY_LIGHT;
            sprite.fillRect(contentX + 6 + b * 4, drawY + 10 - b * 2, 3, 2 + b * 2, barCol);
        }
        
        // Lock icon if encrypted
        if (cocWifiNetworks[i].encrypted) {
            sprite.setCursor(contentX + 24, drawY + 3);
            sprite.print("*");
        }
        
        // SSID
        sprite.setCursor(contentX + 32, drawY + 3);
        String ssid = cocWifiNetworks[i].ssid;
        if (ssid.length() > 22) ssid = ssid.substring(0, 19) + "...";
        sprite.print(ssid);
        
        drawY += itemH;
    }
    
    if (cocWifiNetworks.empty() && !cocWifiScanning) {
        sprite.setTextColor(COL_GRAY_MID, COL_WHITE);
        sprite.setCursor(contentX + 30, listY + 20);
        sprite.print("Press Scan to find networks");
    }
    
    // Status bar
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    sprite.print("Enter:Connect  Esc:Back");
    
    // Draw password dialog overlay if entering password
    if (cocWifiEnteringPass) {
        int dlgW = 200, dlgH = 55;
        int dlgX = (screenW - dlgW) / 2, dlgY = (screenH - dlgH) / 2;
        
        sprite.fillRect(dlgX, dlgY, dlgW, dlgH, COL_GRAY_LIGHT);
        draw3DRaised(dlgX, dlgY, dlgW, dlgH);
        sprite.fillRect(dlgX + 3, dlgY + 3, dlgW - 6, 12, COL_TITLE_BAR);
        sprite.setTextColor(COL_WHITE, COL_TITLE_BAR);
        sprite.setCursor(dlgX + 6, dlgY + 5);
        sprite.print("WiFi Password");
        
        sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
        sprite.setCursor(dlgX + 6, dlgY + 20);
        String ssidDisplay = targetFilePath;
        if (ssidDisplay.length() > 25) ssidDisplay = ssidDisplay.substring(0, 22) + "...";
        sprite.print(ssidDisplay);
        
        // Input field
        sprite.fillRect(dlgX + 6, dlgY + 32, dlgW - 12, 14, COL_WHITE);
        draw3DSunken(dlgX + 6, dlgY + 32, dlgW - 12, 14);
        sprite.setTextColor(COL_BLACK, COL_WHITE);
        sprite.setCursor(dlgX + 10, dlgY + 35);
        String displayPass = "";
        for (int i = 0; i < (int)cocWifiInputPass.length(); i++) displayPass += "*";
        if (displayPass.length() > 28) displayPass = displayPass.substring(displayPass.length() - 28);
        sprite.print(displayPass);
        sprite.fillRect(dlgX + 10 + displayPass.length() * 6, dlgY + 34, 2, 10, COL_BLACK);
    }
    
    sprite.pushSprite(0, 0);
}

void cocConnectWifi(int networkIndex) {
    if (networkIndex < 0 || networkIndex >= (int)cocWifiNetworks.size()) return;
    
    WifiNetwork &net = cocWifiNetworks[networkIndex];
    
    if (net.encrypted) {
        // Need password - use dialog system
        cocWifiInputPass = "";
        targetFilePath = net.ssid;  // Store SSID temporarily
        dialogMessage = "Password for " + net.ssid.substring(0, 15);
        dialogInput = "";
        pendingAction = ACTION_NONE;
        cocWifiEnteringPass = true;
        // Show input in WiFi screen instead of switching modes
    } else {
        // Open network, connect directly
        WiFi.begin(net.ssid.c_str());
        
        // Show connecting message
        sprite.fillRect(60, 50, 120, 30, 0x0000);
        sprite.drawRect(60, 50, 120, 30, COL_WHITE);
        sprite.setTextColor(COL_WHITE, 0x0000);
        sprite.setCursor(75, 60);
        sprite.print("Connecting...");
        sprite.pushSprite(0, 0);
        
        int timeout = 20;
        while (WiFi.status() != WL_CONNECTED && timeout > 0) {
            delay(500);
            timeout--;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            cocSavedWifiSSID = net.ssid;
            cocSavedWifiPass = "";
            saveUserConfig();
        }
    }
    renderCocWifi();
}

void handleCocWifiInput() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    bool fnPressed = status.fn;
    
    // Fn+T - Open Terminal (works even in password mode)
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_T) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('t'))) {
        cocWifiEnteringPass = false;
        currentMode = MODE_TERMINAL;
        needsRedraw = true;
        delay(150);
        return;
    }
    
    // Fn+P - Screenshot
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_P) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('p'))) {
        takeScreenshot();
        renderCocWifi();
        delay(150);
        return;
    }
    
    // Password entry mode
    if (cocWifiEnteringPass) {
        // Type password - accept all printable ASCII except nav keys when used for nav
        for (auto c : status.word) {
            // Skip navigation keys if used as navigation (not part of password)
            // But we still want to allow these chars in passwords
            if (c >= 32 && c < 127) {
                // Don't add nav characters if they're being pressed alone
                // (they go into status.word as regular chars)
                bool isNavKey = (c == ';' || c == '.' || c == ',' || c == '/' || c == '`');
                if (!isNavKey) {
                    cocWifiInputPass += c;
                }
            }
        }
        
        if (status.del && cocWifiInputPass.length() > 0) {
            cocWifiInputPass.remove(cocWifiInputPass.length() - 1);
        }
        
        if (status.enter && cocWifiInputPass.length() > 0) {
            // Connect with password
            String ssid = targetFilePath;
            
            // Show connecting dialog
            int W = M5Cardputer.Display.width();
            int H = M5Cardputer.Display.height();
            sprite.fillRect(W/2 - 60, H/2 - 15, 120, 30, COL_GRAY_LIGHT);
            draw3DRaised(W/2 - 60, H/2 - 15, 120, 30);
            sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
            sprite.setCursor(W/2 - 45, H/2 - 5);
            sprite.print("Connecting...");
            sprite.pushSprite(0, 0);
            
            // Disconnect first and set mode
            WiFi.disconnect(true);
            delay(200);
            WiFi.mode(WIFI_STA);
            delay(200);
            
            // Start connection
            WiFi.begin(ssid.c_str(), cocWifiInputPass.c_str());
            
            // Wait for connection with visual feedback
            int timeout = 40;  // 20 seconds
            while (WiFi.status() != WL_CONNECTED && timeout > 0) {
                delay(500);
                timeout--;
                // Update connecting animation
                sprite.fillRect(W/2 - 60, H/2 - 15, 120, 30, COL_GRAY_LIGHT);
                draw3DRaised(W/2 - 60, H/2 - 15, 120, 30);
                sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
                sprite.setCursor(W/2 - 50, H/2 - 5);
                String dots = "";
                for (int d = 0; d < (40 - timeout) % 4; d++) dots += ".";
                sprite.print("Connecting" + dots);
                sprite.pushSprite(0, 0);
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                cocSavedWifiSSID = ssid;
                cocSavedWifiPass = cocWifiInputPass;
                saveUserConfig();
            }
            
            cocWifiEnteringPass = false;
            cocWifiInputPass = "";
            renderCocWifi();
            return;
        }
        
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            cocWifiEnteringPass = false;
            cocWifiInputPass = "";
            renderCocWifi();
            return;
        }
        
        // Draw password input overlay
        int W = M5Cardputer.Display.width();
        int H = M5Cardputer.Display.height();
        int dlgW = 200, dlgH = 55;
        int dlgX = (W - dlgW) / 2, dlgY = (H - dlgH) / 2;
        
        sprite.fillRect(dlgX, dlgY, dlgW, dlgH, COL_GRAY_LIGHT);
        draw3DRaised(dlgX, dlgY, dlgW, dlgH);
        sprite.fillRect(dlgX + 3, dlgY + 3, dlgW - 6, 12, COL_TITLE_BAR);
        sprite.setTextColor(COL_WHITE, COL_TITLE_BAR);
        sprite.setCursor(dlgX + 6, dlgY + 5);
        sprite.print("WiFi Password");
        
        sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
        sprite.setCursor(dlgX + 6, dlgY + 20);
        String ssidDisplay = targetFilePath;
        if (ssidDisplay.length() > 25) ssidDisplay = ssidDisplay.substring(0, 22) + "...";
        sprite.print(ssidDisplay);
        
        // Input field
        sprite.fillRect(dlgX + 6, dlgY + 32, dlgW - 12, 14, COL_WHITE);
        draw3DSunken(dlgX + 6, dlgY + 32, dlgW - 12, 14);
        sprite.setTextColor(COL_BLACK, COL_WHITE);
        sprite.setCursor(dlgX + 10, dlgY + 35);
        String displayPass = "";
        for (int i = 0; i < (int)cocWifiInputPass.length(); i++) displayPass += "*";
        if (displayPass.length() > 28) displayPass = displayPass.substring(displayPass.length() - 28);
        sprite.print(displayPass);
        sprite.fillRect(dlgX + 10 + displayPass.length() * 6, dlgY + 34, 2, 10, COL_BLACK);
        
        sprite.pushSprite(0, 0);
        return;
    }
    
    // Normal navigation
    int maxIdx = 1 + cocWifiNetworks.size();
    if (WiFi.status() != WL_CONNECTED) maxIdx = 0 + cocWifiNetworks.size();
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
        if (cocSubMenuIndex > 0) cocSubMenuIndex--;
        else cocSubMenuIndex = maxIdx;
        renderCocWifi();
        delay(100);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
        if (cocSubMenuIndex < maxIdx) cocSubMenuIndex++;
        else cocSubMenuIndex = 0;
        renderCocWifi();
        delay(100);
        return;
    }
    
    if (status.enter) {
        if (cocSubMenuIndex == 0) {
            // Scan
            cocScanWifi();
            renderCocWifi();
        } else if (cocSubMenuIndex == 1 && WiFi.status() == WL_CONNECTED) {
            // Disconnect
            WiFi.disconnect();
            renderCocWifi();
        } else {
            // Connect to network
            int netIdx = cocSubMenuIndex - (WiFi.status() == WL_CONNECTED ? 2 : 1);
            if (netIdx >= 0 && netIdx < (int)cocWifiNetworks.size()) {
                cocConnectWifi(netIdx);
            }
        }
        delay(150);
        return;
    }
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        currentMode = MODE_COC_MENU;
        renderCocMenu();
        delay(150);
        return;
    }
}

// ==========================================
//     COC UI - Bluetooth Submenu
// ==========================================

void cocInitBluetooth() {
    if (!cocBtInitialized) {
        // Initialize NimBLE
        NimBLEDevice::init(cocBtDeviceName.c_str());
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // Max power
        
        // Create BLE Server
        pServer = NimBLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());
        
        // Create BLE Service
        NimBLEService *pService = pServer->createService(BLE_SERVICE_UUID);
        
        // Create BLE Characteristic for file transfer
        pCharacteristic = pService->createCharacteristic(
            BLE_CHAR_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
        );
        
        // Start the service
        pService->start();
        
        // Configure and start advertising
        NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
        pAdvertising->start();
        
        cocBtInitialized = true;
    }
}

void renderCocBluetooth() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "Bluetooth");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 12;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    // Status section
    sprite.fillRect(contentX, contentY, contentW, 26, COL_GRAY_LIGHT);
    draw3DSunken(contentX, contentY, contentW, 26);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(contentX + 4, contentY + 3);
    sprite.print("Device: " + cocBtDeviceName);
    
    sprite.setCursor(contentX + 4, contentY + 14);
    if (!cocBtInitialized) {
        sprite.setTextColor(COL_RED, COL_GRAY_LIGHT);
        sprite.print("Status: Off");
    } else if (cocBtDeviceConnected) {
        sprite.setTextColor(COL_GREEN, COL_GRAY_LIGHT);
        sprite.print("Status: Connected!");
    } else {
        sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
        sprite.print("Status: Advertising...");
    }
    
    // Enable button
    int btnY = contentY + 30;
    bool initSel = (cocSubMenuIndex == 0);
    sprite.fillRoundRect(contentX + 4, btnY, 90, 16, 4, initSel ? currentThemeColor : COL_GRAY_LIGHT);
    draw3DRaised(contentX + 4, btnY, 90, 16);
    sprite.setTextColor(initSel ? COL_WHITE : COL_BLACK, initSel ? currentThemeColor : COL_GRAY_LIGHT);
    sprite.setCursor(contentX + 10, btnY + 4);
    sprite.print(cocBtInitialized ? "BLE Active" : "Enable BLE");
    
    // Info box
    int infoY = btnY + 22;
    sprite.fillRect(contentX + 2, infoY, contentW - 4, 40, COL_WHITE);
    sprite.setTextColor(COL_BLACK, COL_WHITE);
    sprite.setCursor(contentX + 6, infoY + 4);
    sprite.print("Use BLE scanner app to find:");
    sprite.setTextColor(COL_SELECTION, COL_WHITE);
    sprite.setCursor(contentX + 6, infoY + 16);
    sprite.print("\"" + cocBtDeviceName + "\"");
    sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
    sprite.setCursor(contentX + 6, infoY + 28);
    sprite.print("Fn+B in FileUI to send files");
    
    // Status bar
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    sprite.print("Enter:Enable  Esc:Back");
    
    sprite.pushSprite(0, 0);
}

void handleCocBluetoothInput() {
    M5Cardputer.update();
    
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    
    if (status.enter) {
        if (!cocBtInitialized) {
            cocInitBluetooth();
        }
        renderCocBluetooth();
        delay(150);
        return;
    }
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        currentMode = MODE_COC_MENU;
        renderCocMenu();
        delay(150);
        return;
    }
}

// ==========================================
//     COC UI - Bluetooth File Send
// ==========================================

void renderBtSend() {
    int W = M5Cardputer.Display.width();
    int H = M5Cardputer.Display.height();
    
    sprite.fillSprite(0x0000);
    
    // Title
    sprite.fillRect(0, 0, W, 14, 0x0014);
    sprite.setTextColor(COL_WHITE, 0x0014);
    sprite.setCursor(W/2 - 40, 3);
    sprite.print("BLE TRANSFER");
    
    // Connection status
    sprite.setCursor(10, 20);
    if (cocBtDeviceConnected) {
        sprite.setTextColor(COL_GREEN, 0x0000);
        sprite.print("Connected");
    } else {
        sprite.setTextColor(COL_YELLOW, 0x0000);
        sprite.print("Waiting for BLE connection...");
    }
    
    // File info
    sprite.setTextColor(COL_WHITE, 0x0000);
    sprite.setCursor(10, 35);
    sprite.print("File: ");
    sprite.setTextColor(COL_CYAN, 0x0000);
    String fname = cocBtSendFile.substring(cocBtSendFile.lastIndexOf('/') + 1);
    if (fname.length() > 25) fname = fname.substring(0, 22) + "...";
    sprite.print(fname);
    
    // Progress bar
    int barX = 20, barY = 55, barW = W - 40, barH = 20;
    sprite.drawRect(barX - 1, barY - 1, barW + 2, barH + 2, COL_WHITE);
    sprite.fillRect(barX, barY, barW, barH, 0x2104);
    
    int fillW = (barW * cocBtSendProgress) / 100;
    sprite.fillRect(barX, barY, fillW, barH, COL_CYAN);
    
    // Percentage
    sprite.setTextColor(COL_WHITE, cocBtSendProgress > 50 ? COL_CYAN : 0x2104);
    sprite.setCursor(barX + barW/2 - 12, barY + 6);
    sprite.print(String(cocBtSendProgress) + "%");
    
    // Status
    sprite.setTextColor(COL_GRAY_MID, 0x0000);
    sprite.setCursor(10, 85);
    if (cocBtSending && cocBtDeviceConnected) {
        sprite.print("Sending via BLE...");
    } else if (cocBtSendProgress == 100) {
        sprite.setTextColor(COL_GREEN, 0x0000);
        sprite.print("Transfer complete!");
    } else if (!cocBtDeviceConnected) {
        sprite.print("Connect with BLE app");
    }
    
    sprite.setCursor(10, 100);
    sprite.setTextColor(COL_GRAY_DARK, 0x0000);
    sprite.print("UUID: 4fafc201...");
    
    // Status bar
    sprite.fillRect(0, H - 10, W, 10, 0x2104);
    sprite.setTextColor(COL_GRAY_LIGHT, 0x2104);
    sprite.setCursor(4, H - 9);
    sprite.print("Esc:Cancel");
    
    sprite.pushSprite(0, 0);
}

void cocSendFileBluetooth(String filepath) {
    if (!cocBtInitialized) {
        cocInitBluetooth();
        delay(1000);  // Give time for BLE to start
    }
    
    cocBtSendFile = filepath;
    cocBtSendProgress = 0;
    cocBtSending = true;
    currentMode = MODE_BT_SEND;
    renderBtSend();
    
    // Wait for connection
    int waitCount = 0;
    while (!cocBtDeviceConnected && waitCount < 300) {  // 30 seconds max
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            cocBtSending = false;
            return;
        }
        delay(100);
        waitCount++;
        if (waitCount % 10 == 0) renderBtSend();  // Update display
    }
    
    if (!cocBtDeviceConnected) {
        cocBtSending = false;
        cocBtSendProgress = 0;
        return;
    }
    
    File file = SD.open(filepath, FILE_READ);
    if (!file) {
        cocBtSending = false;
        cocBtSendProgress = 0;
        return;
    }
    
    size_t fileSize = file.size();
    size_t sent = 0;
    uint8_t buf[240];  // BLE MTU is typically ~244 bytes
    
    // Send filename and size first as header
    String fname = filepath.substring(filepath.lastIndexOf('/') + 1);
    String header = "FILE:" + fname + ":" + String(fileSize);
    pCharacteristic->setValue((uint8_t*)header.c_str(), header.length());
    pCharacteristic->notify();
    delay(50);
    
    while (file.available() && cocBtSending && cocBtDeviceConnected) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            cocBtSending = false;
            break;
        }
        
        int bytesRead = file.read(buf, sizeof(buf));
        if (bytesRead > 0) {
            pCharacteristic->setValue(buf, bytesRead);
            pCharacteristic->notify();
            sent += bytesRead;
            cocBtSendProgress = (sent * 100) / fileSize;
            renderBtSend();
        }
        delay(20);  // BLE needs time between notifications
    }
    
    file.close();
    
    if (cocBtSending && cocBtDeviceConnected) {
        cocBtSendProgress = 100;
        cocBtSending = false;
        String endMsg = "FILE:END";
        pCharacteristic->setValue((uint8_t*)endMsg.c_str(), endMsg.length());
        pCharacteristic->notify();
    }
    
    renderBtSend();
}

void handleBtSendInput() {
    M5Cardputer.update();
    
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        cocBtSending = false;
        currentMode = MODE_FILE_UI;
        renderFileUI();
        delay(150);
        return;
    }
    
    // If transfer complete, any key returns
    if (!cocBtSending && cocBtSendProgress == 100) {
        currentMode = MODE_FILE_UI;
        renderFileUI();
        delay(150);
    }
}

// ==========================================
//     COC UI - USB Transfer
// ==========================================

void renderCocUSB() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "USB Mass Storage");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 12;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    if (!usbModeActive) {
        sprite.setTextColor(COL_BLACK, COL_WHITE);
        sprite.setCursor(contentX + 6, contentY + 8);
        sprite.print("USB Mass Storage Mode");
        
        sprite.setCursor(contentX + 6, contentY + 26);
        sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
        sprite.print("Connect USB-C to computer");
        
        sprite.setCursor(contentX + 6, contentY + 40);
        sprite.print("SD card appears as drive");
        
#ifndef ENABLE_USB_MSC
        sprite.setCursor(contentX + 6, contentY + 58);
        sprite.setTextColor(COL_ORANGE, COL_WHITE);
        sprite.print("Note: USB MSC disabled");
        sprite.setCursor(contentX + 6, contentY + 70);
        sprite.print("Enable in code to use");
#else
        sprite.setCursor(contentX + 6, contentY + 54);
        sprite.setTextColor(COL_RED, COL_WHITE);
        sprite.print("SD unavailable while on");
#endif
    } else {
        sprite.setTextColor(COL_GREEN, COL_WHITE);
        sprite.setCursor(contentX + 6, contentY + 8);
        sprite.print("USB ACTIVE");
        
        sprite.setCursor(contentX + 6, contentY + 26);
        sprite.setTextColor(COL_BLACK, COL_WHITE);
        sprite.print("SD card is now a USB drive");
        
        sprite.setCursor(contentX + 6, contentY + 44);
        sprite.setTextColor(COL_ORANGE, COL_WHITE);
        sprite.print("IMPORTANT:");
        
        sprite.setCursor(contentX + 6, contentY + 56);
        sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
        sprite.print("Safely eject from PC before");
        
        sprite.setCursor(contentX + 6, contentY + 68);
        sprite.print("pressing Esc or turning off!");
    }
    
    // USB icon
    int iconX = contentX + contentW - 45;
    int iconY = contentY + 15;
    uint16_t iconCol = usbModeActive ? COL_GREEN : COL_GRAY_MID;
    sprite.fillRect(iconX, iconY, 30, 20, iconCol);
    draw3DRaised(iconX, iconY, 30, 20);
    sprite.fillRect(iconX + 10, iconY - 6, 10, 8, iconCol);
    sprite.fillRect(iconX + 5, iconY + 5, 20, 10, COL_WHITE);
    
    // Status bar
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    sprite.print(usbModeActive ? "Enter:Disable  Esc:Back" : "Enter:Enable  Esc:Back");
    
    sprite.pushSprite(0, 0);
}

void handleCocUSBInput() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    bool fnPressed = status.fn;
    
    // Fn+T - Open Terminal
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_T) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('t'))) {
        currentMode = MODE_TERMINAL;
        needsRedraw = true;
        delay(150);
        return;
    }
    
    // Fn+P - Screenshot
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_P) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('p'))) {
        takeScreenshot();
        renderCocUSB();
        delay(150);
        return;
    }
    
    if (status.enter) {
#ifdef ENABLE_USB_MSC
        if (!usbModeActive) {
            startUSBMSC();
        } else {
            stopUSBMSC();
        }
#else
        usbModeActive = !usbModeActive;
#endif
        renderCocUSB();
        delay(150);
        return;
    }
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
#ifdef ENABLE_USB_MSC
        if (usbModeActive) {
            stopUSBMSC();
        }
#endif
        usbModeActive = false;
        currentMode = MODE_COC_MENU;
        renderCocMenu();
        delay(150);
        return;
    }
}

// ==========================================
//     COC UI - Settings
// ==========================================

void renderCocSettings() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "Settings");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 28;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    int itemH = 22;
    int maxVisible = contentH / itemH;
    if (maxVisible > COC_SETTINGS_VISIBLE) maxVisible = COC_SETTINGS_VISIBLE;
    
    // Adjust scroll to keep selection visible
    if (cocSettingsIndex < cocSettingsScroll) {
        cocSettingsScroll = cocSettingsIndex;
    }
    if (cocSettingsIndex >= cocSettingsScroll + maxVisible) {
        cocSettingsScroll = cocSettingsIndex - maxVisible + 1;
    }
    
    // Get screen timeout in seconds for display
    int screenTimeoutSec = (screenOffTimeoutMs == 0) ? 0 : screenOffTimeoutMs / 1000;
    
#ifdef ENABLE_SMOOTH_UI
    // Calculate and draw animated selector
    int visibleIdx = cocSettingsIndex - cocSettingsScroll;
    int targetY = contentY + 4 + visibleIdx * itemH;
    int targetW = 8 + sprite.textWidth(cocSettingsItems[cocSettingsIndex]) + 8;  // margin + text + padding
    settingsSelectorY = targetY;
    settingsSelectorW = targetW;  // Animate width too
    int selectorY = (int)settingsSelectorY;
    int selectorW = (int)settingsSelectorW;  // Get current animated width
    
    // Draw animated selector background using theme color
    sprite.fillRoundRect(contentX + 1, selectorY, selectorW, itemH - 2, 4, currentThemeColor);
#endif
    
    // Draw visible items
    for (int v = 0; v < maxVisible && (cocSettingsScroll + v) < COC_SETTINGS_COUNT; v++) {
        int i = cocSettingsScroll + v;
        int y = contentY + 4 + v * itemH;
        bool sel = (i == cocSettingsIndex);
        
#ifdef ENABLE_SMOOTH_UI
        // Check if this item overlaps with animated selector
        int selTop = selectorY;
        int selBot = selectorY + itemH - 2;
        bool inSelector = (y >= selTop - itemH && y <= selBot);
        
        if (inSelector && sel) {
            sprite.setTextColor(COL_WHITE, currentThemeColor);
        } else {
            sprite.setTextColor(COL_BLACK, COL_WHITE);
        }
#else
        if (sel) {
            int selW = 8 + sprite.textWidth(cocSettingsItems[i]) + 8;  // margin + text + padding
            sprite.fillRoundRect(contentX + 1, y, selW, itemH - 2, 4, currentThemeColor);
            sprite.setTextColor(COL_WHITE, currentThemeColor);
        } else {
            sprite.setTextColor(COL_BLACK, COL_WHITE);
        }
#endif
        
        sprite.setCursor(contentX + 8, y + 6);
        sprite.print(cocSettingsItems[i]);
        
        // Value display (always on white background, not highlighted)
        sprite.setCursor(contentX + contentW - 75, y + 6);
        switch (i) {
            case 0:  // Brightness
                {
                    int sliderX = contentX + contentW - 70;
                    int sliderW = 55;
                    sprite.fillRect(sliderX, y + 8, sliderW, 6, COL_GRAY_MID);
                    int fillW = (sliderW * configBrightness) / 100;
                    if (fillW > 0) {
                        sprite.fillRect(sliderX, y + 8, fillW, 6, currentThemeColor);
                    }
                }
                break;
            case 1:  // Sound
                sprite.setTextColor(settingSoundEnabled ? COL_GREEN : COL_GRAY_MID, COL_WHITE);
                sprite.print(settingSoundEnabled ? "[ON]" : "[OFF]");
                break;
            case 2:  // Screen Timeout
                {
                    sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
                    String timeoutStr;
                    if (screenTimeoutSec == 0) timeoutStr = "Never";
                    else if (screenTimeoutSec == 30) timeoutStr = "30 sec";
                    else if (screenTimeoutSec == 60) timeoutStr = "1 min";
                    else if (screenTimeoutSec == 120) timeoutStr = "2 min";
                    else if (screenTimeoutSec == 300) timeoutStr = "5 min";
                    else timeoutStr = String(screenTimeoutSec) + "s";
                    sprite.print(timeoutStr);
                }
                break;
            case 3:  // Timezone
                {
                    sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
                    String tzStr = "GMT";
                    if (configTimezoneOffset >= 0) tzStr += "+";
                    tzStr += String(configTimezoneOffset);
                    sprite.print(tzStr);
                }
                break;
            case 4:  // Nickname
                {
                    sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
                    String displayNick = chatNickname;
                    if (displayNick.length() > 10) displayNick = displayNick.substring(0, 10);
                    if (nicknameEditMode && cocSettingsIndex == 4) {
                        sprite.print(displayNick + "_");
                    } else {
                        sprite.print(displayNick);
                    }
                }
                break;
            case 5:  // SID Duration
                {
                    sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
                    int mins = configSidDuration / 60;
                    int secs = configSidDuration % 60;
                    String durStr;
                    if (secs == 0) durStr = String(mins) + " min";
                    else durStr = String(mins) + ":" + (secs < 10 ? "0" : "") + String(secs);
                    sprite.print(durStr);
                }
                break;
            case 6:  // Save & Exit
                break;
        }
    }
    
    // Draw scrollbar if needed
    if (COC_SETTINGS_COUNT > maxVisible) {
        int scrollBarX = contentX + contentW - 10;
        int scrollBarH = contentH - 4;
        sprite.fillRect(scrollBarX, contentY + 2, 8, scrollBarH, COL_GRAY_LIGHT);
        draw3DSunken(scrollBarX, contentY + 2, 8, scrollBarH);
        
        int thumbH = max(10, scrollBarH * maxVisible / COC_SETTINGS_COUNT);
        int thumbY = contentY + 2 + (scrollBarH - thumbH) * cocSettingsScroll / max(1, COC_SETTINGS_COUNT - maxVisible);
        sprite.fillRect(scrollBarX + 1, thumbY, 6, thumbH, COL_GRAY_MID);
        draw3DRaised(scrollBarX + 1, thumbY, 6, thumbH);
    }
    
    // Item counter
    sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
    sprite.setCursor(contentX + 4, contentY + contentH + 10);
    sprite.print(String(cocSettingsIndex + 1) + "/" + String(COC_SETTINGS_COUNT));
    
    // Instructions based on selection
    sprite.setCursor(contentX + 50, contentY + contentH + 2);
    sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
    if (cocSettingsIndex == 0) {
        sprite.print(",/. adjust");
    } else if (cocSettingsIndex == 1) {
        sprite.print("Enter toggle");
    } else if (cocSettingsIndex == 2 || cocSettingsIndex == 3 || cocSettingsIndex == 4) {
        sprite.print(",/. adjust");
    }
    
    // Status bar
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    sprite.print(";/.:Sel ,//:Adj Esc:Back");
    
    sprite.pushSprite(0, 0);
}

void handleCocSettingsInput() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    bool fnPressed = status.fn;
    
    // Fn+T - Open Terminal
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_T) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('t'))) {
        playSelectSound();
        currentMode = MODE_TERMINAL;
        needsRedraw = true;
        delay(150);
        return;
    }
    
    // Fn+P - Screenshot
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_P) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('p'))) {
        takeScreenshot();
        renderCocSettings();
        delay(150);
        return;
    }
    
    // Navigate up/down
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
        playClickSound();
        cocSettingsIndex = (cocSettingsIndex > 0) ? cocSettingsIndex - 1 : COC_SETTINGS_COUNT - 1;
        renderCocSettings();
        delay(100);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
        playClickSound();
        cocSettingsIndex = (cocSettingsIndex < COC_SETTINGS_COUNT - 1) ? cocSettingsIndex + 1 : 0;
        renderCocSettings();
        delay(100);
        return;
    }
    
    // Handle nickname edit mode typing
    if (nicknameEditMode && cocSettingsIndex == 4) {
        // Backspace
        if (status.del && chatNickname.length() > 0) {
            chatNickname.remove(chatNickname.length() - 1);
            playClickSound();
            renderCocSettings();
            return;
        }
        // Enter to finish editing
        if (status.enter) {
            nicknameEditMode = false;
            if (chatNickname.length() == 0) chatNickname = "User";  // Default if empty
            playSelectSound();
            renderCocSettings();
            delay(150);
            return;
        }
        // ESC to cancel edit
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            nicknameEditMode = false;
            playBackSound();
            renderCocSettings();
            delay(150);
            return;
        }
        // Type characters
        for (auto i : status.word) {
            if (chatNickname.length() < 15) {  // Max 15 chars
                chatNickname += i;
                M5Cardputer.Speaker.tone(1500, 15);
            }
        }
        renderCocSettings();
        return;
    }
    
    // Adjust values with left/right
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_LEFT)) {
        playClickSound();
        switch (cocSettingsIndex) {
            case 0:  // Brightness down (1-100 scale)
                configBrightness = max(10, configBrightness - 10);
                M5Cardputer.Display.setBrightness((configBrightness * 255) / 100);
                break;
            case 2:  // Screen timeout (in ms)
                {
                    int sec = screenOffTimeoutMs / 1000;
                    if (sec >= 300) screenOffTimeoutMs = 120000;
                    else if (sec >= 120) screenOffTimeoutMs = 60000;
                    else if (sec >= 60) screenOffTimeoutMs = 30000;
                    else if (sec >= 30) screenOffTimeoutMs = 0;
                }
                break;
            case 3:  // Timezone
                configTimezoneOffset = max(-12, configTimezoneOffset - 1);
                break;
            case 5:  // SID Duration down (30 sec steps, min 30 sec)
                configSidDuration = max(30, configSidDuration - 30);
                break;
        }
        renderCocSettings();
        delay(100);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_RIGHT)) {
        playClickSound();
        switch (cocSettingsIndex) {
            case 0:  // Brightness up (1-100 scale)
                configBrightness = min(100, configBrightness + 10);
                M5Cardputer.Display.setBrightness((configBrightness * 255) / 100);
                break;
            case 2:  // Screen timeout (in ms)
                {
                    int sec = screenOffTimeoutMs / 1000;
                    if (sec == 0) screenOffTimeoutMs = 30000;
                    else if (sec <= 30) screenOffTimeoutMs = 60000;
                    else if (sec <= 60) screenOffTimeoutMs = 120000;
                    else if (sec <= 120) screenOffTimeoutMs = 300000;
                }
                break;
            case 3:  // Timezone
                configTimezoneOffset = min(14, configTimezoneOffset + 1);
                break;
            case 5:  // SID Duration up (30 sec steps, max 10 min)
                configSidDuration = min(600, configSidDuration + 30);
                break;
        }
        renderCocSettings();
        delay(100);
        return;
    }
    
    // Enter to toggle/select
    if (status.enter) {
        switch (cocSettingsIndex) {
            case 1:  // Toggle sound
                settingSoundEnabled = !settingSoundEnabled;
                if (settingSoundEnabled) {
                    M5Cardputer.Speaker.tone(1000, 50);
                }
                renderCocSettings();
                break;
            case 4:  // Edit nickname
                nicknameEditMode = true;
                playSelectSound();
                renderCocSettings();
                break;
            case 6:  // Save & Exit
                playSelectSound();
                saveUserConfig();
                currentMode = MODE_COC_MENU;
                renderCocMenu();
                break;
        }
        delay(150);
        return;
    }
    
    // ESC to cancel
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        playBackSound();
        currentMode = MODE_COC_MENU;
        renderCocMenu();
        delay(150);
        return;
    }
}

// ==========================================
//     COC UI - Games Submenu
// ==========================================

void renderCocGames() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "Games");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 12;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    const char* games[] = {"Tetris", "Breakout", "Synth"};
    const char* descs[] = {"Classic puzzle game", "Brick breaker", "Grid sequencer"};
    
    // Draw selected game description at top center
    sprite.setTextColor(COL_GRAY_DARK, COL_WHITE);
    int descWidth = sprite.textWidth(descs[cocGameMenuIndex]);
    int descX = contentX + (contentW - descWidth) / 2;
    sprite.setCursor(descX, contentY + 4);
    sprite.print(descs[cocGameMenuIndex]);
    
    int itemH = 18;  // Smaller items since description moved to top
    int baseY = contentY + 20;  // Start items below description
    
#ifdef ENABLE_SMOOTH_UI
    // Calculate target position and draw animated selector
    int targetY = baseY + cocGameMenuIndex * itemH;
    int targetW = 30 + sprite.textWidth(games[cocGameMenuIndex]) + 8;
    gamesSelectorY = targetY;
    gamesSelectorW = targetW;
    int selectorY = (int)gamesSelectorY;
    int selectorW = (int)gamesSelectorW;
    
    // Draw animated selector
    sprite.fillRoundRect(contentX + 1, selectorY, selectorW, itemH - 2, 4, currentThemeColor);
#endif
    
    int y = baseY;
    for (int i = 0; i < 3; i++) {
        bool sel = (i == cocGameMenuIndex);
        
#ifdef ENABLE_SMOOTH_UI
        int selTop = selectorY;
        int selBot = selectorY + itemH - 2;
        bool inSelector = (y >= selTop - itemH && y <= selBot) && sel;
        
        if (inSelector) {
            sprite.setTextColor(COL_WHITE, currentThemeColor);
        } else {
            sprite.setTextColor(COL_BLACK, COL_WHITE);
        }
#else
        if (sel) {
            int selW = 30 + sprite.textWidth(games[i]) + 8;
            sprite.fillRoundRect(contentX + 1, y, selW, itemH - 2, 4, currentThemeColor);
            sprite.setTextColor(COL_WHITE, currentThemeColor);
        } else {
            sprite.setTextColor(COL_BLACK, COL_WHITE);
        }
#endif
        
        // Game icon
        int iconX = contentX + 6, iconY = y + 1;
        
        if (i == 0) {  // Tetris - S piece
            uint16_t col = sel ? COL_WHITE : COL_CYAN;
            sprite.fillRect(iconX, iconY + 4, 5, 5, col);
            sprite.fillRect(iconX + 5, iconY + 4, 5, 5, col);
            sprite.fillRect(iconX + 5, iconY, 5, 5, col);
            sprite.fillRect(iconX + 10, iconY, 5, 5, col);
        } else if (i == 1) {  // Breakout
            sprite.fillRect(iconX, iconY, 15, 3, sel ? COL_WHITE : COL_RED);
            sprite.fillRect(iconX, iconY + 4, 15, 3, sel ? COL_GRAY_LIGHT : COL_YELLOW);
            sprite.fillRect(iconX + 5, iconY + 10, 6, 2, sel ? COL_WHITE : COL_GRAY_MID);
            sprite.fillCircle(iconX + 8, iconY + 7, 2, sel ? COL_WHITE : COL_BLACK);
        } else {  // Synth
            for (int j = 0; j < 4; j++) {
                uint16_t col = sel ? COL_WHITE : synthColors[j + 1];
                sprite.fillRect(iconX + j * 4, iconY + (j % 2) * 2, 3, 12 - (j % 2) * 2, col);
            }
        }
        
        // Name only (no description here anymore)
        sprite.setCursor(contentX + 30, y + 3);
        sprite.print(games[i]);
        
        y += itemH;
    }
    
    // Status bar
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    sprite.print("Enter:Play  Esc:Back");
    
    sprite.pushSprite(0, 0);
}

void handleCocGamesInput() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    bool fnPressed = status.fn;
    
    // Fn+T - Open Terminal
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_T) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('t'))) {
        playSelectSound();
        currentMode = MODE_TERMINAL;
        needsRedraw = true;
        delay(150);
        return;
    }
    
    // Fn+P - Screenshot
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_P) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('p'))) {
        takeScreenshot();
        renderCocGames();
        delay(150);
        return;
    }
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
        playClickSound();
        cocGameMenuIndex = (cocGameMenuIndex > 0) ? cocGameMenuIndex - 1 : 2;
        renderCocGames();
        delay(100);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
        playClickSound();
        cocGameMenuIndex = (cocGameMenuIndex < 2) ? cocGameMenuIndex + 1 : 0;
        renderCocGames();
        delay(100);
        return;
    }
    
    if (status.enter) {
        playSelectSound();
        switch (cocGameMenuIndex) {
            case 0:  // Tetris
                tetrisInit();
                currentMode = MODE_GAME_TETRIS;
                break;
            case 1:  // Breakout
                breakoutInit();
                currentMode = MODE_GAME_BREAKOUT;
                break;
            case 2:  // Synth
                synthInit();
                currentMode = MODE_SYNTH;
                renderSynth();
                break;
        }
        delay(150);
        return;
    }
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        playBackSound();
        currentMode = MODE_COC_MENU;
        renderCocMenu();
        delay(150);
        return;
    }
}

// ==========================================
//     TETRIS GAME
// ==========================================

// Tetris pieces (4 rotations each)
const uint8_t tetrisPieces[7][4][4][4] = {
    // I
    {{{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
     {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
     {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}},
    // O
    {{{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
    // T
    {{{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}},
     {{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}},
    // S
    {{{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}},
     {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
     {{1,0,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}},
    // Z
    {{{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
     {{0,1,0,0},{1,1,0,0},{1,0,0,0},{0,0,0,0}}},
    // J
    {{{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}},
    // L
    {{{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}},
     {{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}}
};

const uint16_t tetrisColors[8] = {
    0x0000,  // Empty
    0x07FF,  // I - Cyan
    0xFFE0,  // O - Yellow
    0xF81F,  // T - Purple
    0x07E0,  // S - Green
    0xF800,  // Z - Red
    0x001F,  // J - Blue
    0xFD20   // L - Orange
};

void tetrisInit() {
    memset(tetrisBoard, 0, sizeof(tetrisBoard));
    tetrisScore = 0;
    tetrisLevel = 1;
    tetrisLines = 0;
    tetrisGameOver = false;
    tetrisLastDrop = millis();
    tetrisNextPiece = random(7);  // Initialize first next piece
    tetrisSpawnPiece();
}

void tetrisSpawnPiece() {
    tetrisPieceType = tetrisNextPiece;  // Use the "next" piece
    tetrisNextPiece = random(7);         // Generate new next piece
    tetrisPieceRot = 0;
    tetrisPieceX = TETRIS_WIDTH / 2 - 2;
    tetrisPieceY = 0;
    
    if (!tetrisCanMove(tetrisPieceX, tetrisPieceY, tetrisPieceRot)) {
        tetrisGameOver = true;
    }
}

bool tetrisCanMove(int x, int y, int rot) {
    for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
            if (tetrisPieces[tetrisPieceType][rot][py][px]) {
                int nx = x + px;
                int ny = y + py;
                if (nx < 0 || nx >= TETRIS_WIDTH || ny >= TETRIS_HEIGHT) return false;
                if (ny >= 0 && tetrisBoard[ny][nx]) return false;
            }
        }
    }
    return true;
}

void tetrisLockPiece() {
    for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
            if (tetrisPieces[tetrisPieceType][tetrisPieceRot][py][px]) {
                int nx = tetrisPieceX + px;
                int ny = tetrisPieceY + py;
                if (ny >= 0 && ny < TETRIS_HEIGHT && nx >= 0 && nx < TETRIS_WIDTH) {
                    tetrisBoard[ny][nx] = tetrisPieceType + 1;
                }
            }
        }
    }
    
    // Check for completed lines
    int linesCleared = 0;
    for (int y = TETRIS_HEIGHT - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < TETRIS_WIDTH; x++) {
            if (!tetrisBoard[y][x]) { full = false; break; }
        }
        if (full) {
            linesCleared++;
            // Move everything down
            for (int yy = y; yy > 0; yy--) {
                for (int x = 0; x < TETRIS_WIDTH; x++) {
                    tetrisBoard[yy][x] = tetrisBoard[yy - 1][x];
                }
            }
            for (int x = 0; x < TETRIS_WIDTH; x++) tetrisBoard[0][x] = 0;
            y++;  // Check this row again
        }
    }
    
    if (linesCleared > 0) {
        tetrisLines += linesCleared;
        int points[] = {0, 100, 300, 500, 800};
        tetrisScore += points[linesCleared] * tetrisLevel;
        tetrisLevel = 1 + tetrisLines / 10;
        M5Cardputer.Speaker.tone(800 + linesCleared * 200, 100);
    }
    
    tetrisSpawnPiece();
}

void renderTetris() {
    int W = M5Cardputer.Display.width();
    int H = M5Cardputer.Display.height();
    
    sprite.fillSprite(0x0000);
    
    int cellSize = 6;
    int boardX = (W - TETRIS_WIDTH * cellSize) / 2 - 30;
    int boardY = (H - TETRIS_HEIGHT * cellSize) / 2;
    
    // Draw board border
    sprite.drawRect(boardX - 1, boardY - 1, TETRIS_WIDTH * cellSize + 2, TETRIS_HEIGHT * cellSize + 2, COL_WHITE);
    
    // Draw board
    for (int y = 0; y < TETRIS_HEIGHT; y++) {
        for (int x = 0; x < TETRIS_WIDTH; x++) {
            uint16_t col = tetrisColors[tetrisBoard[y][x]];
            if (col != 0) {
                sprite.fillRect(boardX + x * cellSize, boardY + y * cellSize, cellSize - 1, cellSize - 1, col);
            }
        }
    }
    
    // Draw current piece
    for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
            if (tetrisPieces[tetrisPieceType][tetrisPieceRot][py][px]) {
                int dx = boardX + (tetrisPieceX + px) * cellSize;
                int dy = boardY + (tetrisPieceY + py) * cellSize;
                if (dy >= boardY) {
                    sprite.fillRect(dx, dy, cellSize - 1, cellSize - 1, tetrisColors[tetrisPieceType + 1]);
                }
            }
        }
    }
    
    // Info panel
    int infoX = boardX + TETRIS_WIDTH * cellSize + 10;
    
    sprite.setTextColor(COL_WHITE, 0x0000);
    sprite.setCursor(infoX, 5);
    sprite.print("TETRIS");
    
    sprite.setTextColor(COL_YELLOW, 0x0000);
    sprite.setCursor(infoX, 20);
    sprite.print("Score");
    sprite.setCursor(infoX, 30);
    sprite.print(tetrisScore);
    
    sprite.setCursor(infoX, 45);
    sprite.print("Level");
    sprite.setCursor(infoX, 55);
    sprite.print(tetrisLevel);
    
    sprite.setCursor(infoX, 70);
    sprite.print("Lines");
    sprite.setCursor(infoX, 80);
    sprite.print(tetrisLines);
    
    // Next piece preview
    sprite.setTextColor(COL_WHITE, 0x0000);
    sprite.setCursor(infoX, 95);
    sprite.print("Next:");
    
    // Draw next piece preview box with border
    int previewX = infoX + 2;
    int previewY = 105;
    int previewCellSize = 5;
    
    // Draw preview box border
    sprite.drawRect(previewX - 2, previewY - 2, 24, 24, COL_GRAY_DARK);
    
    // Draw the next piece
    for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
            if (tetrisPieces[tetrisNextPiece][0][py][px]) {
                int dx = previewX + px * previewCellSize;
                int dy = previewY + py * previewCellSize;
                sprite.fillRect(dx, dy, previewCellSize - 1, previewCellSize - 1, tetrisColors[tetrisNextPiece + 1]);
            }
        }
    }
    
    // Controls hint
    sprite.setTextColor(COL_GRAY_DARK, 0x0000);
    sprite.setCursor(5, H - 10);
    sprite.print(",/:Move ;:Rot Sp:Drop");
    
    // Game over overlay
    if (tetrisGameOver) {
        sprite.fillRect(W/2 - 50, H/2 - 20, 100, 40, 0x0000);
        sprite.drawRect(W/2 - 50, H/2 - 20, 100, 40, COL_RED);
        sprite.setTextColor(COL_RED, 0x0000);
        sprite.setCursor(W/2 - 40, H/2 - 10);
        sprite.print("GAME OVER");
        sprite.setTextColor(COL_WHITE, 0x0000);
        sprite.setCursor(W/2 - 45, H/2 + 5);
        sprite.print("Enter:Retry");
    }
    
    sprite.pushSprite(0, 0);
}

void handleTetrisInput() {
    unsigned long dropInterval = 1000 - (tetrisLevel - 1) * 80;
    if (dropInterval < 100) dropInterval = 100;
    
    if (!tetrisGameOver && millis() - tetrisLastDrop > dropInterval) {
        tetrisLastDrop = millis();
        if (tetrisCanMove(tetrisPieceX, tetrisPieceY + 1, tetrisPieceRot)) {
            tetrisPieceY++;
        } else {
            tetrisLockPiece();
        }
        renderTetris();
    }
    
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        currentMode = MODE_COC_GAMES;
        renderCocGames();
        delay(150);
        return;
    }
    
    if (tetrisGameOver) {
        if (status.enter) {
            tetrisInit();
            renderTetris();
        }
        return;
    }
    
    // Move left
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_LEFT)) {
        if (tetrisCanMove(tetrisPieceX - 1, tetrisPieceY, tetrisPieceRot)) {
            tetrisPieceX--;
            renderTetris();
        }
        delay(80);
        return;
    }
    
    // Move right
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_RIGHT)) {
        if (tetrisCanMove(tetrisPieceX + 1, tetrisPieceY, tetrisPieceRot)) {
            tetrisPieceX++;
            renderTetris();
        }
        delay(80);
        return;
    }
    
    // Soft drop
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
        if (tetrisCanMove(tetrisPieceX, tetrisPieceY + 1, tetrisPieceRot)) {
            tetrisPieceY++;
            tetrisScore += 1;
            renderTetris();
        }
        delay(50);
        return;
    }
    
    // Rotate
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
        int newRot = (tetrisPieceRot + 1) % 4;
        if (tetrisCanMove(tetrisPieceX, tetrisPieceY, newRot)) {
            tetrisPieceRot = newRot;
            renderTetris();
        }
        delay(100);
        return;
    }
    
    // Hard drop
    if (M5Cardputer.Keyboard.isKeyPressed(' ')) {
        while (tetrisCanMove(tetrisPieceX, tetrisPieceY + 1, tetrisPieceRot)) {
            tetrisPieceY++;
            tetrisScore += 2;
        }
        tetrisLockPiece();
        renderTetris();
        delay(100);
        return;
    }
}

// ==========================================
//     BREAKOUT GAME
// ==========================================

void breakoutInit() {
    // Initialize bricks
    for (int r = 0; r < BREAKOUT_ROWS; r++) {
        for (int c = 0; c < BREAKOUT_COLS; c++) {
            breakoutBricks[r][c] = true;
        }
    }
    
    breakoutPaddleX = 120;
    breakoutBallX = 120;
    breakoutBallY = 100;
    breakoutBallVX = 2;
    breakoutBallVY = -2;
    breakoutScore = 0;
    breakoutLives = 3;
    breakoutGameOver = false;
    breakoutBallLaunched = false;
}

void renderBreakout() {
    int W = M5Cardputer.Display.width();
    int H = M5Cardputer.Display.height();
    
    sprite.fillSprite(0x0000);
    
    // Draw bricks
    int brickW = 22;
    int brickH = 8;
    int brickStartX = 10;
    int brickStartY = 20;
    uint16_t brickColors[] = {COL_RED, 0xFD20, COL_YELLOW, COL_GREEN, COL_CYAN};
    
    for (int r = 0; r < BREAKOUT_ROWS; r++) {
        for (int c = 0; c < BREAKOUT_COLS; c++) {
            if (breakoutBricks[r][c]) {
                int x = brickStartX + c * (brickW + 2);
                int y = brickStartY + r * (brickH + 2);
                sprite.fillRect(x, y, brickW, brickH, brickColors[r]);
                sprite.drawRect(x, y, brickW, brickH, COL_WHITE);
            }
        }
    }
    
    // Draw paddle
    int paddleW = 30;
    int paddleH = 5;
    int paddleY = H - 20;
    sprite.fillRect((int)breakoutPaddleX - paddleW/2, paddleY, paddleW, paddleH, COL_WHITE);
    
    // Draw ball
    sprite.fillCircle((int)breakoutBallX, (int)breakoutBallY, 3, COL_WHITE);
    
    // Draw info
    sprite.setTextColor(COL_WHITE, 0x0000);
    sprite.setCursor(5, 5);
    sprite.print("Score:" + String(breakoutScore));
    
    sprite.setCursor(W - 50, 5);
    sprite.print("Lives:" + String(breakoutLives));
    
    // Launch hint
    if (!breakoutBallLaunched) {
        sprite.setTextColor(COL_YELLOW, 0x0000);
        sprite.setCursor(W/2 - 45, H/2);
        sprite.print("Space to launch");
    }
    
    // Game over
    if (breakoutGameOver) {
        sprite.fillRect(W/2 - 50, H/2 - 20, 100, 40, 0x0000);
        sprite.drawRect(W/2 - 50, H/2 - 20, 100, 40, COL_RED);
        
        bool won = true;
        for (int r = 0; r < BREAKOUT_ROWS; r++) {
            for (int c = 0; c < BREAKOUT_COLS; c++) {
                if (breakoutBricks[r][c]) won = false;
            }
        }
        
        sprite.setTextColor(won ? COL_GREEN : COL_RED, 0x0000);
        sprite.setCursor(W/2 - 30, H/2 - 10);
        sprite.print(won ? "YOU WIN!" : "GAME OVER");
        sprite.setTextColor(COL_WHITE, 0x0000);
        sprite.setCursor(W/2 - 45, H/2 + 5);
        sprite.print("Enter:Retry");
    }
    
    sprite.pushSprite(0, 0);
}

void breakoutUpdate() {
    if (!breakoutBallLaunched || breakoutGameOver) return;
    
    int W = M5Cardputer.Display.width();
    int H = M5Cardputer.Display.height();
    
    // Move ball
    breakoutBallX += breakoutBallVX;
    breakoutBallY += breakoutBallVY;
    
    // Wall collisions
    if (breakoutBallX <= 3 || breakoutBallX >= W - 3) {
        breakoutBallVX = -breakoutBallVX;
        breakoutBallX = constrain(breakoutBallX, 3, W - 3);
    }
    if (breakoutBallY <= 3) {
        breakoutBallVY = -breakoutBallVY;
        breakoutBallY = 3;
    }
    
    // Paddle collision
    int paddleW = 30;
    int paddleY = H - 20;
    if (breakoutBallY >= paddleY - 3 && breakoutBallY <= paddleY + 5 &&
        breakoutBallX >= breakoutPaddleX - paddleW/2 - 3 &&
        breakoutBallX <= breakoutPaddleX + paddleW/2 + 3) {
        breakoutBallVY = -abs(breakoutBallVY);
        // Angle based on where ball hits paddle
        float hitPos = (breakoutBallX - breakoutPaddleX) / (paddleW / 2);
        breakoutBallVX = hitPos * 3;
        M5Cardputer.Speaker.tone(600, 30);
    }
    
    // Brick collisions
    int brickW = 22;
    int brickH = 8;
    int brickStartX = 10;
    int brickStartY = 20;
    
    for (int r = 0; r < BREAKOUT_ROWS; r++) {
        for (int c = 0; c < BREAKOUT_COLS; c++) {
            if (breakoutBricks[r][c]) {
                int bx = brickStartX + c * (brickW + 2);
                int by = brickStartY + r * (brickH + 2);
                
                if (breakoutBallX >= bx - 3 && breakoutBallX <= bx + brickW + 3 &&
                    breakoutBallY >= by - 3 && breakoutBallY <= by + brickH + 3) {
                    breakoutBricks[r][c] = false;
                    breakoutBallVY = -breakoutBallVY;
                    breakoutScore += (BREAKOUT_ROWS - r) * 10;
                    M5Cardputer.Speaker.tone(800 + r * 100, 50);
                    
                    // Check win
                    bool won = true;
                    for (int rr = 0; rr < BREAKOUT_ROWS; rr++) {
                        for (int cc = 0; cc < BREAKOUT_COLS; cc++) {
                            if (breakoutBricks[rr][cc]) won = false;
                        }
                    }
                    if (won) breakoutGameOver = true;
                    return;
                }
            }
        }
    }
    
    // Ball lost
    if (breakoutBallY > H) {
        breakoutLives--;
        if (breakoutLives <= 0) {
            breakoutGameOver = true;
        } else {
            breakoutBallX = breakoutPaddleX;
            breakoutBallY = 100;
            breakoutBallVX = 2;
            breakoutBallVY = -2;
            breakoutBallLaunched = false;
        }
        M5Cardputer.Speaker.tone(200, 200);
    }
}

void handleBreakoutInput() {
    breakoutUpdate();
    
    static unsigned long lastRender = 0;
    if (millis() - lastRender > 16) {  // ~60fps
        lastRender = millis();
        renderBreakout();
    }
    
    if (!M5Cardputer.Keyboard.isChange() && !M5Cardputer.Keyboard.isPressed()) {
        return;
    }
    
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
        currentMode = MODE_COC_GAMES;
        renderCocGames();
        delay(150);
        return;
    }
    
    if (breakoutGameOver) {
        if (status.enter) {
            breakoutInit();
            renderBreakout();
        }
        return;
    }
    
    // Move paddle
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_LEFT)) {
        breakoutPaddleX -= 8;
        if (breakoutPaddleX < 20) breakoutPaddleX = 20;
        if (!breakoutBallLaunched) breakoutBallX = breakoutPaddleX;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_RIGHT)) {
        breakoutPaddleX += 8;
        if (breakoutPaddleX > 220) breakoutPaddleX = 220;
        if (!breakoutBallLaunched) breakoutBallX = breakoutPaddleX;
    }
    
    // Launch ball
    if (M5Cardputer.Keyboard.isKeyPressed(' ') && !breakoutBallLaunched) {
        breakoutBallLaunched = true;
        breakoutBallVX = (random(2) == 0) ? 2 : -2;
        breakoutBallVY = -3;
    }
}

void checkScreenTimeout() {
    // Always update traffic data
    updateTrafficData();
    
    // Skip timeout only for active/critical modes (games, synth, keyboard, serial)
    if (currentMode == MODE_SERIAL_MONITOR ||
        currentMode == MODE_BT_KEYBOARD ||
        currentMode == MODE_SYNTH ||
        currentMode == MODE_GAME_TETRIS ||
        currentMode == MODE_GAME_BREAKOUT) {
        return;
    }
    
    unsigned long elapsed = millis() - lastActivityTime;
    
    // Check screen off timeout first (longer timeout)
    if (screenOffTimeoutMs > 0 && !screenOff) {
        if (elapsed > screenOffTimeoutMs) {
            screenOff = true;
            terminalHidden = true;
            M5Cardputer.Display.sleep();
            return;
        }
    }
    
    // Check terminal hide timeout (shorter timeout) - only when sd-share is active
    if (terminalHideTimeoutMs > 0 && !terminalHidden && !screenOff && sdShareEnabled) {
        if (elapsed > terminalHideTimeoutMs) {
            terminalHidden = true;
            renderTrafficPlots();
        }
    }
    
    // If terminal is hidden but screen is on, keep updating plots (only when sd-share active)
    if (terminalHidden && !screenOff && sdShareEnabled) {
        // Update plot periodically (every 500ms)
        static unsigned long lastPlotUpdate = 0;
        if (millis() - lastPlotUpdate > 500) {
            lastPlotUpdate = millis();
            renderTrafficPlots();
        }
    }
    
    // If sd-share was turned off while terminal was hidden, unhide it
    if (terminalHidden && !sdShareEnabled) {
        terminalHidden = false;
        needsRedraw = true;
    }
}

// ==========================================
//     Web Server Handlers
// ==========================================

String getContentType(String filename) {
    if (filename.endsWith(".html") || filename.endsWith(".htm")) return "text/html";
    if (filename.endsWith(".css")) return "text/css";
    if (filename.endsWith(".js")) return "application/javascript";
    if (filename.endsWith(".json")) return "application/json";
    if (filename.endsWith(".png")) return "image/png";
    if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) return "image/jpeg";
    if (filename.endsWith(".gif")) return "image/gif";
    if (filename.endsWith(".ico")) return "image/x-icon";
    if (filename.endsWith(".txt")) return "text/plain";
    if (filename.endsWith(".pdf")) return "application/pdf";
    if (filename.endsWith(".zip")) return "application/zip";
    if (filename.endsWith(".mp3")) return "audio/mpeg";
    return "application/octet-stream";
}

String formatFileSize(size_t bytes) {
    if (bytes < 1024) return String(bytes) + " B";
    if (bytes < 1024 * 1024) return String(bytes / 1024) + " KB";
    return String(bytes / (1024 * 1024)) + " MB";
}

String getFileIcon(bool isDir, String filename) {
    if (isDir) {
        // Folder icon using CSS
        return "<span class='icon folder'></span>";
    }
    
    filename.toLowerCase();
    if (filename.endsWith(".jpg") || filename.endsWith(".jpeg") || 
        filename.endsWith(".png") || filename.endsWith(".gif") || filename.endsWith(".bmp")) {
        return "<span class='icon image'></span>";
    }
    if (filename.endsWith(".mp3") || filename.endsWith(".wav") || filename.endsWith(".flac")) {
        return "<span class='icon music'></span>";
    }
    if (filename.endsWith(".txt") || filename.endsWith(".md") || filename.endsWith(".ini") ||
        filename.endsWith(".cfg") || filename.endsWith(".log")) {
        return "<span class='icon text'></span>";
    }
    if (filename.endsWith(".cpp") || filename.endsWith(".c") || filename.endsWith(".h") ||
        filename.endsWith(".py") || filename.endsWith(".js") || filename.endsWith(".html") ||
        filename.endsWith(".css") || filename.endsWith(".json")) {
        return "<span class='icon code'></span>";
    }
    return "<span class='icon file'></span>";
}

String generateDirectoryHTML(String path) {
    // Get SD card size info
    uint64_t totalBytes = SD.totalBytes();
    uint64_t usedBytes = SD.usedBytes();
    uint64_t freeBytes = totalBytes - usedBytes;
    int usagePercent = (totalBytes > 0) ? (int)((usedBytes * 100) / totalBytes) : 0;
    
    // Format sizes for display
    String totalStr, usedStr, freeStr;
    if (totalBytes >= 1073741824) {  // >= 1GB
        totalStr = String((float)totalBytes / 1073741824.0, 1) + " GB";
        usedStr = String((float)usedBytes / 1073741824.0, 2) + " GB";
        freeStr = String((float)freeBytes / 1073741824.0, 2) + " GB";
    } else {
        totalStr = String(totalBytes / 1048576) + " MB";
        usedStr = String(usedBytes / 1048576) + " MB";
        freeStr = String(freeBytes / 1048576) + " MB";
    }
    
    // Determine bar color class based on usage
    String barClass = "green";
    if (usagePercent >= 90) barClass = "red";
    else if (usagePercent >= 75) barClass = "orange";
    else if (usagePercent >= 50) barClass = "yellow";
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>SD Card - " + path + "</title>";
    html += "<style>";
    // Base styles
    html += "*{box-sizing:border-box;margin:0;padding:0;}";
    html += "body{font-family:'Courier New',monospace;font-size:14px;background:#FFE000;padding:15px;min-height:100vh;}";
    
    // Window container
    html += ".window{background:#C0C0C0;border:3px solid;border-color:#FFFFFF #000000 #000000 #FFFFFF;max-width:650px;margin:0 auto;box-shadow:4px 4px 0 #808080;}";
    html += ".titlebar{background:linear-gradient(90deg,#000080,#1084D0);color:#FFF;padding:4px 8px;font-weight:bold;display:flex;justify-content:space-between;align-items:center;}";
    html += ".titlebar-text{display:flex;align-items:center;gap:8px;}";
    html += ".btn-close{width:16px;height:14px;background:#C0C0C0;border:2px solid;border-color:#FFF #000 #000 #FFF;font-size:10px;line-height:10px;text-align:center;cursor:pointer;}";
    
    // Content area
    html += ".content{padding:8px;}";
    html += ".path-bar{background:#FFF;border:2px solid;border-color:#808080 #FFF #FFF #808080;padding:6px 10px;margin-bottom:10px;font-weight:bold;}";
    
    // Upload section
    html += ".upload-section{background:#C0C0C0;border:2px solid;border-color:#808080 #FFF #FFF #808080;padding:10px;margin-bottom:10px;}";
    html += ".upload-section label{display:block;margin-bottom:8px;font-weight:bold;}";
    html += ".btn{background:#C0C0C0;border:2px solid;border-color:#FFF #000 #000 #FFF;padding:4px 16px;font-family:'Courier New',monospace;font-size:12px;cursor:pointer;}";
    html += ".btn:active{border-color:#000 #FFF #FFF #000;}";
    html += ".btn:disabled{color:#808080;cursor:not-allowed;}";
    html += "input[type=file]{margin-bottom:10px;display:block;width:100%;}";
    
    // Progress bars
    html += ".progress-container{margin-top:10px;display:none;}";
    html += ".progress-item{margin-bottom:8px;padding:5px;background:#FFF;border:1px solid #808080;}";
    html += ".progress-filename{font-size:12px;margin-bottom:3px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}";
    html += ".progress-bar-bg{width:100%;height:16px;background:#FFF;border:2px solid;border-color:#808080 #FFF #FFF #808080;}";
    html += ".progress-bar{height:100%;background:#000080;width:0%;transition:width 0.2s;position:relative;}";
    html += ".progress-bar-text{position:absolute;width:100%;text-align:center;color:#FFF;font-size:10px;line-height:12px;left:0;}";
    html += ".progress-complete{background:#008000;}";
    html += ".progress-error{background:#800000;}";
    html += ".upload-status{margin-top:5px;font-size:11px;color:#008000;}";
    html += ".upload-status.error{color:#800000;}";
    
    // File list
    html += ".file-list{background:#FFF;border:2px solid;border-color:#808080 #FFF #FFF #808080;}";
    html += ".file-header{display:grid;grid-template-columns:24px 1fr 55px 140px;padding:6px 10px;background:#C0C0C0;border-bottom:1px solid #808080;font-weight:bold;}";
    html += ".file-row{display:grid;grid-template-columns:24px 1fr 55px 140px;padding:5px 10px;border-bottom:1px solid #E0E0E0;align-items:center;}";
    html += ".file-row:hover{background:#E8E8E8;}";
    html += ".file-row.selected{background:#000080;color:#FFF;}";
    html += ".file-name{display:flex;align-items:center;gap:8px;min-width:0;}";
    html += ".file-name a{color:#000;text-decoration:none;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;flex:1;min-width:0;}";
    html += ".file-row:hover .file-name a{text-decoration:underline;}";
    html += ".file-size{text-align:right;color:#666;font-size:11px;}";
    html += ".file-actions{display:flex;gap:2px;justify-content:flex-end;flex-wrap:nowrap;}";
    html += ".file-actions a,.file-actions button{color:#000080;text-decoration:none;font-size:10px;padding:1px 3px;border:1px solid #808080;background:#E0E0E0;cursor:pointer;font-family:'Courier New',monospace;white-space:nowrap;}";
    html += ".file-actions a:hover,.file-actions button:hover{background:#C0C0C0;}";
    html += ".file-actions .edit-btn{background:#90EE90;border-color:#228B22;color:#006400;}";
    html += ".file-actions .edit-btn:hover{background:#7CFC00;}";
    html += ".file-checkbox{width:16px;height:16px;cursor:pointer;}";
    
    // Batch action bar
    html += ".batch-bar{display:none;padding:8px;background:#000080;color:#FFF;margin-bottom:10px;border:2px solid;border-color:#FFF #000 #000 #FFF;align-items:center;gap:10px;}";
    html += ".batch-bar.show{display:flex;}";
    html += ".batch-bar span{margin-right:auto;}";
    html += ".batch-btn{background:#C0C0C0;color:#000;border:2px solid;border-color:#FFF #000 #000 #FFF;padding:4px 12px;font-family:'Courier New',monospace;font-size:12px;cursor:pointer;}";
    html += ".batch-btn:hover{background:#E0E0E0;}";
    html += ".batch-btn.danger{background:#FF6B6B;}";
    
    // Modal styles
    html += ".modal{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.5);z-index:100;justify-content:center;align-items:center;}";
    html += ".modal.show{display:flex;}";
    html += ".modal-content{background:#C0C0C0;border:3px solid;border-color:#FFF #000 #000 #FFF;padding:0;min-width:280px;max-width:90%;}";
    html += ".modal-title{background:linear-gradient(90deg,#000080,#1084D0);color:#FFF;padding:4px 8px;font-weight:bold;}";
    html += ".modal-body{padding:15px;}";
    html += ".modal-body label{display:block;margin-bottom:5px;}";
    html += ".modal-body input[type=text],.modal-body select{width:100%;padding:5px;margin-bottom:10px;border:2px solid;border-color:#808080 #FFF #FFF #808080;font-family:'Courier New',monospace;}";
    html += ".modal-buttons{display:flex;gap:10px;justify-content:flex-end;margin-top:10px;}";
    
    // Toolbar
    html += ".toolbar{display:flex;gap:8px;margin-bottom:10px;padding:8px;background:#C0C0C0;border:2px solid;border-color:#808080 #FFF #FFF #808080;}";
    
    // CSS Icons
    html += ".icon{display:inline-block;width:16px;height:16px;flex-shrink:0;position:relative;margin-top:4px;}";
    
    // Folder icon
    html += ".icon.folder{background:#FFD700;border:1px solid #B8860B;border-radius:1px;}";
    html += ".icon.folder:before{content:'';position:absolute;top:-4px;left:0;width:7px;height:4px;background:#FFD700;border:1px solid #B8860B;border-bottom:none;border-radius:1px 1px 0 0;}";
    
    // File icon
    html += ".icon.file{background:#FFF;border:1px solid #808080;}";
    html += ".icon.file:before{content:'';position:absolute;top:0;right:0;border:3px solid;border-color:#808080 #FFF #FFF #808080;}";
    
    // Text icon
    html += ".icon.text{background:#FFF;border:1px solid #808080;}";
    html += ".icon.text:after{content:'';position:absolute;top:3px;left:2px;width:10px;height:1px;background:#000;box-shadow:0 3px 0 #000,0 6px 0 #000;}";
    
    // Image icon
    html += ".icon.image{background:#87CEEB;border:1px solid #4682B4;overflow:hidden;}";
    html += ".icon.image:before{content:'';position:absolute;bottom:2px;left:1px;width:0;height:0;border-left:5px solid transparent;border-right:5px solid transparent;border-bottom:6px solid #228B22;}";
    html += ".icon.image:after{content:'';position:absolute;top:2px;right:2px;width:4px;height:4px;background:#FFD700;border-radius:50%;}";
    
    // Music icon
    html += ".icon.music{background:#FFF;border:1px solid #808080;}";
    html += ".icon.music:before{content:'';position:absolute;bottom:2px;left:3px;width:5px;height:5px;background:#000;border-radius:50%;}";
    html += ".icon.music:after{content:'';position:absolute;bottom:5px;left:7px;width:2px;height:8px;background:#000;}";
    
    // Code icon
    html += ".icon.code{background:#FFF;border:1px solid #808080;}";
    html += ".icon.code:before{content:'<>';position:absolute;top:2px;left:2px;font-size:8px;font-weight:bold;color:#000080;}";
    
    // Back icon
    html += ".icon.back{background:#C0C0C0;border:1px solid #808080;}";
    html += ".icon.back:before{content:'';position:absolute;top:5px;left:3px;width:0;height:0;border:4px solid transparent;border-right:6px solid #000;}";
    
    // Status bar
    html += ".statusbar{background:#C0C0C0;border-top:2px solid #808080;padding:4px 10px;font-size:12px;margin-top:2px;}";
    
    // Disk usage bar
    html += ".disk-usage{background:#C0C0C0;border:2px solid;border-color:#808080 #FFF #FFF #808080;padding:8px 10px;margin-top:8px;}";
    html += ".disk-label{display:flex;justify-content:space-between;margin-bottom:4px;font-size:12px;}";
    html += ".disk-bar-bg{width:100%;height:18px;background:#FFF;border:2px solid;border-color:#808080 #FFF #FFF #808080;}";
    html += ".disk-bar{height:100%;transition:width 0.3s;}";
    html += ".disk-bar.green{background:linear-gradient(to right,#00AA00,#00DD00);}";
    html += ".disk-bar.yellow{background:linear-gradient(to right,#00AA00,#DDDD00);}";
    html += ".disk-bar.orange{background:linear-gradient(to right,#DDDD00,#FF8800);}";
    html += ".disk-bar.red{background:linear-gradient(to right,#FF8800,#DD0000);}";
    
    // Footer
    html += ".footer{text-align:center;padding:10px;color:#808080;font-size:11px;}";
    
    // Empty message
    html += ".empty-msg{padding:30px;text-align:center;color:#808080;font-style:italic;}";
    
    html += "</style></head><body>";
    
    // JavaScript for multi-file upload with progress
    html += "<script>";
    html += "var uploadPath='" + path + "';";
    html += "var selectedItems=[];";
    
    // Upload function
    html += "var uploadQueue=[];";
    html += "var currentUpload=0;";
    html += "function uploadFiles(){";
    html += "  var input=document.getElementById('fileInput');";
    html += "  var files=input.files;";
    html += "  if(files.length===0){alert('Please select files first');return;}";
    html += "  var container=document.getElementById('progressContainer');";
    html += "  var uploadBtn=document.getElementById('uploadBtn');";
    html += "  container.style.display='block';";
    html += "  container.innerHTML='';";
    html += "  uploadBtn.disabled=true;";
    html += "  uploadQueue=[];";
    html += "  currentUpload=0;";
    // Create progress items for all files first
    html += "  for(var i=0;i<files.length;i++){";
    html += "    uploadQueue.push(files[i]);";
    html += "    var item=document.createElement('div');";
    html += "    item.className='progress-item';";
    html += "    item.innerHTML='<div class=\"progress-filename\">'+files[i].name+'</div>';";
    html += "    item.innerHTML+='<div class=\"progress-bar-bg\"><div class=\"progress-bar\" id=\"bar'+i+'\"><span class=\"progress-bar-text\">Waiting</span></div></div>';";
    html += "    item.innerHTML+='<div class=\"upload-status\" id=\"status'+i+'\"></div>';";
    html += "    container.appendChild(item);";
    html += "  }";
    html += "  uploadNextFile();";
    html += "}";
    // Sequential upload function
    html += "function uploadNextFile(){";
    html += "  if(currentUpload>=uploadQueue.length){";
    html += "    document.getElementById('uploadBtn').disabled=false;";
    html += "    setTimeout(function(){location.reload();},1500);";
    html += "    return;";
    html += "  }";
    html += "  var file=uploadQueue[currentUpload];";
    html += "  var index=currentUpload;";
    html += "  var bar=document.getElementById('bar'+index);";
    html += "  var status=document.getElementById('status'+index);";
    html += "  bar.querySelector('.progress-bar-text').textContent='0%';";
    html += "  status.textContent='Uploading...';";
    html += "  var formData=new FormData();";
    html += "  formData.append('path',uploadPath);";
    html += "  formData.append('file',file);";
    html += "  var xhr=new XMLHttpRequest();";
    html += "  xhr.upload.addEventListener('progress',function(e){";
    html += "    if(e.lengthComputable){";
    html += "      var pct=Math.round((e.loaded/e.total)*100);";
    html += "      bar.style.width=pct+'%';";
    html += "      bar.querySelector('.progress-bar-text').textContent=pct+'%';";
    html += "    }";
    html += "  });";
    html += "  xhr.addEventListener('load',function(){";
    html += "    if(xhr.status===200||xhr.status===303){";
    html += "      bar.classList.add('progress-complete');";
    html += "      bar.style.width='100%';";
    html += "      bar.querySelector('.progress-bar-text').textContent='Done';";
    html += "      status.textContent='Uploaded successfully';";
    html += "    }else{";
    html += "      bar.classList.add('progress-error');";
    html += "      status.textContent='Error uploading';";
    html += "      status.classList.add('error');";
    html += "    }";
    html += "    currentUpload++;";
    html += "    setTimeout(uploadNextFile,300);";  // Small delay between files
    html += "  });";
    html += "  xhr.addEventListener('error',function(){";
    html += "    bar.classList.add('progress-error');";
    html += "    status.textContent='Network error';";
    html += "    status.classList.add('error');";
    html += "    currentUpload++;";
    html += "    setTimeout(uploadNextFile,500);";  // Retry delay on error
    html += "  });";
    html += "  xhr.open('POST','/upload',true);";
    html += "  xhr.send(formData);";
    html += "}";
    
    // Modal functions
    html += "function showModal(id){document.getElementById(id).classList.add('show');}";
    html += "function hideModal(id){document.getElementById(id).classList.remove('show');}";
    
    // New folder
    html += "function createFolder(){";
    html += "  var name=document.getElementById('newFolderName').value.trim();";
    html += "  if(name){window.location='/mkdir?path='+encodeURIComponent(uploadPath)+'&name='+encodeURIComponent(name);}";
    html += "}";
    
    // Rename
    html += "var renamePath='';";
    html += "function showRename(path,name){";
    html += "  renamePath=path;";
    html += "  document.getElementById('renameInput').value=name;";
    html += "  showModal('renameModal');";
    html += "}";
    html += "function doRename(){";
    html += "  var newName=document.getElementById('renameInput').value.trim();";
    html += "  if(newName){window.location='/rename?path='+encodeURIComponent(renamePath)+'&newname='+encodeURIComponent(newName);}";
    html += "}";
    
    // Move
    html += "var movePath='';";
    html += "function showMove(path){";
    html += "  movePath=path;";
    html += "  var sel=document.getElementById('moveDestination');";
    html += "  sel.innerHTML='<option>Loading...</option>';";
    html += "  fetch('/listdirs').then(r=>r.text()).then(html=>{sel.innerHTML=html;});";
    html += "  showModal('moveModal');";
    html += "}";
    html += "function doMove(){";
    html += "  var dest=document.getElementById('moveDestination').value;";
    html += "  if(dest){window.location='/move?path='+encodeURIComponent(movePath)+'&dest='+encodeURIComponent(dest);}";
    html += "}";
    
    // Batch selection functions
    html += "function toggleSelect(cb,path){";
    html += "  if(cb.checked){";
    html += "    if(selectedItems.indexOf(path)===-1)selectedItems.push(path);";
    html += "  }else{";
    html += "    selectedItems=selectedItems.filter(function(p){return p!==path;});";
    html += "  }";
    html += "  updateBatchBar();";
    html += "}";
    
    html += "function toggleSelectAll(cb){";
    html += "  var boxes=document.querySelectorAll('.file-checkbox');";
    html += "  selectedItems=[];";
    html += "  boxes.forEach(function(b){";
    html += "    b.checked=cb.checked;";
    html += "    if(cb.checked&&b.dataset.path)selectedItems.push(b.dataset.path);";
    html += "  });";
    html += "  updateBatchBar();";
    html += "}";
    
    html += "function updateBatchBar(){";
    html += "  var bar=document.getElementById('batchBar');";
    html += "  var count=document.getElementById('selectedCount');";
    html += "  if(selectedItems.length>0){";
    html += "    bar.classList.add('show');";
    html += "    count.textContent=selectedItems.length+' selected';";
    html += "  }else{";
    html += "    bar.classList.remove('show');";
    html += "  }";
    html += "}";
    
    html += "function batchDelete(){";
    html += "  if(selectedItems.length===0)return;";
    html += "  if(!confirm('Delete '+selectedItems.length+' items?'))return;";
    html += "  window.location='/batchdelete?path='+encodeURIComponent(uploadPath)+'&items='+encodeURIComponent(selectedItems.join(','));";
    html += "}";
    
    html += "function showBatchMove(){";
    html += "  if(selectedItems.length===0)return;";
    html += "  var sel=document.getElementById('batchMoveDestination');";
    html += "  sel.innerHTML='<option>Loading...</option>';";
    html += "  fetch('/listdirs').then(r=>r.text()).then(html=>{sel.innerHTML=html;});";
    html += "  showModal('batchMoveModal');";
    html += "}";
    
    html += "function doBatchMove(){";
    html += "  var dest=document.getElementById('batchMoveDestination').value;";
    html += "  if(dest){window.location='/batchmove?path='+encodeURIComponent(uploadPath)+'&dest='+encodeURIComponent(dest)+'&items='+encodeURIComponent(selectedItems.join(','));}";
    html += "}";
    
    html += "</script>";
    
    // Window
    html += "<div class='window'>";
    
    // Title bar
    html += "<div class='titlebar'>";
    html += "<div class='titlebar-text'>";
    html += "<span class='icon folder'></span>";
    html += "<span>SD Card File Browser</span>";
    html += "</div>";
    html += "<div class='btn-close'>x</div>";
    html += "</div>";
    
    // Content
    html += "<div class='content'>";
    
    // Path bar
    html += "<div class='path-bar'>Path: " + path + "</div>";
    
    // Batch action bar (hidden by default)
    html += "<div class='batch-bar' id='batchBar'>";
    html += "<span id='selectedCount'>0 selected</span>";
    html += "<button class='batch-btn' onclick='showBatchMove()'>Move Selected</button>";
    html += "<button class='batch-btn danger' onclick='batchDelete()'>Delete Selected</button>";
    html += "</div>";
    
    // Toolbar with New Folder button
    html += "<div class='toolbar'>";
    html += "<button class='btn' onclick='showModal(\"newFolderModal\")'>+ New Folder</button>";
    html += "</div>";
    
    // Upload section with multi-file support
    html += "<div class='upload-section'>";
    html += "<label>Upload Files:</label>";
    html += "<input type='file' id='fileInput' multiple>";
    html += "<input type='button' id='uploadBtn' value='Upload Selected Files' class='btn' onclick='uploadFiles()'>";
    html += "<div class='progress-container' id='progressContainer'></div>";
    html += "</div>";
    
    // File list
    html += "<div class='file-list'>";
    html += "<div class='file-header'><input type='checkbox' class='file-checkbox' onchange='toggleSelectAll(this)'><span>Name</span><span style='text-align:right'>Size</span><span style='text-align:center'>Actions</span></div>";
    
    // Parent directory link
    if (path != "/") {
        String parent = path;
        if (parent.endsWith("/")) parent.remove(parent.length() - 1);
        int lastSlash = parent.lastIndexOf('/');
        parent = parent.substring(0, lastSlash);
        if (parent.length() == 0) parent = "/";
        html += "<div class='file-row'>";
        html += "<div></div>";  // Empty checkbox column
        html += "<div class='file-name'><span class='icon back'></span><a href='/?path=" + parent + "'>..</a></div>";
        html += "<div class='file-size'>-</div>";
        html += "<div class='file-actions'>-</div>";
        html += "</div>";
    }
    
    // List directory contents
    File dir = SD.open(path);
    if (dir && dir.isDirectory()) {
        std::vector<FileInfo> files;
        
        while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            
            FileInfo info;
            info.name = entry.name();
            info.isDir = entry.isDirectory();
            info.size = entry.size();
            files.push_back(info);
            entry.close();
        }
        dir.close();
        
        // Sort: folders first, then alphabetical
        std::sort(files.begin(), files.end(), [](const FileInfo &a, const FileInfo &b) {
            if (a.isDir && !b.isDir) return true;
            if (!a.isDir && b.isDir) return false;
            return a.name < b.name;
        });
        
        if (files.empty()) {
            html += "<div class='empty-msg'>(Empty Directory)</div>";
        }
        
        for (auto &f : files) {
            String fullPath = path;
            if (!fullPath.endsWith("/")) fullPath += "/";
            fullPath += f.name;
            
            if (f.isDir) {
                html += "<div class='file-row'>";
                html += "<input type='checkbox' class='file-checkbox' data-path='" + fullPath + "' onchange='toggleSelect(this,\"" + fullPath + "\")'>";
                html += "<div class='file-name'>" + getFileIcon(true, f.name);
                html += "<a href='/?path=" + fullPath + "'>" + f.name + "</a></div>";
                html += "<div class='file-size'>-</div>";
                html += "<div class='file-actions'>";
                html += "<button onclick=\"showRename('" + fullPath + "','" + f.name + "')\">Ren</button>";
                html += "<button onclick=\"showMove('" + fullPath + "')\">Mov</button>";
                html += "<a href='/delete?path=" + fullPath + "' onclick=\"return confirm('Delete folder: " + f.name + "?')\">Del</a>";
                html += "</div>";
                html += "</div>";
            } else {
                // Check if file is editable (text/cfg/ini/json/etc)
                String nameLower = f.name;
                nameLower.toLowerCase();
                bool isEditable = nameLower.endsWith(".txt") || nameLower.endsWith(".cfg") || 
                                  nameLower.endsWith(".ini") || nameLower.endsWith(".json") ||
                                  nameLower.endsWith(".log") || nameLower.endsWith(".md") ||
                                  nameLower.endsWith(".csv") || nameLower.endsWith(".xml") ||
                                  nameLower.endsWith(".html") || nameLower.endsWith(".htm") ||
                                  nameLower.endsWith(".css") || nameLower.endsWith(".js");
                
                html += "<div class='file-row'>";
                html += "<input type='checkbox' class='file-checkbox' data-path='" + fullPath + "' onchange='toggleSelect(this,\"" + fullPath + "\")'>";
                html += "<div class='file-name'>" + getFileIcon(false, f.name);
                html += "<a href='/download?path=" + fullPath + "'>" + f.name + "</a></div>";
                html += "<div class='file-size'>" + formatFileSize(f.size) + "</div>";
                html += "<div class='file-actions'>";
                if (isEditable) {
                    html += "<a href='/edit?path=" + fullPath + "' class='edit-btn'>Edit</a>";
                }
                html += "<a href='/download?path=" + fullPath + "'>Get</a>";
                html += "<button onclick=\"showRename('" + fullPath + "','" + f.name + "')\">Ren</button>";
                html += "<button onclick=\"showMove('" + fullPath + "')\">Mov</button>";
                html += "<a href='/delete?path=" + fullPath + "' onclick=\"return confirm('Delete file: " + f.name + "?')\">Del</a>";
                html += "</div>";
                html += "</div>";
            }
        }
    }
    
    html += "</div>";  // file-list
    
    // Disk usage bar
    html += "<div class='disk-usage'>";
    html += "<div class='disk-label'>";
    html += "<span>SD Card: " + usedStr + " / " + totalStr + " used</span>";
    html += "<span>" + freeStr + " free (" + String(usagePercent) + "%)</span>";
    html += "</div>";
    html += "<div class='disk-bar-bg'>";
    html += "<div class='disk-bar " + barClass + "' style='width:" + String(usagePercent) + "%'></div>";
    html += "</div>";
    html += "</div>";
    
    // Status bar
    html += "<div class='statusbar'>M5Shell v4.2 - SD Web Share</div>";
    
    html += "</div>";  // content
    html += "</div>";  // window
    
    html += "<div class='footer'>Access from any device on your local network</div>";
    
    // Modal dialogs
    // New Folder Modal
    html += "<div id='newFolderModal' class='modal' onclick='if(event.target===this)hideModal(\"newFolderModal\")'>";
    html += "<div class='modal-content'>";
    html += "<div class='modal-title'>Create New Folder</div>";
    html += "<div class='modal-body'>";
    html += "<label>Folder name:</label>";
    html += "<input type='text' id='newFolderName' placeholder='Enter folder name'>";
    html += "<div class='modal-buttons'>";
    html += "<button class='btn' onclick='hideModal(\"newFolderModal\")'>Cancel</button>";
    html += "<button class='btn' onclick='createFolder()'>Create</button>";
    html += "</div></div></div></div>";
    
    // Rename Modal
    html += "<div id='renameModal' class='modal' onclick='if(event.target===this)hideModal(\"renameModal\")'>";
    html += "<div class='modal-content'>";
    html += "<div class='modal-title'>Rename</div>";
    html += "<div class='modal-body'>";
    html += "<label>New name:</label>";
    html += "<input type='text' id='renameInput' placeholder='Enter new name'>";
    html += "<div class='modal-buttons'>";
    html += "<button class='btn' onclick='hideModal(\"renameModal\")'>Cancel</button>";
    html += "<button class='btn' onclick='doRename()'>Rename</button>";
    html += "</div></div></div></div>";
    
    // Move Modal
    html += "<div id='moveModal' class='modal' onclick='if(event.target===this)hideModal(\"moveModal\")'>";
    html += "<div class='modal-content'>";
    html += "<div class='modal-title'>Move To</div>";
    html += "<div class='modal-body'>";
    html += "<label>Destination folder:</label>";
    html += "<select id='moveDestination'><option>Loading...</option></select>";
    html += "<div class='modal-buttons'>";
    html += "<button class='btn' onclick='hideModal(\"moveModal\")'>Cancel</button>";
    html += "<button class='btn' onclick='doMove()'>Move</button>";
    html += "</div></div></div></div>";
    
    // Batch Move Modal
    html += "<div id='batchMoveModal' class='modal' onclick='if(event.target===this)hideModal(\"batchMoveModal\")'>";
    html += "<div class='modal-content'>";
    html += "<div class='modal-title'>Move Selected Items</div>";
    html += "<div class='modal-body'>";
    html += "<label>Destination folder:</label>";
    html += "<select id='batchMoveDestination'><option>Loading...</option></select>";
    html += "<div class='modal-buttons'>";
    html += "<button class='btn' onclick='hideModal(\"batchMoveModal\")'>Cancel</button>";
    html += "<button class='btn' onclick='doBatchMove()'>Move All</button>";
    html += "</div></div></div></div>";
    
    html += "</body></html>";
    return html;
}

void handleRoot() {
    String path = webServer->hasArg("path") ? webServer->arg("path") : "/";
    if (path.length() == 0) path = "/";
    
    // Check available heap before generating large HTML
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 25000) {
        // Low memory - send simple error page
        webServer->send(200, "text/html", 
            "<!DOCTYPE html><html><body style='background:#FFE000;padding:20px;'>"
            "<div style='background:#C0C0C0;padding:20px;max-width:400px;margin:0 auto;'>"
            "<h2>Low Memory</h2><p>Free heap: " + String(freeHeap) + " bytes</p>"
            "<p>Please restart device.</p></div></body></html>");
        return;
    }
    
    String html = generateDirectoryHTML(path);
    
    // Check if HTML was generated successfully
    if (html.length() < 1000) {
        webServer->send(200, "text/html", 
            "<!DOCTYPE html><html><body style='background:#FFE000;padding:20px;'>"
            "<div style='background:#C0C0C0;padding:20px;max-width:400px;margin:0 auto;'>"
            "<h2>Error</h2><p>Failed to generate page. HTML length: " + String(html.length()) + "</p>"
            "<p>Free heap: " + String(ESP.getFreeHeap()) + "</p></div></body></html>");
        return;
    }
    
    totalBytesOut += html.length();  // Track outgoing page data
    webServer->send(200, "text/html", html);
}

void handleDownload() {
    if (!webServer->hasArg("path")) {
        webServer->send(400, "text/plain", "Missing path");
        return;
    }
    
    String path = webServer->arg("path");
    if (!SD.exists(path)) {
        webServer->send(404, "text/plain", "File not found");
        return;
    }
    
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        webServer->send(400, "text/plain", "Cannot read file");
        if (file) file.close();
        return;
    }
    
    String filename = path.substring(path.lastIndexOf('/') + 1);
    String contentType = getContentType(filename);
    
    // Track outgoing traffic
    totalBytesOut += file.size();
    
    // Log the download
    logWebActivity("DOWNLOAD", filename + " (" + formatFileSize(file.size()) + ")");
    
    webServer->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    webServer->streamFile(file, contentType);
    file.close();
}

void handleUpload() {
    HTTPUpload &upload = webServer->upload();
    static File uploadFile;
    static String uploadPath;
    static String uploadFilename;
    
    if (upload.status == UPLOAD_FILE_START) {
        uploadPath = webServer->hasArg("path") ? webServer->arg("path") : "/";
        if (!uploadPath.endsWith("/")) uploadPath += "/";
        uploadFilename = upload.filename;
        uploadPath += upload.filename;
        uploadFile = SD.open(uploadPath, FILE_WRITE);
    } 
    else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) {
            uploadFile.write(upload.buf, upload.currentSize);
            // Track incoming traffic
            totalBytesIn += upload.currentSize;
        }
    } 
    else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
            uploadFile.close();
            // Log the upload
            logWebActivity("UPLOAD", uploadFilename + " (" + formatFileSize(upload.totalSize) + ")");
        }
    }
}

void handleUploadComplete() {
    String path = webServer->hasArg("path") ? webServer->arg("path") : "/";
    // Check if this is an AJAX request
    if (webServer->hasHeader("X-Requested-With")) {
        webServer->send(200, "text/plain", "OK");
    } else {
        webServer->sendHeader("Location", "/?path=" + path);
        webServer->send(303);
    }
}

void handleDelete() {
    if (!webServer->hasArg("path")) {
        webServer->send(400, "text/plain", "Missing path");
        return;
    }
    
    String path = webServer->arg("path");
    String filename = path.substring(path.lastIndexOf('/') + 1);
    String parent = path;
    int lastSlash = parent.lastIndexOf('/');
    parent = parent.substring(0, lastSlash);
    if (parent.length() == 0) parent = "/";
    
    if (SD.exists(path)) {
        File f = SD.open(path);
        bool isDir = f.isDirectory();
        f.close();
        if (isDir) {
            SD.rmdir(path);
            logWebActivity("DELETE FOLDER", filename);
        } else {
            SD.remove(path);
            logWebActivity("DELETE FILE", filename);
        }
    }
    
    webServer->sendHeader("Location", "/?path=" + parent);
    webServer->send(303);
}

void handleMkdir() {
    if (!webServer->hasArg("path") || !webServer->hasArg("name")) {
        webServer->send(400, "text/plain", "Missing parameters");
        return;
    }
    
    String path = webServer->arg("path");
    String name = webServer->arg("name");
    
    if (!path.endsWith("/")) path += "/";
    String newDir = path + name;
    
    if (SD.mkdir(newDir)) {
        logWebActivity("NEW FOLDER", name + " in " + path);
    }
    
    webServer->sendHeader("Location", "/?path=" + path);
    webServer->send(303);
}

void handleRename() {
    if (!webServer->hasArg("path") || !webServer->hasArg("newname")) {
        webServer->send(400, "text/plain", "Missing parameters");
        return;
    }
    
    String oldPath = webServer->arg("path");
    String newName = webServer->arg("newname");
    String oldName = oldPath.substring(oldPath.lastIndexOf('/') + 1);
    
    // Get parent directory
    int lastSlash = oldPath.lastIndexOf('/');
    String parent = oldPath.substring(0, lastSlash);
    if (parent.length() == 0) parent = "/";
    
    String newPath = parent + "/" + newName;
    
    if (SD.exists(oldPath) && !SD.exists(newPath)) {
        SD.rename(oldPath, newPath);
        logWebActivity("RENAME", oldName + " -> " + newName);
    }
    
    webServer->sendHeader("Location", "/?path=" + parent);
    webServer->send(303);
}

void handleMove() {
    if (!webServer->hasArg("path") || !webServer->hasArg("dest")) {
        webServer->send(400, "text/plain", "Missing parameters");
        return;
    }
    
    String srcPath = webServer->arg("path");
    String destDir = webServer->arg("dest");
    
    // Get filename from source
    int lastSlash = srcPath.lastIndexOf('/');
    String filename = srcPath.substring(lastSlash + 1);
    String parent = srcPath.substring(0, lastSlash);
    if (parent.length() == 0) parent = "/";
    
    if (!destDir.endsWith("/")) destDir += "/";
    String destPath = destDir + filename;
    
    if (SD.exists(srcPath) && !SD.exists(destPath)) {
        SD.rename(srcPath, destPath);
        logWebActivity("MOVE", filename + " -> " + destDir);
    }
    
    webServer->sendHeader("Location", "/?path=" + parent);
    webServer->send(303);
}

// Get list of directories for move dialog
void handleListDirs() {
    String html = "<option value='/'>/ (Root)</option>";
    
    std::function<void(String, int)> listDirs = [&](String path, int depth) {
        if (depth > 3) return;  // Limit depth
        File dir = SD.open(path);
        if (!dir || !dir.isDirectory()) return;
        
        while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (entry.isDirectory()) {
                String dirPath = path;
                if (!dirPath.endsWith("/")) dirPath += "/";
                dirPath += entry.name();
                
                String indent = "";
                for (int i = 0; i < depth; i++) indent += "&nbsp;&nbsp;";
                html += "<option value='" + dirPath + "'>" + indent + "/" + entry.name() + "</option>";
                
                listDirs(dirPath, depth + 1);
            }
            entry.close();
        }
        dir.close();
    };
    
    listDirs("/", 1);
    webServer->send(200, "text/html", html);
}

// Text editor page
void handleEdit() {
    if (!webServer->hasArg("path")) {
        webServer->send(400, "text/plain", "Missing path");
        return;
    }
    
    String path = webServer->arg("path");
    if (!SD.exists(path)) {
        webServer->send(404, "text/plain", "File not found");
        return;
    }
    
    // Read file content
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        webServer->send(400, "text/plain", "Cannot read file");
        if (file) file.close();
        return;
    }
    
    String content = "";
    while (file.available()) {
        content += (char)file.read();
    }
    file.close();
    
    // Get parent path for back button
    String parent = path;
    int lastSlash = parent.lastIndexOf('/');
    parent = parent.substring(0, lastSlash);
    if (parent.length() == 0) parent = "/";
    
    String filename = path.substring(path.lastIndexOf('/') + 1);
    
    // Generate editor HTML
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Edit - " + filename + "</title>";
    html += "<style>";
    html += "*{box-sizing:border-box;margin:0;padding:0;}";
    html += "body{font-family:'Courier New',monospace;font-size:14px;background:#FFE000;padding:15px;min-height:100vh;}";
    html += ".window{background:#C0C0C0;border:3px solid;border-color:#FFFFFF #000000 #000000 #FFFFFF;max-width:800px;margin:0 auto;box-shadow:4px 4px 0 #808080;}";
    html += ".titlebar{background:linear-gradient(90deg,#000080,#1084D0);color:#FFF;padding:4px 8px;font-weight:bold;display:flex;justify-content:space-between;align-items:center;}";
    html += ".content{padding:8px;}";
    html += ".toolbar{display:flex;gap:8px;margin-bottom:8px;padding:8px;background:#C0C0C0;border:2px solid;border-color:#808080 #FFF #FFF #808080;align-items:center;}";
    html += ".btn{background:#C0C0C0;border:2px solid;border-color:#FFF #000 #000 #FFF;padding:6px 16px;font-family:'Courier New',monospace;font-size:12px;cursor:pointer;display:flex;align-items:center;gap:5px;}";
    html += ".btn:active{border-color:#000 #FFF #FFF #000;}";
    html += ".btn:hover{background:#E0E0E0;}";
    html += ".btn-save{background:#90EE90;}";
    html += ".btn-save:hover{background:#7CFC00;}";
    html += ".btn-back{background:#ADD8E6;}";
    html += ".btn-back:hover{background:#87CEEB;}";
    html += ".editor-container{border:2px solid;border-color:#808080 #FFF #FFF #808080;background:#FFF;}";
    html += "textarea{width:100%;height:60vh;min-height:300px;padding:10px;border:none;font-family:'Courier New',monospace;font-size:13px;resize:vertical;outline:none;line-height:1.4;}";
    html += ".filename{flex-grow:1;font-weight:bold;}";
    html += ".status{padding:4px 8px;background:#C0C0C0;border:2px solid;border-color:#808080 #FFF #FFF #808080;font-size:12px;margin-top:8px;}";
    html += ".modified{color:#800000;font-weight:bold;}";
    // Save icon (floppy disk)
    html += ".icon-save{width:16px;height:16px;background:#000080;border:1px solid #000;position:relative;}";
    html += ".icon-save:before{content:'';position:absolute;top:1px;left:2px;right:2px;height:5px;background:#C0C0C0;}";
    html += ".icon-save:after{content:'';position:absolute;bottom:1px;left:3px;right:3px;height:6px;background:#FFF;border:1px solid #808080;}";
    // Back icon
    html += ".icon-back{width:0;height:0;border:6px solid transparent;border-right:8px solid #000080;}";
    html += "</style></head><body>";
    
    html += "<div class='window'>";
    html += "<div class='titlebar'><span>Text Editor - " + filename + "</span></div>";
    html += "<div class='content'>";
    
    // Toolbar
    html += "<div class='toolbar'>";
    html += "<button class='btn btn-back' onclick='goBack()'><span class='icon-back'></span>Back</button>";
    html += "<span class='filename'>" + path + "</span>";
    html += "<button class='btn btn-save' onclick='saveFile()'><span class='icon-save'></span>Save</button>";
    html += "</div>";
    
    // Editor
    html += "<form id='editForm' method='POST' action='/saveedit'>";
    html += "<input type='hidden' name='path' value='" + path + "'>";
    html += "<input type='hidden' name='backpath' value='" + parent + "'>";
    html += "<div class='editor-container'>";
    html += "<textarea name='content' id='editor' spellcheck='false'>";
    
    // Escape HTML entities in content
    content.replace("&", "&amp;");
    content.replace("<", "&lt;");
    content.replace(">", "&gt;");
    content.replace("\"", "&quot;");
    html += content;
    
    html += "</textarea>";
    html += "</div>";
    html += "</form>";
    
    // Status bar
    html += "<div class='status'><span id='status'>Ready</span> | <span id='lineinfo'>Line: 1, Col: 1</span></div>";
    
    html += "</div></div>";
    
    // JavaScript
    html += "<script>";
    html += "var modified=false;";
    html += "var editor=document.getElementById('editor');";
    html += "var statusEl=document.getElementById('status');";
    html += "var lineInfo=document.getElementById('lineinfo');";
    
    // Track modifications
    html += "editor.addEventListener('input',function(){";
    html += "  if(!modified){modified=true;statusEl.innerHTML='<span class=\"modified\">Modified</span>';}";
    html += "});";
    
    // Update line/column info
    html += "editor.addEventListener('keyup',updateLineInfo);";
    html += "editor.addEventListener('click',updateLineInfo);";
    html += "function updateLineInfo(){";
    html += "  var text=editor.value.substring(0,editor.selectionStart);";
    html += "  var lines=text.split('\\n');";
    html += "  var line=lines.length;";
    html += "  var col=lines[lines.length-1].length+1;";
    html += "  lineInfo.textContent='Line: '+line+', Col: '+col;";
    html += "}";
    
    // Save function
    html += "function saveFile(){";
    html += "  statusEl.textContent='Saving...';";
    html += "  document.getElementById('editForm').submit();";
    html += "}";
    
    // Back function with confirmation
    html += "function goBack(){";
    html += "  if(modified){";
    html += "    if(confirm('You have unsaved changes. Discard and go back?')){";
    html += "      window.location='/?path=" + parent + "';";
    html += "    }";
    html += "  }else{";
    html += "    window.location='/?path=" + parent + "';";
    html += "  }";
    html += "}";
    
    // Keyboard shortcuts
    html += "document.addEventListener('keydown',function(e){";
    html += "  if((e.ctrlKey||e.metaKey)&&e.key==='s'){";
    html += "    e.preventDefault();";
    html += "    saveFile();";
    html += "  }";
    html += "});";
    
    // Warn before leaving if modified
    html += "window.onbeforeunload=function(){if(modified)return true;};";
    
    html += "</script>";
    html += "</body></html>";
    
    totalBytesOut += html.length();
    webServer->send(200, "text/html", html);
}

// Save edited file
void handleSaveEdit() {
    if (!webServer->hasArg("path") || !webServer->hasArg("content")) {
        webServer->send(400, "text/plain", "Missing parameters");
        return;
    }
    
    String path = webServer->arg("path");
    String content = webServer->arg("content");
    String backPath = webServer->hasArg("backpath") ? webServer->arg("backpath") : "/";
    
    // Convert line endings (browser sends \r\n, we want \n)
    content.replace("\r\n", "\n");
    
    // Write file
    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        webServer->send(500, "text/plain", "Failed to open file for writing");
        return;
    }
    
    file.print(content);
    file.close();
    
    // Track incoming data
    totalBytesIn += content.length();
    
    // Log the edit
    String filename = path.substring(path.lastIndexOf('/') + 1);
    logWebActivity("EDIT SAVE", filename + " (" + formatFileSize(content.length()) + ")");
    
    // Redirect back to file manager
    webServer->sendHeader("Location", "/?path=" + backPath);
    webServer->send(303);
}

// Batch delete handler
void handleBatchDelete() {
    if (!webServer->hasArg("items") || !webServer->hasArg("path")) {
        webServer->send(400, "text/plain", "Missing parameters");
        return;
    }
    
    String items = webServer->arg("items");
    String currentPath = webServer->arg("path");
    int deleteCount = 0;
    
    // Parse comma-separated list of items
    int startIdx = 0;
    while (startIdx < (int)items.length()) {
        int endIdx = items.indexOf(',', startIdx);
        if (endIdx == -1) endIdx = items.length();
        
        String itemPath = items.substring(startIdx, endIdx);
        itemPath.trim();
        
        if (itemPath.length() > 0 && SD.exists(itemPath)) {
            String filename = itemPath.substring(itemPath.lastIndexOf('/') + 1);
            File f = SD.open(itemPath);
            bool isDir = f.isDirectory();
            f.close();
            
            if (isDir) {
                if (SD.rmdir(itemPath)) {
                    logWebActivity("BATCH DELETE", filename + " (folder)");
                    deleteCount++;
                }
            } else {
                if (SD.remove(itemPath)) {
                    logWebActivity("BATCH DELETE", filename);
                    deleteCount++;
                }
            }
        }
        startIdx = endIdx + 1;
    }
    
    logWebActivity("BATCH COMPLETE", String(deleteCount) + " items deleted");
    
    webServer->sendHeader("Location", "/?path=" + currentPath);
    webServer->send(303);
}

// Batch move handler
void handleBatchMove() {
    if (!webServer->hasArg("items") || !webServer->hasArg("dest") || !webServer->hasArg("path")) {
        webServer->send(400, "text/plain", "Missing parameters");
        return;
    }
    
    String items = webServer->arg("items");
    String destDir = webServer->arg("dest");
    String currentPath = webServer->arg("path");
    int moveCount = 0;
    
    if (!destDir.endsWith("/")) destDir += "/";
    
    // Parse comma-separated list of items
    int startIdx = 0;
    while (startIdx < (int)items.length()) {
        int endIdx = items.indexOf(',', startIdx);
        if (endIdx == -1) endIdx = items.length();
        
        String itemPath = items.substring(startIdx, endIdx);
        itemPath.trim();
        
        if (itemPath.length() > 0 && SD.exists(itemPath)) {
            String filename = itemPath.substring(itemPath.lastIndexOf('/') + 1);
            String destPath = destDir + filename;
            
            if (!SD.exists(destPath)) {
                if (SD.rename(itemPath, destPath)) {
                    logWebActivity("BATCH MOVE", filename + " -> " + destDir);
                    moveCount++;
                }
            }
        }
        startIdx = endIdx + 1;
    }
    
    logWebActivity("BATCH COMPLETE", String(moveCount) + " items moved");
    
    webServer->sendHeader("Location", "/?path=" + currentPath);
    webServer->send(303);
}

void startWebServer() {
    if (webServer != nullptr) {
        webServer->stop();
        delete webServer;
    }
    
    webServer = new WebServer(WEB_SERVER_PORT);
    webServer->on("/", HTTP_GET, handleRoot);
    webServer->on("/download", HTTP_GET, handleDownload);
    webServer->on("/upload", HTTP_POST, handleUploadComplete, handleUpload);
    webServer->on("/delete", HTTP_GET, handleDelete);
    webServer->on("/mkdir", HTTP_GET, handleMkdir);
    webServer->on("/rename", HTTP_GET, handleRename);
    webServer->on("/move", HTTP_GET, handleMove);
    webServer->on("/listdirs", HTTP_GET, handleListDirs);
    webServer->on("/edit", HTTP_GET, handleEdit);
    webServer->on("/saveedit", HTTP_POST, handleSaveEdit);
    webServer->on("/batchdelete", HTTP_GET, handleBatchDelete);
    webServer->on("/batchmove", HTTP_GET, handleBatchMove);
    webServer->begin();
    sdShareEnabled = true;
    
    // Log server start
    logWebActivity("SERVER", "Started on " + WiFi.localIP().toString());
}

void stopWebServer() {
    if (webServer != nullptr) {
        webServer->stop();
        delete webServer;
        webServer = nullptr;
    }
    sdShareEnabled = false;
}

// ==========================================
//     Retro Pixel Art Icon Drawing
// ==========================================

void drawFolderIcon(int x, int y) {
    sprite.fillRect(x, y + 2, 6, 3, COL_BLACK);
    sprite.fillRect(x + 1, y + 3, 4, 1, COL_WHITE);
    sprite.fillRect(x, y + 4, 14, 10, COL_BLACK);
    sprite.fillRect(x + 1, y + 5, 12, 8, COL_WHITE);
    sprite.drawLine(x + 1, y + 7, x + 12, y + 7, COL_BLACK);
}

void drawFileIcon(int x, int y) {
    sprite.fillRect(x + 1, y, 10, 14, COL_BLACK);
    sprite.fillRect(x + 2, y + 1, 8, 12, COL_WHITE);
    sprite.fillRect(x + 8, y, 3, 3, COL_WHITE);
    sprite.drawLine(x + 8, y, x + 8, y + 3, COL_BLACK);
    sprite.drawLine(x + 8, y + 3, x + 11, y + 3, COL_BLACK);
    sprite.drawLine(x + 8, y, x + 11, y + 3, COL_BLACK);
    sprite.drawLine(x + 3, y + 5, x + 8, y + 5, COL_BLACK);
    sprite.drawLine(x + 3, y + 7, x + 8, y + 7, COL_BLACK);
    sprite.drawLine(x + 3, y + 9, x + 6, y + 9, COL_BLACK);
}

void drawImageIcon(int x, int y) {
    sprite.fillRect(x + 1, y, 12, 14, COL_BLACK);
    sprite.fillRect(x + 2, y + 1, 10, 12, COL_WHITE);
    sprite.fillTriangle(x + 3, y + 10, x + 6, y + 4, x + 9, y + 10, COL_BLACK);
    sprite.fillTriangle(x + 7, y + 10, x + 9, y + 6, x + 11, y + 10, COL_GRAY_MID);
    sprite.fillRect(x + 9, y + 3, 2, 2, COL_BLACK);
}

void drawMusicIcon(int x, int y) {
    sprite.fillRect(x + 1, y, 12, 14, COL_BLACK);
    sprite.fillRect(x + 2, y + 1, 10, 12, COL_WHITE);
    sprite.fillRect(x + 8, y + 3, 2, 7, COL_BLACK);
    sprite.fillCircle(x + 6, y + 10, 2, COL_BLACK);
    sprite.fillRect(x + 8, y + 3, 3, 2, COL_BLACK);
}

void drawCodeIcon(int x, int y) {
    sprite.fillRect(x + 1, y, 10, 14, COL_BLACK);
    sprite.fillRect(x + 2, y + 1, 8, 12, COL_WHITE);
    sprite.drawLine(x + 3, y + 5, x + 5, y + 3, COL_BLACK);
    sprite.drawLine(x + 3, y + 5, x + 5, y + 7, COL_BLACK);
    sprite.drawLine(x + 9, y + 5, x + 7, y + 3, COL_BLACK);
    sprite.drawLine(x + 9, y + 5, x + 7, y + 7, COL_BLACK);
    sprite.drawLine(x + 5, y + 9, x + 7, y + 11, COL_BLACK);
}

void drawFileTypeIcon(int x, int y, bool isDir, String filename) {
    filename.toLowerCase();
    if (isDir) drawFolderIcon(x, y);
    else if (filename.endsWith(".jpg") || filename.endsWith(".png") || 
             filename.endsWith(".bmp") || filename.endsWith(".gif")) drawImageIcon(x, y);
    else if (filename.endsWith(".mp3") || filename.endsWith(".wav") || 
             filename.endsWith(".mid") || filename.endsWith(".flac") ||
             filename.endsWith(".sid")) drawMusicIcon(x, y);
    else if (filename.endsWith(".cpp") || filename.endsWith(".h") || 
             filename.endsWith(".py") || filename.endsWith(".js") ||
             filename.endsWith(".txt") || filename.endsWith(".ini") ||
             filename.endsWith(".json") || filename.endsWith(".md")) drawCodeIcon(x, y);
    else drawFileIcon(x, y);
}

// ==========================================
//     Retro Window Drawing
// ==========================================

void draw3DRaised(int x, int y, int w, int h) {
    sprite.drawLine(x, y, x + w - 1, y, COL_WHITE);
    sprite.drawLine(x, y, x, y + h - 1, COL_WHITE);
    sprite.drawLine(x, y + h - 1, x + w - 1, y + h - 1, COL_BLACK);
    sprite.drawLine(x + w - 1, y, x + w - 1, y + h - 1, COL_BLACK);
    sprite.drawLine(x + 1, y + h - 2, x + w - 2, y + h - 2, COL_GRAY_DARK);
    sprite.drawLine(x + w - 2, y + 1, x + w - 2, y + h - 2, COL_GRAY_DARK);
}

void draw3DSunken(int x, int y, int w, int h) {
    sprite.drawLine(x, y, x + w - 1, y, COL_GRAY_DARK);
    sprite.drawLine(x, y, x, y + h - 1, COL_GRAY_DARK);
    sprite.drawLine(x, y + h - 1, x + w - 1, y + h - 1, COL_WHITE);
    sprite.drawLine(x + w - 1, y, x + w - 1, y + h - 1, COL_WHITE);
}

void drawTitleButton(int x, int y, int type) {
    sprite.fillRect(x, y, 11, 11, COL_GRAY_LIGHT);
    draw3DRaised(x, y, 11, 11);
    if (type == 0) {
        sprite.drawLine(x + 3, y + 3, x + 7, y + 7, COL_BLACK);
        sprite.drawLine(x + 3, y + 7, x + 7, y + 3, COL_BLACK);
    } else if (type == 1) {
        sprite.drawRect(x + 2, y + 2, 7, 7, COL_BLACK);
        sprite.drawLine(x + 2, y + 3, x + 8, y + 3, COL_BLACK);
    } else {
        sprite.drawLine(x + 2, y + 7, x + 8, y + 7, COL_BLACK);
    }
}

void drawWindowFrame(int x, int y, int w, int h, String title) {
    sprite.fillRect(x, y, w, h, COL_GRAY_LIGHT);
    draw3DRaised(x, y, w, h);
    sprite.fillRect(x + 3, y + 3, w - 6, titleBarHeight, COL_TITLE_BAR);
    sprite.setTextColor(COL_WHITE, COL_TITLE_BAR);
    sprite.setCursor(x + 6, y + 5);
    sprite.print(title);
    drawTitleButton(x + w - 40, y + 4, 2);
    drawTitleButton(x + w - 28, y + 4, 1);
    drawTitleButton(x + w - 16, y + 4, 0);
}

void drawButton(int x, int y, int w, int h, String label, bool selected) {
    if (selected) {
        sprite.fillRect(x, y, w, h, COL_GRAY_DARK);
        draw3DSunken(x, y, w, h);
        sprite.setTextColor(COL_WHITE, COL_GRAY_DARK);
    } else {
        sprite.fillRect(x, y, w, h, COL_GRAY_LIGHT);
        draw3DRaised(x, y, w, h);
        sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    }
    int textX = x + (w - sprite.textWidth(label)) / 2;
    int textY = y + (h - 8) / 2;
    sprite.setCursor(textX, textY);
    sprite.print(label);
}

// ==========================================
//     Dialog System
// ==========================================

void clearKeyboardBuffer() {
    delay(200);
    M5Cardputer.update();
    while (M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(10); }
    delay(100);
}

void showYesNoDialog(String message, DialogAction action, AppMode returnTo) {
    dialogMessage = message;
    pendingAction = action;
    dialogYesSelected = false;
    returnMode = returnTo;
    currentMode = MODE_DIALOG_YESNO;
    clearKeyboardBuffer();
}

void showSaveDialog(String message, AppMode returnTo) {
    dialogMessage = message;
    pendingAction = ACTION_SAVE_AND_EXIT;
    dialogYesSelected = true;
    returnMode = returnTo;
    currentMode = MODE_DIALOG_SAVE;
    clearKeyboardBuffer();
}

void showInputDialog(String message, String defaultValue, DialogAction action) {
    dialogMessage = message;
    dialogInput = defaultValue;
    pendingAction = action;
    currentMode = MODE_DIALOG_INPUT;
    clearKeyboardBuffer();
}

void renderYesNoDialog() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int dlgW = 200, dlgH = 70;
    int dlgX = (screenW - dlgW) / 2, dlgY = (screenH - dlgH) / 2;
    
    sprite.fillRect(dlgX, dlgY, dlgW, dlgH, COL_GRAY_LIGHT);
    draw3DRaised(dlgX, dlgY, dlgW, dlgH);
    sprite.fillRect(dlgX + 3, dlgY + 3, dlgW - 6, 14, COL_TITLE_BAR);
    sprite.setTextColor(COL_WHITE, COL_TITLE_BAR);
    sprite.setCursor(dlgX + 6, dlgY + 5);
    sprite.print("Confirm");
    
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(dlgX + 10, dlgY + 25);
    String msg = dialogMessage;
    if (msg.length() > 30) {
        sprite.print(msg.substring(0, 30));
        sprite.setCursor(dlgX + 10, dlgY + 35);
        sprite.print(msg.substring(30));
    } else sprite.print(msg);
    
    int btnW = 50, btnH = 18, btnY = dlgY + dlgH - btnH - 8;
    drawButton(dlgX + dlgW/2 - btnW - 10, btnY, btnW, btnH, "Yes", dialogYesSelected);
    drawButton(dlgX + dlgW/2 + 10, btnY, btnW, btnH, "No", !dialogYesSelected);
    
    sprite.setTextColor(COL_GRAY_DARK, COL_GRAY_LIGHT);
    sprite.setCursor(dlgX + 10, dlgY + dlgH - 8);
    sprite.print("[;/.]=Sel [Enter]=OK");
    sprite.pushSprite(0, 0);
}

void renderSaveDialog() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int dlgW = 210, dlgH = 75;
    int dlgX = (screenW - dlgW) / 2, dlgY = (screenH - dlgH) / 2;
    
    sprite.fillRect(dlgX, dlgY, dlgW, dlgH, COL_GRAY_LIGHT);
    draw3DRaised(dlgX, dlgY, dlgW, dlgH);
    sprite.fillRect(dlgX + 3, dlgY + 3, dlgW - 6, 14, COL_TITLE_BAR);
    sprite.setTextColor(COL_WHITE, COL_TITLE_BAR);
    sprite.setCursor(dlgX + 6, dlgY + 5);
    sprite.print("Save Changes?");
    
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(dlgX + 10, dlgY + 25);
    sprite.print(dialogMessage);
    
    int btnW = 45, btnH = 18, btnY = dlgY + dlgH - btnH - 8;
    drawButton(dlgX + 15, btnY, btnW, btnH, "Yes", dialogYesSelected);
    drawButton(dlgX + dlgW/2 - btnW/2, btnY, btnW, btnH, "No", !dialogYesSelected);
    drawButton(dlgX + dlgW - btnW - 15, btnY, btnW + 10, btnH, "Cancel", false);
    
    sprite.setTextColor(COL_GRAY_DARK, COL_GRAY_LIGHT);
    sprite.setCursor(dlgX + 10, dlgY + dlgH - 8);
    sprite.print("[;/.]=Sel [Esc]=Cancel");
    sprite.pushSprite(0, 0);
}

void renderInputDialog() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int dlgW = 220, dlgH = 80;
    int dlgX = (screenW - dlgW) / 2, dlgY = (screenH - dlgH) / 2;
    
    sprite.fillRect(dlgX, dlgY, dlgW, dlgH, COL_GRAY_LIGHT);
    draw3DRaised(dlgX, dlgY, dlgW, dlgH);
    
    String title = (pendingAction == ACTION_SAVE_NEW_FILE) ? "Save As" : "Rename";
    sprite.fillRect(dlgX + 3, dlgY + 3, dlgW - 6, 14, COL_TITLE_BAR);
    sprite.setTextColor(COL_WHITE, COL_TITLE_BAR);
    sprite.setCursor(dlgX + 6, dlgY + 5);
    sprite.print(title);
    
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(dlgX + 10, dlgY + 24);
    sprite.print(dialogMessage);
    
    int inputX = dlgX + 10, inputY = dlgY + 38;
    int inputW = dlgW - 20, inputH = 16;
    sprite.fillRect(inputX, inputY, inputW, inputH, COL_WHITE);
    draw3DSunken(inputX, inputY, inputW, inputH);
    
    sprite.setTextColor(COL_BLACK, COL_WHITE);
    sprite.setCursor(inputX + 3, inputY + 4);
    String displayText = dialogInput;
    if (displayText.length() > 28) displayText = displayText.substring(displayText.length() - 28);
    sprite.print(displayText);
    
    int cursorXPos = inputX + 3 + sprite.textWidth(displayText);
    sprite.fillRect(cursorXPos, inputY + 2, 2, inputH - 4, COL_BLACK);
    
    sprite.setTextColor(COL_GRAY_DARK, COL_GRAY_LIGHT);
    sprite.setCursor(dlgX + 10, dlgY + dlgH - 12);
    sprite.print("Enter=OK  Esc=Cancel");
    sprite.pushSprite(0, 0);
}

void handleYesNoInput();
void handleSaveDialogInput();
void handleInputDialogInput();
void saveEditorFile();

void handleYesNoInput() {
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP) || M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
            dialogYesSelected = !dialogYesSelected;
            renderYesNoDialog();
            delay(150);
        }
        if (status.enter) {
            if (dialogYesSelected && pendingAction == ACTION_DELETE) {
                if (SD.exists(targetFilePath)) {
                    File f = SD.open(targetFilePath);
                    bool isDir = f.isDirectory();
                    f.close();
                    if (isDir) SD.rmdir(targetFilePath);
                    else SD.remove(targetFilePath);
                }
                loadFileList();
            }
            pendingAction = ACTION_NONE;
            currentMode = MODE_FILE_UI;
            renderFileUI();
            delay(200);
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            pendingAction = ACTION_NONE;
            currentMode = MODE_FILE_UI;
            renderFileUI();
            delay(200);
        }
    }
}

void handleSaveDialogInput() {
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP) || M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN) ||
            M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_LEFT) || M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_RIGHT)) {
            dialogYesSelected = !dialogYesSelected;
            renderSaveDialog();
            delay(150);
        }
        if (status.enter) {
            if (dialogYesSelected) {
                if (editorFilePath.length() > 0) {
                    saveEditorFile();
                    currentMode = returnMode;
                } else {
                    showInputDialog("Filename:", editorFileName, ACTION_SAVE_NEW_FILE);
                    return;
                }
            } else currentMode = returnMode;
            editorDirty = false;
            if (currentMode == MODE_TERMINAL) needsRedraw = true;
            else if (currentMode == MODE_FILE_UI) { loadFileList(); renderFileUI(); }
            delay(200);
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            currentMode = MODE_TEXT_EDITOR;
            delay(200);
        }
    }
}

void handleInputDialogInput() {
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        for (auto c : status.word) {
            if (c >= 32 && c < 127) dialogInput += c;
        }
        if (status.del && dialogInput.length() > 0) dialogInput.remove(dialogInput.length() - 1);
        if (status.enter) {
            if (pendingAction == ACTION_RENAME && dialogInput.length() > 0) {
                int lastSlash = targetFilePath.lastIndexOf('/');
                String dirPath = targetFilePath.substring(0, lastSlash + 1);
                String newPath = dirPath + dialogInput;
                if (SD.exists(targetFilePath) && !SD.exists(newPath)) SD.rename(targetFilePath, newPath);
                loadFileList();
                pendingAction = ACTION_NONE;
                currentMode = MODE_FILE_UI;
                renderFileUI();
            } else if (pendingAction == ACTION_SAVE_NEW_FILE && dialogInput.length() > 0) {
                editorFileName = dialogInput;
                editorFilePath = getFullPath(dialogInput);
                saveEditorFile();
                editorDirty = false;
                pendingAction = ACTION_NONE;
                currentMode = returnMode;
                if (currentMode == MODE_TERMINAL) needsRedraw = true;
                else if (currentMode == MODE_FILE_UI) { loadFileList(); renderFileUI(); }
            }
            delay(200);
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            pendingAction = ACTION_NONE;
            currentMode = MODE_FILE_UI;
            renderFileUI();
            delay(200);
        }
        renderInputDialog();
    }
}

// ==========================================
//     Text Editor
// ==========================================

void initEditor() {
    editorLines.clear();
    editorLines.push_back("");
    editorCursorX = 0;
    editorCursorY = 0;
    editorScrollY = 0;
    editorDirty = false;
}

void openEditorNewFile() {
    initEditor();
    editorFilePath = "";
    editorFileName = "untitled.txt";
    currentMode = MODE_TEXT_EDITOR;
}

void openEditorFile(String filepath) {
    initEditor();
    if (!SD.exists(filepath)) {
        editorFilePath = filepath;
        editorFileName = filepath.substring(filepath.lastIndexOf('/') + 1);
        currentMode = MODE_TEXT_EDITOR;
        return;
    }
    File file = SD.open(filepath, FILE_READ);
    if (!file) { currentMode = MODE_TEXT_EDITOR; return; }
    editorFilePath = filepath;
    editorFileName = filepath.substring(filepath.lastIndexOf('/') + 1);
    editorLines.clear();
    String line = "";
    while (file.available()) {
        char c = file.read();
        if (c == '\n') { editorLines.push_back(line); line = ""; }
        else if (c != '\r') line += c;
    }
    editorLines.push_back(line);
    file.close();
    if (editorLines.empty()) editorLines.push_back("");
    currentMode = MODE_TEXT_EDITOR;
}

void saveEditorFile() {
    if (editorFilePath.length() == 0) return;
    File file = SD.open(editorFilePath, FILE_WRITE);
    if (!file) return;
    for (int i = 0; i < (int)editorLines.size(); i++) {
        file.print(editorLines[i]);
        if (i < (int)editorLines.size() - 1) file.print("\n");
    }
    file.close();
    editorDirty = false;
}

void renderTextEditor() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    sprite.fillRect(winX, winY, winW, winH, COL_GRAY_LIGHT);
    draw3DRaised(winX, winY, winW, winH);
    sprite.fillRect(winX + 3, winY + 3, winW - 6, titleBarHeight, COL_TITLE_BAR);
    sprite.setTextColor(COL_WHITE, COL_TITLE_BAR);
    sprite.setCursor(winX + 6, winY + 5);
    String title = editorFileName;
    if (editorDirty) title += " *";
    if (title.length() > 25) title = title.substring(0, 22) + "...";
    sprite.print(title);
    drawTitleButton(winX + winW - 16, winY + 4, 0);
    
    int textX = winX + 5, textY = winY + titleBarHeight + 7;
    int textW = winW - 10, textH = winH - titleBarHeight - 24;
    sprite.fillRect(textX, textY, textW, textH, COL_WHITE);
    draw3DSunken(textX, textY, textW, textH);
    
    int lineHeight = 10;
    int visibleLines = (textH - 4) / lineHeight;
    int maxCharsPerLine = (textW - 6) / 6;
    if (editorCursorY < editorScrollY) editorScrollY = editorCursorY;
    if (editorCursorY >= editorScrollY + visibleLines) editorScrollY = editorCursorY - visibleLines + 1;
    
    sprite.setTextColor(COL_BLACK, COL_WHITE);
    int drawY = textY + 3;
    for (int i = editorScrollY; i < (int)editorLines.size() && i < editorScrollY + visibleLines; i++) {
        sprite.setCursor(textX + 3, drawY);
        String line = editorLines[i];
        if ((int)line.length() > maxCharsPerLine) line = line.substring(0, maxCharsPerLine - 1);
        sprite.print(line);
        if (i == editorCursorY && editorCursorVisible) {
            int cursorDrawX = textX + 3 + editorCursorX * 6;
            if (cursorDrawX < textX + textW - 4) sprite.fillRect(cursorDrawX, drawY, 2, lineHeight - 2, COL_BLACK);
        }
        drawY += lineHeight;
    }
    
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    sprite.print("Ln:" + String(editorCursorY + 1) + " Col:" + String(editorCursorX + 1));
    sprite.setCursor(winX + winW - 85, winY + winH - 14);
    sprite.print("Fn+S=Save");
    sprite.pushSprite(0, 0);
}

void handleTextEditorInput() {
    if (millis() - editorLastBlink > CURSOR_BLINK_MS) {
        editorLastBlink = millis();
        editorCursorVisible = !editorCursorVisible;
        renderTextEditor();
    }
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        bool fnPressed = status.fn;
        
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_S) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('s'))) {
            if (editorFilePath.length() > 0) saveEditorFile();
            else { returnMode = MODE_TEXT_EDITOR; showInputDialog("Filename:", editorFileName, ACTION_SAVE_NEW_FILE); }
            delay(200); return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
            if (editorDirty) showSaveDialog("Save changes?", returnMode);
            else {
                currentMode = returnMode;
                if (currentMode == MODE_TERMINAL) needsRedraw = true;
                else if (currentMode == MODE_FILE_UI) renderFileUI();
            }
            delay(200); return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
            if (editorCursorY > 0) {
                editorCursorY--;
                if (editorCursorX > (int)editorLines[editorCursorY].length()) editorCursorX = editorLines[editorCursorY].length();
            }
            editorCursorVisible = true; editorLastBlink = millis(); renderTextEditor(); delay(80); return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
            if (editorCursorY < (int)editorLines.size() - 1) {
                editorCursorY++;
                if (editorCursorX > (int)editorLines[editorCursorY].length()) editorCursorX = editorLines[editorCursorY].length();
            }
            editorCursorVisible = true; editorLastBlink = millis(); renderTextEditor(); delay(80); return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_LEFT)) {
            if (editorCursorX > 0) editorCursorX--;
            else if (editorCursorY > 0) { editorCursorY--; editorCursorX = editorLines[editorCursorY].length(); }
            editorCursorVisible = true; editorLastBlink = millis(); renderTextEditor(); delay(80); return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_RIGHT)) {
            if (editorCursorX < (int)editorLines[editorCursorY].length()) editorCursorX++;
            else if (editorCursorY < (int)editorLines.size() - 1) { editorCursorY++; editorCursorX = 0; }
            editorCursorVisible = true; editorLastBlink = millis(); renderTextEditor(); delay(80); return;
        }
        if (status.enter) {
            String currentLine = editorLines[editorCursorY];
            editorLines[editorCursorY] = currentLine.substring(0, editorCursorX);
            editorLines.insert(editorLines.begin() + editorCursorY + 1, currentLine.substring(editorCursorX));
            editorCursorY++; editorCursorX = 0; editorDirty = true;
            editorCursorVisible = true; editorLastBlink = millis(); renderTextEditor(); delay(100); return;
        }
        if (status.del) {
            if (editorCursorX > 0) {
                String &line = editorLines[editorCursorY];
                line = line.substring(0, editorCursorX - 1) + line.substring(editorCursorX);
                editorCursorX--; editorDirty = true;
            } else if (editorCursorY > 0) {
                int prevLen = editorLines[editorCursorY - 1].length();
                editorLines[editorCursorY - 1] += editorLines[editorCursorY];
                editorLines.erase(editorLines.begin() + editorCursorY);
                editorCursorY--; editorCursorX = prevLen; editorDirty = true;
            }
            editorCursorVisible = true; editorLastBlink = millis(); renderTextEditor(); delay(50); return;
        }
        for (auto c : status.word) {
            if (c >= 32 && c < 127 && c != KEY_BTN_UP && c != KEY_BTN_DOWN && 
                c != KEY_BTN_LEFT && c != KEY_BTN_RIGHT && c != KEY_BTN_ESC) {
                String &line = editorLines[editorCursorY];
                line = line.substring(0, editorCursorX) + String(c) + line.substring(editorCursorX);
                editorCursorX++; editorDirty = true;
            }
        }
        editorCursorVisible = true; editorLastBlink = millis(); renderTextEditor();
    }
}

// ==========================================
//           File UI Logic
// ==========================================

bool compareFiles(const FileInfo &a, const FileInfo &b) {
    if (a.isDir && !b.isDir) return true;
    if (!a.isDir && b.isDir) return false;
    return a.name < b.name;
}

// Count total files in directory (without loading them all)
int countFilesInDir(String path) {
    File root = SD.open(path);
    if (!root || !root.isDirectory()) return 0;
    
    int count = 0;
    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        count++;
        entry.close();
    }
    root.close();
    return count;
}

void loadFileList() {
    loadFileListPage(fileCurrentPage);
}

void loadFileListPage(int page) {
    fileList.clear();
    fileList.reserve(FILES_PER_PAGE);
    
    // Count total files first
    fileTotalCount = countFilesInDir(currentPath);
    fileTotalPages = (fileTotalCount + FILES_PER_PAGE - 1) / FILES_PER_PAGE;
    if (fileTotalPages < 1) fileTotalPages = 1;
    
    // Clamp page to valid range
    if (page < 0) page = 0;
    if (page >= fileTotalPages) page = fileTotalPages - 1;
    fileCurrentPage = page;
    
    File root = SD.open(currentPath);
    if (!root || !root.isDirectory()) return;
    
    // Temporary vector to hold all files for sorting
    std::vector<FileInfo> allFiles;
    allFiles.reserve(min(fileTotalCount, 500));  // Cap reserve to prevent huge allocation
    
    int count = 0;
    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        
        // Only load up to 500 files for sorting (safety limit)
        if (count < 500) {
            FileInfo info;
            info.name = entry.name();
            info.isDir = entry.isDirectory();
            info.size = entry.size();
            allFiles.push_back(info);
        }
        count++;
        entry.close();
    }
    root.close();
    
    // Sort all loaded files
    std::sort(allFiles.begin(), allFiles.end(), compareFiles);
    
    // Copy only the current page to fileList
    int startIdx = page * FILES_PER_PAGE;
    int endIdx = min(startIdx + FILES_PER_PAGE, (int)allFiles.size());
    
    for (int i = startIdx; i < endIdx; i++) {
        fileList.push_back(allFiles[i]);
    }
    
    // Update total count based on what we actually found
    fileTotalCount = allFiles.size();
    if (uiSelectedIndex >= (int)fileList.size()) uiSelectedIndex = 0;
    if (fileList.empty()) uiSelectedIndex = 0;
}

String getFullPath(String filename) {
    if (currentPath.endsWith("/")) return currentPath + filename;
    return currentPath + "/" + filename;
}

bool copyFile(String src, String dst) {
    File srcFile = SD.open(src, FILE_READ);
    if (!srcFile) return false;
    File dstFile = SD.open(dst, FILE_WRITE);
    if (!dstFile) { srcFile.close(); return false; }
    uint8_t buf[512];
    while (srcFile.available()) {
        int bytesRead = srcFile.read(buf, sizeof(buf));
        dstFile.write(buf, bytesRead);
    }
    srcFile.close(); dstFile.close();
    return true;
}

bool isTextFile(String filename) {
    filename.toLowerCase();
    return filename.endsWith(".txt") || filename.endsWith(".ini") || filename.endsWith(".cfg") ||
           filename.endsWith(".log") || filename.endsWith(".json") || filename.endsWith(".md") ||
           filename.endsWith(".csv") || filename.endsWith(".xml") || filename.endsWith(".html") ||
           filename.endsWith(".htm") || filename.endsWith(".css") || filename.endsWith(".js") ||
           filename.endsWith(".py") || filename.endsWith(".cpp") || filename.endsWith(".c") ||
           filename.endsWith(".h");
}

void renderFileUI() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    sprite.fillSprite(currentThemeColor);
    
    int winX = 4, winY = 4, winW = screenW - 8, winH = screenH - 8;
    drawWindowFrame(winX, winY, winW, winH, "File Manager");
    
    int contentX = winX + 5, contentY = winY + titleBarHeight + 7;
    int contentW = winW - 10, contentH = winH - titleBarHeight - 12;
    sprite.fillRect(contentX, contentY, contentW, contentH, COL_WHITE);
    
    sprite.fillRect(contentX, contentY, contentW, 14, COL_GRAY_LIGHT);
    draw3DSunken(contentX, contentY, contentW, 14);
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(contentX + 4, contentY + 3);
    String displayPath = currentPath;
    if (displayPath.length() > 28) displayPath = "..." + displayPath.substring(displayPath.length() - 25);
    sprite.print(displayPath);
    
    if (clipboardMode != CLIP_NONE) {
        sprite.setTextColor(COL_RED, COL_GRAY_LIGHT);
        sprite.setCursor(contentX + contentW - 20, contentY + 3);
        sprite.print(clipboardMode == CLIP_COPY ? "[C]" : "[M]");
    }
    
    int listY = contentY + 16, listH = contentH - 16;
    int visibleLines = listH / uiItemHeight;
    if (uiSelectedIndex < uiScrollOffset) uiScrollOffset = uiSelectedIndex;
    if (uiSelectedIndex >= uiScrollOffset + visibleLines) uiScrollOffset = uiSelectedIndex - visibleLines + 1;

#ifdef ENABLE_SMOOTH_UI
    // Calculate and draw animated selector
    int visibleIdx = uiSelectedIndex - uiScrollOffset;
    int targetY = listY + visibleIdx * uiItemHeight;
    fileUiSelectorY = targetY;
    int selectorY = (int)fileUiSelectorY;
    
    // Draw animated selector background - width based on text, using theme color
    if (!fileList.empty()) {
        String selName = fileList[uiSelectedIndex].name;
        if (selName.length() > 18) selName = selName.substring(0, 15) + "...";
        int targetW = 22 + sprite.textWidth(selName) + 8;  // icon + text + padding
        fileUiSelectorW = targetW;  // Animate width too
        int selectorW = (int)fileUiSelectorW;  // Get current animated width
        sprite.fillRoundRect(contentX + 1, selectorY, selectorW, uiItemHeight - 1, 4, currentThemeColor);
    }
#endif

    int y = listY;
    for (int i = uiScrollOffset; i < (int)fileList.size() && i < uiScrollOffset + visibleLines; i++) {
        bool isSelected = (i == uiSelectedIndex);
#ifdef ENABLE_SMOOTH_UI
        // Check if this item overlaps with animated selector
        int selTop = selectorY;
        int selBot = selectorY + uiItemHeight - 1;
        bool inSelector = (y >= selTop - uiItemHeight && y <= selBot);
        
        if (inSelector && isSelected) {
            sprite.setTextColor(COL_WHITE, currentThemeColor);
        } else {
            sprite.setTextColor(COL_BLACK, COL_WHITE);
        }
#else
        if (isSelected) {
            String selName = fileList[i].name;
            if (selName.length() > 18) selName = selName.substring(0, 15) + "...";
            int selW = 22 + sprite.textWidth(selName) + 8;  // icon + text + padding
            sprite.fillRoundRect(contentX + 1, y, selW, uiItemHeight - 1, 4, currentThemeColor);
            sprite.setTextColor(COL_WHITE, currentThemeColor);
        } else sprite.setTextColor(COL_BLACK, COL_WHITE);
#endif
        drawFileTypeIcon(contentX + 4, y + 1, fileList[i].isDir, fileList[i].name);
        sprite.setCursor(contentX + 22, y + 5);
        String name = fileList[i].name;
        if (name.length() > 18) name = name.substring(0, 15) + "...";
        sprite.print(name);
        if (!fileList[i].isDir) {
            String sizeStr;
            if (fileList[i].size < 1024) sizeStr = String(fileList[i].size) + "B";
            else if (fileList[i].size < 1024 * 1024) sizeStr = String(fileList[i].size / 1024) + "KB";
            else sizeStr = String(fileList[i].size / (1024 * 1024)) + "MB";
            int tw = sprite.textWidth(sizeStr);
            sprite.setTextColor(COL_GRAY_MID, COL_WHITE);  // File size always on white background
            sprite.setCursor(contentX + contentW - tw - 16, y + 5);
            sprite.print(sizeStr);
        }
        y += uiItemHeight;
    }
    if (fileList.empty()) {
        sprite.setTextColor(COL_GRAY_MID, COL_WHITE);
        sprite.setCursor(contentX + 20, listY + 20);
        sprite.print("(Empty Folder)");
    }
    if ((int)fileList.size() > visibleLines) {
        int scrollBarH = listH - 4;
        int thumbH = max(10, (int)(scrollBarH * visibleLines / fileList.size()));
        int thumbY = listY + 2 + (scrollBarH - thumbH) * uiScrollOffset / max(1, (int)(fileList.size() - visibleLines));
        sprite.fillRect(contentX + contentW - 12, listY, 10, listH, COL_GRAY_LIGHT);
        draw3DSunken(contentX + contentW - 12, listY, 10, listH);
        sprite.fillRect(contentX + contentW - 11, thumbY, 8, thumbH, COL_GRAY_LIGHT);
        draw3DRaised(contentX + contentW - 11, thumbY, 8, thumbH);
    }
    sprite.fillRect(winX + 3, winY + winH - 16, winW - 6, 13, COL_GRAY_LIGHT);
    draw3DSunken(winX + 3, winY + winH - 16, winW - 6, 13);
    
    // Show item count and page info
    sprite.setTextColor(COL_BLACK, COL_GRAY_LIGHT);
    sprite.setCursor(winX + 8, winY + winH - 14);
    if (fileTotalPages > 1) {
        sprite.print(String(fileTotalCount) + " Pg" + String(fileCurrentPage + 1) + "/" + String(fileTotalPages) + " Fn+;.");
    } else {
        sprite.print(String(fileList.size()) + " items");
    }
    sprite.setCursor(winX + winW - 55, winY + winH - 14);
    sprite.print("[`]=Exit");
    sprite.pushSprite(0, 0);
}

void handleInputUI() {
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    bool fnPressed = status.fn;
    
    // Fn+P - Screenshot
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_P) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('p'))) {
        takeScreenshot();
        renderFileUI();
        delay(150);
        return;
    }
    
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_UP)) {
        if (fnPressed && fileTotalPages > 1) {
            // Fn+Up = Previous page
            if (fileCurrentPage > 0) {
                fileCurrentPage--;
                uiSelectedIndex = 0;
                uiScrollOffset = 0;
                loadFileListPage(fileCurrentPage);
                playSelectSound();
                renderFileUI();
                delay(200);
            }
        } else if (uiSelectedIndex > 0) { 
            uiSelectedIndex--; 
            playClickSound();
            renderFileUI(); 
            delay(120); 
        } 
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_DOWN)) {
        if (fnPressed && fileTotalPages > 1) {
            // Fn+Down = Next page
            if (fileCurrentPage < fileTotalPages - 1) {
                fileCurrentPage++;
                uiSelectedIndex = 0;
                uiScrollOffset = 0;
                loadFileListPage(fileCurrentPage);
                playSelectSound();
                renderFileUI();
                delay(200);
            }
        } else if (uiSelectedIndex < (int)fileList.size() - 1) { 
            uiSelectedIndex++; 
            playClickSound();
            renderFileUI(); 
            delay(120); 
        } 
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_2) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('2'))) {
        if (!fileList.empty()) { 
            playSelectSound();
            targetFilePath = getFullPath(fileList[uiSelectedIndex].name); 
            showInputDialog("New name:", fileList[uiSelectedIndex].name, ACTION_RENAME); 
        } 
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_C) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('c'))) {
        if (!fileList.empty() && !fileList[uiSelectedIndex].isDir) { 
            playSelectSound();
            clipboardPath = getFullPath(fileList[uiSelectedIndex].name); 
            clipboardMode = CLIP_COPY; 
            renderFileUI(); 
        } 
        delay(200); 
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_M) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('m'))) {
        if (!fileList.empty() && !fileList[uiSelectedIndex].isDir) { 
            playSelectSound();
            clipboardPath = getFullPath(fileList[uiSelectedIndex].name); 
            clipboardMode = CLIP_MOVE; 
            renderFileUI(); 
        } 
        delay(200); 
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_V) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('v'))) {
        if (clipboardPath.length() > 0 && clipboardMode != CLIP_NONE) {
            String filename = clipboardPath.substring(clipboardPath.lastIndexOf('/') + 1);
            String destPath = getFullPath(filename);
            if (destPath != clipboardPath) {
                playSelectSound();
                if (clipboardMode == CLIP_COPY) copyFile(clipboardPath, destPath);
                else if (clipboardMode == CLIP_MOVE) { SD.rename(clipboardPath, destPath); clipboardPath = ""; clipboardMode = CLIP_NONE; }
                loadFileList(); renderFileUI();
            }
        } 
        delay(200); 
        return;
    }
    // Fn+B - WiFi Direct file share
    if (fnPressed && M5Cardputer.Keyboard.isKeyPressed('b')) {
        if (!fileList.empty() && !fileList[uiSelectedIndex].isDir) {
            playSelectSound();
            String filepath = getFullPath(fileList[uiSelectedIndex].name);
            startWifiDirectTransfer(filepath);
        }
        delay(200); 
        return;
    }
    if (fnPressed && status.del) {
        if (!fileList.empty()) { 
            playSelectSound();
            targetFilePath = getFullPath(fileList[uiSelectedIndex].name); 
            showYesNoDialog("Delete " + fileList[uiSelectedIndex].name + "?", ACTION_DELETE, MODE_FILE_UI); 
        } 
        return;
    }
    if (status.enter) {
        if (fileList.empty()) return;
        FileInfo &selected = fileList[uiSelectedIndex];
        if (selected.isDir) { 
            playSelectSound();
            currentPath += selected.name + "/"; 
            uiSelectedIndex = 0; 
            uiScrollOffset = 0;
            fileCurrentPage = 0;  // Reset to first page
            loadFileList(); 
            renderFileUI(); 
            delay(250); 
        }
        else if (isTextFile(selected.name)) { 
            playSelectSound();
            returnMode = MODE_FILE_UI; 
            openEditorFile(getFullPath(selected.name)); 
            delay(200); 
        }
        else if (selected.name.endsWith(".wav") || selected.name.endsWith(".WAV")) {
            // Play WAV file
            playSelectSound();
            playWavFile(getFullPath(selected.name));
            renderFileUI();
            delay(200);
        }
        else if (selected.name.endsWith(".mp3") || selected.name.endsWith(".MP3")) {
            // Play MP3 file
            playSelectSound();
            playMp3File(getFullPath(selected.name));
            renderFileUI();
            delay(200);
        }
#ifdef ENABLE_SID
        else if (selected.name.endsWith(".sid") || selected.name.endsWith(".SID")) {
            // Play SID file (C64 music) with navigation support
            playSelectSound();
            int sidResult = 0;
            do {
                sidResult = playSidFile(getFullPath(fileList[uiSelectedIndex].name));
                
                if (sidResult == 1) {
                    // Next song - find next SID file
                    int startIdx = uiSelectedIndex;
                    do {
                        uiSelectedIndex = (uiSelectedIndex + 1) % fileList.size();
                        String fname = fileList[uiSelectedIndex].name;
                        fname.toLowerCase();
                        if (fname.endsWith(".sid")) break;
                    } while (uiSelectedIndex != startIdx);
                } else if (sidResult == -1) {
                    // Previous song - find previous SID file
                    int startIdx = uiSelectedIndex;
                    do {
                        uiSelectedIndex = (uiSelectedIndex - 1 + fileList.size()) % fileList.size();
                        String fname = fileList[uiSelectedIndex].name;
                        fname.toLowerCase();
                        if (fname.endsWith(".sid")) break;
                    } while (uiSelectedIndex != startIdx);
                }
            } while (sidResult != 0);
            
            // Update scroll to keep selection visible
            if (uiSelectedIndex < uiScrollOffset) {
                uiScrollOffset = uiSelectedIndex;
            } else if (uiSelectedIndex >= uiScrollOffset + 6) {
                uiScrollOffset = uiSelectedIndex - 5;
            }
            renderFileUI();
            delay(200);
        }
#endif
    }
    if (status.del && !fnPressed) {
        if (currentPath.length() > 1) {
            playBackSound();
            currentPath.remove(currentPath.length() - 1);
            int lastSlash = currentPath.lastIndexOf('/');
            currentPath = currentPath.substring(0, lastSlash + 1);
            uiSelectedIndex = 0; uiScrollOffset = 0; fileCurrentPage = 0; loadFileList(); renderFileUI(); delay(250);
        }
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) { 
        playBackSound();
        currentMode = MODE_COC_MENU;  // Go back to COC menu instead of terminal
        renderCocMenu();
        delay(250); 
    }
}

// ==========================================
//           Terminal Logic
// ==========================================

String resolvePath(String path) {
    String newPath = currentPath;
    if (path.startsWith("/")) newPath = "";
    if (!newPath.endsWith("/")) newPath += "/";
    int len = path.length();
    String temp = "";
    for (int i = 0; i < len; i++) {
        char c = path.charAt(i);
        if (c == '/') {
            if (temp == "..") { if (newPath.length() > 1) { newPath.remove(newPath.length() - 1); int lastSlash = newPath.lastIndexOf('/'); newPath = newPath.substring(0, lastSlash + 1); } }
            else if (temp != "." && temp != "") newPath += temp + "/";
            temp = "";
        } else temp += c;
    }
    if (temp == "..") { if (newPath.length() > 1) { newPath.remove(newPath.length() - 1); int lastSlash = newPath.lastIndexOf('/'); newPath = newPath.substring(0, lastSlash + 1); } }
    else if (temp != "." && temp != "") newPath += temp + "/";
    if (newPath.length() > 1 && newPath.endsWith("/")) newPath.remove(newPath.length() - 1);
    if (newPath == "") newPath = "/";
    return newPath;
}

void addToHistory(String text) {
    terminalHistory.push_back(text);
    if (terminalHistory.size() > MAX_HISTORY) terminalHistory.erase(terminalHistory.begin());
    needsRedraw = true;
}

void loadCmdHistory() {
    cmdHistory.clear();
    if (!SD.exists(cmdHistoryFile)) return;
    
    File file = SD.open(cmdHistoryFile, FILE_READ);
    if (!file) return;
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            cmdHistory.push_back(line);
        }
    }
    file.close();
    
    // Keep only last MAX_CMD_HISTORY commands
    while (cmdHistory.size() > MAX_CMD_HISTORY) {
        cmdHistory.erase(cmdHistory.begin());
    }
}

void saveCmdToHistory(String cmd) {
    cmd.trim();
    if (cmd.length() == 0) return;
    
    // Don't save duplicate of last command
    if (!cmdHistory.empty() && cmdHistory.back() == cmd) return;
    
    cmdHistory.push_back(cmd);
    
    // Keep only last MAX_CMD_HISTORY commands
    while (cmdHistory.size() > MAX_CMD_HISTORY) {
        cmdHistory.erase(cmdHistory.begin());
    }
    
    // Append to file
    File file = SD.open(cmdHistoryFile, FILE_APPEND);
    if (file) {
        file.println(cmd);
        file.close();
    }
    
    // Periodically rewrite file to keep it trim
    if (cmdHistory.size() == MAX_CMD_HISTORY) {
        File rewrite = SD.open(cmdHistoryFile, FILE_WRITE);
        if (rewrite) {
            for (auto &c : cmdHistory) {
                rewrite.println(c);
            }
            rewrite.close();
        }
    }
    
    cmdHistoryIndex = -1;  // Reset history browsing position
}

// Web server activity logging
String webLogFile = "/WebServerActivityLog.txt";

void logWebActivity(String action, String details) {
    // Create timestamp (uptime since boot)
    unsigned long secs = millis() / 1000;
    unsigned long mins = secs / 60;
    unsigned long hrs = mins / 60;
    String timestamp = String(hrs) + ":" + String(mins % 60) + ":" + String(secs % 60);
    
    // Format log entry
    String logEntry = "[WEB " + timestamp + "] " + action + ": " + details;
    
    // Add to terminal history
    addToHistory(logEntry);
    needsRedraw = true;
    
    // Append to log file
    File file = SD.open(webLogFile, FILE_APPEND);
    if (file) {
        file.println(logEntry);
        file.close();
    }
}

void renderTerminal() {
    int screenW = M5Cardputer.Display.width();
    int screenH = M5Cardputer.Display.height();
    
    sprite.fillSprite(TERM_BG_COLOR);
    
    // Draw network traffic visualization as background
    if (sdShareEnabled || WiFi.status() == WL_CONNECTED) {
        // Update traffic data periodically
        if (millis() - lastTrafficUpdate > 200) {
            lastTrafficUpdate = millis();
            
            // Calculate traffic delta
            int deltaIn = totalBytesIn - lastBytesIn;
            int deltaOut = totalBytesOut - lastBytesOut;
            lastBytesIn = totalBytesIn;
            lastBytesOut = totalBytesOut;
            
            // Store in circular buffer
            trafficIn[trafficIdx] = deltaIn;
            trafficOut[trafficIdx] = deltaOut;
            trafficIdx = (trafficIdx + 1) % TRAFFIC_HISTORY_SIZE;
        }
        
        // Draw traffic graph as background
        int graphHeight = screenH - 20;
        int maxVal = 1;  // Minimum to prevent division by zero
        
        // Find max value for scaling
        for (int i = 0; i < TRAFFIC_HISTORY_SIZE; i++) {
            if (trafficIn[i] > maxVal) maxVal = trafficIn[i];
            if (trafficOut[i] > maxVal) maxVal = trafficOut[i];
        }
        
        // Draw the graph lines
        for (int i = 0; i < TRAFFIC_HISTORY_SIZE - 1; i++) {
            int idx1 = (trafficIdx + i) % TRAFFIC_HISTORY_SIZE;
            int idx2 = (trafficIdx + i + 1) % TRAFFIC_HISTORY_SIZE;
            
            int x1 = (i * screenW) / TRAFFIC_HISTORY_SIZE;
            int x2 = ((i + 1) * screenW) / TRAFFIC_HISTORY_SIZE;
            
            // Incoming traffic (green)
            if (trafficIn[idx1] > 0 || trafficIn[idx2] > 0) {
                int y1 = graphHeight - (trafficIn[idx1] * (graphHeight - 20)) / maxVal;
                int y2 = graphHeight - (trafficIn[idx2] * (graphHeight - 20)) / maxVal;
                // Draw thicker line with slight transparency effect
                sprite.drawLine(x1, y1, x2, y2, 0x03E0);  // Dark green
                sprite.drawLine(x1, y1 + 1, x2, y2 + 1, 0x07E0);  // Bright green
            }
            
            // Outgoing traffic (red/orange)
            if (trafficOut[idx1] > 0 || trafficOut[idx2] > 0) {
                int y1 = graphHeight - (trafficOut[idx1] * (graphHeight - 20)) / maxVal;
                int y2 = graphHeight - (trafficOut[idx2] * (graphHeight - 20)) / maxVal;
                // Draw thicker line
                sprite.drawLine(x1, y1, x2, y2, 0x7800);  // Dark red
                sprite.drawLine(x1, y1 + 1, x2, y2 + 1, 0xF800);  // Bright red
            }
        }
        
        // Draw subtle grid lines
        for (int y = 20; y < graphHeight; y += 20) {
            for (int x = 0; x < screenW; x += 4) {
                sprite.drawPixel(x, y, 0x2945);  // Very dark gray dots
            }
        }
    }
    
    int lineHeight = sprite.fontHeight();
    int maxLines = screenH / lineHeight;
    int totalLines = terminalHistory.size();
    int startLine = 0;
    int linesToDraw = maxLines - 2;  // Leave room for prompt and status
    if (totalLines > linesToDraw) startLine = totalLines - linesToDraw;
    
    int drawY = 2;
    
    // Draw history lines
    for (int i = startLine; i < totalLines; i++) {
        sprite.setCursor(4, drawY);
        String line = terminalHistory[i];
        
        // Check if this is a prompt line (contains "> ")
        int promptIdx = line.indexOf("> ");
        if (promptIdx != -1 && promptIdx < 30) {
            // Draw fish-style prompt for history
            // Lambda symbol
            sprite.setTextColor(COL_CYAN, TERM_BG_COLOR);
            sprite.print("^ ");
            
            // Path in yellow
            sprite.setTextColor(COL_YELLOW, TERM_BG_COLOR);
            String path = line.substring(0, promptIdx);
            sprite.print(path);
            
            // Arrow in magenta
            sprite.setTextColor(COL_MAGENTA, TERM_BG_COLOR);
            sprite.print(" > ");
            
            // Command in white
            sprite.setTextColor(COL_WHITE, TERM_BG_COLOR);
            sprite.print(line.substring(promptIdx + 2));
        } else {
            // Regular output - check for special formatting
            if (line.startsWith("---") || line.startsWith("===")) {
                // Section headers in cyan
                sprite.setTextColor(COL_CYAN, TERM_BG_COLOR);
                sprite.print(line);
            } else if (line.startsWith("  ") && line.indexOf("  ") == 0) {
                // Indented items (like help) - command in cyan, desc in white
                sprite.setTextColor(COL_FISH_GREEN, TERM_BG_COLOR);
                int spaceIdx = line.indexOf(' ', 2);
                if (spaceIdx > 2) {
                    sprite.print(line.substring(0, spaceIdx));
                    sprite.setTextColor(COL_GRAY_LIGHT, TERM_BG_COLOR);
                    sprite.print(line.substring(spaceIdx));
                } else {
                    sprite.print(line);
                }
            } else if (line.startsWith("Error") || line.startsWith("Fail") || line.startsWith("Not ")) {
                // Errors in orange/red
                sprite.setTextColor(COL_ORANGE, TERM_BG_COLOR);
                sprite.print(line);
            } else if (line.startsWith("Connected") || line.startsWith("Created") || line.startsWith("Deleted") || line.startsWith("Uploaded")) {
                // Success messages in green
                sprite.setTextColor(COL_FISH_GREEN, TERM_BG_COLOR);
                sprite.print(line);
            } else if (line.startsWith("IP:") || line.startsWith("URL:") || line.startsWith("SSID:")) {
                // Network info - label in cyan, value in yellow
                int colonIdx = line.indexOf(':');
                sprite.setTextColor(COL_CYAN, TERM_BG_COLOR);
                sprite.print(line.substring(0, colonIdx + 1));
                sprite.setTextColor(COL_YELLOW, TERM_BG_COLOR);
                sprite.print(line.substring(colonIdx + 1));
            } else if (line.startsWith("[WEB")) {
                // Web activity logs - highlight
                sprite.setTextColor(COL_CYAN, TERM_BG_COLOR);
                sprite.print("[WEB]");
                sprite.setTextColor(COL_FISH_GREEN, TERM_BG_COLOR);
                sprite.print(line.substring(5));
            } else if (line.startsWith("[")) {
                // Signal bars in appropriate colors
                sprite.setTextColor(COL_FISH_GREEN, TERM_BG_COLOR);
                sprite.print(line);
            } else {
                // Normal output in light gray
                sprite.setTextColor(COL_GRAY_LIGHT, TERM_BG_COLOR);
                sprite.print(line);
            }
        }
        drawY += lineHeight;
    }
    
    // Draw current prompt line
    sprite.setCursor(4, drawY);
    
    // Lambda/chevron symbol in cyan
    sprite.setTextColor(COL_CYAN, TERM_BG_COLOR);
    sprite.print("^ ");
    
    // "m5" in cyan
    sprite.setTextColor(COL_CYAN, TERM_BG_COLOR);
    sprite.print("m5");
    
    // "shell" in magenta
    sprite.setTextColor(COL_MAGENTA, TERM_BG_COLOR);
    sprite.print("shell ");
    
    // "in" in gray
    sprite.setTextColor(COL_GRAY_LIGHT, TERM_BG_COLOR);
    sprite.print("in ");
    
    // Path in yellow
    sprite.setTextColor(COL_YELLOW, TERM_BG_COLOR);
    String displayPath = currentPath;
    if (displayPath.length() > 15) {
        displayPath = "~" + displayPath.substring(displayPath.length() - 14);
    }
    sprite.print(displayPath);
    sprite.print(" ");
    
    // Show WiFi/Share status inline
    if (sdShareEnabled) {
        sprite.setTextColor(COL_FISH_GREEN, TERM_BG_COLOR);
        sprite.print("[web] ");
    } else if (WiFi.status() == WL_CONNECTED) {
        sprite.setTextColor(COL_CYAN, TERM_BG_COLOR);
        sprite.print("[wifi] ");
    }
    
    // New line for input
    drawY += lineHeight;
    sprite.setCursor(4, drawY);
    
    // Prompt arrow in magenta
    sprite.setTextColor(COL_MAGENTA, TERM_BG_COLOR);
    sprite.print("> ");
    
    // User input in white
    sprite.setTextColor(COL_WHITE, TERM_BG_COLOR);
    sprite.print(inputBuffer);
    
    // Cursor
    if (cursorVisible) {
        int cursorX = sprite.getCursorX();
        sprite.fillRect(cursorX, drawY, 6, lineHeight - 1, CURSOR_COLOR);
    }
    
    // Right side info - battery and time
    // Battery
    int battX = screenW - 55;
    int battY = 2;
    
    // Update battery status
    if (millis() - lastBatteryUpdate > 2000) {
        lastBatteryUpdate = millis();
        batteryLevel = M5Cardputer.Power.getBatteryLevel();
        int vbus = M5Cardputer.Power.getVBUSVoltage();
        batteryCharging = (vbus > 4000);
    }
    
    if (batteryCharging && millis() - lastChargingAnim > 300) {
        lastChargingAnim = millis();
        chargingAnimFrame = (chargingAnimFrame + 1) % 4;
    }
    
    // Battery icon
    sprite.drawRect(battX, battY, 18, 8, COL_GRAY_LIGHT);
    sprite.fillRect(battX + 18, battY + 2, 2, 4, COL_GRAY_LIGHT);
    
    int fillWidth;
    if (batteryCharging) {
        fillWidth = (chargingAnimFrame + 1) * 4;
        if (fillWidth > 16) fillWidth = 16;
    } else {
        fillWidth = (batteryLevel * 16) / 100;
    }
    
    uint16_t battColor;
    if (batteryCharging) battColor = COL_YELLOW;
    else if (batteryLevel > 50) battColor = COL_FISH_GREEN;
    else if (batteryLevel > 20) battColor = COL_YELLOW;
    else battColor = COL_RED;
    
    if (fillWidth > 0) {
        sprite.fillRect(battX + 1, battY + 1, fillWidth, 6, battColor);
    }
    
    // Battery percentage
    sprite.setTextColor(COL_GRAY_LIGHT, TERM_BG_COLOR);
    sprite.setCursor(battX + 22, battY);
    if (batteryCharging) sprite.print("+");
    else sprite.print(String(batteryLevel) + "%");
    
    // Last command duration (if any)
    if (lastCmdDuration > 0) {
        sprite.setTextColor(COL_YELLOW, TERM_BG_COLOR);
        sprite.setCursor(screenW - 55, drawY);
        if (lastCmdDuration < 1000) {
            sprite.print(String(lastCmdDuration) + "ms");
        } else {
            float secs = lastCmdDuration / 1000.0;
            sprite.print(String(secs, 2) + "s");
        }
    }
    
    // Network traffic legend (when active)
    if (sdShareEnabled || WiFi.status() == WL_CONNECTED) {
        sprite.setTextColor(0x07E0, TERM_BG_COLOR);  // Green
        sprite.setCursor(screenW - 55, 12);
        sprite.print("IN");
        sprite.setTextColor(0xF800, TERM_BG_COLOR);  // Red
        sprite.setCursor(screenW - 35, 12);
        sprite.print("OUT");
    }
    
    // Date and Time display at bottom center
    if (timeSynced) {
        String dateStr = getDateString();
        String timeStr = getTimeString();
        String dateTimeStr = dateStr + " " + timeStr;
        
        int textWidth = sprite.textWidth(dateTimeStr);
        int dateTimeX = (screenW - textWidth) / 2;
        int dateTimeY = screenH - 10;  // Bottom of screen
        
        // Draw semi-transparent background for readability
        sprite.fillRect(dateTimeX - 4, dateTimeY - 1, textWidth + 8, 10, TERM_BG_COLOR);
        
        // Draw date in cyan
        sprite.setTextColor(COL_CYAN, TERM_BG_COLOR);
        sprite.setCursor(dateTimeX, dateTimeY);
        sprite.print(dateStr);
        
        // Draw time in yellow
        sprite.setTextColor(COL_YELLOW, TERM_BG_COLOR);
        sprite.print(" " + timeStr);
    }
    
    sprite.pushSprite(0, 0);
    needsRedraw = false;
}

void printDirectory(File dir) {
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        String entryName = entry.name();
        if (entry.isDirectory()) entryName += "/";
        addToHistory("  " + entryName);
        entry.close();
    }
}

// ==========================================
//     Tab Completion
// ==========================================

// List of all commands for tab completion
const char* commandList[] = {
    "ls", "cd", "mkdir", "rm", "cat", "touch", "echo",
    "cocui", "coc", "fileui", "fm", "filetxt", "synth",
    "wifi", "sd-share", "sdqr", "ip", "ping", "httpget", "wget", "weather",
    "time", "disk", "df", "clear", "cls", "history", "config", "help",
    "sysinfo", "reboot", "restart", "shutdown", "shutoff", "poweroff", "sleep", "hibernate",
    "brightness", "screenshot", "find", "hexview", "hv", "calc",
    "serial", "sound",
#ifdef ENABLE_IR_FEATURES
    "ir",
#endif
#ifdef ENABLE_BT_KEYBOARD
    "btkey",
#endif
    nullptr
};

String tabComplete(String input) {
    input.trim();
    if (input.length() == 0) return input;
    
    // Find the last space to determine if we're completing a command or argument
    int lastSpace = input.lastIndexOf(' ');
    String prefix, toComplete;
    bool isCommand = (lastSpace == -1);
    
    if (isCommand) {
        prefix = "";
        toComplete = input;
    } else {
        prefix = input.substring(0, lastSpace + 1);
        toComplete = input.substring(lastSpace + 1);
    }
    
    std::vector<String> matches;
    
    if (isCommand) {
        // Complete command names
        for (int i = 0; commandList[i] != nullptr; i++) {
            String cmd = commandList[i];
            if (cmd.startsWith(toComplete)) {
                matches.push_back(cmd);
            }
        }
    } else {
        // Complete file/directory names
        String pathPart = "";
        String namePart = toComplete;
        
        // Check if toComplete has a path component
        int lastSlash = toComplete.lastIndexOf('/');
        if (lastSlash != -1) {
            pathPart = toComplete.substring(0, lastSlash + 1);
            namePart = toComplete.substring(lastSlash + 1);
        }
        
        // Resolve the directory to search
        String searchDir;
        if (pathPart.startsWith("/")) {
            searchDir = pathPart;
        } else if (pathPart.length() > 0) {
            searchDir = currentPath;
            if (!searchDir.endsWith("/")) searchDir += "/";
            searchDir += pathPart;
        } else {
            searchDir = currentPath;
        }
        
        // Remove trailing slash for opening
        if (searchDir.length() > 1 && searchDir.endsWith("/")) {
            searchDir = searchDir.substring(0, searchDir.length() - 1);
        }
        
        File dir = SD.open(searchDir);
        if (dir && dir.isDirectory()) {
            while (true) {
                File entry = dir.openNextFile();
                if (!entry) break;
                
                String name = entry.name();
                bool isDir = entry.isDirectory();
                entry.close();
                
                // Check if name starts with what we're completing
                if (namePart.length() == 0 || name.startsWith(namePart)) {
                    String completion = pathPart + name;
                    if (isDir) completion += "/";
                    matches.push_back(completion);
                }
            }
            dir.close();
        }
    }
    
    if (matches.size() == 0) {
        // No matches - beep and return unchanged
        M5Cardputer.Speaker.tone(400, 50);
        return input;
    } else if (matches.size() == 1) {
        // Single match - complete it
        M5Cardputer.Speaker.tone(1000, 30);
        String result = prefix + matches[0];
        // Add space after command completion (but not after directory)
        if (isCommand) result += " ";
        return result;
    } else {
        // Multiple matches - find common prefix and show options
        M5Cardputer.Speaker.tone(800, 30);
        delay(50);
        M5Cardputer.Speaker.tone(800, 30);
        
        // Find longest common prefix
        String common = matches[0];
        for (int i = 1; i < (int)matches.size(); i++) {
            int j = 0;
            while (j < (int)common.length() && j < (int)matches[i].length() && 
                   common.charAt(j) == matches[i].charAt(j)) {
                j++;
            }
            common = common.substring(0, j);
        }
        
        // Show matches in terminal history
        String matchList = "";
        for (int i = 0; i < (int)matches.size() && i < 8; i++) {
            if (i > 0) matchList += "  ";
            matchList += matches[i];
        }
        if (matches.size() > 8) matchList += " ...";
        addToHistory(matchList);
        needsRedraw = true;
        
        // Return with common prefix
        return prefix + common;
    }
}

void executeCommand(String cmdLine) {
    cmdLine.trim();
    if (cmdLine.length() == 0) return;
    
    // Start timing
    cmdStartTime = millis();
    
    // Save command to history
    saveCmdToHistory(cmdLine);
    
    addToHistory(currentPath + "> " + cmdLine);
    
    int spaceIdx = cmdLine.indexOf(' ');
    String cmd = (spaceIdx == -1) ? cmdLine : cmdLine.substring(0, spaceIdx);
    String arg = (spaceIdx == -1) ? "" : cmdLine.substring(spaceIdx + 1);
    arg.trim();
    
    if (cmd == "ls") {
        File dir = SD.open(currentPath);
        if (dir && dir.isDirectory()) printDirectory(dir);
        else addToHistory("Error: Cannot open directory");
        if (dir) dir.close();
    }
    else if (cmd == "cd") {
        String target = resolvePath(arg);
        File dir = SD.open(target);
        if (dir && dir.isDirectory()) { currentPath = target; addToHistory("Changed to: " + currentPath); }
        else addToHistory("Not a directory");
        if (dir) dir.close();
    }
    else if (cmd == "cocui" || cmd == "coc") {
        // COC Menu - Main UI system
        cocMenuIndex = 0;
        currentMode = MODE_COC_MENU;
        renderCocMenu();
    }
    else if (cmd == "fileui" || cmd == "fm") {
        // Direct access to file manager (backward compatible)
        currentPath = "/"; uiSelectedIndex = 0; uiScrollOffset = 0; fileCurrentPage = 0;
        currentMode = MODE_FILE_UI; loadFileList(); renderFileUI();
    }
    else if (cmd == "filetxt") {
        returnMode = MODE_TERMINAL;
        if (arg.length() > 0) {
            String target = resolvePath(arg);
            if (target.endsWith("/") && target.length() > 1) target.remove(target.length() - 1);
            openEditorFile(target);
        } else openEditorNewFile();
    }
    else if (cmd == "synth") {
        // Grid Synth - PixiTracker-style sequencer
        if (arg == "" || arg == "run" || arg == "new") {
            synthInit();
            currentMode = MODE_SYNTH;
            renderSynth();
        }
        else if (arg.startsWith("load ")) {
            String filename = arg.substring(5);
            filename.trim();
            synthInit();
            synthLoad(filename);
            currentMode = MODE_SYNTH;
            renderSynth();
        }
        else if (arg.startsWith("save ")) {
            String filename = arg.substring(5);
            filename.trim();
            synthSave(filename);
            addToHistory("Saved: " + filename);
        }
        else {
            addToHistory("SYNTH - Grid Sequencer");
            addToHistory("");
            addToHistory("Usage:");
            addToHistory("  synth run    Open synth");
            addToHistory("  synth load <file>");
            addToHistory("  synth save <file>");
            addToHistory("");
            addToHistory("Grid Controls:");
            addToHistory("  ;/./,// Navigate");
            addToHistory("  1-8     Place colored note");
            addToHistory("  0/Del   Clear cell");
            addToHistory("  Space   Play/Stop");
            addToHistory("  TAB     Open sound menu");
            addToHistory("");
            addToHistory("Sound Menu:");
            addToHistory("  ;/.     Select color");
            addToHistory("  +/-     Change sound type");
            addToHistory("");
            addToHistory("Fn+=/Fn+- BPM up/down");
            addToHistory("Fn+S Save  Fn+C Clear");
        }
    }
    else if (cmd == "render") {
        // Display test image centered on screen
        int screenW = M5Cardputer.Display.width();
        int screenH = M5Cardputer.Display.height();
        int imgX = (screenW - Mic1_width) / 2;
        int imgY = (screenH - Mic1_height) / 2;
        
        sprite.fillSprite(COL_BLACK);
        // Icons are already RGB565_SWAPPED from png_to_cpp_array.py
        sprite.pushImage(imgX, imgY, Mic1_width, Mic1_height, Mic1);
        sprite.pushSprite(0, 0);
        
        // Wait for ESC to return
        addToHistory("Rendering image... Press ESC");
        while (true) {
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_ESC)) {
                break;
            }
            delay(10);
        }
        needsRedraw = true;
    }
    else if (cmd == "wifi") {
        // wifi list or wifi -l - scan for networks
        if (arg == "list" || arg == "-l") {
            addToHistory("Scanning WiFi networks...");
            needsRedraw = true; renderTerminal();
            
            int numNetworks = WiFi.scanNetworks();
            
            if (numNetworks == 0) {
                addToHistory("No networks found");
            } else {
                // Create array to sort by signal strength
                struct NetworkInfo {
                    String ssid;
                    int rssi;
                    bool encrypted;
                };
                std::vector<NetworkInfo> networks;
                
                for (int i = 0; i < numNetworks; i++) {
                    NetworkInfo net;
                    net.ssid = WiFi.SSID(i);
                    net.rssi = WiFi.RSSI(i);
                    net.encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
                    networks.push_back(net);
                }
                
                // Sort by signal strength (weakest first, strongest last - better for small screens)
                std::sort(networks.begin(), networks.end(), [](const NetworkInfo &a, const NetworkInfo &b) {
                    return a.rssi < b.rssi;
                });
                
                addToHistory("Found " + String(numNetworks) + " networks:");
                addToHistory("");
                
                for (auto &net : networks) {
                    // Create signal strength bar
                    String signalBar = "";
                    int bars = 0;
                    if (net.rssi >= -50) bars = 4;
                    else if (net.rssi >= -60) bars = 3;
                    else if (net.rssi >= -70) bars = 2;
                    else if (net.rssi >= -80) bars = 1;
                    else bars = 0;
                    
                    // Build visual signal indicator
                    for (int b = 0; b < 4; b++) {
                        signalBar += (b < bars) ? "|" : ".";
                    }
                    
                    // Lock icon for encrypted networks
                    String lockIcon = net.encrypted ? "*" : " ";
                    
                    // Format: [||||] -45dBm * NetworkName
                    String line = "[" + signalBar + "] " + String(net.rssi) + "dBm " + lockIcon + " " + net.ssid;
                    addToHistory(line);
                }
                
                addToHistory("");
                addToHistory("* = Password protected");
            }
            
            WiFi.scanDelete();  // Clean up scan results
        }
        // wifi (no args) - show status
        else if (arg.length() == 0) {
            if (WiFi.status() == WL_CONNECTED) {
                addToHistory("Status: Connected");
                addToHistory("SSID: " + WiFi.SSID());
                addToHistory("IP: " + WiFi.localIP().toString());
                addToHistory("Signal: " + String(WiFi.RSSI()) + " dBm");
            } else {
                addToHistory("Status: Not connected");
                addToHistory("");
                addToHistory("Usage:");
                addToHistory("  wifi list    - Scan networks");
                addToHistory("  wifi -l      - Scan networks");
                addToHistory("  wifi off     - Disconnect");
                addToHistory("  wifi <ssid> <pass>");
            }
        }
        // wifi off - disconnect
        else if (arg == "off" || arg == "disconnect") {
            if (WiFi.status() == WL_CONNECTED) {
                // Stop web server if running
                if (sdShareEnabled) {
                    stopWebServer();
                    addToHistory("SD Share stopped");
                }
                WiFi.disconnect();
                addToHistory("WiFi disconnected");
            } else {
                addToHistory("WiFi not connected");
            }
        }
        // wifi <ssid> <password> - connect
        else {
            int firstSpace = arg.indexOf(' ');
            if (firstSpace == -1) {
                // Single argument - might be open network or error
                addToHistory("Connecting to " + arg + "...");
                needsRedraw = true; renderTerminal();
                WiFi.begin(arg.c_str());
                int timeout = 20;
                while (WiFi.status() != WL_CONNECTED && timeout > 0) { delay(500); timeout--; }
                if (WiFi.status() == WL_CONNECTED) {
                    addToHistory("Connected!");
                    addToHistory("IP: " + WiFi.localIP().toString());
                    // Auto-sync time
                    addToHistory("Syncing time...");
                    needsRedraw = true; renderTerminal();
                    if (syncTimeFromNTP()) {
                        addToHistory("Time: " + getTimeString() + " " + getTimezoneString());
                    }
                } else { 
                    addToHistory("Connection failed!");
                    addToHistory("If password needed:");
                    addToHistory("  wifi <ssid> <password>");
                    WiFi.disconnect(); 
                }
            } else {
                wifiSSID = arg.substring(0, firstSpace);
                wifiPassword = arg.substring(firstSpace + 1);
                wifiPassword.trim();
                addToHistory("Connecting to " + wifiSSID + "...");
                needsRedraw = true; renderTerminal();
                WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
                int timeout = 20;
                while (WiFi.status() != WL_CONNECTED && timeout > 0) { delay(500); timeout--; }
                if (WiFi.status() == WL_CONNECTED) {
                    addToHistory("Connected!");
                    addToHistory("IP: " + WiFi.localIP().toString());
                    // Auto-sync time
                    addToHistory("Syncing time...");
                    needsRedraw = true; renderTerminal();
                    if (syncTimeFromNTP()) {
                        addToHistory("Time: " + getTimeString() + " " + getTimezoneString());
                    }
                } else { addToHistory("Connection failed!"); WiFi.disconnect(); }
            }
        }
    }
    else if (cmd == "sd-share") {
        if (arg == "on") {
            if (WiFi.status() != WL_CONNECTED) {
                addToHistory("WiFi not connected!");
                addToHistory("Use: wifi <ssid> <password>");
            } else {
                startWebServer();
                addToHistory("SD Share: ON");
                addToHistory("URL: http://" + WiFi.localIP().toString());
            }
        } else if (arg == "off") {
            stopWebServer();
            addToHistory("SD Share: OFF");
        } else {
            addToHistory("Usage: sd-share <on/off>");
            addToHistory("Status: " + String(sdShareEnabled ? "ON" : "OFF"));
            if (sdShareEnabled && WiFi.status() == WL_CONNECTED)
                addToHistory("URL: http://" + WiFi.localIP().toString());
        }
    }
    else if (cmd == "sdqr") {
        if (!sdShareEnabled) {
            addToHistory("SD Share not running!");
            addToHistory("Use 'sd-share on' first");
        } else if (WiFi.status() != WL_CONNECTED) {
            addToHistory("WiFi not connected!");
        } else {
            currentMode = MODE_QR_DISPLAY;
            renderQRCode();
        }
    }
    else if (cmd == "mkdir") {
        String target = resolvePath(arg);
        if (SD.mkdir(target)) addToHistory("Created: " + target);
        else addToHistory("Failed: " + target);
    }
    else if (cmd == "rm") {
        String target = resolvePath(arg);
        if (SD.exists(target)) {
            if (SD.remove(target)) addToHistory("Deleted file");
            else if (SD.rmdir(target)) addToHistory("Deleted directory");
            else addToHistory("Delete failed");
        } else addToHistory("Not found");
    }
    else if (cmd == "cat") {
        String target = resolvePath(arg);
        if (SD.exists(target)) {
            File f = SD.open(target, FILE_READ);
            if (f && !f.isDirectory()) {
                while (f.available()) { String line = f.readStringUntil('\n'); line.trim(); addToHistory(line); }
                f.close();
            } else addToHistory("Cannot read file");
        } else addToHistory("File not found");
    }
    else if (cmd == "clear" || cmd == "cls") { terminalHistory.clear(); }
    else if (cmd == "history") {
        if (arg == "clear" || arg == "-c") {
            cmdHistory.clear();
            SD.remove(cmdHistoryFile);
            addToHistory("Command history cleared");
        } else {
            addToHistory("Last " + String(cmdHistory.size()) + " commands:");
            int start = max(0, (int)cmdHistory.size() - 10);
            for (int i = start; i < (int)cmdHistory.size(); i++) {
                addToHistory("  " + String(i + 1) + ": " + cmdHistory[i]);
            }
            addToHistory("");
            addToHistory("Use Fn+;/. to scroll");
            addToHistory("'history clear' to erase");
        }
    }
    else if (cmd == "ip") {
        if (WiFi.status() == WL_CONNECTED) {
            addToHistory("IP: " + WiFi.localIP().toString());
            addToHistory("SSID: " + WiFi.SSID());
        } else addToHistory("WiFi not connected");
    }
    else if (cmd == "disk" || cmd == "df") {
        // Get SD card size info
        uint64_t totalBytes = SD.totalBytes();
        uint64_t usedBytes = SD.usedBytes();
        uint64_t freeBytes = totalBytes - usedBytes;
        int usagePercent = (totalBytes > 0) ? (int)((usedBytes * 100) / totalBytes) : 0;
        
        // Format sizes
        String totalStr, usedStr, freeStr;
        if (totalBytes >= 1073741824) {
            totalStr = String((float)totalBytes / 1073741824.0, 2) + " GB";
            usedStr = String((float)usedBytes / 1073741824.0, 2) + " GB";
            freeStr = String((float)freeBytes / 1073741824.0, 2) + " GB";
        } else {
            totalStr = String(totalBytes / 1048576) + " MB";
            usedStr = String(usedBytes / 1048576) + " MB";
            freeStr = String(freeBytes / 1048576) + " MB";
        }
        
        // Create usage bar
        String usageBar = "[";
        int barLen = 20;
        int filledLen = (usagePercent * barLen) / 100;
        for (int i = 0; i < barLen; i++) {
            if (i < filledLen) usageBar += "#";
            else usageBar += "-";
        }
        usageBar += "]";
        
        addToHistory("--- SD Card Usage ---");
        addToHistory("Total:  " + totalStr);
        addToHistory("Used:   " + usedStr + " (" + String(usagePercent) + "%)");
        addToHistory("Free:   " + freeStr);
        addToHistory(usageBar + " " + String(usagePercent) + "%");
        
        // Log to web activity log
        String logMsg = "Total:" + totalStr + " Used:" + usedStr + " Free:" + freeStr;
        logWebActivity("DISK", logMsg);
    }
    else if (cmd == "config") {
        if (arg.length() == 0) {
            addToHistory("--- Current Configuration ---");
            if (terminalHideTimeoutMs == 0) {
                addToHistory("  terminal_hide: disabled");
            } else {
                addToHistory("  terminal_hide: " + String(terminalHideTimeoutMs / 1000) + "s");
            }
            if (screenOffTimeoutMs == 0) {
                addToHistory("  screen_off: disabled");
            } else {
                addToHistory("  screen_off: " + String(screenOffTimeoutMs / 1000) + "s");
            }
            addToHistory("  brightness: " + String(configBrightness) + "%");
            addToHistory("  timezone: " + getTimezoneString());
            addToHistory("");
            addToHistory("'config edit' to modify");
            addToHistory("'config reset' for defaults");
            addToHistory("Reboot to apply changes.");
        } else if (arg == "edit") {
            returnMode = MODE_TERMINAL;
            openEditorFile(configFile);
        } else if (arg == "reset") {
            SD.remove(configFile);
            createDefaultConfig();
            addToHistory("Config reset to defaults.");
            addToHistory("Reboot to apply changes.");
        }
    }
    else if (cmd == "time") {
        if (arg == "-u") {
            // Update time from NTP
            if (WiFi.status() != WL_CONNECTED) {
                addToHistory("Error: WiFi not connected!");
                addToHistory("Connect with: wifi <ssid> <pass>");
            } else {
                addToHistory("Syncing time from NTP...");
                needsRedraw = true; renderTerminal();
                if (syncTimeFromNTP()) {
                    addToHistory("Time synced successfully!");
                    addToHistory("Date: " + getDateString());
                    addToHistory("Time: " + getTimeString());
                    addToHistory("Zone: " + getTimezoneString());
                } else {
                    addToHistory("Failed to sync time!");
                }
            }
        }
        else if (arg == "-z") {
            // Show timezone selection menu
            addToHistory("--- Select Timezone (GMT offset) ---");
            addToHistory("Common US timezones:");
            addToHistory("  -5  EST (Eastern)");
            addToHistory("  -6  CST (Central)");
            addToHistory("  -7  MST (Mountain)");
            addToHistory("  -8  PST (Pacific)");
            addToHistory("  -9  AKST (Alaska)");
            addToHistory("  -10 HST (Hawaii)");
            addToHistory("");
            addToHistory("Usage: time -z <offset>");
            addToHistory("Example: time -z -6");
        }
        else if (arg.startsWith("-z ")) {
            // Set timezone and sync
            String tzStr = arg.substring(3);
            tzStr.trim();
            int tz = tzStr.toInt();
            if (tz >= -12 && tz <= 14) {
                configTimezoneOffset = tz;
                saveUserConfig();  // Save timezone to config file
                addToHistory("Timezone set to " + getTimezoneString());
                addToHistory("(Saved to config)");
                
                // Auto-sync time after setting timezone
                if (WiFi.status() == WL_CONNECTED) {
                    addToHistory("Syncing time...");
                    needsRedraw = true; renderTerminal();
                    if (syncTimeFromNTP()) {
                        addToHistory("Time synced!");
                        addToHistory("Date: " + getDateString());
                        addToHistory("Time: " + getTimeString());
                    } else {
                        addToHistory("Sync failed!");
                    }
                } else {
                    addToHistory("WiFi not connected - time not synced");
                }
            } else {
                addToHistory("Invalid timezone! Use -12 to +14");
            }
        }
        else if (arg.length() == 0) {
            // Show current time status
            if (timeSynced) {
                addToHistory("Date: " + getDateString());
                addToHistory("Time: " + getTimeString());
                addToHistory("Zone: " + getTimezoneString());
            } else {
                addToHistory("Time not synced!");
                addToHistory("Use 'time -u' to sync from internet");
                addToHistory("Use 'time -z' to set timezone");
            }
        }
        else {
            addToHistory("Usage:");
            addToHistory("  time      Show current time");
            addToHistory("  time -u   Update from internet");
            addToHistory("  time -z   Show timezone list");
            addToHistory("  time -z <offset>  Set timezone");
        }
    }
    // ========== NEW COMMANDS v4.0 ==========
    else if (cmd == "sysinfo") {
        addToHistory("--- System Information ---");
        addToHistory("Chip: " + String(ESP.getChipModel()));
        addToHistory("Cores: " + String(ESP.getChipCores()));
        addToHistory("CPU Freq: " + String(ESP.getCpuFreqMHz()) + " MHz");
        addToHistory("Free Heap: " + String(ESP.getFreeHeap() / 1024) + " KB");
        addToHistory("Total Heap: " + String(ESP.getHeapSize() / 1024) + " KB");
        addToHistory("Flash: " + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB");
        
        // Uptime calculation
        unsigned long uptimeSec = (millis() - bootTime) / 1000;
        unsigned long days = uptimeSec / 86400;
        unsigned long hours = (uptimeSec % 86400) / 3600;
        unsigned long mins = (uptimeSec % 3600) / 60;
        unsigned long secs = uptimeSec % 60;
        String uptimeStr = "";
        if (days > 0) uptimeStr += String(days) + "d ";
        if (hours > 0 || days > 0) uptimeStr += String(hours) + "h ";
        uptimeStr += String(mins) + "m " + String(secs) + "s";
        addToHistory("Uptime: " + uptimeStr);
        
        // WiFi info
        if (WiFi.status() == WL_CONNECTED) {
            addToHistory("WiFi: " + WiFi.SSID() + " (" + String(WiFi.RSSI()) + " dBm)");
            addToHistory("IP: " + WiFi.localIP().toString());
        } else {
            addToHistory("WiFi: Not connected");
        }
        
        // Battery
        int batt = M5Cardputer.Power.getBatteryLevel();
        bool charging = M5Cardputer.Power.getVBUSVoltage() > 4000;
        addToHistory("Battery: " + String(batt) + "%" + (charging ? " (charging)" : ""));
    }
    else if (cmd == "reboot" || cmd == "restart") {
        addToHistory("Rebooting in 2 seconds...");
        needsRedraw = true;
        renderTerminal();
        delay(2000);
        ESP.restart();
    }
    else if (cmd == "shutdown" || cmd == "shutoff" || cmd == "poweroff") {
        addToHistory("Shutting down...");
        needsRedraw = true;
        renderTerminal();
        delay(1000);
        M5Cardputer.Display.setBrightness(0);
        M5Cardputer.Power.powerOff();
    }
    else if (cmd == "sleep" || cmd == "hibernate") {
        addToHistory("Entering deep sleep...");
        addToHistory("Press reset button to wake");
        needsRedraw = true;
        renderTerminal();
        delay(1500);
        M5Cardputer.Display.setBrightness(0);
        esp_deep_sleep_start();
    }
    else if (cmd == "brightness") {
        if (arg.length() == 0) {
            addToHistory("Brightness: " + String(configBrightness));
            addToHistory("Usage: brightness <0-100>");
        } else {
            int val = arg.toInt();
            if (val < 0) val = 0;
            if (val > 100) val = 100;
            configBrightness = val;
            int hwBrightness = map(val, 0, 100, 0, 255);
            M5Cardputer.Display.setBrightness(hwBrightness);
            saveUserConfig();
            addToHistory("Brightness set to " + String(val));
        }
    }
    else if (cmd == "sound") {
        if (arg.length() == 0) {
            addToHistory("Volume: " + String(globalVolume) + "%" + (globalMuted ? " [MUTED]" : ""));
            addToHistory("Usage: sound -m      (toggle mute)");
            addToHistory("       sound -v <0-100>  (set volume)");
        } else if (arg == "-m") {
            globalMuted = !globalMuted;
            applyVolume();
            addToHistory(globalMuted ? "Sound muted" : "Sound unmuted");
            if (!globalMuted) playMenuSound(1000, 50);
        } else if (arg.startsWith("-v ") || arg.startsWith("-v")) {
            String volStr = arg.substring(2);
            volStr.trim();
            if (volStr.length() == 0) {
                addToHistory("Volume: " + String(globalVolume) + "%");
            } else {
                int val = volStr.toInt();
                if (val < 0) val = 0;
                if (val > 100) val = 100;
                globalVolume = val;
                applyVolume();
                addToHistory("Volume set to " + String(globalVolume) + "%");
                if (!globalMuted && globalVolume > 0) playMenuSound(1000, 50);
            }
        } else {
            addToHistory("Usage: sound -m | -v <0-100>");
        }
    }
#ifdef ENABLE_SID
    else if (cmd == "sidplay") {
        if (arg.length() == 0) {
            addToHistory("SIDPLAY - C64 SID Music Player");
            addToHistory("");
            addToHistory("Usage: sidplay <file.sid>");
            addToHistory("");
            addToHistory("Controls:");
            addToHistory("  ESC     Stop and return");
            addToHistory("  [/]     Volume down/up");
            addToHistory("  ,/.     Previous/Next song");
        } else {
            String target = resolvePath(arg);
            if (!target.endsWith(".sid") && !target.endsWith(".SID")) {
                target += ".sid";
            }
            if (SD.exists(target)) {
                playSidTerminal(target);
                needsRedraw = true;
                renderTerminal();
            } else {
                addToHistory("File not found: " + target);
            }
        }
    }
#endif
    else if (cmd == "touch") {
        if (arg.length() == 0) {
            addToHistory("Usage: touch <filename>");
        } else {
            String target = resolvePath(arg);
            if (SD.exists(target)) {
                addToHistory("File already exists: " + arg);
            } else {
                File f = SD.open(target, FILE_WRITE);
                if (f) {
                    f.close();
                    addToHistory("Created: " + arg);
                } else {
                    addToHistory("Error creating file");
                }
            }
        }
    }
    else if (cmd == "echo") {
        // Parse echo "text" > filename or echo "text" >> filename
        int gtIdx = arg.indexOf('>');
        if (gtIdx == -1) {
            // Just echo to terminal
            addToHistory(arg);
        } else {
            bool append = (arg.charAt(gtIdx + 1) == '>');
            String text = arg.substring(0, gtIdx);
            String filename = arg.substring(gtIdx + (append ? 2 : 1));
            text.trim();
            filename.trim();
            
            // Remove quotes from text
            if (text.startsWith("\"") && text.endsWith("\"")) {
                text = text.substring(1, text.length() - 1);
            }
            
            String target = resolvePath(filename);
            File f = SD.open(target, append ? FILE_APPEND : FILE_WRITE);
            if (f) {
                f.println(text);
                f.close();
                addToHistory((append ? "Appended to: " : "Wrote to: ") + filename);
            } else {
                addToHistory("Error writing file");
            }
        }
    }
    else if (cmd == "ping") {
        if (arg.length() == 0) {
            addToHistory("Usage: ping <host>");
        } else if (WiFi.status() != WL_CONNECTED) {
            addToHistory("WiFi not connected");
        } else {
            addToHistory("Pinging " + arg + "...");
            needsRedraw = true;
            renderTerminal();
            
            // Resolve hostname
            IPAddress ip;
            if (WiFi.hostByName(arg.c_str(), ip)) {
                addToHistory("Host: " + ip.toString());
                
                // Simple ping using TCP connect (not true ICMP, but works without raw sockets)
                WiFiClient client;
                unsigned long startTime = millis();
                bool success = false;
                
                // Try common ports
                int ports[] = {80, 443, 22, 21};
                for (int i = 0; i < 4 && !success; i++) {
                    client.setTimeout(2000);
                    if (client.connect(ip, ports[i])) {
                        unsigned long elapsed = millis() - startTime;
                        addToHistory("Reply from " + ip.toString() + ": time=" + String(elapsed) + "ms");
                        client.stop();
                        success = true;
                    }
                }
                
                if (!success) {
                    addToHistory("Host unreachable (no open ports)");
                }
            } else {
                addToHistory("Could not resolve host");
            }
        }
    }
    else if (cmd == "find") {
        if (arg.length() == 0) {
            addToHistory("Usage: find <pattern>");
            addToHistory("Searches for files matching pattern");
        } else {
            addToHistory("Searching for: " + arg);
            needsRedraw = true;
            renderTerminal();
            
            int found = 0;
            std::function<void(String)> searchDir = [&](String path) {
                File dir = SD.open(path);
                if (!dir || !dir.isDirectory()) return;
                
                while (true) {
                    File entry = dir.openNextFile();
                    if (!entry) break;
                    
                    String name = entry.name();
                    String fullPath = path;
                    if (!fullPath.endsWith("/")) fullPath += "/";
                    fullPath += name;
                    
                    // Check if name contains pattern (case insensitive)
                    String nameLower = name;
                    String argLower = arg;
                    nameLower.toLowerCase();
                    argLower.toLowerCase();
                    
                    if (nameLower.indexOf(argLower) != -1) {
                        addToHistory("  " + fullPath);
                        found++;
                    }
                    
                    if (entry.isDirectory()) {
                        searchDir(fullPath);
                    }
                    entry.close();
                }
                dir.close();
            };
            
            searchDir("/");
            addToHistory("Found " + String(found) + " file(s)");
        }
    }
    else if (cmd == "hexview" || cmd == "hv") {
        if (arg.length() == 0) {
            addToHistory("Usage: hexview <filename>");
        } else {
            String target = resolvePath(arg);
            if (!SD.exists(target)) {
                addToHistory("File not found: " + arg);
            } else {
                File f = SD.open(target, FILE_READ);
                if (f && !f.isDirectory()) {
                    hexViewFilePath = target;
                    hexViewFileSize = f.size();
                    hexViewOffset = 0;
                    f.close();
                    currentMode = MODE_HEX_VIEW;
                    renderHexView();
                } else {
                    addToHistory("Cannot open file");
                    if (f) f.close();
                }
            }
        }
    }
    else if (cmd == "calc") {
        if (arg.length() == 0) {
            addToHistory("Usage: calc <expression>");
            addToHistory("Examples: calc 2+2, calc 10*5");
        } else {
            // Simple expression evaluator
            double result = 0;
            bool error = false;
            
            // Remove spaces
            String expr = arg;
            expr.replace(" ", "");
            
            // Find operator
            int opIdx = -1;
            char op = 0;
            for (int i = 1; i < (int)expr.length(); i++) {
                char c = expr.charAt(i);
                if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^') {
                    opIdx = i;
                    op = c;
                    break;
                }
            }
            
            if (opIdx == -1) {
                // Just a number
                result = expr.toDouble();
            } else {
                double a = expr.substring(0, opIdx).toDouble();
                double b = expr.substring(opIdx + 1).toDouble();
                
                switch (op) {
                    case '+': result = a + b; break;
                    case '-': result = a - b; break;
                    case '*': result = a * b; break;
                    case '/': 
                        if (b == 0) { addToHistory("Error: Division by zero"); error = true; }
                        else result = a / b;
                        break;
                    case '%': result = (int)a % (int)b; break;
                    case '^': result = pow(a, b); break;
                }
            }
            
            if (!error) {
                // Format result nicely
                if (result == (int)result) {
                    addToHistory("= " + String((int)result));
                } else {
                    addToHistory("= " + String(result, 6));
                }
            }
        }
    }
    else if (cmd == "screenshot") {
        String filename = "/screenshot_" + String(millis()) + ".bmp";
        if (arg.length() > 0) {
            filename = resolvePath(arg);
            if (!filename.endsWith(".bmp")) filename += ".bmp";
        }
        
        addToHistory("Saving screenshot...");
        needsRedraw = true;
        renderTerminal();
        
        // Create BMP file
        int w = M5Cardputer.Display.width();
        int h = M5Cardputer.Display.height();
        
        File f = SD.open(filename, FILE_WRITE);
        if (!f) {
            addToHistory("Error creating file");
        } else {
            // BMP Header (54 bytes)
            uint32_t fileSize = 54 + w * h * 3;
            uint8_t header[54] = {
                'B', 'M',           // Signature
                (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
                0, 0, 0, 0,         // Reserved
                54, 0, 0, 0,        // Data offset
                40, 0, 0, 0,        // Header size
                (uint8_t)(w), (uint8_t)(w >> 8), (uint8_t)(w >> 16), (uint8_t)(w >> 24),
                (uint8_t)(h), (uint8_t)(h >> 8), (uint8_t)(h >> 16), (uint8_t)(h >> 24),
                1, 0,               // Planes
                24, 0,              // Bits per pixel
                0, 0, 0, 0,         // Compression
                0, 0, 0, 0,         // Image size
                0, 0, 0, 0,         // X pixels per meter
                0, 0, 0, 0,         // Y pixels per meter
                0, 0, 0, 0,         // Colors used
                0, 0, 0, 0          // Important colors
            };
            f.write(header, 54);
            
            // Write pixel data (bottom to top, BGR format)
            uint8_t row[w * 3];
            for (int y = h - 1; y >= 0; y--) {
                for (int x = 0; x < w; x++) {
                    uint16_t pixel = M5Cardputer.Display.readPixel(x, y);
                    // Convert RGB565 to BGR888
                    row[x * 3 + 0] = (pixel & 0x001F) << 3;        // Blue
                    row[x * 3 + 1] = (pixel & 0x07E0) >> 3;        // Green
                    row[x * 3 + 2] = (pixel & 0xF800) >> 8;        // Red
                }
                f.write(row, w * 3);
            }
            f.close();
            addToHistory("Saved: " + filename);
        }
    }
    else if (cmd == "httpget" || cmd == "wget") {
        if (arg.length() == 0) {
            addToHistory("Usage: httpget <url>");
        } else if (WiFi.status() != WL_CONNECTED) {
            addToHistory("WiFi not connected");
        } else {
            String url = arg;
            if (!url.startsWith("http://") && !url.startsWith("https://")) {
                url = "http://" + url;
            }
            
            addToHistory("Fetching: " + url);
            needsRedraw = true;
            renderTerminal();
            
            HTTPClient http;
            http.begin(url);
            http.setTimeout(10000);
            
            int httpCode = http.GET();
            if (httpCode > 0) {
                addToHistory("HTTP " + String(httpCode));
                String payload = http.getString();
                
                // Show first 500 chars
                if (payload.length() > 500) {
                    payload = payload.substring(0, 500) + "...";
                }
                
                // Split into lines
                int start = 0;
                while (start < (int)payload.length()) {
                    int end = payload.indexOf('\n', start);
                    if (end == -1) end = payload.length();
                    String line = payload.substring(start, end);
                    line.trim();
                    if (line.length() > 0) {
                        if (line.length() > 45) line = line.substring(0, 42) + "...";
                        addToHistory(line);
                    }
                    start = end + 1;
                }
            } else {
                addToHistory("Error: " + http.errorToString(httpCode));
            }
            http.end();
        }
    }
    else if (cmd == "weather") {
        if (WiFi.status() != WL_CONNECTED) {
            addToHistory("WiFi not connected");
        } else {
            String location = arg.length() > 0 ? arg : "auto";
            
            addToHistory("Fetching weather...");
            needsRedraw = true;
            renderTerminal();
            
            // Use wttr.in - free weather API, no key needed
            HTTPClient http;
            String url = "http://wttr.in/" + location + "?format=%l:+%c+%t+%h+%w";
            http.begin(url);
            http.setTimeout(10000);
            http.addHeader("User-Agent", "curl");
            
            int httpCode = http.GET();
            if (httpCode == 200) {
                String payload = http.getString();
                payload.trim();
                addToHistory(payload);
                
                // Get more details
                http.end();
                url = "http://wttr.in/" + location + "?format=%l\\n%C\\nTemp:+%t+(feels+%f)\\nHumidity:+%h\\nWind:+%w\\nUV:+%u";
                http.begin(url);
                http.addHeader("User-Agent", "curl");
                httpCode = http.GET();
                if (httpCode == 200) {
                    payload = http.getString();
                    int start = 0;
                    while (start < (int)payload.length()) {
                        int end = payload.indexOf('\n', start);
                        if (end == -1) end = payload.length();
                        String line = payload.substring(start, end);
                        line.trim();
                        // Skip the location line since we already showed it
                        if (line.length() > 0 && start > 0) {
                            addToHistory("  " + line);
                        }
                        start = end + 1;
                    }
                }
            } else {
                addToHistory("Error fetching weather");
            }
            http.end();
        }
    }
    else if (cmd == "serial") {
        if (arg == "on" || arg == "start") {
            Serial.begin(serialBaudRate);
            serialMonitorActive = true;
            serialBuffer.clear();
            addToHistory("Serial monitor started at " + String(serialBaudRate) + " baud");
            addToHistory("Type 'serial off' to stop");
            currentMode = MODE_SERIAL_MONITOR;
            renderSerialMonitor();
        } else if (arg == "off" || arg == "stop") {
            serialMonitorActive = false;
            currentMode = MODE_TERMINAL;
            addToHistory("Serial monitor stopped");
        } else if (arg.startsWith("baud ")) {
            serialBaudRate = arg.substring(5).toInt();
            if (serialBaudRate < 300) serialBaudRate = 9600;
            addToHistory("Baud rate set to " + String(serialBaudRate));
        } else {
            addToHistory("Usage: serial <on/off>");
            addToHistory("       serial baud <rate>");
            addToHistory("Current baud: " + String(serialBaudRate));
        }
    }
#ifdef ENABLE_IR_FEATURES
    else if (cmd == "ir") {
        if (arg == "recv" || arg == "receive") {
            addToHistory("IR receiver starting...");
            addToHistory("Press ESC to stop");
            IrReceiver.begin(IR_RECV_PIN);
            // Would need a separate mode for continuous IR reception
        } else if (arg.startsWith("send ")) {
            String code = arg.substring(5);
            unsigned long irCode = strtoul(code.c_str(), NULL, 16);
            IrSender.begin(IR_SEND_PIN);
            IrSender.sendNEC(irCode, 32);
            addToHistory("Sent IR code: 0x" + String(irCode, HEX));
        } else {
            addToHistory("Usage: ir recv - Receive IR codes");
            addToHistory("       ir send <hex> - Send NEC code");
        }
    }
    else if (cmd == "roku") {
        // Secret TCL Roku TV IR Remote mode
        rokuSelectedBtn = 2;  // Start with OK selected
        currentMode = MODE_ROKU_REMOTE;
        renderRokuRemote();
    }
#endif
#ifdef ENABLE_BT_KEYBOARD
    else if (cmd == "btkey") {
        if (arg == "on" || arg == "start") {
            if (!btKeyboardActive) {
                addToHistory("Starting BT Keyboard...");
                needsRedraw = true;
                renderTerminal();
                bleKeyboard.begin();
                btKeyboardActive = true;
                addToHistory("BT Keyboard active");
                addToHistory("Device name: M5Cardputer");
                currentMode = MODE_BT_KEYBOARD;
                renderBTKeyboard();
            }
        } else if (arg == "off" || arg == "stop") {
            btKeyboardActive = false;
            currentMode = MODE_TERMINAL;
            addToHistory("BT Keyboard stopped");
        } else {
            addToHistory("Usage: btkey <on/off>");
            addToHistory("When on, keypresses sent via BT");
        }
    }
#endif
    else if (cmd == "help") {
        if (arg == "2" || arg == "more") {
            // Page 2 of help
            addToHistory("--- System/Power ---");
            addToHistory("  sysinfo   System information");
            addToHistory("  restart   Restart device");
            addToHistory("  shutdown  Power off device");
            addToHistory("  sleep     Deep sleep mode");
            addToHistory("  brightness <0-100>");
            addToHistory("  sound -m/-v  Volume control");
            addToHistory("  screenshot [file]");
            addToHistory("--- Utilities ---");
            addToHistory("  calc <expr>  Calculator");
            addToHistory("  hexview <file>  Hex viewer");
            addToHistory("  find <pattern>  Search files");
            addToHistory("--- Network Tools ---");
            addToHistory("  ping <host>");
            addToHistory("  httpget <url>");
            addToHistory("  weather [city]");
            addToHistory("--- Hardware ---");
            addToHistory("  serial <on/off>  Serial monitor");
#ifdef ENABLE_IR_FEATURES
            addToHistory("  ir send/recv  IR blaster");
#endif
#ifdef ENABLE_BT_KEYBOARD
            addToHistory("  btkey <on/off>  BT keyboard");
#endif
            addToHistory("--- Keyboard Shortcuts ---");
            addToHistory("  Fn+[/]    Volume down/up");
            addToHistory("  Fn+;/.    Browse cmd history");
        } else {
            // Page 1 of help
            addToHistory("--- File Commands ---");
            addToHistory("  ls        List files");
            addToHistory("  cd <dir>  Change directory");
            addToHistory("  mkdir     Create directory");
            addToHistory("  rm        Delete file/folder");
            addToHistory("  cat       View file contents");
            addToHistory("  touch     Create empty file");
            addToHistory("  echo      Write to file");
            addToHistory("--- Applications ---");
            addToHistory("  cocui     COC Menu System");
            addToHistory("  fileui    File Manager GUI");
            addToHistory("  filetxt   Text Editor");
            addToHistory("  synth     Grid Sequencer");
            addToHistory("--- Network ---");
            addToHistory("  wifi list/off/<ssid> <pass>");
            addToHistory("  sd-share <on/off>");
            addToHistory("  sdqr      Show QR code");
            addToHistory("  ip        Show IP address");
            addToHistory("--- Other ---");
            addToHistory("  disk      SD card usage");
            addToHistory("  time      Time commands");
            addToHistory("  config    Settings");
            addToHistory("  clear     Clear screen");
            addToHistory("Type 'help 2' for more...");
        }
    }
    else { addToHistory("Unknown: " + cmd); addToHistory("Type 'help' for commands"); }
    
    // Calculate command duration
    lastCmdDuration = millis() - cmdStartTime;
}

// ==========================================
//           Main Setup & Loop
// ==========================================

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(TERM_BG_COLOR);
    M5Cardputer.Display.setBrightness(100);
    sprite.setColorDepth(16);
    sprite.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
    sprite.setTextSize(FONT_SIZE);
    sprite.setTextWrap(false);
    
    // Initialize smooth UI animations
#ifdef ENABLE_SMOOTH_UI
    smooth_ui_toolkit::ui_hal::on_get_tick([]() { return millis(); });
    smooth_ui_toolkit::ui_hal::on_delay([](uint32_t ms) { delay(ms); });
    
    // Configure spring animation parameters for rubber-band effect
    cocMenuSelectorY.springOptions().bounce = 0.3;
    cocMenuSelectorY.springOptions().visualDuration = 0.25;
    cocMenuSelectorW.springOptions().bounce = 0.35;  // Slightly bouncier for width
    cocMenuSelectorW.springOptions().visualDuration = 0.2;
    fileUiSelectorY.springOptions().bounce = 0.3;
    fileUiSelectorY.springOptions().visualDuration = 0.25;
    fileUiSelectorW.springOptions().bounce = 0.35;
    fileUiSelectorW.springOptions().visualDuration = 0.2;
    settingsSelectorY.springOptions().bounce = 0.3;
    settingsSelectorY.springOptions().visualDuration = 0.25;
    settingsSelectorW.springOptions().bounce = 0.35;
    settingsSelectorW.springOptions().visualDuration = 0.2;
    webServerSelectorY.springOptions().bounce = 0.3;
    webServerSelectorY.springOptions().visualDuration = 0.25;
    webServerSelectorW.springOptions().bounce = 0.35;
    webServerSelectorW.springOptions().visualDuration = 0.2;
    wifiSelectorY.springOptions().bounce = 0.3;
    wifiSelectorY.springOptions().visualDuration = 0.25;
    wifiSelectorW.springOptions().bounce = 0.35;
    wifiSelectorW.springOptions().visualDuration = 0.2;
    btAudioSelectorY.springOptions().bounce = 0.3;
    btAudioSelectorY.springOptions().visualDuration = 0.25;
    btAudioSelectorW.springOptions().bounce = 0.35;
    btAudioSelectorW.springOptions().visualDuration = 0.2;
    gamesSelectorY.springOptions().bounce = 0.3;
    gamesSelectorY.springOptions().visualDuration = 0.25;
    gamesSelectorW.springOptions().bounce = 0.35;
    gamesSelectorW.springOptions().visualDuration = 0.2;
    chatSelectorX.springOptions().bounce = 0.3;
    chatSelectorX.springOptions().visualDuration = 0.25;
    chatSelectorY.springOptions().bounce = 0.3;
    chatSelectorY.springOptions().visualDuration = 0.25;
    chatModeSelectorY.springOptions().bounce = 0.3;
    chatModeSelectorY.springOptions().visualDuration = 0.25;
    smoothUiInitialized = true;
#endif
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    // Record boot time for uptime calculation
    bootTime = millis();
    
    // Initialize traffic arrays
    for (int i = 0; i < TRAFFIC_HISTORY_SIZE; i++) {
        trafficIn[i] = 0;
        trafficOut[i] = 0;
    }
    
    if (!SD.begin(SD_CS_PIN, SPI, 25000000)) {
        addToHistory("Error: SD Card Init Failed!");
        addToHistory("Insert SD and restart.");
        // Stay in terminal mode on SD fail
    } else {
        loadUserConfig();  // Load user configuration
        loadCmdHistory();  // Load command history from SD
        addToHistory("Welcome to M5Shell v4.4.0");
        addToHistory("Type 'help' for commands");
        addToHistory("");
        
        // Check for autorun script
        if (SD.exists("/autorun.sh")) {
            addToHistory("Running autorun.sh...");
            File f = SD.open("/autorun.sh", FILE_READ);
            if (f) {
                while (f.available()) {
                    String line = f.readStringUntil('\n');
                    line.trim();
                    // Skip comments and empty lines
                    if (line.length() > 0 && !line.startsWith("#")) {
                        executeCommand(line);
                    }
                }
                f.close();
                addToHistory("Autorun complete.");
                addToHistory("");
            }
        }
        
        // Boot into COC UI (Fn+T to access terminal)
        currentMode = MODE_COC_MENU;
        cocMenuIndex = 0;
        cocMenuScroll = 0;
        renderCocMenu();
    }
    
    lastActivityTime = millis();  // Initialize activity timer
}

void loop() {
    M5Cardputer.update();
    if (sdShareEnabled && webServer != nullptr) webServer->handleClient();
    
    // Check for screen timeout
    checkScreenTimeout();
    
    // Handle wake from screen off - any key press wakes without processing
    if (screenOff) {
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            resetActivityTimer();
            // Consume the keypress so it doesn't trigger actions
            delay(100);
            return;
        }
        delay(50);  // Slower loop when screen is off to save power
        return;
    }
    
    // Handle wake from terminal hidden - any key press shows terminal again
    if (terminalHidden) {
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            resetActivityTimer();
            // Consume the keypress so it doesn't trigger actions
            delay(100);
            return;
        }
        delay(20);  // Slightly slower loop when showing plots
        return;
    }
    
    // Check volume overlay timeout (3 seconds)
    if (volumeOverlayVisible && (millis() - volumeOverlayShownTime > 3000)) {
        volumeOverlayVisible = false;
        needsRedraw = true;
        // Force immediate redraw for non-terminal modes
        switch (currentMode) {
            case MODE_COC_MENU: renderCocMenu(); break;
            case MODE_COC_WEBSERVER: renderCocWebServer(); break;
            case MODE_COC_SETTINGS: renderCocSettings(); break;
            case MODE_COC_GAMES: renderCocGames(); break;
            case MODE_COC_WIFI: renderCocWifi(); break;
            case MODE_FILE_UI: renderFileUI(); break;
            case MODE_MIC_RECORDER: renderMicRecorder(); break;
            case MODE_BT_AUDIO: renderBtAudio(); break;
            case MODE_COC_CHAT: renderCocChat(); break;
            default: break;
        }
    }
    
    // Global volume control: Fn+[ (Fn+C) = volume down, Fn+] (Fn+V) = volume up
    // Note: On M5Cardputer, Fn+C produces '[' and Fn+V produces ']'
    if (M5Cardputer.Keyboard.isKeyPressed('[')) {
        globalVolume = max(0, globalVolume - 10);
        applyVolume();
        showVolumeOverlay();
        if (!globalMuted && globalVolume > 0) playMenuSound(600, 30);
        delay(200);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(']')) {
        globalVolume = min(100, globalVolume + 10);
        applyVolume();
        showVolumeOverlay();
        if (!globalMuted) playMenuSound(1200, 30);
        delay(200);
        return;
    }
    
    switch (currentMode) {
        case MODE_TERMINAL:
            if (M5Cardputer.Keyboard.isChange()) {
                if (M5Cardputer.Keyboard.isPressed()) {
                    resetActivityTimer();  // Reset timeout on any key
                    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
                    bool fnPressed = status.fn;
                    bool keyProcessed = false;
                    
                    // Fn+P - Screenshot
                    if (M5Cardputer.Keyboard.isKeyPressed(KEY_FN_P) || (fnPressed && M5Cardputer.Keyboard.isKeyPressed('p'))) {
                        takeScreenshot();
                        needsRedraw = true;
                        delay(150);
                        keyProcessed = true;
                    }
                    // Fn+; - History up (older commands)
                    else if (fnPressed && M5Cardputer.Keyboard.isKeyPressed(';')) {
                        if (!cmdHistory.empty()) {
                            if (cmdHistoryIndex == -1) {
                                // Starting to browse - save current input
                                savedInputBuffer = inputBuffer;
                                cmdHistoryIndex = cmdHistory.size() - 1;
                            } else if (cmdHistoryIndex > 0) {
                                cmdHistoryIndex--;
                            }
                            inputBuffer = cmdHistory[cmdHistoryIndex];
                            cursorVisible = true; lastBlinkTime = millis(); needsRedraw = true;
                        }
                        M5Cardputer.Speaker.tone(1200, 30);  // Key beep
                        delay(150);
                        keyProcessed = true;
                    }
                    // Fn+. - History down (newer commands)
                    else if (fnPressed && M5Cardputer.Keyboard.isKeyPressed('.')) {
                        if (cmdHistoryIndex != -1) {
                            if (cmdHistoryIndex < (int)cmdHistory.size() - 1) {
                                cmdHistoryIndex++;
                                inputBuffer = cmdHistory[cmdHistoryIndex];
                            } else {
                                // Back to current input
                                cmdHistoryIndex = -1;
                                inputBuffer = savedInputBuffer;
                            }
                            cursorVisible = true; lastBlinkTime = millis(); needsRedraw = true;
                        }
                        M5Cardputer.Speaker.tone(1200, 30);  // Key beep
                        delay(150);
                        keyProcessed = true;
                    }
                    // Tab - Auto-completion
                    else if (status.tab || M5Cardputer.Keyboard.isKeyPressed(KEY_BTN_TAB)) {
                        inputBuffer = tabComplete(inputBuffer);
                        cursorVisible = true; lastBlinkTime = millis(); needsRedraw = true;
                        delay(150);
                        keyProcessed = true;
                    }
                    // Normal key handling (only if Fn not pressed for ; and .)
                    if (!keyProcessed) {
                        bool typed = false;
                        for (auto i : status.word) {
                            // Skip ; and . if Fn is pressed (already handled above)
                            if (fnPressed && (i == ';' || i == '.')) continue;
                            // Skip tab character (handled above)
                            if (i == '\t') continue;
                            inputBuffer += i;
                            typed = true;
                        }
                        if (status.del && inputBuffer.length() > 0) {
                            inputBuffer.remove(inputBuffer.length() - 1);
                            typed = true;
                        }
                        if (status.enter) { 
                            // Nerdy beep for enter - two-tone chirp
                            M5Cardputer.Speaker.tone(800, 40);
                            delay(50);
                            M5Cardputer.Speaker.tone(1200, 60);
                            
                            executeCommand(inputBuffer); 
                            inputBuffer = ""; 
                            savedInputBuffer = "";
                            cmdHistoryIndex = -1;
                        } else if (typed) {
                            // Short click beep for typing
                            M5Cardputer.Speaker.tone(1500, 15);
                        }
                        cursorVisible = true; lastBlinkTime = millis(); needsRedraw = true;
                    }
                }
            }
            if (millis() - lastBlinkTime > CURSOR_BLINK_MS) { lastBlinkTime = millis(); cursorVisible = !cursorVisible; needsRedraw = true; }
            // Force redraw every second to update clock display
            if (timeSynced) {
                static unsigned long lastClockUpdate = 0;
                if (millis() - lastClockUpdate > 1000) {
                    lastClockUpdate = millis();
                    needsRedraw = true;
                }
            }
            // Only render terminal if we're still in terminal mode (command may have changed mode)
            if (needsRedraw && currentMode == MODE_TERMINAL) renderTerminal();
            break;
        case MODE_FILE_UI: 
            if (M5Cardputer.Keyboard.isPressed()) {
                resetActivityTimer();
                handleInputUI(); 
            }
#ifdef ENABLE_SMOOTH_UI
            // Continuously render while animation is playing for smooth effect
            // Only render if still in File UI mode (might have changed via file selection)
            if (currentMode == MODE_FILE_UI && (!fileUiSelectorY.done() || !fileUiSelectorW.done())) {
                renderFileUI();
            }
#endif
            break;
        case MODE_DIALOG_YESNO: 
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleYesNoInput(); 
            break;
        case MODE_DIALOG_INPUT: 
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleInputDialogInput(); 
            break;
        case MODE_DIALOG_SAVE: 
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleSaveDialogInput(); 
            break;
        case MODE_TEXT_EDITOR: 
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleTextEditorInput(); 
            break;
        case MODE_QR_DISPLAY:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleQRInput();
            break;
        case MODE_HEX_VIEW:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleHexViewInput();
            break;
        case MODE_SERIAL_MONITOR:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleSerialMonitorInput();
            break;
        case MODE_BT_KEYBOARD:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleBTKeyboardInput();
            break;
        case MODE_SYNTH:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleSynthInput();
            break;
        case MODE_COC_MENU:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleCocMenuInput();
#ifdef ENABLE_SMOOTH_UI
            // Continuously render while animation is playing for smooth effect
            // Only render if still in COC Menu mode (might have changed via menu selection)
            if (currentMode == MODE_COC_MENU && (!cocMenuSelectorY.done() || !cocMenuSelectorW.done())) {
                renderCocMenu();
            }
#endif
            break;
        case MODE_COC_WEBSERVER:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleCocWebServerInput();
#ifdef ENABLE_SMOOTH_UI
            if (currentMode == MODE_COC_WEBSERVER && (!webServerSelectorY.done() || !webServerSelectorW.done())) {
                renderCocWebServer();
            }
#endif
            break;
        case MODE_MIC_RECORDER:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleMicRecorderInput();
            break;
        case MODE_COC_WIFI:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleCocWifiInput();
#ifdef ENABLE_SMOOTH_UI
            if (currentMode == MODE_COC_WIFI && (!wifiSelectorY.done() || !wifiSelectorW.done())) {
                renderCocWifi();
            }
#endif
            break;
        case MODE_COC_USB:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleCocUSBInput();
            break;
        case MODE_COC_SETTINGS:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleCocSettingsInput();
#ifdef ENABLE_SMOOTH_UI
            // Continuously render while animation is playing for smooth effect
            // Only render if still in Settings mode (might have changed via ESC)
            if (currentMode == MODE_COC_SETTINGS && (!settingsSelectorY.done() || !settingsSelectorW.done())) {
                renderCocSettings();
            }
#endif
            break;
        case MODE_COC_GAMES:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleCocGamesInput();
#ifdef ENABLE_SMOOTH_UI
            if (currentMode == MODE_COC_GAMES && (!gamesSelectorY.done() || !gamesSelectorW.done())) {
                renderCocGames();
            }
#endif
            break;
        case MODE_GAME_TETRIS:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleTetrisInput();
            break;
        case MODE_GAME_BREAKOUT:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleBreakoutInput();
            break;
        case MODE_BT_SEND:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleWifiDirectInput();
            break;
        case MODE_BT_AUDIO:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleBtAudioInput();
#ifdef ENABLE_SMOOTH_UI
            // Only render if still in BT Audio mode (might have changed via ESC)
            if (currentMode == MODE_BT_AUDIO && (!btAudioSelectorY.done() || !btAudioSelectorW.done())) {
                renderBtAudio();
            }
#endif
            break;
        case MODE_COC_CHAT:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleCocChatInput();
#ifdef ENABLE_SMOOTH_UI
            // Animate mode selection or room selection
            if (currentMode == MODE_COC_CHAT && !chatInRoom) {
                if (chatConnectMode == CHAT_MODE_SELECT && !chatModeSelectorY.done()) {
                    renderCocChat();
                } else if (chatConnectMode == CHAT_MODE_WIFI && (!chatSelectorX.done() || !chatSelectorY.done())) {
                    renderCocChat();
                }
            }
#endif
            // Periodically redraw for room presence updates
            if (millis() - lastChatCheck > 200) {
                lastChatCheck = millis();
                renderCocChat();
            }
            break;
#ifdef ENABLE_IR_FEATURES
        case MODE_ROKU_REMOTE:
            if (M5Cardputer.Keyboard.isPressed()) resetActivityTimer();
            handleRokuRemoteInput();
            break;
#endif
    }
    delay(10);
}
