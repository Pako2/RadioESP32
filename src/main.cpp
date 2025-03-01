#define STR(A) #A
#define STRINGIFY(A) STR(A)
#define REPLACEMENTCHARACTER '*'
#define sv_ DRAM_ATTR static volatile

#include "Arduino.h"
#include <ArduinoJson.h>
#define NO_LED_FEEDBACK_CODE        // saves 92 bytes program memory
#define EXCLUDE_UNIVERSAL_PROTOCOLS // Saves up to 1000 bytes program memory.
#define EXCLUDE_EXOTIC_PROTOCOLS    // saves around 650 bytes program memory if all other protocols are active
#include <IRremote.hpp>
#if defined(DISP)
#include <SPI.h>
#include <TFT_eSPI.h>
#include <u8g2_for_TFT_eSPI.h>
#endif
#include <Wire.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <freertos/task.h> // FreeRtos task handling
#include <esp_task_wdt.h>
#include <LittleFS.h>
#if defined(DATAWEB)
#include <FS_editor.h>
#else
// from vendors - fix
#include "webh/glyphicons-halflings-regular.woff.gz.h"
#include "webh/required.css.gz.h"
#include "webh/required.js.gz.h"
// custom, can be updated and changed - generated from files in the LittleFS partition using Custom scripts
#include "webh/radioesp32.js.gz.h"
#include "webh/radioesp32.html.gz.h"
#include "webh/index.html.gz.h"
#endif
#if defined(OTA)
#include <Update.h>
#endif
#include <time.h>
#include "esp_sntp.h"
#include "Audio.h"
#include "esp_log.h"

// Enums and structs
enum mode_wifi
{
  WF_NONE,
  WF_STA,
  WF_WAITSTA
};
mode_wifi WF_MODE = WF_NONE;

enum player_mode
{
  PM_RADIO,
  PM_SDCARD
};

enum player_icons
{
  PI_RADIO,
  PI_SDCARD,
  PI_PLAY,
  PI_PAUSE,
  PI_STOP,
  PI_RANDOM,
  PI_REPEAT,
  PI_MUTE_OFF,
  PI_MUTE_ON
};

struct pi_params
{
  player_icons id;
  uint8_t x;
  uint8_t y;
  uint8_t y2;
  uint8_t idx;
  uint16_t color;
};
struct pi_params *PIparams;

enum IRcmd
{
  IR_0,
  IR_1,
  IR_2,
  IR_3,
  IR_4,
  IR_5,
  IR_6,
  IR_7,
  IR_8,
  IR_9,
  IR_MUTE,
  IR_VOLP,  // volume+
  IR_VOLM,  // volume-
  IR_CHP,   // channel+ (station+)
  IR_CHM,   // channel- (station-)
  IR_PP,    // pause/play
  IR_STOP,  // stop
  IR_RNDM,  // random
  IR_RPT,   // repeat
  IR_RADIO, // radio
  IR_SD,    // SD player
  IR_OK,    // OK
  IR_EX,    // exit
  IR_BS,    // backspace
  IR_FORW,  // next
  IR_BACKW, // previous
#if defined(AUTOSHUTDOWN)
  IR_ISD, // immediate shutdown
  IR_SSD  // scheduled shutdown
#endif
};

#if defined(DISP)
enum disp_mode_t
{
  DSP_RADIO,
  DSP_CLOCK,
  DSP_DIMMED,
  DSP_OFFED,
  DSP_PRESETNR,
  DSP_ASD,   // automatic shut-down set-up
  DSP_PWOFF, // automatic shut-down
  DSP_LOWBATT,
  DSP_OTHER
}; // Display mode status
disp_mode_t dispmode = DSP_OTHER;
#endif

enum enc_menu_t
{
  VOLUME,
  STATIONS,
  TRACKS,
  MODECHANGE,
#if defined(AUTOSHUTDOWN)
  AUTOPWOFF
#endif
}; // State for rotary encoder menu

struct IR_CMD
{
  char descr[25];
  uint32_t ircode;
  IRcmd ircmd;
};

struct WLAN
{
  char name[17];
  uint8_t bssid[6];
  char pass[33];
  char ssid[33];
  uint8_t dhcp;
  IPAddress ipaddress;
  IPAddress subnet;
  IPAddress dnsadd;
  IPAddress gateway;
};

struct PRESET
{
  uint8_t nr = 255; // qsort !!!
  char name[33];
  char url[97];
};

const uint8_t MAXGAP = 16;
#define BUFFLEN (255 - MAXGAP - 2)
#if defined(DISP)
struct RowData
{
  uint8_t ypos;
  char input[BUFFLEN + MAXGAP + 1] = {'\0'};
  char rowmap[BUFFLEN + MAXGAP + 1] = {'\0'};
  uint8_t rowlen;
  uint16_t pixlen = 0;
  uint16_t scrollpos = 0;
  uint8_t lock;
  uint8_t type = 0; // 0-none, 1-plane text, 2-scrolled text, 3-clear
  bool updated = false;
};
#endif

#if defined(SDCARD)
bool sdin = false;
bool oldsdin = false;
#endif

// Forward declaration
void WiFiEvent(WiFiEvent_t, arduino_event_info_t);
#if defined(DISP)
void changeDispMode(disp_mode_t mode);
void volumebar(uint8_t val);
void prgrssbar(uint32_t val, bool clr);
void drawIcon(uint8_t id);
void clearIcons(uint8_t id);
void restoreIcons();
void clearLines();
#endif
#if defined(SDCARD)
void Random();
void Repeat();
void updateTrack(int8_t trckstep);
void setTrack(int16_t trck);
void pausePlay();
void handle_mp3list(AsyncWebServerRequest *request);
#endif
char *cpycharar(char *destination, const char *source, size_t num);
void setPreset(uint8_t prst);
void updatePreset(int8_t prsstep, bool play);
void setMutepin(uint8_t mute_, bool test);
void mute(bool source);
void testUrl(const char *url);
void updateLine(uint8_t row, char *input);

// Global variables
#if defined(SDCARD)
uint32_t pastpos = 0xFFFFFFFF;
uint8_t poscounter = 0;
bool random_ = false;
bool loop_ = true;
#endif
bool sdp_icons_req = false;

#if defined(BATTERY)
#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"
#define CHANNEL_BAT ADC_CHANNEL_0 // adc1_0, GPIO36
adc_oneshot_unit_handle_t adc1_handle;
int adcvalraw = 0;
uint16_t adcval = 0;
bool batbarflag = false;
#define LOG_2(n) ((n == 8) ? 3 : ((n == 16) ? 4 : ((n == 32) ? 5 : 6)))
#define FILTER_LEN 32 // allowed values: 8, 16, 32, 64
const uint8_t FILTER_SHIFT = LOG_2(FILTER_LEN);
const uint8_t FILTER_MASK = FILTER_LEN - 1;
uint16_t *Adc1_Buffer; // PSRAM !
uint8_t Adc1_i = 0;    // Index
uint32_t Sum = 0;
#endif
bool asdmode = false;

#if defined(DISP)
/*
  Fontname: SD_Player
  Copyright: Created with Fony 1.4.0.2
  Glyphs: 7/7
  BBX Build Mode: 0
*/
const uint8_t PROGMEM SD_player[211] U8G2_FONT_SECTION("SD_player") =
    "\11\0\4\3\4\4\4\3\5\17\17\0\0\0\0\0\0\0\0\0\0\0\266\0\25\333\332\17A)b\222"
    "\304d\222*\42YD\272\311\320\274\2\1\35\277\350O\42\67\7\7#\63\26Z\264\220\60\231\220\70\210"
    "PQ\241\205\311\314\301\7\4\2\23\327\334\17aRC\64%\26\7\26%\64CRa\0\3\11\270\353"
    "\17#\376O\6\4\11\273\352\17\377\377\7\2\5\33\337\330\317\341\22s\65Q!R\61\351\42\242S\304"
    "\306\244J\42\61W,\35\2\6\32\317\350/\7D\221A\221Aq\265\323A\321\263uA\221A\221A"
    "\7$\0\7\21\370\313\177aRC\24\7\177PqD\65&\27\10\27\370\313\177aR\21A!\21\63"
    "b:\213\230\11\12\211\212\10\223\13\0\0\0\4\377\377\0";

TFT_eSPI tft = TFT_eSPI(); // tft instance
U8g2_for_TFT_eSPI u8g2;    // u8f2 font instance
U8g2_for_TFT_eSPI u8h2[3]; // u8f2 font instance for rows
TFT_eSprite sprite[3] = {TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft)};
TFT_eSprite volsprite = TFT_eSprite(&tft);
uint16_t *volsprPtr;
TFT_eSprite prgrsssprite = TFT_eSprite(&tft);
uint16_t *prgrsssprPtr;
#if defined(BATTERY)
TFT_eSprite batsprite = TFT_eSprite(&tft);
uint16_t *batsprPtr;
#endif
const int freq = 500;
const int resolution = 8;
#endif
const char SPACES[33] = "                                ";
char *shorttrname;
#if defined(DISP)
const uint8_t *font = u8g2_font_t0_22_me;
const uint8_t *rowfont = font;
struct RowData *rows;
bool volbarflag = false;
bool prgrssbarflag = false;
int16_t oldprgrssw = -1;
int8_t dispmuteflag = -1;
bool scrollpair = false;
uint8_t xshift = 2; // display scroll step
uint32_t lastblick;
bool coloncolor = false;

const char gaps[MAXGAP + 1] = "                ";
uint32_t lastredrw;
uint32_t textcolor = 1;
uint32_t backcolor = 0;
char oldtimetxt[6];  // Converted timeinfo
char olddatetxt[16]; // Converted timeinfo/date
#else
const char gaps[1] = "";
#endif

struct WLAN *wlans;
struct PRESET *presets;

const char cmd_0[] PROGMEM = "Digit 0";
const char cmd_1[] PROGMEM = "Digit 1";
const char cmd_2[] PROGMEM = "Digit 2";
const char cmd_3[] PROGMEM = "Digit 3";
const char cmd_4[] PROGMEM = "Digit 4";
const char cmd_5[] PROGMEM = "Digit 5";
const char cmd_6[] PROGMEM = "Digit 6";
const char cmd_7[] PROGMEM = "Digit 7";
const char cmd_8[] PROGMEM = "Digit 8";
const char cmd_9[] PROGMEM = "Digit 9";
const char cmd_10[] PROGMEM = "Mute";
const char cmd_11[] PROGMEM = "Volume+";
const char cmd_12[] PROGMEM = "Volume-";
const char cmd_13[] PROGMEM = "Channel+";
const char cmd_14[] PROGMEM = "Channel-";
const char cmd_15[] PROGMEM = "Pause/Play";
const char cmd_16[] PROGMEM = "Stop";
const char cmd_17[] PROGMEM = "Random";
const char cmd_18[] PROGMEM = "Repeat";
const char cmd_19[] PROGMEM = "Radio";
const char cmd_20[] PROGMEM = "SD_player";
const char cmd_21[] PROGMEM = "OK";
const char cmd_22[] PROGMEM = "Exit";
const char cmd_23[] PROGMEM = "Backspace";
const char cmd_24[] PROGMEM = "Step Forward";
const char cmd_25[] PROGMEM = "Step Backward";
#if defined(AUTOSHUTDOWN)
const char cmd_26[] PROGMEM = "Power OFF";
const char cmd_27[] PROGMEM = "Sleep";
#endif
const char *const cmd_table[] PROGMEM =
    {
        cmd_0,
        cmd_1,
        cmd_2,
        cmd_3,
        cmd_4,
        cmd_5,
        cmd_6,
        cmd_7,
        cmd_8,
        cmd_9,
        cmd_10,
        cmd_11,
        cmd_12,
        cmd_13,
        cmd_14,
        cmd_15,
        cmd_16,
        cmd_17,
        cmd_18,
        cmd_19,
        cmd_20,
        cmd_21,
        cmd_22,
        cmd_23,
        cmd_24,
        cmd_25,
#if defined(AUTOSHUTDOWN)
        cmd_26,
        cmd_27
#endif
};
#if defined(AUTOSHUTDOWN)
const uint8_t cmd_table_len = 28;
#else
const uint8_t cmd_table_len = 26;
#endif
uint8_t *RESERVEDGPIOS;
struct IR_CMD *ir_cmds;
char *testurl;
int Weekday;
uint8_t irnum = 0;
uint8_t presetnum = 0;
uint8_t wlannum = 0;
bool shouldReboot = false;
bool formatreq = false;

/*
Username and password are used in two cases:
============================================
1. to enter the website editor
2. when updating firmware
*/
const char *http_username = "admin";
const char *http_password = "admin";

Audio audio;
TaskHandle_t Task0, xsdtask, maintask;

#define FSIF true // Format LittleFS if not existing
bool STAmode = true;
bool APstart = false;
bool gotIP = false;
uint8_t scanmode = 0;
bool clockReq = false;
unsigned long digtime = 0;
uint16_t idleTimer = 0;
uint16_t dgt_inactivity = 255; // Digit input inactive
uint8_t dgt_count = 0;         // Digit input count
uint16_t dgt_cmd;              // Buffer for digit input

#if defined(AUTOSHUTDOWN)
uint16_t dgt_asd;          // Buffer for digit input for AUTOSHUTDOWN
uint8_t dgt_count_asd = 0; // Digit input count
#define MAXPWOFF 100
#define MINPWOFF 1
bool pwoff_req = false; // Set on/off requested
bool fade_req = false;  // Set fade requested
uint32_t lastfade = 0;
uint16_t pwoffminutes = MAXPWOFF; // Current power off [minutes] duration
time_t pwofftime = 0;
time_t now;
#endif
bool digitsReq = false;
uint8_t presetReq = 255;
bool testurlFlag = false;
uint16_t reqpreset = 255;
uint16_t audpreset = 255;
bool reconnect = false;
bool rcnnct = false;
bool scanfinished = false;
bool muteflag = false; // Mute output
uint8_t reqvol = 12;   // Requested volume
player_mode pmode = PM_RADIO;
int16_t nets;
AsyncWebServer server(80);
AsyncWebSocket weso("/ws");

#if defined(SDCARD)
int SD_curindex = 0;     // Current index in mp3names
int SD_ix = 0;           // work index in mp3names
bool SD_okay = false;    // SD is okay
bool SD_mounted = false; // SD is mounted
int SD_filecount = 0;    // Number of filenames in SD_nodelist
int SD_oldindex = 0;
bool sdready_req = false;
bool oldsdix_req = false;
#endif

#if defined(DISP)
uint8_t WID = 160;
uint8_t HGT = 128;
uint8_t LINEOFFSET = 17;
uint8_t ROW_LEN = 14;
uint8_t CELLWID = 11;
uint8_t CELLHGT = 22;
uint8_t ROWSNUM = 3;
#else // NO DISPLAY
#define ROWSNUM 0
#define MAXGAP 0
#define ROW_LEN 0
// uint8_t ROWSNUM = 0;
#endif
char *station; //  PSRAM !
char *artist;  //  PSRAM !
char *title;   //  PSRAM !
bool updatemetadata = false;
bool updatealbum = false;
bool updateartist = false;
bool updatetitle = false;
uint8_t messageid = 0;

#include "config.h"
Config *config;
#include "loadConfig.h"
#include "wsResponses.h"
#include "websocket.h"
#include "webserver.h"
#if defined(SDCARD)
#include "SDcard.h" // For SD card interface
#endif

const char *TAG = "main"; // For debug lines

char timetxt[9];  // Converted timeinfo
char datetxt[16]; // Converted timeinfo/date

bool time_req = false;   // Set time requested
bool proc1s_req = false; // Set proc1s requested
bool proc5s_req = false; // Set proc5s requested
struct tm timeinfo;      // Will be filled by NTP server
unsigned long currentMillis = 0;
float btvol = 0.12;       // Bluetooth volume
hw_timer_t *timer = NULL; // For timer

// Rotary encoder stuff
int16_t enc_preset = 0;      // Selected preset
uint16_t enc_inactivity = 0; // Time inactive
uint16_t clickcount = 0;     // Incremented per encoder click
int16_t rotationcount = 0;   // Current position of rotary switch
bool singleclick = false;    // True if single click detected: MUTE or ENTER
bool doubleclick = false;    // True if double click detected: STATION mode
bool tripleclick = false;    // True if triple click detected: TRACK mode
bool longclick = false;      // True if longclick detected:    reboot request mode

#if defined(AUTOSHUTDOWN)
bool pwoffclick = false; // True if power off click detected
#endif
#define DIR_CW 0x10  // Clockwise step.
#define DIR_CCW 0x20 // Anti-clockwise step.
#define R_START 0x0
#define F_CW_FINAL 0x1
#define F_CW_BEGIN 0x2
#define F_CW_NEXT 0x3
#define F_CCW_BEGIN 0x4
#define F_CCW_FINAL 0x5
#define F_CCW_NEXT 0x6
uint8_t table_state; // Internal state

const uint8_t _ttable_full[7][4] = {
    //   00          01           10           11                  // BA
    {R_START, F_CW_BEGIN, F_CCW_BEGIN, R_START},           // R_START
    {F_CW_NEXT, R_START, F_CW_FINAL, R_START | DIR_CW},    // F_CW_FINAL
    {F_CW_NEXT, F_CW_BEGIN, R_START, R_START},             // F_CW_BEGIN
    {F_CW_NEXT, F_CW_BEGIN, F_CW_FINAL, R_START},          // F_CW_NEXT
    {F_CCW_NEXT, R_START, F_CCW_BEGIN, R_START},           // F_CCW_BEGIN
    {F_CCW_NEXT, F_CCW_FINAL, R_START, R_START | DIR_CCW}, // F_CCW_FINAL
    {F_CCW_NEXT, F_CCW_FINAL, F_CCW_BEGIN, R_START},       // F_CCW_NEXT
};
enc_menu_t enc_menu_mode = VOLUME; // Default is VOLUME mode

int playingtime = 0;

#if defined(DISP)
void drawStr(int16_t x, int16_t y, const char *str)
{
  if (dispmode != DSP_LOWBATT)
  {
    u8g2.drawStr(x, y, str);
  }
}

void drawUTF8(int16_t x, int16_t y, const char *str)
{
  if (dispmode != DSP_LOWBATT)
  {
    u8g2.drawUTF8(x, y, str);
  }
}

void clearLines()
{
  station[0] = '\0';
  artist[0] = '\0';
  title[0] = '\0';
#if defined(DISP)
  for (uint8_t row = 0; row < ROWSNUM; row++)
  {
    updateLine(row, "");
  }
#endif
}
#endif

#if defined(SDCARD)

bool getStopped()
{
  return ((audio.getAudioFileDuration() == 0) && !audio.isRunning());
}

//**************************************************************************************************
//                                        C B  _ M P 3 L I S T                                     *
//**************************************************************************************************
// Callback function for handle_mp3list, will be called for every chunk to send to client.         *
// If no more data is available, this function will return 0.                                      *
//**************************************************************************************************
size_t cb_mp3list(uint8_t *buffer, size_t maxLen, size_t index)
{
  static int i;             // Index in track list
  static const char *path;  // Pointer in file path
  size_t len = 0;           // Number of bytes filled in buffer
  char *p = (char *)buffer; // Treat as pointer to aray of char
  static bool eolSeen;      // Remember if End Of List

  if (index == 0) // First call for this page?
  {
    path = getSDFileName(0);     // Yes, make trackfile seek and get FIRST path from list
    strcpy(fullName, path);      // Copy first path to fullName
    eolSeen = (path == nullptr); // Any file?
    i = 1;                       // Set index (track number) for second track
  }
  while ((maxLen > len) && (!eolSeen)) // Space for another char from path?
  {
    if (*path) // End of path?
    {
      *p++ = *path++; // No, add another character to send buffer
      len++;          // Update total length
    }
    else
    {
      // End of path
      if (i) // At least one path in output?
      {
        *p++ = '\n'; // Yes, add separator
        len++;       // Update total length
      }
      path = getNextEntry();
      i++;
      if (i > SD_filecount) // No more files?
      {
        eolSeen = true; // Yes, stop
        break;
      }
    }
  }
  // We arrive here if output buffer is completely full or end of tracklist is reached
  return len; // Return filled length of buffer
}

//**************************************************************************************************
//                                    H A N D L E _ M P 3 L I S T                                  *
//**************************************************************************************************
// Called from mp3play page to list all the MP3 tracks.                                            *
// It will handle the chunks for the client.  The buffer is filled by the callback routine.        *
//**************************************************************************************************
void handle_mp3list(AsyncWebServerRequest *request)
{
  AsyncWebServerResponse *response;
  response = request->beginChunkedResponse("text/plain", cb_mp3list);
  response->addHeader("Server", config->hostnm);
  request->send(response);
}
#endif

char *cpycharar(char *destination, const char *source, size_t num)
{
  destination[0] = '\0';
  return strncat(destination, source, num);
}

uint8_t getPresetByNr(uint32_t val)
{
  for (uint8_t i = 0; i < presetnum; i++)
  {
    if (presets[i].nr == val)
    {
      return i;
    }
  }
  return 255;
}

uint8_t getCmdByCode(uint32_t val)
{
  for (uint8_t i = 0; i < irnum; i++)
  {
    if (ir_cmds[i].ircode == val)
    {
      return i;
    }
  }
  return 255;
}

#if defined(SDCARD)
void Random()
{
  if (pmode == PM_SDCARD)
  {
    random_ = !random_; // Toggle random play
#if defined(DISP)
    if (random_)
    {
      drawIcon(PI_RANDOM);
    }
    else
    {
      if (loop_)
      {
        drawIcon(PI_REPEAT);
      }
      else
      {
        clearIcons(5);
      }
    }
#endif
    sendRndLoop();
  }
}

void Repeat()
{
  if (!random_)
  {
#if defined(DISP)
    loop_ = !loop_;
    if (loop_)
    {
      drawIcon(PI_REPEAT);
    }
    else
    {
      clearIcons(5);
    }
#endif
    sendRndLoop();
  }
}
#endif

#if defined(DISP)
void updateLine(uint8_t row, char *input)
{
  uint8_t k = 0;
  uint8_t val;
  uint8_t skip = 0;
  rows[row].lock = 1;
  char *p = input;
  for (uint8_t i = 0; i < strlen(input); i++)
  {
    if (skip > 0)
    {
      skip--;
    }
    else
    {
      rows[row].rowmap[k] = i;
      k++;
      val = (uint8_t)*p;
      if ((val & 0b10000000) == 0) // One-byte utf-8 char
      {
        p++;
        continue;
      }
      else if ((val & 0b11100000) == 0b11000000) // Two-bytes utf-8 char
      {
        skip = 1;
      }
      else if ((val & 0b11110000) == 0b11100000) // Three-bytes utf-8 char
      {
        skip = 2;
      }
      else if ((val & 0b11110000) == 0b11110000) // Four-bytes utf-8 char
      {
        skip = 3;
      }
    }
    p++;
  }
  rows[row].rowmap[k] = strlen(input);
  rows[row].rowlen = k;
  cpycharar(rows[row].input, input, BUFFLEN - 1);

  if (k <= ROW_LEN)
  {
    rows[row].type = 1;
    strcat(rows[row].input, gaps); // padded with spaces
  }
  else
  {
    strncat(rows[row].input, gaps, config->scrollgap);
    strncat(rows[row].input, input, rows[row].rowmap[ROW_LEN + 1]);
    rows[row].type = 2;
  }
  rows[row].lock = 0;
  rows[row].updated = true;
}
#endif

// cleanText is used only in the case of the station name during the URL test from the WEB !!!
void cleanText(char *txt)
{
  uint8_t val;
  uint8_t skip = 0;
  char *p = txt;
  for (uint8_t i = 0; i < strlen(txt); i++)
  {
    if (skip > 0)
    {
      skip--;
      p++;
      continue;
    }
    val = (uint8_t)*p;
    if ((val & 0b10000000) == 0) // One-byte utf-8 char
    {
      p++;
      continue;
    }
    if (((val & 0b11100000) == 0b11000000) && ((uint8_t)*(p + 1) > 0x7F)) // Two-bytes utf-8 char
    {
      skip = 1;
      p++;
      continue;
    }
    if (((val & 0b11110000) == 0b11100000) && ((uint8_t)*(p + 1) > 0x7F) && ((uint8_t)*(p + 2) > 0x7F)) // Three-bytes utf-8 char
    {
      skip = 2;
      p++;
      continue;
    }
    if (((val & 0b11110000) == 0b11110000) && ((uint8_t)*(p + 1) > 0x7F) && ((uint8_t)*(p + 2) > 0x7F) && ((uint8_t)*(p + 3) > 0x7F)) // Four-bytes utf-8 char
    {
      skip = 3;
      p++;
      continue;
    }
    if (val > 0x7F)
    {
      *p = REPLACEMENTCHARACTER;
    }
    p++;
  }
}

#if defined(DISP)
void show_station(char *info)
{
  updateLine(0, info);
}

void show_artist(char *info)
{
  if (enc_menu_mode == VOLUME)
  {
    updateLine(1, info);
  }
}

void show_title(char *info)
{
  if (enc_menu_mode == VOLUME)
  {
    updateLine(2, info);
  }
}
#endif

// ESP32-audioI2S optional callbacks:
// #if defined(DEBUG)
void audio_info(const char *info)
{
  Serial.print("I2S AUDIO_INFO        ");
  Serial.println(info);
}
// #endif

void audio_id3data(const char *info)
{
  Serial.print("id3data     ");
  Serial.println(info);
  char *indx;
  indx = strstr(info, "Artist: ");
  if (indx == info)
  {
    cpycharar(artist, info + 8, strlen(info) - 8);
    updateartist = true;
  }
  else
  {
    indx = strstr(info, "Title: ");
    if (indx == info)
    {
      cpycharar(title, info + 7, strlen(info) - 7);
      updatetitle = true;
    }
    else
    {
      indx = strstr(info, "Album: ");
      if (indx == info)
      {
        cpycharar(station, info + 7, strlen(info) - 7);
        updatealbum = true;
      }
    }
  }
}

#if defined(SDCARD)
void handleEOF()
{
  if (random_)
  {
    setTrack(-1);
  }
  else
  {
    if (getNextSDFileName())
    {
      SD_ix = SD_curindex;
      setTrack(SD_ix);
    }
  }
}

void audio_eof_mp3(const char *info)
{
  handleEOF();
}
#endif

// I decided to use exclusively a dedicated station name, defined in the playlist.
// The callback "audio_showstation" is used only in case of URL-test !!!
//--------------------------------------------------------------------------------

void audio_showstation(const char *info)
{
  if (testurlFlag)
  {
    char tmp[BUFFLEN];
    char *tmpbf = tmp;
    snprintf(tmp, BUFFLEN, info);
    cleanText(tmpbf);
    snprintf(station, BUFFLEN, tmpbf);
    reqpreset = 255;
#if defined(DISP)
    show_station(tmpbf);
#endif
    updatemetadata = true;
    testurlFlag = false;
  }
}

void audio_showstreamtitle(const char *info)
{
  ESP_LOGW(TAG, " : \"%s\"", info);
  if (enc_menu_mode == VOLUME)
  {
    char *inx;
    uint8_t len;
    const char *p = info;
    uint16_t limit = BUFFLEN - 6;
    title[0] = '\0'; // preventive cleaning - otherwise if the separator is missing,
                     // the original content remains there
    char *_title = artist;
    inx = strstr(info, " - "); // Find separator between artist and title
    if (inx)                   // Separator found?
    {
      _title = title;
      len = (uint8_t)(inx - info);
      if (len < BUFFLEN) // no need to shorten the artist
      {
        cpycharar(artist, info, len);
        p = inx + 3; //+ strlen(" - ")
      }
      else // artist needs to be shortened
      {
        artist[0] = '\0';
        // it is very unlikely that the length of artist would exceed the buffer...
        // so nothing will happen, in that case we will display nothing
      }
    }
    // now the "title" part needs to be processed (it starts at position "p")
    // the integrity of the words is preserved when truncating
    if ((uint16_t)strlen(p) > (BUFFLEN - 1)) // title needs to be shortened
    {
      const char *q = p;
      const char *last = p;
      for (uint16_t i = 0; i <= limit; i++)
      {
        if (*q == ' ')
        {
          last = (char *)q;
        }
        q++;
      }
      if (last > p)
      {
        memcpy(_title, p, (int)(last - p));
        memcpy(_title + (int)(last - p), " ...", 5);
      }
      else
      {
        cpycharar(_title, p, BUFFLEN - 1);
      }
    }
    else // no need to shorten the title
    {
      strcpy(_title, p);
    }
    updatemetadata = true;
  }
}

#if defined(DISP)
void drawColon(bool disp)
{
  uint8_t posy = HGT - (42 / 2);
  uint8_t posx = WID / 2;
  uint16_t colcolor = TFT_WHITE;
  if (disp)
  {
#if defined(AUTOSHUTDOWN)
    if (pwofftime > 0)
    {
      colcolor = TFT_RED;
    }
#endif
    tft.fillCircle(posx, posy - 8, 3, colcolor);
    tft.fillCircle(posx, posy + 8, 3, colcolor);
  }
  else
  {
    tft.fillCircle(posx, posy - 8, 3, TFT_BLACK);
    tft.fillCircle(posx, posy + 8, 3, TFT_BLACK);
  }
}
#endif

void testUrl(const char *url)
{
  testurlFlag = true;
  artist[0] = '\0';
  title[0] = '\0';
#if defined(DISP)
  cpycharar(testurl, url, BUFFLEN - 1);
#endif
  reqpreset = 255;
}

#if defined(DISP)
#if defined(BATTERY)
void batbar()
{
  if (config->batenabled)
  {
    uint16_t val = adcval;
    val = (val > config->bat0) ? val - config->bat0 : 0;
    val = (val < config->batw) ? val : config->batw;
    uint8_t perc = 100 * (val / (float)config->batw);
    if (config->lowbatt)
    {
      if (perc <= config->critbatt)
      {
        changeDispMode(DSP_LOWBATT);
        return;
      }
      else if (dispmode == DSP_LOWBATT)
      {
        changeDispMode(DSP_RADIO);
      }
    }

    if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
    {
      uint8_t w1 = uint8_t((val / (float)config->batw) * (WID - 2)); // uint16_t ?
      batsprite.fillSprite(TFT_RED);
      batsprite.fillRect(0, 0, w1, 6 - 2, TFT_GREEN);
      batbarflag = true; // flag for display loop
    }
  }
}
#endif // BATTERY

void volumebar(uint8_t val)
{
  if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
  {
    uint8_t w1 = uint8_t((val / 100.0) * (WID - 2));
    volsprite.fillSprite(TFT_RED);
    volsprite.fillRect(0, 0, w1, 9 - 2, TFT_GREEN);
    volbarflag = true; // flag for display loop
  }
}

void prgrssbar(uint32_t val, bool clr)
{
  if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
  {
    if (clr)
    {
      prgrsssprite.fillSprite(TFT_BLACK);
      prgrssbarflag = true;
    }
    else
    {
      uint8_t w1 = 0;
      uint32_t drtn = audio.getAudioFileDuration();
      if (drtn)
      {
        w1 = uint8_t((val / (float)drtn) * (WID - 2));
      }
      if (w1 != oldprgrssw)
      {
        prgrsssprite.fillSprite(TFT_DARKGREY);
        prgrsssprite.fillRect(0, 0, w1, 9 - 2, TFT_BLUE);
        prgrssbarflag = true; // vlajka pro display loop, aby se vykreslil volumebar
        oldprgrssw = w1;
      }
    }
  }
}

void drawIcon(uint8_t id)
{
  if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
  {
    tft.fillRect(PIparams[id].x, PIparams[id].y, 15, 15, TFT_BLACK);
    u8g2.setFont(SD_player);
    u8g2.setForegroundColor(PIparams[id].color);
    u8g2.drawGlyph(PIparams[id].x, PIparams[id].y2, PIparams[id].id);
    u8g2.setForegroundColor(TFT_WHITE);
    u8g2.setFont(font);
    if (PIparams[id].idx)
    {
      tft.fillRect(PIparams[PIparams[id].idx].x, PIparams[2].y, 32, 15, TFT_BLACK);
    }
  }
}

void clearIcons(uint8_t id)
{
  if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
  {
    tft.fillRect(PIparams[id].x, PIparams[2].y, 32, 15, TFT_BLACK);
  }
}

void sdp_icons()
{
#if defined(SDCARD)
  if (pmode == PM_SDCARD)
  {
    if (audio.isRunning())
    {
      drawIcon(PI_PLAY);
    }
    else if (audio.getAudioFileDuration() == 0) // stopped
    {
      drawIcon(PI_STOP);
    }
    else // paused
    {
      drawIcon(PI_PAUSE);
    }
    if (random_)
    {
      drawIcon(PI_RANDOM);
    }
    else if (loop_)
    {
      drawIcon(PI_REPEAT);
    }
  }
#endif
}

void restoreIcons()
{
  drawIcon((uint8_t)pmode); // Radio OR SD player
  drawIcon((uint8_t)muteflag + 7);
  sdp_icons();
}

#endif // DISP

void setMutepin(uint8_t mute_, bool test)
{
  if (config->mutepin != 255)
  {
    if (test && muteflag)
    {
      return;
    }
    digitalWrite(config->mutepin, mute_); // turn on/off (unmute/mute) the amplifier
  }
}

void mute(bool source)
{
  uint8_t ypos = 0;
  if (muteflag)
  {
    setMutepin(1, false);
    audio.setVolume(0);
#if defined(DISP)
    dispmuteflag = 1;
#endif
    if (source)
    {
      sendMute(muteflag);
    }
  }
  else
  {
    setMutepin(0, false);
    audio.setVolume(reqvol);
#if defined(DISP)
    dispmuteflag = 0;
#endif
    if (source)
    {
      sendMute(muteflag);
    }
  }
}

//**************************************************************************************************
//                                          I S R _ E N C _ S W I T C H                            *
//**************************************************************************************************
// Interrupts received from rotary encoder switch.                                                 *
//**************************************************************************************************
void IRAM_ATTR isr_enc_switch()
{
  sv_ uint32_t oldtime = 0; // Time in millis previous interrupt
  sv_ bool sw_state;        // True is pushed (LOW)

  bool newstate;    // Current state of input signal
  uint32_t newtime; // Current timestamp
  uint32_t dtime;   // Time difference with previous interrupt
  newstate = (digitalRead(config->encswpin) == LOW);
  newtime = xTaskGetTickCount();        // Time of last interrupt
  dtime = (newtime - oldtime) & 0xFFFF; // Compute delta
  if (dtime < 50)                       // Debounce
  {
    return; // Ignore bouncing
  }
  if (newstate != sw_state) // State changed?
  {
    oldtime = newtime;   // Time of change for next compare
    sw_state = newstate; // Yes, set current (new) state
    if (!sw_state)       // SW released?
    {
      if ((dtime) > 2000) // More than 2 second?
      {
        longclick = true; // Yes, register tripleclick
        clickcount = 0;   // Forget normal count
      }
      else
      {
        clickcount++; // Yes, click detected
      }
      enc_inactivity = 0; // Not inactive anymore
      idleTimer = 0;
    }
  }
}

//**************************************************************************************************
//                                          I S R _ E N C _ T U R N                                *
//**************************************************************************************************
// This is an adaptation of Ben Buxton's excellent rotary library                                  *
// http://www.buxtronix.net/2011/10/rotary-encoders-done-properly.html                             *
// The code uses a Finite State Machine (FSM) algorithm that follows a Gray Code sequence          *
//**************************************************************************************************
void IRAM_ATTR isr_enc_turn()
{
  // Get state of input pins.
  uint8_t pin_state = (digitalRead(config->encclkpin) << 1) +
                      digitalRead(config->encdtpin);
  // Determine new state from the pins and state table.
  table_state = _ttable_full[table_state & 0xf][pin_state];

  // Return emit bits, i.e. the generated event.
  uint8_t event = table_state & 0x30;
  switch (event)
  {
  case DIR_CW:
    rotationcount++;
    break;
  case DIR_CCW:
    rotationcount--;
    break;
  default:
    break;
  }
  enc_inactivity = 0; // Mark activity
  idleTimer = 0;
}

#if defined(AUTOSHUTDOWN)
//**************************************************************************************************
//                                          P W _ O F F                                            *
//**************************************************************************************************
// Interrupts received from ON/OFF switch.                                                         *
//**************************************************************************************************
void IRAM_ATTR pw_OFF()
{
  sv_ uint32_t pwoldtime = 0; // Time in millis previous interrupt
  sv_ bool pwsw_state;        // True is pushed (HIGH)

  bool pwnewstate;    // Current state of input signal
  uint32_t pwnewtime; // Current timestamp
  uint32_t pwdtime;   // Time difference with previous interrupt
  pwnewstate = (digitalRead(config->onoffipin) == HIGH);
  pwnewtime = xTaskGetTickCount();            // Time of last interrupt
  pwdtime = (pwnewtime - pwoldtime) & 0xFFFF; // Compute delta
  if (pwdtime < 50)                           // Debounce
  {
    return; // Ignore bouncing
  }
  if (pwnewstate != pwsw_state) // State changed?
  {
    pwoldtime = pwnewtime;   // Time of change for next compare
    pwsw_state = pwnewstate; // Yes, set current (new) state
    if (!pwsw_state)         // Button released?
    {
      if ((pwdtime) > 3000) // More than 3 second?
      {
        pwoff_req = true;
      }
      else
      {
        pwoffclick = true;
        enc_inactivity = 0; // force encoder activity
      }
    }
  }
}
#endif

#if defined(SDCARD)
//**************************************************************************************************
//                                          S D _ i n s e r t e d                                  *
//**************************************************************************************************
// Interrupts received from SD detect pin.                                                         *
//**************************************************************************************************
void IRAM_ATTR SD_inserted()
{
  sv_ uint32_t sdoldtime = 0;                           // Time in millis previous interrupt
                                                        //  sv_ bool sdsw_state;        // True is pushed (LOW)
  bool sdnewstate = digitalRead(config->sddpin) == LOW; // Current state of input signal
  uint32_t sdnewtime = xTaskGetTickCount();             // Current timestamp
  uint32_t sddtime = (sdnewtime - sdoldtime) & 0xFFFF;  // Compute delta

  if (sddtime < 100) // Debounce
  {
    return; // Ignore bouncing
  }
  if (sdnewstate != sdin) // State changed?
  {
    sdoldtime = sdnewtime; // Time of change for next compare
    sdin = sdnewstate;
  }
}
#endif

void gettime()
{
  if (!getLocalTime(&timeinfo)) // Reload local time
  {
    ESP_LOGW(TAG, "Failed to obtain time!"); // Error
  }
  sprintf(timetxt, "%02d:%02d:%02d", // Format new time to a string
          timeinfo.tm_hour,
          timeinfo.tm_min,
          timeinfo.tm_sec);
  if (config->calendar)
  {
    uint8_t Mon = timeinfo.tm_mon + 1;
    int Year = timeinfo.tm_year + 1900;
    Weekday = timeinfo.tm_wday - 1;
    int Day = timeinfo.tm_mday;
    if (Weekday == -1)
    {
      Weekday = 6;
    }
    char buffer[16];
    if (strcmp(config->dateformat, "yyyy-mm-dd") == 0)
    {
      sprintf(datetxt, "%04d-%02d-%02d", Year, Mon, Day);
    }
    else if (strcmp(config->dateformat, "dd-mm-yyyy") == 0)
    {
      sprintf(datetxt, "%02d-%02d-%04d", Day, Mon, Year);
    }
    else if (strcmp(config->dateformat, "mm-dd-yyyy") == 0)
    {
      sprintf(datetxt, "%02d-%02d-%04d", Mon, Day, Year);
    }
  }
}

//**************************************************************************************************
//                                          T I M E R 1 0 0                                        *
//**************************************************************************************************
// Called every 100 msec on interrupt level, so must be in IRAM and no lengthy operations          *
// allowed.                                                                                        *
//**************************************************************************************************
void IRAM_ATTR timer100()
{
  sv_ bool shiftflag = false;
  sv_ int16_t count1sec = 0;     // Counter for activatie 5 seconds process
  sv_ int16_t count5sec = 0;     // Counter for activatie 5 seconds process
  sv_ int16_t eqcount = 0;       // Counter for equal number of clicks
  sv_ int16_t oldclickcount = 0; // To detect difference

  if (!shiftflag)
  {
    if (count1sec == 5)
    {
      shiftflag = true;
    }
  }

  if (++count1sec == 10) // 1 second passed?
  {
    proc1s_req = true;
    count1sec = 0; // Reset count
  }

  if (shiftflag && (++count5sec == 50)) // 5 seconds passed?
  {
    proc5s_req = true;
    count5sec = 0; // Reset count
  }

#if defined(DISP)

  if (config->tmode > 0)
  {
    if ((dispmode == DSP_RADIO) && !clockReq)
    {
      if (idleTimer++ >= config->idle)
      {
        clockReq = true;
      }
    }
  }

  if (dispmode != DSP_PRESETNR)
  {
    if ((count5sec % 10) == 0) // One second over?
    {
      if (++timeinfo.tm_sec >= 60) // Yes, update number of seconds
      {
        timeinfo.tm_sec = 0; // Wrap after 60 seconds
        if (++timeinfo.tm_min >= 60)
        {
          timeinfo.tm_min = 0; // Wrap after 60 minutes
          if (++timeinfo.tm_hour >= 24)
          {
            timeinfo.tm_hour = 0; // Wrap after 24 hours
          }
        }
      }
      time_req = true; // Yes, show current time request
    }
  }
#endif

  // Handle rotary encoder. Inactivity counter will be reset by encoder interrupt
  if (enc_inactivity < 36000) // Count inactivity time, but limit to 36000
  {
    enc_inactivity++;
  }
  if (dgt_inactivity < 24) // Count inactivity time, but limit 2.4 sec
  {
    dgt_inactivity++;
  }
  else if (dgt_count > 0)
  {
    digitsReq = true;
  }

  // Now detection of single/double click of rotary encoder switch
  if (clickcount) // Any click?
  {
    if (oldclickcount == clickcount) // Yes, stable situation?
    {
      if (++eqcount == 6) // Long time stable?
      {
        eqcount = 0;
        if (clickcount > 2) // Long click?
        {
          tripleclick = true; // Yes, set result
        }
        else if (clickcount == 2) // Double click?
        {
          doubleclick = true; // Yes, set result
        }
        else
        {
          singleclick = true; // Just one click seen
        }
        clickcount = 0; // Reset number of clicks
      }
    }
    else
    {
      oldclickcount = clickcount; // To detect change
      eqcount = 0;                // Not stable, reset count
    }
  }
}

void updateVolume(int8_t volstep)
{
  float tmpbtvol;
  volstep *= 4;
  if ((reqvol + volstep) < 0) // Limit volume
  {
    reqvol = 0; // Limit to normal values
  }
  else if ((reqvol + volstep) > 100)
  {
    reqvol = 100; // Limit to normal values
  }
  else
  {
    reqvol += volstep;
  }
  audio.setVolume(reqvol);
#if defined(DISP)
  volumebar(reqvol);
#endif
  sendVolume(reqvol);
}

#if defined(DISP)
void changeDispMode(disp_mode_t mode)
{
  if (dispmode == mode)
  {
    idleTimer = 0;
    return;
  }
  if (mode != DSP_LOWBATT)
  {
    if (config->batenabled)
    {
      uint16_t val = adcval;
      val = (val > config->bat0) ? val - config->bat0 : 0;
      val = (val < config->batw) ? val : config->batw;
      uint8_t perc = 100 * (val / (float)config->batw);
      if (perc <= config->critbatt)
      {
        return;
      }
    }
    dispmode = mode;
    asdmode = false;
#if defined(DISP)
    tft.fillScreen(TFT_BLACK);
#endif
    switch (mode)
    {
    case DSP_RADIO:
    case DSP_DIMMED:
      oldtimetxt[0] = '\0';
      restoreIcons();
      if (mode == DSP_RADIO)
      {
        ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight1 : config->backlight1);
      }
      else
      {
        ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight2 : config->backlight2);
      }
      idleTimer = 0;
      u8g2.setFont(font);
      u8g2.setFontDirection(0);
      tft.drawRect(0, 0, WID, 6, TFT_WHITE);
      tft.drawRect(0, 25, WID, 9, TFT_WHITE);
      tft.drawRect(0, 118, WID, 9, TFT_WHITE);
#if defined(BATTERY)
      batbar();
#endif
      if (!muteflag)
      {
        volumebar(reqvol);
      }
      else
      {
        dispmuteflag = 1;
      }
      for (uint8_t row = 0; row < ROWSNUM; row++)
      {
        if (rows[row].type == 1)
        {
          u8h2[row].drawUTF8(0, LINEOFFSET, rows[row].input);
          sprite[row].pushSprite(0, rows[row].ypos);
        }
      }
      enc_inactivity = 0;
      break;

    case DSP_CLOCK:
    case DSP_PWOFF:
      ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight2 : config->backlight2);
      break;
    case DSP_OFFED: // backlight OFF
      ledcWrite(config->bckpin, (config->bckinv) ? 255 : 0);
      break;
    case DSP_PRESETNR:
      u8g2.setFont(u8g2_font_inb42_mn);
      u8g2.setForegroundColor(TFT_WHITE);
      tft.drawRect(WID / 2 - 40, 4 + HGT / 2 - 32, 80, 66, TFT_WHITE);
      break;
    case DSP_ASD:
      u8g2.setFont(font);
      drawUTF8(0, 0 + LINEOFFSET, "Auto-shutdown");
      u8g2.setFont(u8g2_font_inb42_mn);
      u8g2.setForegroundColor(TFT_WHITE);
      tft.drawRect(WID / 2 - 60, 4 + HGT / 2 - 32, 120, 66, TFT_WHITE);
      break;
    case DSP_OTHER:
      ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight1 : config->backlight1);
      tft.fillScreen(TFT_BLACK);
      break;
    }
  }
  else
  {
    dispmode = mode;
    ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight1 : config->backlight1);
    tft.fillScreen(TFT_BLACK);
    u8g2.setFont(font);
    u8g2.setForegroundColor(TFT_RED);
    u8g2.drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, " Low battery !");
  }
}
#endif

#if defined(SDCARD)
void setTrack(int16_t trck)
{
#if defined(DISP)
  clearLines();
#endif
  if (trck < 0) // random
  {
    SD_curindex = (int)random(SD_filecount); // Yes, pick random track
  }
  else
  {
    random_ = false;
    SD_curindex = trck;
  }
  SD_ix = SD_curindex;
  setMutepin(0, true);
  getSDFileName(SD_curindex);
  setSDFileName(mp3spec);
  char *shortname = getShortSDFileName();
  cpycharar(artist, shortname, strlen(shortname));
  updateartist = true;
  sendSDtrack(SD_curindex, shortname);
}
// #endif

void countTrack(int8_t step)
{
  if (step != 0)
  {
    SD_ix += step;
    if (SD_ix >= SD_filecount)
    {
      SD_ix = 0;
    }
    else if (SD_ix < 0)
    {
      SD_ix = SD_filecount - 1;
    }
  }
}

void getAdjacentTrack(int8_t trckstep)
{
  countTrack(trckstep);
#if defined(DISP)
  getSDFileName(SD_ix);
  drawUTF8(0, rows[0].ypos + LINEOFFSET, getShortSDFileName()); // prevents scrolling of a long station title
#endif
}

void updateTrack(int8_t trckstep)
{
  if (!random_)
  {
    countTrack(trckstep);
    setTrack(SD_ix); // not random
  }
  else
  {
    setTrack(-1); // random
  }
}

void pausePlay()
{
  if (pmode == PM_SDCARD)
  {
    if (getStopped())
    {
      SD_oldindex = 65534;
#if defined(DISP)
      drawIcon(PI_PLAY);
#endif
      setMutepin(0, true);
    }
    else
    {
      audio.pauseResume();
      if (audio.isRunning())
      {
        ESP_LOGW(TAG, "Audio RESUME");
#if defined(DISP)
        drawIcon(PI_PLAY);
#endif
        setMutepin(0, true);
      }
      else
      {
        ESP_LOGW(TAG, "Audio PAUSE");
#if defined(DISP)
        drawIcon(PI_PAUSE);
#endif
        setMutepin(1, true);
      }
    }
    uint32_t act = audio.getAudioCurrentTime();
    sendSDstat(act);
#if defined(DISP)
    prgrssbar(act, false);
#endif
  }
}
#endif

void setPreset(uint8_t prst)
{
  artist[0] = '\0';
  title[0] = '\0';
  reqpreset = prst;
  enc_preset = prst;
  char nr[6];
  sprintf(nr, "[%02d] ", presets[prst].nr);
  cpycharar(station, nr, 5);
#if defined(DISP)
  cpycharar(station + 5, presets[prst].name, BUFFLEN - 6);
#endif
  updatemetadata = true;
#if defined(DISP)
  show_station(station);
#endif
  ESP_LOGW(TAG, "Preset is [%d] : %s", prst, presets[prst].url);
}

void updatePreset(int8_t prsstep, bool play)
{
  if ((enc_preset + prsstep) < 0) // Limit
  {
    enc_preset = presetnum - 1; // Limit to normal values
  }
  else if ((enc_preset + prsstep) >= presetnum)
  {
    enc_preset = 0; // Limit to normal values
  }
  else
  {
    enc_preset += prsstep;
  }
  ESP_LOGW(TAG, "Requested preset = %d", enc_preset);
  if (play)
  {
    setPreset(enc_preset);
  }
#if defined(DISP)
  else
  {
    char stname[40];
    sprintf(stname, "[%02d]", presets[enc_preset].nr);
    cpycharar(stname + 4, presets[enc_preset].name, 40 - 6);
    strncat(stname, gaps, 40 - strlen(stname) - 1);
    u8g2.setFont(u8g2_font_t0_17_me);
    drawUTF8(0, rows[0].ypos + LINEOFFSET, stname);
  }
#endif
}

//**************************************************************************************************
//                                       P R O C _ D I G I T                                       *
//**************************************************************************************************
// Processes the digits received by the remote control sensor.                                     *
// Digits can be a maximum of two. After receiving the first digit, a maximum of 1200 ms is waited *
// for the second digit. The command is executed either immediately after receiving the second     *
// digit or 1200 ms after receiving the first digit.                                               *
//**************************************************************************************************
void proc_digit(uint8_t dig)
{
  char dgts[3];
  uint16_t result = 255;
  switch (dgt_count)
  {
  case 0:
    dgt_cmd = dig;
    dgt_count = 1;
    dgt_inactivity = 0;
    sprintf(dgts, "%2d", dig);
#if defined(DISP)
    drawStr(WID / 2 - 34, HGT / 2 + 50 / 2, dgts);

#endif
    break;
  case 1:
    result = 10 * dgt_cmd + dig;
    sprintf(dgts, "%2d", result);
#if defined(DISP)
    drawStr(WID / 2 - 34, HGT / 2 + 50 / 2, dgts);
#endif
    break;
  default:
    break;
  }
  if (result == 0)
  {
#if defined(DISP)
    changeDispMode(DSP_RADIO);
#endif
  }
  else if (result != 255)
  {

    ESP_LOGW(TAG, "Remote IR number: %2d", result); // Result for debugging
    dgt_count = 0;
    if (pmode != PM_RADIO)
    {
      reqpreset = 254;
      audpreset = 254;
      pmode = PM_RADIO;
#if defined(DISP)
      drawIcon(PI_RADIO);
      prgrssbar(0, true);
#endif
    }
    presetReq = result;
  }
}

#if defined(AUTOSHUTDOWN)
//****************************************************************************************************
//                                        P R O C  _ A S D                                           *
//****************************************************************************************************
// Displays the auto-off time entered using the remote control.                                      *
//****************************************************************************************************
void proc_asd()
{
  char dgts[4];
  if (dgt_asd > 0)
  {
    sprintf(dgts, "%3d", dgt_asd);
  }
  else
  {
    strcpy(dgts, "   ");
  }
#if defined(DISP)
  drawStr(WID / 2 - (3 * 35 / 2), HGT / 2 + 50 / 2, dgts);
#endif
}
#endif

//**************************************************************************************************
//                                           C H K _ E N C                                         *
//**************************************************************************************************
// See if rotary encoder is activated and perform its functions.                                   *
//**************************************************************************************************
void chk_enc()
{
  int newinx;
#if defined(AUTOSHUTDOWN)
  char pwoffbuf[16];
#endif
  if (enc_menu_mode != VOLUME) // In no-default mode?
  {
    if (enc_inactivity > 50) // No, more than 5 seconds inactive
    {
      enc_inactivity = 0;
      enc_menu_mode = VOLUME; // Return to VOLUME mode
      ESP_LOGW(TAG, "Encoder mode back to VOLUME");
#if defined(DISP)
      if (dispmode != DSP_LOWBATT)
      {
        dispmode = DSP_OTHER;
        changeDispMode(DSP_RADIO);
      }
#endif
      return;
    }
  }
#if defined(AUTOSHUTDOWN)
  if (singleclick || doubleclick || // Any activity?
      tripleclick || longclick || pwoffclick ||
      (rotationcount != 0))
#else
  if (singleclick || doubleclick || // Any activity?
      tripleclick || longclick ||
      (rotationcount != 0))
#endif

  {
#if defined(DISP)
    if (!(dispmode == DSP_OTHER))
    {
      changeDispMode(DSP_RADIO);
    }
#endif
  }
  else
  {
    return; // No, nothing to do
  }
#if defined(AUTOSHUTDOWN)
  if (pwoffclick) // First handle power off click
  {
    pwoffclick = false; // Reset flag
    if (enc_menu_mode != AUTOPWOFF)
    {

      enc_menu_mode = AUTOPWOFF; // Swich to AUTOPWOFF mode
      ESP_LOGW(TAG, "Encoder mode set to AUTOPWOFF");
#if defined(DISP)
      changeDispMode(DSP_OTHER);
      u8g2.setForegroundColor(TFT_WHITE);
      u8g2.setFont(u8g2_font_t0_17_me);
      drawUTF8(0, HGT - 3 * CELLHGT - 6 + LINEOFFSET, "Turn to schedule");
      drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "aut. shutdown");
      drawUTF8(0, HGT - CELLHGT - 2 + LINEOFFSET, "Press to confirm");
#endif

      pwoffclick = false; // Reset flag
    }
    else
    {
      if (pwofftime > 0)
      {
        ESP_LOGW(TAG, "Automatic shutdown mode canceled !");
      }
      pwofftime = 0;
      pwoffminutes = config->dasd;
      sendAsd(NULL);
      enc_menu_mode = VOLUME; // Back to default mode
#if defined(DISP)
      if (dispmode != DSP_LOWBATT)
      {
        dispmode = DSP_OTHER;
        changeDispMode(DSP_RADIO); // Restore screen
      }
#endif
    }
  }
#endif
  if (longclick) // First handle long click
  {

#if defined(DISP)
    changeDispMode(DSP_OTHER);
    u8g2.setForegroundColor(TFT_WHITE);
    u8g2.setFont(u8g2_font_t0_17_me);
    drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "Press to confirm");
    drawUTF8(0, HGT - CELLHGT - 2 + LINEOFFSET, "reboot request !");
#endif
    ESP_LOGW(TAG, "Long click"); //
    longclick = false;           // Reset flag
    enc_menu_mode = MODECHANGE;  // Swich to MODECHANGE mode
  }

  if (doubleclick) // Handle the doubleclick
  {
    ESP_LOGW(TAG, "Double click");
    doubleclick = false;
    enc_menu_mode = STATIONS; // Swich to STATIONS mode
#if defined(DISP)
    if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
    {
      changeDispMode(DSP_OTHER);
      u8g2.setForegroundColor(TFT_WHITE);
      u8g2.setFont(u8g2_font_t0_17_me);
      strncpy(shorttrname, SPACES, 32); // reset buffer to spaces
      sprintf(shorttrname, "[%02d]", presets[reqpreset].nr);
      strncpy(shorttrname + 4, presets[reqpreset].name, 124);
      drawUTF8(0, rows[0].ypos + LINEOFFSET, shorttrname); // prevents scrolling of a long station title
      drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "Rotate to select");
      drawUTF8(0, HGT - CELLHGT - 2 + LINEOFFSET, "Press to confirm");
    }
#endif
    ESP_LOGW(TAG, "Encoder mode set to STATIONS");
  }

  if (singleclick)
  {
    ESP_LOGW(TAG, "Single click");
    singleclick = false;
    switch (enc_menu_mode) // Which mode (VOLUME, STATIONS)?
    {
    case VOLUME:
      muteflag = !muteflag; // Mute/unmute REQUEST
      ESP_LOGW(TAG, "Mute set to %d", muteflag);
      mute(1);
      break;
    case STATIONS:
      pmode = PM_RADIO;
#if defined(DISP)
      drawIcon(PI_RADIO);
      prgrssbar(0, true);
#endif
      audpreset = 254; // set diff preset
      setPreset(enc_preset);
      enc_menu_mode = VOLUME; // Back to default mode
      break;

#if defined(SDCARD)
    case TRACKS:
      pmode = PM_SDCARD;
#if defined(DISP)
      drawIcon(PI_SDCARD);
      oldprgrssw = -1;
#endif
      SD_oldindex = 65534;
      setTrack(SD_ix);
      enc_menu_mode = VOLUME; // Back to default mode
#if defined(DISP)
      if (dispmode != DSP_LOWBATT)
      {
        dispmode = DSP_OTHER;
        changeDispMode(DSP_RADIO); // Restore screen
      }
#endif
      break;
#endif
    case MODECHANGE:
      shouldReboot = true;
      break;
#if defined(AUTOSHUTDOWN)
    case AUTOPWOFF:
      ESP_LOGW(TAG, "Automatic shutdown occurs after %i minutes", pwoffminutes);
      time(&now);
      pwofftime = now + 60 * pwoffminutes;
      enc_menu_mode = VOLUME; // Back to default mode
      sendAsd(NULL);
#if defined(DISP)
      if (dispmode != DSP_LOWBATT)
      {
        dispmode = DSP_OTHER;
        changeDispMode(DSP_RADIO); // Restore screen
      }
#endif
      break;
#endif
    default:
      break;
    }
  }
  if (tripleclick)
  {
    ESP_LOGW(TAG, "Triple click"); //
    tripleclick = false;           // Reset condition

#if defined(SDCARD)
    if (SD_filecount) // Tracks on SD?
    {
      SD_ix = SD_curindex;
      enc_menu_mode = TRACKS; // Swich to TRACK mode
      ESP_LOGW(TAG, "Encoder mode set to TRACK");
#if defined(DISP)
      char *csfn = getCurrentShortSDFileName();
#endif
#if defined(DISP)
      if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
      {
        changeDispMode(DSP_OTHER);
        u8g2.setForegroundColor(TFT_WHITE);
        u8g2.setFont(u8g2_font_t0_17_me);
        drawUTF8(0, rows[0].ypos + LINEOFFSET, csfn); // prevents scrolling of a long file name
        drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "Rotate to select");
        drawUTF8(0, HGT - CELLHGT - 2 + LINEOFFSET, "Press to confirm");
      }
#endif
    }
    else
    {
      ESP_LOGW(TAG, "No tracks on SD");
    }
#endif
  }

  // HANDLE ROTATIONCOUNT
  if (rotationcount == 0) // Any rotation?
  {
    return; // No, return
  }
  switch (enc_menu_mode) // Which mode (VOLUME, STATIONS, TRACKS, AUTOPWOFF)?
  {
  case VOLUME:
    updateVolume(rotationcount);
    muteflag = false; // Mute off
    mute(1);
    break;
  case STATIONS:
    updatePreset(rotationcount, false);
    break;
#if defined(SDCARD)
  case TRACKS:
    getAdjacentTrack(rotationcount);
    break;
#endif
#if defined(AUTOSHUTDOWN)
  case AUTOPWOFF:
    if (rotationcount != 0)
    {
      newinx = pwoffminutes + rotationcount;
      if (newinx < MINPWOFF)
      {
        newinx = MAXPWOFF;
      }
      else if (newinx > MAXPWOFF)
      {
        newinx = MINPWOFF;
      }
      pwoffminutes = newinx;
      sprintf(pwoffbuf, "%u minutes", pwoffminutes);
      ESP_LOGW(TAG, "%u minutes", pwoffminutes);
#if defined(DISP)
      changeDispMode(DSP_OTHER);
      tft.fillScreen(TFT_BLACK);
      u8g2.setForegroundColor(TFT_WHITE);
      u8g2.setFont(u8g2_font_t0_17_me);
      drawUTF8(0, HGT - CELLHGT - 2 + LINEOFFSET, pwoffbuf);
#endif
    }
    break;
#endif
  default:
    break;
  }
  rotationcount = 0; // Reset
}

#if defined(DISP)

void displayloop(void)
{
  uint8_t offset = 4 + u8h2[0].getFontAscent();

  // handle long lines (scroll)
  uint32_t now_ = millis();
  if (now_ - lastredrw > config->refr)
  {
    lastredrw = now_;
    for (uint8_t row = 0; row < ROWSNUM; row++)
    {
      if (rows[row].type == 2)
      {
        if (rows[row].updated && (rows[row].lock == 0))
        {
          u8h2[row].drawUTF8(0, LINEOFFSET, rows[row].input);
          rows[row].pixlen = CELLWID * (rows[row].rowlen + config->scrollgap);
          rows[row].scrollpos = 0;
          rows[row].updated = false;
        }
        sprite[row].pushSprite(0 - rows[row].scrollpos, rows[row].ypos); // scrolling !!!
        rows[row].scrollpos += xshift;
        if (rows[row].scrollpos >= rows[row].pixlen)
        {
          rows[row].scrollpos -= rows[row].pixlen;
        }
      }
    }
  }

  // handle short lines (no scroll)
  for (uint8_t row = 0; row < ROWSNUM; row++)
  {
    if ((rows[row].type == 1) && rows[row].updated)
    {
      u8h2[row].drawUTF8(0, offset, rows[row].input);
      sprite[row].pushSprite(0, rows[row].ypos);
      rows[row].updated = false;
    }
  }
  uint16_t bcol = TFT_BLACK;
  uint16_t fcol = TFT_LIGHTGREY;
  uint8_t volval = reqvol;

  if (dispmuteflag != -1)
  {
    if (dispmuteflag == 1)
    {
      volval = 0;
    }
    volumebar(volval);
#if defined(DISP)
    drawIcon(dispmuteflag + 7);
#endif
    dispmuteflag = -1;
  }
#if defined(BATTERY)
  if (batbarflag)
  {
    tft.pushImageDMA(1, 1, WID - 2, 6 - 2, batsprPtr);
    batbarflag = false;
  }
#endif
  if (volbarflag)
  {
    tft.pushImageDMA(1, 119, WID - 2, 9 - 2, volsprPtr);
    volbarflag = false;
  }
  if (prgrssbarflag)
  {
    tft.pushImageDMA(1, 26, WID - 2, 9 - 2, prgrsssprPtr);
    prgrssbarflag = false;
  }
}
#endif

bool compareBSSID(const uint8_t *val1, uint8_t val2[6])
{
  for (uint8_t i = 0; i < 6; i++)
  {
    if (*(val1 + i) != (val2[i]))
    {
      return false;
    }
  }
  return true;
}

bool isZero(uint8_t arr[6])
{
  for (uint8_t i = 0; i < 6; i++)
  {
    if (arr[i] != 0)
    {
      return false;
    }
  }
  return true;
}

void audioTask(void *parameter)
{
  while (true)
  {
    audio.loop(); // out radio stream
    if ((pmode == PM_RADIO) && (audpreset != reqpreset))
    {
      if (reqpreset == 255)
      {
        audio.stopSong();
        bool conn = audio.connecttohost(testurl);
        if (!conn)
        {
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          conn = audio.connecttohost(testurl);
        }
        reqpreset = 254;
      }
      else
      {
        bool conn = audio.connecttohost(presets[reqpreset].url);
        if (!conn)
        {
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          conn = audio.connecttohost(presets[reqpreset].url);
        }
      }
      audpreset = reqpreset;
    }
#if defined(SDCARD)
    else if ((pmode == PM_SDCARD) && (SD_curindex != SD_oldindex))
    {
      audio.stopSong();
      if (audio.connecttoFS(SD_MMC, SD_lastmp3spec))
      {
        SD_oldindex = SD_curindex;
      }
      sdp_icons_req = true;
    }
#endif
    vTaskDelay(3 / portTICK_PERIOD_MS); // Give some time for WiFi
  }
}

void secondsToHMS(uint32_t seconds, char timestr[])
{
  uint8_t hrs = seconds / 3600;                    // Number of seconds in an hour
  uint8_t mins = (seconds - hrs * 3600) / 60;      // Remove the number of hours and calculate the minutes.
  uint8_t secs = seconds - hrs * 3600 - mins * 60; // Remove the number of hours and minutes, leaving only seconds.
  if (hrs > 0)
  {
    sprintf(timestr, "%2d:%02d:%02d", hrs, mins, secs); // format new remaining time
  }
  else
  {
    sprintf(timestr, "%5d:%02d", mins, secs); // format new remaining time
  }
}

void initConfig()                        //*
{                                        //*
  size_t cfgsize = sizeof(Config);       //*
  Config cfg_;                           // Only for initConfig() !!!                         //*
  config = (Config *)ps_malloc(cfgsize); // WARNING! Random memory contents !!!               //*
  memcpy(config, &cfg_, cfgsize);        // The default state of the structure is set !!!     //*
}

SET_LOOP_TASK_STACK_SIZE(5120);

#if defined(BATTERY)
void adc_Init(void)
{
  adc_oneshot_unit_init_cfg_t init_config1 =
      {
          .unit_id = ADC_UNIT_1,
          .ulp_mode = ADC_ULP_MODE_DISABLE,
      };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
  adc_oneshot_chan_cfg_t config =
      {
          .atten = ADC_ATTEN_DB_0,
          .bitwidth = ADC_BITWIDTH_12,
      };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, CHANNEL_BAT, &config));
}

int read_adc_input(adc_channel_t channel)
{
  int adc_raw;
  ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, channel, &adc_raw));
  return adc_raw;
}
#endif

void findWifi(bool async)
{
  IPAddress zeroaddr(0, 0, 0, 0);
  WiFi.config(zeroaddr, zeroaddr, zeroaddr); // reset static addres (issue reconnect from static to DHCP)
  WiFi.disconnect(true, true);
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(config->hostnm); // define hostname
  scanmode = (uint8_t)async;
  WiFi.scanNetworks(async, true); // async, show hidden
}

bool wifiFound()
{
  bool wififound = false;
  if (nets == 0)
  {
    ESP_LOGW(TAG, "No networks found");
  }
  else
  {
    int n = nets;
    int indices[n];
    int skip[n];
    for (int i = 0; i < nets; i++)
    {
      indices[i] = i;
    }
    for (int i = 0; i < nets; i++) // sort by RSSI
    {
      for (int j = i + 1; j < nets; j++)
      {
        if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i]))
        {
          std::swap(indices[i], indices[j]);
          std::swap(skip[i], skip[j]);
        }
      }
    }
    for (int i = 0; i < nets; i++)
    {
      ESP_LOGW(TAG, "WLAN %2i: %s", i + 1, WiFi.SSID(indices[i]).c_str());
    }
    for (int i = 0; i < nets; i++)
    {
      const uint8_t *ibssid = WiFi.BSSID(indices[i]);
      for (uint8_t j = 0; j < wlannum; j++)
      {
        if (WiFi.SSID(indices[i]) == wlans[j].ssid)
        {
          if (isZero(wlans[j].bssid)) // the value of bssid does not matter
          {
            ESP_LOGW(TAG, "Best WLAN found: %s", wlans[j].name);
            if (wlans[j].dhcp == 0)
            {
              if (!WiFi.config(wlans[j].ipaddress, wlans[j].gateway, wlans[j].subnet, wlans[j].dnsadd, (uint32_t)0))
              {
                ESP_LOGW(TAG, "STA Failed to configure !");
              }
            }
            WiFi.begin(wlans[j].ssid, wlans[j].pass);
            wififound = true;
            i = nets; // break for outer loop
            break;
          }
          else if (compareBSSID(ibssid, wlans[j].bssid)) // bssid must be the same as entered
          {
            ESP_LOGW(TAG, "Best WLAN found: %s", wlans[j].name);
            if (wlans[j].dhcp == 0)
            {
              if (!WiFi.config(wlans[j].ipaddress, wlans[j].gateway, wlans[j].subnet, wlans[j].dnsadd, (uint32_t)0))
              {
                ESP_LOGW(TAG, "STA Failed to configure !");
              }
            }
            WiFi.begin(wlans[j].ssid, wlans[j].pass, WiFi.channel(indices[i]), wlans[j].bssid);
            wififound = true;
            i = nets; // break for outer loop
            break;
          }
        }
        else if ((WiFi.SSID(indices[i]) == "") && (compareBSSID(ibssid, wlans[j].bssid)))
        {
          ESP_LOGW(TAG, "Best WLAN found: %s", wlans[j].name);
          if (wlans[j].dhcp == 0)
          {
            if (!WiFi.config(wlans[j].ipaddress, wlans[j].gateway, wlans[j].subnet, wlans[j].dnsadd, (uint32_t)0))
            {
              ESP_LOGW(TAG, "STA Failed to configure !");
            }
          }
          WiFi.begin(wlans[j].ssid, wlans[j].pass, WiFi.channel(indices[i]), wlans[j].bssid);
          wififound = true;
          i = nets; // break for outer loop
          break;
        }
      }
    } // for
  } // else (networksFound)
  return wififound;
  WiFi.scanDelete();
}

void note(struct timeval *tv)
{
  struct tm ti;
  getLocalTime(&ti);
  ESP_LOGW(TAG, "TOD synced: %04d-%02d-%02d %02d:%02d:%02d",
           ti.tm_year + 1900,
           ti.tm_mon + 1,
           ti.tm_mday,
           ti.tm_hour,
           ti.tm_min,
           ti.tm_sec);
}

void setup()
{
  const char DEFCFG[] PROGMEM =
      "{\"command\":\"configfile\","
      "\"network\":{"
      "\"apssid\":\"RadioESP32\","
      "\"apaddress\":\"192.168.4.1\","
      "\"apsubnet\":\"255.255.255.0\","
      "\"networks\":[]"
      "},"
      "\"hardware\":{"
#if defined(DISP)
      "\"dsptype\":0,"
      "\"angle\":0,"
      "\"bckpin\":255,"
      "\"bckinv\":0,"
#endif
      "\"bclkpin\":255,"
      "\"doutpin\":255,"
      "\"wspin\":255,"
      "\"extpullup\":0,"
      "\"encclkpin\":255,"
      "\"encdtpin\":255,"
      "\"encswpin\":255,"
      "\"irpin\":255,"
      "\"mutepin\":255,"
#if defined(AUTOSHUTDOWN)
      "\"onoffipin\":255,"
      "\"onoffopin\":255,"
#endif
#if defined(SDCARD)
      "\"sdpullup\":0,"
      "\"sddpin\":255"
#endif
      "},"
      "\"general\":{"
      "\"psswd\":\"YWRtaW4=\","
#if defined(AUTOSHUTDOWN)
      "\"dasd\":20,"
#endif
      "\"bat0\":4094,"
      "\"bat100\":4095,"
      "\"hostnm\":\"RadioESP32\","
      "\"lowbatt\":0,"
      "\"critbatt\":10"
      "},"
      "\"ntp\":{"
      "\"server\":\"pool.ntp.org\","
      "\"interval\":30,"
      "\"timezone\":\"CET-1CEST,M3.5.0,M10.5.0/3\","
      "\"tzname\":\"Europe/Prague\""
      "},"
      "\"radio\":{"
      "\"defstat\":0,"
      "\"defvol\":12,"
      "\"mid\":0,"
      "\"treble\":0,"
      "\"stations\":[]"
      "},"
      "\"irremote\":{"
      "\"commands\":[]"
      "},"
      "\"display\":{"
      "\"backlight1\":150,"
      "\"speed\":5,"
      "\"scrollgap\":5,"
      "\"backlight2\":50,"
      "\"idle\":30,"
      "\"tmode\":3,"
      "\"sdclock\":1,"
      "\"calendar\":0,"
      "\"dateformat\":\"yyyy-mm-dd\","
      "\"monday\":\"Monday\","
      "\"tuesday\":\"Tuesday\","
      "\"wednesday\":\"Wednesday\","
      "\"thursday\":\"Thursday\","
      "\"friday\":\"Friday\","
      "\"saturday\":\"Saturday\","
      "\"sunday\":\"Sunday\""
      "},"
      "\"sdplayer\":{"
      "\"seekstep\":30},"
      "\"default\":1"
      "}";
  maintask = xTaskGetCurrentTaskHandle(); // My taskhandle
  // DEBUG !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait for PlatformIO monitor to start
  // DEBUG !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  Serial.begin(115200);
  ESP_LOGW(TAG, "Starting ...");
  presets = (PRESET *)ps_malloc(100 * sizeof(PRESET));
  wlans = (WLAN *)ps_malloc(8 * sizeof(WLAN));
  station = (char *)ps_malloc(BUFFLEN * sizeof(char));
  artist = (char *)ps_malloc(BUFFLEN * sizeof(char));
  title = (char *)ps_malloc(BUFFLEN * sizeof(char));
  testurl = (char *)ps_malloc(BUFFLEN * sizeof(char));
  RESERVEDGPIOS = (uint8_t *)ps_malloc(16 * sizeof(uint8_t));
  for (uint8_t i = 0; i < 16; i++)
  {
    RESERVEDGPIOS[i] = 0;
  }

#if defined(BATTERY)
  adc_Init();
  Adc1_Buffer = (uint16_t *)ps_malloc(FILTER_LEN * sizeof(uint16_t));
  adcvalraw = read_adc_input(CHANNEL_BAT);
  Sum = adcvalraw << FILTER_SHIFT;
  for (uint8_t i = 0; i < FILTER_LEN; i++)
  {
    Adc1_Buffer[i] = adcvalraw;
  }
#endif

#if defined(DISP)
  shorttrname = (char *)ps_malloc(129 * sizeof(char));
#endif
#if defined(SDCARD)
  fullName = (char *)ps_malloc((MAXFNLEN + 1) * sizeof(char));
  mp3spec = (char *)ps_malloc((MAXFNLEN + 1) * sizeof(char));
  SD_lastmp3spec = (char *)ps_malloc((MAXFNLEN + 1) * sizeof(char));
#endif
  initConfig();
  ir_cmds = (IR_CMD *)ps_malloc(100 * sizeof(IR_CMD));
#if defined(DISP)
  rows = (RowData *)ps_malloc(ROWSNUM * sizeof(RowData));
  PIparams = (pi_params *)ps_malloc(9 * sizeof(pi_params));
  updateLine(0, "");
  updateLine(1, "");
  updateLine(2, "");
#endif

  uint8_t ypos = 0;
  timer = timerBegin(100000);             // 100 kHz (period = 10 us)
  timerAttachInterrupt(timer, &timer100); // Call timer100() on timer alarm
  timerAlarm(timer, 10000, true, 0);      // 10000us * 10 = 100ms
  bool configured = false;
  if (!LittleFS.begin(FSIF)) // Mount and test LittleFS
  {
    ESP_LOGE(TAG, "LittleFS Mount Error!");
  }
  else
  {
    ESP_LOGW(TAG, "LittleFS is okay, space %d, used %d", // Show available LittleFS space
             LittleFS.totalBytes(),
             LittleFS.usedBytes());
    File jsoncfg = LittleFS.open("/config.json", // Try to read from LittleFS file
                                 FILE_READ);
    if (jsoncfg) // Open success?
    {
      jsoncfg.close(); // Yes, close file
    }
    else
    {
      jsoncfg = LittleFS.open("/config.json", FILE_WRITE);
      if (jsoncfg.print(DEFCFG))
      {
        ESP_LOGW(TAG, "Default config.json written !"); // No, show warning, upload data to FS
      }
      else
      {
        ESP_LOGW(TAG, "Default config.json write failed !"); // No, show warning, upload data to FS
      }
      jsoncfg.close();
    }
    configured = loadConfiguration(config);
  }
  WiFi.onEvent(WiFiEvent);
  if (configured)
  {
#if defined(DISP)
    int16_t _height = tft.height();
    int16_t _width = tft.width();
    if (config->default_ && (_height > _width))
    {
      config->angle = 1;
    }
    tft.setRotation(config->angle);
    _height = tft.height();
    _width = tft.width();
    if (_height > _width)
    {
      HGT = _width;
      WID = _height;
    }
    else
    {
      WID = _width;
      HGT = _height;
    }
    switch (config->dsptype)
    {
    case 128:          // DISP ST7735
      LINEOFFSET = 17; // ToDo
      ROW_LEN = 14;
      CELLWID = 11;
      CELLHGT = 22;
      ROWSNUM = 3;
      break;
    default:
      LINEOFFSET = 17; // ToDo
      ROW_LEN = 14;
      CELLWID = 11;
      CELLHGT = 22;
      ROWSNUM = 3;
      break;
    }
    for (int i = 0; i < ROWSNUM; i++)
    {
      rows[i].ypos = 40 + i * (CELLHGT + 5);
    }
    for (int i = 0; i < 9; i++)
    {
      PIparams[i].id = (player_icons)i;
      PIparams[i].x = 34;
      PIparams[i].y = 23 - 15;
      PIparams[i].y2 = 23;
      PIparams[i].idx = 0;
      PIparams[i].color = TFT_WHITE;
    }
    PIparams[0].x = 0;
    PIparams[1].x = 0;
    PIparams[5].x = 51;
    PIparams[6].x = 51;
    PIparams[7].x = 17;
    PIparams[8].x = 17;
    PIparams[0].idx = 2;
    PIparams[8].id = (player_icons)7;
    PIparams[8].color = TFT_RED;

    tft.init(); // ATTENTION! - it must be BEFORE PWM settings !!!

    u8g2.begin(tft);
    tft.initDMA(true);
    for (uint8_t r = 0; r < 3; r++)
    {
      sprite[r].createSprite(BUFFLEN * CELLWID, CELLHGT);
      u8h2[r].begin(sprite[r]);
      u8h2[r].setFont(font);
      u8h2[r].setFontMode(0);      // use u8f2 non-transparent mode
      u8h2[r].setFontDirection(0); // left to right (this is default)
      sprite[r].setColorDepth(1);
      sprite[r].setBitmapColor(TFT_CYAN, TFT_BLACK); // foreground, background
      u8h2[r].setForegroundColor(1);
      u8h2[r].setBackgroundColor(0);
    }
    volsprite.setColorDepth(16);
    volsprPtr = (uint16_t *)volsprite.createSprite(WID - 2, 9 - 2);
    volsprite.fillSprite(TFT_RED);
#if defined(BATTERY)
    batsprite.setColorDepth(16);
    batsprPtr = (uint16_t *)batsprite.createSprite(WID - 2, 6 - 2);
    batsprite.fillSprite(TFT_RED);
#endif
    prgrsssprite.setColorDepth(16);
    prgrsssprPtr = (uint16_t *)prgrsssprite.createSprite(WID - 2, 9 - 2);
    prgrsssprite.fillSprite(TFT_DARKGREY);
    // PWM settings - ATTENTION! - it must be AFTER tft.init() !!!
    if (config->bckpin != 255)
    {
      ledcAttach(config->bckpin, freq, resolution);
      ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight1 : config->backlight1);
    }
    tft.fillScreen(TFT_BLACK);
#endif // DISP

#if defined(AUTOSHUTDOWN)
    pwoffminutes = config->dasd;
#endif
    reqvol = config->defvol;
    if (config->encclkpin != 255 && config->encdtpin != 255 && config->encswpin != 255)
    {
      ;
      attachInterrupt(config->encswpin, isr_enc_switch, CHANGE);
      attachInterrupt(config->encclkpin, isr_enc_turn, CHANGE);
      attachInterrupt(config->encdtpin, isr_enc_turn, CHANGE);
    }
    if (config->irpin != 255)
    {
      IrReceiver.begin(config->irpin, DISABLE_LED_FEEDBACK);
    }
#if defined(AUTOSHUTDOWN)
    if (config->onoffipin != 255 && config->onoffopin != 255)
    {
      pinMode(config->onoffipin, INPUT);
      attachInterrupt(config->onoffipin, pw_OFF, CHANGE); // Interrupts will be handle by ON/OFF button
    }
#endif
    audio.setVolumeSteps(100);
    audio.setVolume(reqvol);
    uint16_t timeout_ms = 1000;
    uint16_t timeout_ms_ssl = 3000;
    audio.setConnectionTimeout(timeout_ms, timeout_ms_ssl);
    audio.setTone(config->bass, config->mid, config->treble);
    ESP_LOGW(TAG, "WiFi Radio mode");
    scanfinished = false;
    findWifi(false); // first call - no async !
    nets = WiFi.scanComplete();
    if (wifiFound())
    {
      while (WiFi.status() != WL_CONNECTED)
      {
        delay(250);
      }
      sntp_set_sync_interval(60000 * config->ntpInterval);
      sntp_set_time_sync_notification_cb(note);
      configTime(0, 0, config->ntpServer);
      setenv("TZ", config->timeZone, 1);
      tzset();
      ESP_LOGW(TAG, "  Setting Timezone to %s\n", config->tzname);
    } // wififound
    else // wifi not found -> AP mode
    {
      WiFi.mode(WIFI_AP_STA);
      STAmode = false;
      IPAddress IP = config->apaddress;
      IPAddress NMask = config->apsubnet;
      WiFi.softAPConfig(IP, IP, NMask);
      WiFi.softAP(config->apssid);
      IPAddress myIP = WiFi.softAPIP();
      ESP_LOGW(TAG, "AP IP address: \"%d.%d.%d.%d\"", myIP[0], myIP[1], myIP[2], myIP[3]);
      APstart = true;
    } // wifi not found

#if defined(DATAWEB)
    File index_html = LittleFS.open("/index.html", // Try to read from LittleFS file
                                    FILE_READ);
    if (index_html) // Open success?
    {
      index_html.close(); // Yes, close file
    }
    else
    {
      ESP_LOGE(TAG, "Web interface incomplete!"); // No, show warning, upload data to FS
    }
#endif // DATAWEB
    if (config->bclkpin != 255 && config->doutpin != 255 && config->wspin != 255)
    {
      audio.setPinout(config->bclkpin, config->wspin, config->doutpin);
    }
    if (config->defstat == 0)
    {
      enc_preset = random(presetnum);
    }
    else
    {
      enc_preset = 0;
      for (uint8_t i = 0; i < presetnum; i++)
      {
        if (presets[i].nr == config->defstat)
        {
          enc_preset = i;
          break;
        }
      }
    } // default station selected
    xTaskCreatePinnedToCore(audioTask, "audio", 4096, NULL, 2, &Task0, 0);
#if defined(SDCARD)
    uint8_t dpin = config->sddpin;
    if (dpin != 255)
    {
      attachInterrupt(dpin, SD_inserted, CHANGE);
      sdin = digitalRead(dpin) == LOW;
    }
    else
    {
      sdin = true; // force change !!!
    }
    // init arrays of pointers
    for (uint8_t ii; ii < MAXFOLDERS; ii++)
    {
      folders[ii] = (char *)ps_malloc((MAXFOLDLEN + 1) * sizeof(char));
    }
    for (uint8_t ii; ii < MAXFILES; ii++)
    {
      files[ii] = (char *)ps_malloc((MAXFILELEN + 1) * sizeof(char));
    }
    xTaskCreatePinnedToCore(
        SDtask,   // Task to get filenames from SD card
        "SDtask", // Name of task.
        4096,     // Stack size of task
        NULL,     // parameter of the task
        1,        // priority of the task
        &xsdtask, // Task handle to keep track of created task
        1);       // Run on CPU 1
#endif
  }
  setupWebServer();
  ESP_LOGW(TAG, "SketchSize:     0x%X", ESP.getSketchSize());
  ESP_LOGW(TAG, "MaxSketchSpace: 0x%X", ESP.getFreeSketchSpace());
  ESP_LOGW(TAG, "Total PSRAM:    0x%X", ESP.getPsramSize());
  ESP_LOGW(TAG, "Free PSRAM:     0x%X", ESP.getFreePsram());
}

void irloop()
{
  uint32_t rawdata = IrReceiver.decodedIRData.decodedRawData;
  if (rawdata > 0)
  {
    uint8_t ix = getCmdByCode(rawdata);
    if (ix < 255)
    {
      enc_inactivity = 0;
      uint8_t ircmd = ir_cmds[ix].ircmd;
      ESP_LOGW(TAG, "IR command: >>> %s <<<", cmd_table[ircmd]);
#ifndef AUTOSHUTDOWN
      if (ircmd > 9)
      {
#if defined(DISP)
        changeDispMode(DSP_RADIO);
#endif
        switch (ircmd)
        {
        case IR_MUTE:
          muteflag = !muteflag;
          mute(1);
          break;
        case IR_VOLP:
          updateVolume(1);
          break;
        case IR_VOLM:
          updateVolume(-1);
          break;
        case IR_CHP:
          updatePreset(1, true);
          break;
        case IR_CHM:
          updatePreset(-1, true);
          break;
        case IR_PP:
          break;
        default:
          ESP_LOGW(TAG, "Unknown IR command %08X", rawdata);
          sendIRcode(rawdata);
          break;
        }
      }
      else
      {
#if defined(DISP)
        changeDispMode(DSP_PRESETNR);
#endif
        proc_digit(ircmd);
      }
// #endif
#else // AUTOSHUTDOWN defined
      if (!asdmode)
      {
        if (ircmd > 9)
        {
#if defined(DISP)
          changeDispMode(DSP_RADIO);
#endif
          switch (ircmd)
          {
          case IR_MUTE:
            muteflag = !muteflag;
            mute(1);
            break;
          case IR_VOLP:
            updateVolume(1);
            break;
          case IR_VOLM:
            updateVolume(-1);
            break;
          case IR_CHP:
            if (pmode == PM_RADIO)
            {
              updatePreset(1, true);
            }
#if defined(SDCARD)
            else
            {
              updateTrack(1);
            }
#endif
            break;
          case IR_CHM:
            if (pmode == PM_RADIO)
            {
              updatePreset(-1, true);
            }
#if defined(SDCARD)
            else
            {
              updateTrack(-1);
            }
#endif
            break;
          case IR_ISD:
            pwoff_req = true;
            break;
          case IR_SSD:
#if defined(DISP)
            changeDispMode(DSP_ASD);
            asdmode = true;
            dgt_count_asd = 0;
            dgt_asd = 0;
#endif
            break;
          case IR_OK:
            break;
          case IR_EX:
            break;
          case IR_BS:
            break;
          case IR_PP:
#if defined(SDCARD)
            pausePlay();
#endif
            break;
#if defined(SDCARD)
          case IR_STOP:
            if (pmode == PM_SDCARD)
            {
              audio.stopSong();
#if defined(DISP)
              drawIcon(PI_STOP);
#endif
              setMutepin(1, true);
              sendSDstat(0);
#if defined(DISP)
              prgrssbar(0, false);
#endif
            }
            break;
          case IR_FORW:
            audio.setTimeOffset(config->seekstep * 1);
            break;
          case IR_BACKW:
            audio.setTimeOffset(config->seekstep * -1);
            break;
          case IR_RNDM:
            Random();
            break;
          case IR_RPT:
            if (pmode == PM_SDCARD)
            {
              Repeat();
            }
            break;
#endif
          case IR_RADIO:
            if (pmode != PM_RADIO)
            {
              audio.stopSong();
              pmode = PM_RADIO;
              setMutepin(0, true);
#if defined(DISP)
              drawIcon(PI_RADIO);
              prgrssbar(0, true);
              clearLines();
#endif
              audpreset = 254;
              setPreset(reqpreset);
            }
            sendRadio(NULL);
            break;
          case IR_SD:
#if defined(SDCARD)
            if (SD_okay)
            {
              if (pmode != PM_SDCARD)
              {
                oldsdix_req = true;
              }
              else
              {
                updateTrack(0);
              }
            }
#endif
            break;
          default:
            ESP_LOGW(TAG, "Unknown IR command %08X", rawdata);
            sendIRcode(rawdata);
            break;
          }
        }
        else
        {
#if defined(DISP)
          changeDispMode(DSP_PRESETNR);
#endif
          proc_digit(ircmd);
        }
      } // !asdmode
      else // asdmode
      {
        if (ircmd <= 9)
        {
          if (dgt_count_asd < 3)
          {
            dgt_count_asd += 1;
            dgt_asd = 10 * dgt_asd + ircmd;
            proc_asd();
          }
        }
        else
        {
          switch (ircmd)
          {
          case IR_OK:
            if (dgt_asd > 0)
            {
              if (dgt_asd > MAXPWOFF)
              {
                dgt_asd = MAXPWOFF;
              }
              else if (dgt_asd < MINPWOFF)
              {
                dgt_asd = MINPWOFF;
              }
              pwoffminutes = dgt_asd;
              time(&now);
              pwofftime = now + 60 * pwoffminutes;
              asdmode = false;
              sendAsd(NULL);
#if defined(DISP)
              if (dispmode != DSP_LOWBATT)
              {
                dispmode = DSP_OTHER;
                changeDispMode(DSP_RADIO); // Restore screen
              }
#endif
            }
            else
            {
              pwofftime = 0;
              pwoffminutes = config->dasd;
              asdmode = false;
              sendAsd(NULL);
              enc_menu_mode = VOLUME; // Back to default mode
#if defined(DISP)
              if (dispmode != DSP_LOWBATT)
              {
                dispmode = DSP_OTHER;
                changeDispMode(DSP_RADIO); // Restore screen
              }
#endif
            }
            break;
          case IR_EX:
            enc_menu_mode = VOLUME; // Back to default mode
#if defined(DISP)
            if (dispmode != DSP_LOWBATT)
            {
              dispmode = DSP_OTHER;
              changeDispMode(DSP_RADIO); // Restore screen
            }
#endif
            break;
          case IR_BS:
            if (dgt_count_asd < 4 && dgt_count_asd > 0)
            {
              dgt_asd = dgt_asd / 10;
              proc_asd();
              dgt_count_asd -= 1;
            }
            break;
          default:
            break;
          }
        }
      }
#endif // AUTOSHUTDOWN
    }
    else
    {
      ESP_LOGW(TAG, "Unknown IR command %08X", rawdata);
      sendIRcode(rawdata);
    }
  }
  IrReceiver.resume();
}

void loop()
{
  char timetxt2[9];
  char shorttimetxt[6];
  char sub[3];
  uint32_t now_;
  uint8_t ix;
  if (gotIP)
  {
    WF_MODE = WF_STA;
#if defined(DISP)
    tft.fillScreen(TFT_BLACK);
    u8g2.setFont(u8g2_font_t0_17_me);
    uint8_t offset = 6 + u8g2.getFontDescent() + u8g2.getFontAscent();
    uint8_t linehgt = offset + 15;
    u8g2.setForegroundColor(TFT_WHITE);
    char charbuf[24];
    sprintf(charbuf, "RadioESP32 - %s", STRINGIFY(VERSION));
    drawUTF8(0, offset, charbuf);
    drawUTF8(0, linehgt + offset, "SSID:");
    drawUTF8(0, 2 * linehgt + offset - 8, WiFi.SSID().c_str());
    drawUTF8(0, 3 * linehgt + offset, "IP Address:");
    IPAddress ip = WiFi.localIP();
    sprintf(charbuf, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    drawUTF8(0, 4 * linehgt + offset - 8, charbuf);
#endif
    if (reconnect)
    {
      vTaskDelay(8000 / portTICK_PERIOD_MS);
      audpreset = 254;
      reconnect = false;
    }
    else
    {
      vTaskDelay(4000 / portTICK_PERIOD_MS);
      reqpreset = enc_preset;
    }
    setMutepin(0, false);
#if defined(DISP)
    changeDispMode(DSP_RADIO);
#endif
    char nr[6];
    sprintf(nr, "[%02d] ", presets[enc_preset].nr);
    cpycharar(station, nr, 5);
    cpycharar(station + 5, presets[enc_preset].name, BUFFLEN - 6);
#if defined(DISP)
    show_station(station);
#endif
    gotIP = false;
  }
  if (APstart)
  {
#if defined(DISP)
    u8g2.setFont(u8g2_font_t0_17_me);
    u8g2.setBackgroundColor(TFT_BLACK);
    u8g2.setForegroundColor(TFT_WHITE);
    uint8_t offset = 6 + u8g2.getFontDescent() + u8g2.getFontAscent();
    uint8_t linehgt = offset + 15;
    char charbuf[24];
    sprintf(charbuf, "RadioESP32 - %s", STRINGIFY(VERSION));
    drawUTF8(0, offset, charbuf);
    u8g2.setBackgroundColor(TFT_WHITE);
    u8g2.setForegroundColor(TFT_BLACK);
    drawUTF8(0, linehgt + offset, "AP SSID:");
    drawUTF8(0, 2 * linehgt + offset - 8, WiFi.softAPSSID().c_str());
    drawUTF8(0, 3 * linehgt + offset, "AP IP Address:");
    IPAddress ip = WiFi.softAPIP();
    sprintf(charbuf, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    drawUTF8(0, 4 * linehgt + offset - 8, charbuf);
    u8g2.setBackgroundColor(TFT_BLACK);
    u8g2.setForegroundColor(TFT_WHITE);
#endif
    APstart = false;
  }
  if (clockReq)
  {
#if defined(DISP)
#if defined(AUTOSHUTDOWN)
    if (pwofftime > 0)
    {
      changeDispMode(DSP_PWOFF);
    }
    else
    {
#endif
      switch (config->tmode)
      {
      case 3:
        changeDispMode(DSP_CLOCK);
        break;
      case 2:
        changeDispMode(DSP_DIMMED);
        break;
      case 1:
        changeDispMode(DSP_OFFED);
        break;
      default:
        break;
      }
#if defined(AUTOSHUTDOWN)
    }
#endif
#endif
    clockReq = false;
  }

  chk_enc(); // Check rotary encoder functions
  if (updatemetadata)
  {
    if (enc_menu_mode == VOLUME)
    {
#if defined(DISP)
      show_title(title);
      show_artist(artist);
#endif
    }
#if defined(DISP)
#if defined(AUTOSHUTDOWN)
    if (dispmode != DSP_ASD)
    {
#endif
      changeDispMode(DSP_RADIO);
#if defined(AUTOSHUTDOWN)
    }
#endif
#endif
    if (pmode == PM_RADIO)
    {
      sendRadio(NULL);
    }
#if defined(SDCARD)
    else
    {
      sendSDplayer(3); // useful for .ogg tracks !!!
    }
#endif
    updatemetadata = false;
  }
  if (updatealbum)
  {
    if (enc_menu_mode == VOLUME)
    {
#if defined(DISP)
      show_station(station);
#endif
    }
#if defined(DISP)
#if defined(AUTOSHUTDOWN)
    if (dispmode != DSP_ASD)
    {
#endif
      changeDispMode(DSP_RADIO);
#if defined(AUTOSHUTDOWN)
    }
#endif
#endif
#if defined(SDCARD)
    sendSDplayer(0);
#endif
    updatealbum = false;
  }
  if (updateartist)
  {
    if (enc_menu_mode == VOLUME)
    {
#if defined(DISP)
      show_artist(artist);
#endif
    }
#if defined(DISP)
#if defined(AUTOSHUTDOWN)
    if (dispmode != DSP_ASD)
    {
#endif
      changeDispMode(DSP_RADIO);
#if defined(AUTOSHUTDOWN)
    }
#endif
#endif
#if defined(SDCARD)
    sendSDplayer(1);
#endif
    updateartist = false;
  }
  if (updatetitle)
  {
    if (enc_menu_mode == VOLUME)
    {
#if defined(DISP)
      show_title(title);
#endif
    }
#if defined(DISP)
#if defined(AUTOSHUTDOWN)
    if (dispmode != DSP_ASD)
    {
#endif
      changeDispMode(DSP_RADIO);
#if defined(AUTOSHUTDOWN)
    }
#endif
#endif
#if defined(SDCARD)
    sendSDplayer(2);
#endif
    updatetitle = false;
  }
#if defined(DISP)
  if (time_req)
  {
    gettime(); // update timetxt (mandatory for big clock !)
    if (WiFi.status() == WL_CONNECTED)
    {
      time_req = false;
      if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
      {
#if defined(DISP)
        u8g2.setFont(u8g2_font_t0_17_mn); // time font
#if defined(AUTOSHUTDOWN)
        if (pwofftime > 0)
        {
          time(&now);
          uint32_t pwofft = pwofftime - now;
          u8g2.setForegroundColor(TFT_RED);
          secondsToHMS(pwofft, timetxt2);
          drawStr(WID - 8 * 9, 21, timetxt2);
          u8g2.setForegroundColor(TFT_WHITE);
        }
        else
        {
#endif
          if (config->sdclock && pmode == PM_SDCARD)
          {
            uint32_t remtime = audio.getAudioFileDuration() - audio.getAudioCurrentTime();
            u8g2.setForegroundColor(TFT_YELLOW);
            secondsToHMS(remtime, timetxt2);
            drawStr(WID - 8 * 9, 21, timetxt2);
            u8g2.setForegroundColor(TFT_WHITE);
          }
          else
          {
            u8g2.setForegroundColor(TFT_WHITE);
            drawStr(WID - 8 * 9, 21, timetxt);
          }
#if defined(AUTOSHUTDOWN)
        }
#endif
#endif
      }
      else if (dispmode == DSP_CLOCK)
      {
        cpycharar(shorttimetxt, timetxt, 5);
        if ((strcmp(shorttimetxt, oldtimetxt) != 0) || (strcmp(datetxt, olddatetxt) != 0))
        {
          cpycharar(olddatetxt, datetxt, 15);
          cpycharar(oldtimetxt, shorttimetxt, 5);
#if defined(DISP)
          u8g2.setFont(u8g2_font_inb42_mn);
          u8g2.setForegroundColor(TFT_WHITE);
          cpycharar(sub, timetxt, 2);
          uint8_t cw = u8g2.getUTF8Width(sub);
          uint8_t colw = min(cw / 2, (WID - 2 * cw));
          drawStr((WID - colw) / 2 - cw, HGT - 1, sub);
          cpycharar(sub, timetxt + 3, 2);
          drawStr((WID + colw) / 2, HGT - 1, sub);
          if (config->calendar)
          {
            u8g2.setFont(u8g2_font_inr19_mn);
            drawStr(0, 24, datetxt);
            u8g2.setFont(font);
            cw = u8g2.getUTF8Width(config->wdays[Weekday]);
            u8g2.drawUTF8((WID - cw) / 2, 60, config->wdays[Weekday]);
          }
#endif
        }
      }
#if defined(AUTOSHUTDOWN)
      else if (dispmode == DSP_PWOFF)
      {
        if (pwofftime > 0)
        {
          time(&now);
          uint32_t pwofft = pwofftime - now;
          sprintf(timetxt2, "%02d:%02d", pwofft / 60, pwofft % 60); // format new remaining time
                                                                    // u8g2.drawStr(WID - 8 * 9, 21, timetxt2);
#if defined(DISP)
          u8g2.setForegroundColor(TFT_RED);
          u8g2.setFont(u8g2_font_inb42_mn);
          cpycharar(sub, timetxt2, 2);
          uint8_t cw = u8g2.getUTF8Width(sub);
          uint8_t colw = min(cw / 2, (WID - 2 * cw));
          drawStr((WID - colw) / 2 - cw, HGT - 1, sub);
          cpycharar(sub, timetxt2 + 3, 2);
          drawStr((WID + colw) / 2, HGT - 1, sub);
#endif
        }
      }
#endif
    }
  }
  if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
  {
    displayloop();
  }

  if (((config->tmode > 0) && (dispmode == DSP_CLOCK)) || (dispmode == DSP_PWOFF))
  {
    // colon blick
    now_ = millis();
    if (now_ - lastblick > 500)
    {
      lastblick = now_;
      coloncolor = !coloncolor;
      drawColon(coloncolor);
    }
  }
#if defined(DISP)
  if (sdp_icons_req)
  {
    sdp_icons();
    sdp_icons_req = false;
  }
#endif
#endif
  if (proc1s_req)
  {
#if defined(SDCARD)
    if (pmode == PM_SDCARD)
    {
      if (audio.isRunning())
      {
        uint32_t pstn = audio.getAudioCurrentTime();
        if (pstn == pastpos)
        {
          if (poscounter++ == 8)
          {
            poscounter = 0;
            pastpos = 0xFFFFFFFF;
            handleEOF();
          }
        }
        else
        {
          pastpos = pstn;
          poscounter = 0;
          if (weso.count() > 0)
          {
            sendSDstat(pstn);
#if defined(DISP)
            prgrssbar(pstn, false);
#endif
          }
        }
      }
    }
#endif
    proc1s_req = false;
  }
  if (proc5s_req)
  {
    ESP_LOGW(TAG, "Free heap, free stack: %s, %d", String(ESP.getFreeHeap()), uxTaskGetStackHighWaterMark(NULL));
    if (weso.count() > 0)
    {
      sendHeartBeat();
    }
#if defined(BATTERY)
    adcvalraw = read_adc_input(CHANNEL_BAT);
    // The following routine calculates the average value from the last FILTER_LEN measurements:
    // =========================================================================================
    Sum -= Adc1_Buffer[Adc1_i];        // Subtract the old value (of position Adc1_i) from the Sum
    Sum += adcvalraw;                  // Add the current value to the Sum
    Adc1_Buffer[Adc1_i++] = adcvalraw; // Store the current value at position Adc1_i and increment the pointer
    Adc1_i &= FILTER_MASK;             // Mask unnecessary bits
    adcval = Sum >> FILTER_SHIFT;      // Calculation of average value
#if defined(DISP)
    batbar();
#endif
#endif
    proc5s_req = false;
  }

  if (WF_MODE == WF_WAITSTA)
  {
    if (!rcnnct)
    {
      scanfinished = false;
      findWifi(true); // async
      rcnnct = true;
    }
    else
    {
      if (scanfinished)
      {
        if (!wifiFound())
        {
          rcnnct = false;
        }
        scanfinished = false;
      }
    }
  }

#if defined(AUTOSHUTDOWN)
  time(&now);
  if (pwoff_req || ((pwofftime > 0) && (now > pwofftime)))
  {
    ESP_LOGW(TAG, "It's time to shut down! GOOD BYE.");
#if defined(DISP)

    changeDispMode(DSP_OTHER);
    u8g2.setForegroundColor(TFT_RED);
    u8g2.setFont(u8g2_font_t0_22_me);
    u8g2.drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "   Bye, bye !");
#endif
    pwofftime = 0;
    pwoff_req = false;
    fade_req = true;
    lastfade = millis();
    uint8_t vol100 = audio.getVolume();
    audio.setVolumeSteps(64);
    audio.setVolume(vol100 * 64 / 100);
  }
  if (fade_req)
  {
    uint8_t fadevol = audio.getVolume();
    if (audio.getVolume() == 0)
    {
      digitalWrite(config->onoffopin, HIGH); // Power off !
    }
    else
    {
      uint32_t fadenow = millis();
      if ((fadenow - lastfade) >= 800)
      {
        lastfade = fadenow;
        if (fadevol > 32)
        {
          fadevol -= 4;
        }
        else if (fadevol > 16)
        {
          fadevol -= 3;
        }
        else if (fadevol > 8)
        {
          fadevol -= 2;
        }
        else
        {
          fadevol -= 1;
        }
        audio.setVolume(fadevol);
        if (fadevol == 0)
        {
          digitalWrite(config->onoffopin, HIGH); // Power off !
        }
      }
    }
  }
#endif

  if (shouldReboot)
  {
    ESP_LOGW(TAG, "System is going to reboot ...");
#if defined(DISP)
    tft.fillScreen(TFT_BLACK);
    u8g2.setCursor(30, 45);
    u8g2.setForegroundColor(TFT_WHITE);
    u8g2.print("Reboot ...");
#endif
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP.restart();
  }
  if (formatreq)
  {
    shouldReboot = true; // display ...
    ESP_LOGW(TAG, "Factory reset initiated ...");
#if defined(DATAWEB)
    File cfgjson = LittleFS.open("/config.json", FILE_READ);
    if (cfgjson) // Open success?
    {
      cfgjson.close(); // Yes, close file
      LittleFS.remove("/config.json");
    }
#else
    LittleFS.end();
    weso.enable(false);
    LittleFS.format();
#endif
#if defined(DISP)
    tft.fillScreen(TFT_BLACK);
    u8g2.setCursor(0, 45);
    u8g2.setForegroundColor(TFT_WHITE);
    u8g2.print("Factory reset...");
#endif
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP.restart();
  }
  if (IrReceiver.decode())
  {
    irloop();
  }
  if (digitsReq)
  {
    digitsReq = false;
    dgt_count = 0;
#if defined(DISP)
    changeDispMode(DSP_RADIO);
#endif
    if (dgt_cmd != 0)
    {
      ix = getPresetByNr(dgt_cmd);
      if (ix != 255)
      {
        enc_preset = ix;
        setPreset(enc_preset);
      }
    }
  }
  if ((digtime != 0))
  {
    if ((millis() - digtime) > 1200)
    {
#if defined(DISP)
      if ((dispmode != DSP_RADIO) && (dispmode != DSP_DIMMED))
      {
        changeDispMode(DSP_RADIO);
      }
#endif
      digtime = 0;
    }
  }
  if (presetReq != 255)
  {
    ix = getPresetByNr(presetReq);
    presetReq = 255;
    if (ix != 255)
    {
      enc_preset = ix;
      setPreset(enc_preset);
    }
    digtime = millis();
  }
#if defined(SDCARD)
  if (sdready_req)
  {
    if (SD_okay)
    {
      SD_ix = 0;
      SD_curindex = 0;
      SD_oldindex = 0;
    }
    else
    {
      SD_filecount = 0;
    }
    sendSDready();
    sdready_req = false;
  } //

  if (oldsdix_req)
  {
    pmode = PM_SDCARD;
#if defined(DISP)
    drawIcon(PI_SDCARD);
    oldprgrssw = -1;
#endif
    audio.stopSong();
#if defined(DISP)
    clearLines();
#endif
    getSDFileName(SD_curindex);
    char *shortname = getShortSDFileName();
    SD_oldindex = 65534;
    updateTrack(0);
    oldsdix_req = false;
  }
#endif
}

void WiFiEvent(WiFiEvent_t event, arduino_event_info_t info)
{
  char s[40];
  switch (event)
  {
  case ARDUINO_EVENT_WIFI_SCAN_DONE:
    cpycharar(s, "ARDUINO_EVENT_WIFI_SCAN_DONE", 34);
    if (scanmode == 1) // reconnect request
    {
      nets = WiFi.scanComplete();
      scanfinished = true;
      ESP_LOGW(TAG, "Scan complete. Number of networks found: %d", nets);
    }
    else if (scanmode == 2) // web client request
    {
      nets = WiFi.scanComplete();
      sendScanResult(nets, NULL);
    }
    // else                  // scanmode == 0 ... setup() request
    //{
    // }
    break;

  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    STAmode = true;
    cpycharar(s, "WIFI_EVENT_STA_CONNECTED", 34);
    break;

  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    gotIP = true;
    cpycharar(s, "ARDUINO_EVENT_WIFI_STA_GOT_IP", 34);
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    if (WF_MODE != WF_WAITSTA)
    {
      reconnect = true;
      rcnnct = false;
      WF_MODE = WF_WAITSTA;
    }
    cpycharar(s, "ARDUINO_EVENT_WIFI_STA_DISCONNECTED", 36);
    break;
    /*
      case ARDUINO_EVENT_WIFI_READY:
        cpycharar(s, "ARDUINO_EVENT_WIFI_READY", 34);
        break;

      case ARDUINO_EVENT_WIFI_STA_START:
        cpycharar(s, "ARDUINO_EVENT_WIFI_STA_START", 34);
        break;

      case ARDUINO_EVENT_WIFI_STA_STOP:
        cpycharar(s, "ARDUINO_EVENT_WIFI_STA_STOP", 34);
        break;

      case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
        cpycharar(s, "ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE", 34);
        break;

      case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        cpycharar(s, "ARDUINO_EVENT_WIFI_STA_LOST_IP", 34);
        break;

      case WIFI_EVENT_STA_WPS_ER_SUCCESS:
        cpycharar(s, "WIFI_EVENT_STA_WPS_ER_SUCCESS", 34);
        break;

      case WIFI_EVENT_STA_WPS_ER_FAILED:
        cpycharar(s, "WIFI_EVENT_STA_WPS_ER_FAILED", 34);
        break;

      case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
        cpycharar(s, "WIFI_EVENT_STA_WPS_ER_TIMEOUT", 34);
        break;

      case WIFI_EVENT_STA_WPS_ER_PIN:
        cpycharar(s, "WIFI_EVENT_STA_WPS_ER_PIN", 34);
        break;

      case ARDUINO_EVENT_WIFI_AP_START:
        cpycharar(s, "ARDUINO_EVENT_WIFI_AP_START", 34);
        break;

      case ARDUINO_EVENT_WIFI_AP_STOP:
        cpycharar(s, "ARDUINO_EVENT_WIFI_AP_STOP", 34);
        break;

      case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        cpycharar(s, "ARDUINO_EVENT_WIFI_AP_STACONNECTED", 35);
        break;

      case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        cpycharar(s, "ARDUINO_EVENT_WIFI_AP_STADISCONNECTED", 34);
        break;

      case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
        cpycharar(s, "ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED", 34);
        break;
    */
  default:
    cpycharar(s, "[ UNKNOWN ]", 34);
    break;

  } // switch
  if (strcmp(s, "[ UNKNOWN ]") != 0)
  {
    ESP_LOGW(TAG, "WiFi-event: >>>%s<<< (%i)", s, (int)event);
  }
} // WiFiEvent
