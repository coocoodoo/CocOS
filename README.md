
<img width="1024" height="1024" alt="Cocos" src="https://github.com/user-attachments/assets/2ebeea57-4104-43f6-8197-280181befe06" />
===========================================================================
  M5Stack Cardputer Terminal & COC UI System
  Retro Pixel Art UI Edition v4.5.0
===========================================================================

A feature-rich terminal and menu system for the M5Stack Cardputer, with a
retro pixel-art style UI. Boots into the COC menu; press Fn+T to open the
terminal. Requires an SD card.

--------------------------------------------------------------------------------
  HARDWARE
--------------------------------------------------------------------------------
  - M5Stack Cardputer (ESP32-S3)
  - SD card (required)

--------------------------------------------------------------------------------
  COC MENU SYSTEM (type 'cocui' or 'coc' in terminal)
--------------------------------------------------------------------------------

  File Browser
    - Browse SD card with pagination, to avoid crash (100 files per page)
    - Play WAV files (Enter to play)
    - Copy, move, delete, rename files/folders
    - Fn+B: Send file via WiFi 
    - Fn+Up/Down: Previous/next page
    - Fn+2: Rename
    - Fn+C: Copy, Fn+M: Move (after copy), Fn+V: Paste

  WiFi Chat
    - Local chat with other Cardputers on same network (UDP broadcast)
    - Modes: Use WiFi Network | Host Direct Chat | Join Direct Chat
    - Host: Creates "M5Chat_<nickname>" AP; others join your hotspot
    - Join: Scan for M5Chat_* APs, connect, then chat
    - 4 chat rooms (2x2 grid) when using WiFi; direct mode uses room 0
    - Nickname configurable in Settings

  Mic Recorder
    - Record audio to SD as WAV (/Recordings/rec_*.wav)
    - VU meter, 16 kHz sample rate
    - Centered UI with mic icons (red/gray)

  BT Audio
    - Bluetooth speaker pairing (UI only; ESP32-S3 lacks Classic BT/A2DP)

  WiFi
    - Scan, connect, save credentials (via Settings)
    - Status, disconnect

  USB Transfer
    - USB Mass Storage mode: SD card appears as drive on PC
    - Requires USB-OTG (TinyUSB), ESP32 Arduino Core 2.x
    - Connect via USB; use COC menu or follow prompts

  Settings
    - Brightness (1–100)
    - Sound (on/off), Volume (0–100)
    - Screen Timeout (seconds; 0 = disabled)
    - Terminal Hide Timeout (seconds; hide terminal, show traffic plot; 0 = off)
    - Timezone (GMT offset -12 to +14)
    - Nickname (for WiFi Chat)
    - SID Duration (seconds, for sidplay; 30–600)
    - Save & Exit

  Games
    - Tetris (classic puzzle)
    - Breakout (brick breaker)
    - Synth (PixiTracker-style 8x16 grid sequencer)
      - 8 sound types: Square, Bass, Lead, Pluck, Kick, Snare, HiHat, Bell
      - Load/save .syx; BPM control; Fn+S save, Fn+C clear

--------------------------------------------------------------------------------
  TERMINAL COMMANDS
--------------------------------------------------------------------------------

  File
    ls                    List files
    cd <dir>              Change directory
    mkdir <path>          Create directory
    rm <path>             Delete file or folder
    cat <file>            View file contents
    touch <file>          Create empty file
    echo "text" > <file>  Write to file
    echo "text" >> <file> Append to file

  Applications
    cocui, coc            COC Menu
    fileui, fm            File Manager GUI
    filetxt [path]        Text editor (new or open file)
    synth [run|load|save <file>]  Grid sequencer

  Network (no Web Server)
    wifi                  Status
    wifi list, wifi -l    Scan networks
    wifi off              Disconnect
    wifi <ssid> [password]  Connect
    ip                    Show IP

  System & Config
    disk, df              SD card usage
    config                Show config
    config edit           Edit /usrconfig.cfg
    config reset          Reset config to defaults
    time                  Show time
    time -u               Sync time from NTP
    time -z               Timezone list
    time -z <offset>      Set timezone (e.g. -6 for CST)
    clear, cls            Clear screen
    history               Last commands (Fn+;/ . to scroll)
    history clear         Clear command history

  System / Power
    sysinfo               Chip, heap, uptime, WiFi, battery
    restart, reboot       Restart device
    shutdown, poweroff    Power off
    sleep, hibernate      Deep sleep (reset to wake)
    brightness <0-100>    Set brightness
    sound -m              Toggle mute
    sound -v <0-100>      Set volume
    screenshot [file]     Save screenshot to SD (Fn+P in cocui)

  Utilities
    calc <expr>           Calculator
    hexview, hv <file>    Hex viewer
    find <pattern>        Search files on SD
    ping <host>           Ping (WiFi)
    httpget, wget <url>   Download file to SD
    weather [city]        Weather via wttr.in (no API key)

  Hardware (if enabled)
    serial on|off         Serial monitor
    serial baud <rate>    Set baud (default 115200)
    ir recv               IR receive (ESC to stop)
    ir send <hex>         Send NEC IR code
    roku                  TCL Roku TV IR remote (NEC; secret command)

--------------------------------------------------------------------------------
  KEYBOARD SHORTCUTS
--------------------------------------------------------------------------------
    Fn+T                  Open terminal (from COC menu or elsewhere)
    Fn+P                  Screenshot
    Fn+[ / Fn+]           Volume down / up (global)
    Fn+; / Fn+.           Command history up / down (in terminal)

  Cardputer navigation
    ; . , /               Up, Down, Left, Right
    `                     ESC
    Tab                   Context-dependent

--------------------------------------------------------------------------------
  CONFIG FILE: /usrconfig.cfg
--------------------------------------------------------------------------------
  Edit via 'config edit' or Settings. Reboot to apply.

    terminal_hide         Seconds until terminal hides (traffic plot); 0=off
    screen_off            Seconds until screen off; 0=off
    brightness            1–100
    timezone              GMT offset (-12 to +14)
    sound                 1/0 or on/off
    volume                0–100
    sid_duration          30–600 (seconds) for sidplay
    ap_ssid, ap_password  Hotspot (e.g. for future use)
    wifi_ssid, wifi_password  Saved WiFi
    nickname              WiFi Chat nickname (max 15 chars)

--------------------------------------------------------------------------------
  OPTIONAL FEATURES (compile-time)
--------------------------------------------------------------------------------
    ENABLE_IR_FEATURES    IR send/receive, Roku remote (ir, roku commands)
    ENABLE_BT_KEYBOARD    BLE keyboard (btkey)
    ENABLE_SID            C64 SID player (sidplay <file.sid>)
    ENABLE_USB_MSC        USB Mass Storage (USB Transfer)
    ENABLE_SMOOTH_UI      Rubber-band menu animations

  SID player (when ENABLE_SID):
    sidplay <file.sid>    Play .sid file
    [/] Volume; ,/. Previous/Next song; ESC stop

--------------------------------------------------------------------------------
  ROKU REMOTE (IR, when ENABLE_IR_FEATURES)
--------------------------------------------------------------------------------
  Type 'roku' in terminal. TCL Roku TV NEC codes. Navigate with ;/./,//,
  Enter to send. Buttons: Home, Back, OK, Up, Down, Left, Right, Rewind,
  Forward, Play/Pause, Replay, Options, Power, Mute, Vol Up, Vol Down.

--------------------------------------------------------------------------------
  AUTORUN <3
--------------------------------------------------------------------------------
  If /autorun.sh exists on SD, it runs at boot. One command per line;
  # for comments. Example:
    wifi MyNetwork MyPassword
    cocui

--------------------------------------------------------------------------------
  REQUIRED LIBRARIES
--------------------------------------------------------------------------------
    - M5Unified / M5Cardputer
    - NimBLE-Arduino
    - smooth_ui_toolkit (optional; for ENABLE_SMOOTH_UI)
    - IRremote (optional; for ENABLE_IR_FEATURES)
    - BleKeyboard (optional; for ENABLE_BT_KEYBOARD)
    - arduino-audio-tools, SIDPlayer (optional; for ENABLE_SID)

  USB Mass Storage (ENABLE_USB_MSC): ESP32 Arduino Core 2.x, USB-OTG (TinyUSB).

--------------------------------------------------------------------------------
  WORK IN PROGRESS IN THIS BUILD
--------------------------------------------------------------------------------
  - MP3 playback
  - Web Server / SD Share (sd-share, sdqr)

--------------------------------------------------------------------------------
  VERSION
--------------------------------------------------------------------------------
  v4.5.0 — WiFi Chat (UDP), Host/Join direct modes, 4 rooms.
  v4.4.0 — Global volume (Fn+[/]), sound command, improved Mic Recorder UI.
  v4.3.1 — Audio fixes, terminal "sound" command, VU meter improvements.

===========================================================================


<img width="240" height="135" alt="SCR_004406340" src="https://github.com/user-attachments/assets/9efda188-5208-4057-961d-69fa1e1838e7" />
<img width="240" height="135" alt="SCR_004409831" src="https://github.com/user-attachments/assets/cea4cf04-a658-4acb-ba4c-a43e4c4e64c3" />
<img width="240" height="135" alt="SCR_004413114" src="https://github.com/user-attachments/assets/2c3fda1e-d069-45cf-accc-1755fb298fc8" />
<img width="240" height="135" alt="SCR_004417755" src="https://github.com/user-attachments/assets/76568c7e-462f-40ff-b31d-e8df1636547e" />
<img width="240" height="135" alt="SCR_004420806" src="https://github.com/user-attachments/assets/871473c4-05ab-4288-8b1a-36bca7979af3" />
<img width="240" height="135" alt="SCR_004423886" src="https://github.com/user-attachments/assets/02ea2e04-c9cb-4016-a7c5-dd069d0745ce" />
<img width="240" height="135" alt="SCR_004427028" src="https://github.com/user-attachments/assets/fffff372-9d59-4e59-b731-c0d02618ebc8" />




<img width="500" height="100" alt="Spectral-10 (1)" src="https://github.com/user-attachments/assets/ebbb766e-49e3-4bd6-9a98-f816bb4cde76" />
