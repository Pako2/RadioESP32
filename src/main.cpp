#define MYSTR(A) #A
#define STRINGIFY(A) MYSTR(A)
#define REPLACEMENTCHARACTER '*'
#define sv_ DRAM_ATTR static volatile

#include "Arduino.h"
#include "WiFi.h"
#include <ArduinoJson.h>
#define NO_LED_FEEDBACK_CODE        // saves 92 bytes program memory
#define EXCLUDE_UNIVERSAL_PROTOCOLS // Saves up to 1000 bytes program memory.
#define EXCLUDE_EXOTIC_PROTOCOLS    // saves around 650 bytes program memory if all other protocols are active
#include <ESPAsyncWebServer.h>
#include <IRremote.hpp>
#include "Audio.h"
Audio audio;
#include "HW_Manager.h"
Config *config;
#if defined(AUTOSHUTDOWN)
bool fade_req = false; // Set fade requested
#endif
#include "display.h"
#include <Wire.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "esp_wifi.h"

#include <LittleFS.h>
#if DATAWEB
#include <FS_editor.h>
#else
// from vendors - fix
#include "webh/radio_app/glyphicons-halflings-regular.woff.gz.h"
#include "webh/radio_app/required.css.gz.h"
#include "webh/radio_app/required.js.gz.h"
// custom, can be updated and changed - generated from files in the LittleFS partition using Custom scripts
#include "webh/radio_app/radioesp32.js.gz.h"
#include "webh/radio_app/radioesp32.html.gz.h"
#include "webh/radio_app/index.html.gz.h"
#endif
#include <time.h>
#include "esp_sntp.h"
#include "esp_log.h"

// Enums and structs
enum mode_wifi
{
  WF_NONE,
  WF_STA,
  WF_WAITSTA
};
mode_wifi WF_MODE = WF_NONE;

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

#if defined(SDCARD)
bool sdin = false;
bool oldsdin = false;
#endif

// Forward declaration
void WiFiEvent(WiFiEvent_t, arduino_event_info_t);
void jumptoupman();

#if defined(SDCARD)
void Random();
void Repeat();
void updateTrack(int8_t trckstep);
void setTrack(int16_t trck);
void pausePlay();
void handle_mp3list(AsyncWebServerRequest *request);
#endif
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
#endif
bool sdp_icons_req = false;

const char SPACES[33] = "                                ";
char *shorttrname;
uint32_t lastblick;
bool coloncolor = false;
uint32_t textcolor = 1;
uint32_t backcolor = 0;
char olddatetxt[16]; // Converted timeinfo/date

struct WLAN *wlans;
struct PRESET *presets;

#if defined(AUTOSHUTDOWN)
const uint8_t cmd_table_len = 28;
#else
const uint8_t cmd_table_len = 26;
#endif
uint8_t *RESERVEDGPIOS;
char *testurl;
int Weekday;
uint8_t presetnum = 0;
uint8_t wlannum = 0;
bool shouldReboot = false;
bool formatreq = false;

/*
Username and password are used in two cases:
============================================
1. to enter the website editor
2. to enter the Update Manager
*/
const char *http_username = "admin";
const char *http_password = "admin";

TaskHandle_t Task0, xsdtask, maintask;

#define FSIF true // Format LittleFS if not existing
bool STAmode = true;
bool APstart = false;
bool gotIP = false;
uint8_t scanmode = 0;
unsigned long digtime = 0;
uint16_t dgt_cmd; // Buffer for digit input

#if defined(AUTOSHUTDOWN)
uint16_t dgt_asd;          // Buffer for digit input for AUTOSHUTDOWN
uint8_t dgt_count_asd = 0; // Digit input count
#define MAXPWOFF 100
#define MINPWOFF 1
uint32_t lastfade = 0;
uint16_t pwoffminutes = MAXPWOFF; // Current power off [minutes] duration
time_t now;
#endif
uint8_t presetReq = 255;
bool testurlFlag = false;
uint16_t reqpreset = 255;
uint16_t audpreset = 255;
bool reconnect = false;
bool rcnnct = false;
bool scanfinished = false;
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

bool updatemetadata = false;
bool updatealbum = false;
bool updateartist = false;
bool updatetitle = false;
uint8_t messageid = 0;

bool config_req = false;

#include "configManager.h"
#include "wsResponses.h"
#include "websocket.h"
#include "webserver.h"
#if defined(SDCARD)
#include "SDcard.h" // For SD card interface
#endif

const char *TAG = "main"; // For debug lines

char timetxt[9];  // Converted timeinfo
char datetxt[16]; // Converted timeinfo/date

struct tm timeinfo; // Will be filled by NTP server
unsigned long currentMillis = 0;

// Rotary encoder stuff
int16_t enc_preset = 0; // Selected preset

int playingtime = 0;
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

#if defined(SDCARD)
void Random()
{
  if (pmode == PM_SDCARD)
  {
    random_ = !random_; // Toggle random play
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
    sendRndLoop();
  }
}

void Repeat()
{
  if (!random_)
  {
    loop_ = !loop_;
    if (loop_)
    {
      drawIcon(PI_REPEAT);
    }
    else
    {
      clearIcons(5);
    }
    sendRndLoop();
  }
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

void audio_id3data(const char *info)
{
#if defined(DEBUG)
  Serial.print("id3data     ");
  Serial.println(info);
#endif
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
    show_station(tmpbf);
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

// ESP32-audioI2S callbacks:
void audio_info(Audio::msg_t m)
{
#if defined(DEBUG)
  Serial.printf("I2S AUDIO_INFO        %s: %s\n", m.s, m.msg);
#endif
  switch (m.e)
  {
  case Audio::evt_streamtitle:
    audio_showstreamtitle(m.msg);
    break;
  case Audio::evt_eof:
    audio_eof_mp3(m.msg);
    break;
  case Audio::evt_id3data:
    audio_id3data(m.msg);
    break; // id3-data or metadata
  case Audio::evt_name:
    audio_showstation(m.msg);
    break; // station name or icy-name

    /*        case Audio::evt_info:           audioinfo(m.msg); break;
            case Audio::evt_bitrate:        Serial.printf("bitrate: .... %s\n", m.msg); break; // icy-bitrate or bitrate from metadata
            case Audio::evt_icyurl:         Serial.printf("icy URL: .... %s\n", m.msg); break;
            case Audio::evt_lasthost:       Serial.printf("last URL: ... %s\n", m.msg); break;
            case Audio::evt_icylogo:        Serial.printf("icy logo: ... %s\n", m.msg); break;
            case Audio::evt_icydescription: Serial.printf("icy descr: .. %s\n", m.msg); break;
            case Audio::evt_image: for(int i = 0; i < m.vec.size(); i += 2){
                                            Serial.printf("cover image:  segment %02i, pos %07lu, len %05lu\n", i / 2, m.vec[i], m.vec[i + 1]);} break; // APIC
            case Audio::evt_lyrics:         Serial.printf("sync lyrics:  %s\n", m.msg); break;
            case Audio::evt_log   :         Serial.printf("audio_logs:   %s\n", m.msg); break;
    */
  default:
    break;
  }
}

void testUrl(const char *url)
{
  testurlFlag = true;
  station[0] = '\0';
  show_station((char *)">> T E S T <<");
  artist[0] = '\0';
  title[0] = '\0';
  reqpreset = 253;
  sendRadio();
  cpycharar(testurl, url, BUFFLEN - 1);
  reqpreset = 255;
}

void mute(bool source)
{
  uint8_t ypos = 0;
  if (muteflag)
  {
    setMutepin(1, false);
    audio.setVolume(0);
    dispmuteflag = 1;
    if (source)
    {
      sendMute(muteflag);
    }
  }
  else
  {
    setMutepin(0, false);
    audio.setVolume(reqvol);
    dispmuteflag = 0;
    if (source)
    {
      sendMute(muteflag);
    }
  }
}

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
  volumebar(reqvol);
  sendVolume(reqvol);
}

#if defined(SDCARD)
void setTrack(int16_t trck)
{
  clearLines();
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
  getSDFileName(SD_ix);
  drawUTF8(0, rows[0].ypos + LINEOFFSET, getShortSDFileName()); // prevents scrolling of a long station title
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
      drawIcon(PI_PLAY);
      setMutepin(0, true);
    }
    else
    {
      audio.pauseResume();
      if (audio.isRunning())
      {
        ESP_LOGW(TAG, "Audio RESUME");
        drawIcon(PI_PLAY);
        setMutepin(0, true);
      }
      else
      {
        ESP_LOGW(TAG, "Audio PAUSE");
        drawIcon(PI_PAUSE);
        setMutepin(1, true);
      }
    }
    uint32_t act = audio.getAudioCurrentTime();
    sendSDstat(act);
    prgrssbar(act, false);
  }
}
#endif

void setPreset(uint8_t prst)
{
  artist[0] = '\0';
  title[0] = '\0';
  audpreset = 254; // set diff preset
  reqpreset = prst;
  enc_preset = prst;
  char nr[6];
  sprintf(nr, "[%02d] ", presets[prst].nr);
  cpycharar(station, nr, 5);
  cpycharar(station + 5, presets[prst].name, BUFFLEN - 6);
  updatemetadata = true;
  show_station(station);
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
  else
  {
    char stname[40];
    sprintf(stname, "[%02d]", presets[enc_preset].nr);
    cpycharar(stname + 4, presets[enc_preset].name, 40 - 6);
    strncat(stname, gaps, 40 - strlen(stname) - 1);
    u8g2.setFont(u8g2_font_t0_17_me);
    drawUTF8(0, rows[0].ypos + LINEOFFSET, stname);
  }
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
    drawStr(WID / 2 - 34, HGT / 2 + 50 / 2, dgts);
    break;
  case 1:
    result = 10 * dgt_cmd + dig;
    sprintf(dgts, "%2d", result);
    drawStr(WID / 2 - 34, HGT / 2 + 50 / 2, dgts);
    break;
  default:
    break;
  }
  if (result == 0)
  {
    changeDispMode(DSP_RADIO);
  }
  else if (result != 255)
  {

    ESP_LOGW(TAG, "Remote IR number: %2d", result); // Result for debugging
    dgt_count = 0;
    if (pmode != PM_RADIO)
    {
      reqpreset = 254;
      pmode = PM_RADIO;
      drawIcon(PI_RADIO);
      prgrssbar(0, true);
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
  drawStr(WID / 2 - (3 * 35 / 2), HGT / 2 + 50 / 2, dgts);
}
#endif

void bootToPartition(esp_partition_subtype_t subtype, const char *name)
{
  ESP_LOGW(TAG, ">>> Finding the right partition: %s...\n", name);
  const esp_partition_t *target_part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, NULL);

  if (target_part != NULL)
  {
    esp_err_t err = esp_ota_set_boot_partition(target_part);
    if (err == ESP_OK)
    {
      ESP_LOGW(TAG, "Bootloader set to %s. Preparing hardware reset...\n", name);
      // 1. Stopping software services
      weso.closeAll();
      weso.enable(false);
      server.end();
      vTaskDelay(pdMS_TO_TICKS(100)); // Time for FreeRTOS to delete network tasks

      // 2. Correctly turning off Wi-Fi via Arduino API
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      vTaskDelay(pdMS_TO_TICKS(50));
      // delay(50);

      // 3. Low-level hardware halt (ESP-IDF)
      // In ESP-IDF v5.x it is safer to stop the hardware first
      esp_wifi_stop();
      // 4. Deinitialization itself
      // We will use a condition so that deinit only happens if the stack is really active
      esp_err_t err = esp_wifi_deinit();
      if (err != ESP_OK)
      {
        ESP_LOGW(TAG, "[WiFi] Low-level deinit status / note: %s\n", esp_err_to_name(err));
      }
      Serial.flush();

      // 5. Essential pause to clear hardware cache and DMA controller
      vTaskDelay(pdMS_TO_TICKS(200));
      // 6. NVS deinitialization (flash write and close)
      nvs_flash_deinit();
      vTaskDelay(pdMS_TO_TICKS(200));
      // DISABLE INTERRUPTS - FreeRTOS and all tasks freeze a second before death
      noInterrupts();

      // DIRECT HARDWARE RESET VIA CONTROLLER RTC REGISTERS
      // Address 0x3ff48000 is the base address of RTC_CNTL on ESP32
      // Writing the value will cause an immediate system reboot of the entire SoC at the silicon level
      *((volatile uint32_t *)(0x3ff48000 + 0x00)) = 0x9c000000;
      // A processor trap from which there is no return
      while (1)
      {
        asm volatile("nop");
      }
    }
    else
    {
      ESP_LOGW(TAG, "ERROR writing to bootloader: %d\n", err);
    }
  }
  else
  {
    ESP_LOGW(TAG, "ERROR: Partition %s not found in memory!\n", name);
  }
}

void jumptobtls()
{
  tft.fillScreen(TFT_BLACK);
  u8g2.setFont(u8g2_font_t0_17_me);
  drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "Jump to");
  drawUTF8(0, HGT - CELLHGT - 2 + LINEOFFSET, "Bluetooth LS");
  ESP_LOGW(TAG, "Jump to bluetooth loadspeaker !"); //
  enc_menu_mode = MODECHANGE;                       // Swich to MODECHANGE mode
  sendJump(1);
  bootToPartition(ESP_PARTITION_SUBTYPE_APP_OTA_1, "BT Loadspeaker (app1)");
}

void jumptoupman()
{
  tft.fillScreen(TFT_BLACK);
  u8g2.setFont(u8g2_font_t0_17_me);
  drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "Jump to");
  drawUTF8(0, HGT - CELLHGT - 2 + LINEOFFSET, "Update Manager");
  ESP_LOGW(TAG, "Jump to Update Manager !"); //
  enc_menu_mode = MODECHANGE;                // Swich to MODECHANGE mode
  sendJump(0);
  bootToPartition(ESP_PARTITION_SUBTYPE_APP_FACTORY, "Update Manager (factory)");
}

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
      if (dispmode != DSP_LOWBATT)
      {
        dispmode = DSP_OTHER;
        changeDispMode(DSP_RADIO);
      }
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
    if (dispmode != DSP_OTHER)
    {
      changeDispMode(DSP_RADIO);
    }
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
      changeDispMode(DSP_OTHER);
      u8g2.setForegroundColor(TFT_WHITE);
      u8g2.setFont(u8g2_font_t0_17_me);
      drawUTF8(0, HGT - 3 * CELLHGT - 6 + LINEOFFSET, "Turn to schedule");
      drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "aut. shutdown");
      drawUTF8(0, HGT - CELLHGT - 2 + LINEOFFSET, "Press to confirm");
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
      if (dispmode != DSP_LOWBATT)
      {
        dispmode = DSP_OTHER;
        changeDispMode(DSP_RADIO); // Restore screen
      }
    }
  }
#endif
  if (longclick) // First handle long click
  {
    changeDispMode(DSP_OTHER);
    enc_menu_mode = MENU;
    menuinx = 0;
    updateMenu(0);
    longclick = false; // Reset flag
  }

  if (doubleclick) // Handle the doubleclick
  {
    ESP_LOGW(TAG, "Double click");
    doubleclick = false;
    enc_menu_mode = STATIONS; // Swich to STATIONS mode
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
      drawIcon(PI_RADIO);
      prgrssbar(0, true);
      setPreset(enc_preset);
      enc_menu_mode = VOLUME; // Back to default mode
      break;

#if defined(SDCARD)
    case TRACKS:
      pmode = PM_SDCARD;
      drawIcon(PI_SDCARD);
      oldprgrssw = -1;
      SD_oldindex = 65534;
      setTrack(SD_ix);
      enc_menu_mode = VOLUME; // Back to default mode
      if (dispmode != DSP_LOWBATT)
      {
        dispmode = DSP_OTHER;
        changeDispMode(DSP_RADIO); // Restore screen
      }
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
      if (dispmode != DSP_LOWBATT)
      {
        dispmode = DSP_OTHER;
        changeDispMode(DSP_RADIO); // Restore screen
      }
      break;
#endif
    case MENU:
      tft.fillScreen(TFT_BLACK);
      switch (purport[menuinx])
      {
      case 0:
        pwoff_req = true;
        enc_menu_mode = MODECHANGE; // Swich to MODECHANGE mode
        break;
      case 1:
        jumptoupman();
        break;
      case 2:
        dispmode = DSP_OTHER;
        changeDispMode(DSP_RADIO); // Restore screen
        break;
      case 3:
        jumptobtls();
        break;
      default:
        break;
      }
      break;
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
      char *csfn = getCurrentShortSDFileName();
      if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
      {
        changeDispMode(DSP_OTHER);
        u8g2.setForegroundColor(TFT_WHITE);
        u8g2.setFont(u8g2_font_t0_17_me);
        drawUTF8(0, rows[0].ypos + LINEOFFSET, csfn); // prevents scrolling of a long file name
        drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "Rotate to select");
        drawUTF8(0, HGT - CELLHGT - 2 + LINEOFFSET, "Press to confirm");
      }
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
      changeDispMode(DSP_OTHER);
      tft.fillScreen(TFT_BLACK);
      u8g2.setForegroundColor(TFT_WHITE);
      u8g2.setFont(u8g2_font_t0_17_me);
      drawUTF8(0, HGT - CELLHGT - 2 + LINEOFFSET, pwoffbuf);
    }
    break;
#endif
  case MENU:
    if (rotationcount != 0)
    {
      menuinx += rotationcount;
      if (menuinx < 0)
      {
        menuinx = menu_table_len - 1;
      }
      else if (menuinx >= menu_table_len)
      {
        menuinx = 0;
      }
      updateMenu(menuinx);
    }
    break;
  default:
    break;
  }
  rotationcount = 0; // Reset
}

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
        audpreset = 254;
      }
      else if (reqpreset <= presetnum)
      {
        testurlFlag = false;
        bool conn = audio.connecttohost(presets[reqpreset].url);
        if (!conn)
        {
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          conn = audio.connecttohost(presets[reqpreset].url);
        }
        audpreset = reqpreset;
      }
    }
#if defined(SDCARD)
    else if ((pmode == PM_SDCARD) && (SD_curindex != SD_oldindex))
    {
      audio.stopSong();
      if (audio.connecttoFS(SD_MMC, SD_lastmp3spec, 0))
      {
        SD_oldindex = SD_curindex;
      }
      sdp_icons_req = true;
    }
#endif
    vTaskDelay(3 / portTICK_PERIOD_MS); // Give some time for WiFi
  }
}

SET_LOOP_TASK_STACK_SIZE(5120);

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
  esp_ota_mark_app_valid_cancel_rollback();

  vTaskDelay(100 / portTICK_PERIOD_MS);   // delay before PSRAM use
  maintask = xTaskGetCurrentTaskHandle(); // My taskhandle
  // DEBUG !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  // vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait for PlatformIO monitor to start
  // DEBUG !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  Serial.begin(115200);
  ESP_LOGW(TAG, "Starting ...");

  ESP_LOGW(TAG, "SketchSize:     0x%X", ESP.getSketchSize());
  ESP_LOGW(TAG, "MaxSketchSpace: 0x%X", ESP.getFreeSketchSpace());
  if (psramFound())
  {
    ESP_LOGW(TAG, "Total PSRAM:    0x%X", ESP.getPsramSize());
    ESP_LOGW(TAG, "Free PSRAM:     0x%X", ESP.getFreePsram());
  }
  Audio::audio_info_callback = audio_info;
  presets = (PRESET *)ps_malloc(100 * sizeof(PRESET));
  wlans = (WLAN *)ps_malloc(8 * sizeof(WLAN));
  testurl = (char *)ps_malloc(BUFFLEN * sizeof(char));

  // init some values:
  cpycharar(presets[0].name, "", 1);
  cpycharar(presets[0].url, "", 1);
  //

  RESERVEDGPIOS = (uint8_t *)ps_malloc(16 * sizeof(uint8_t));
  for (uint8_t i = 0; i < 16; i++)
  {
    RESERVEDGPIOS[i] = 0;
  }

  shorttrname = (char *)ps_malloc(129 * sizeof(char));
#if defined(SDCARD)
  fullName = (char *)ps_malloc((MAXFNLEN + 1) * sizeof(char));
  mp3spec = (char *)ps_malloc((MAXFNLEN + 1) * sizeof(char));
  SD_lastmp3spec = (char *)ps_malloc((MAXFNLEN + 1) * sizeof(char));
#endif
  initConfig();
  vTaskDelay(100 / portTICK_PERIOD_MS);
  ir_cmds = (IR_CMD *)ps_malloc(100 * sizeof(IR_CMD));

  uint8_t ypos = 0;

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
    updateBinariesJson();
    File jsoncfg = LittleFS.open("/config.json", // Try to read from LittleFS file
                                 FILE_READ);
    if (jsoncfg) // Open success?
    {
      ESP_LOGW(TAG, "Config.json file opened successfully !");
      jsoncfg.close(); // Yes, close file
    }
    else
    {
      ESP_LOGW(TAG, "File config.json creating ...");
      jsoncfg = LittleFS.open("/config.json", FILE_WRITE);
      if (saveConfigToJSON())
      {
        ESP_LOGW(TAG, "Default config.json written !"); // No, show warning, upload data to FS
        jsoncfg.close();
      }
      else
      {
        ESP_LOGW(TAG, "Default config.json write failed !"); // No, show warning, upload data to FS
        ESP_LOGW(TAG, "I can't continue, I'm stopping.");    // No, show warning, upload data to FS
        jsoncfg.close();
        while (1)
        {
          asm volatile("nop");
          vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
      }
    }
    configured = loadConfiguration();
  }
  ESP_LOGW(TAG, "Free PSRAM:     0x%X", ESP.getFreePsram());

  ESP_LOGW(TAG, "Audio version:  %s", audio.getVersion());

  WiFi.onEvent(WiFiEvent);
  if (configured)
  {

#if defined(AUTOSHUTDOWN)
    pwoffminutes = config->dasd;
#endif
    reqvol = config->defvol;

    initHardwareManager(audio, config, timeinfo);
    setupDisplay();

    if (config->irpin != 255)
    {
      IrReceiver.begin(config->irpin, DISABLE_LED_FEEDBACK);
    }

    uint16_t timeout_ms = 1000;
    uint16_t timeout_ms_ssl = 3000;
    audio.setVolumeSteps(100);
    audio.setVolume(reqvol);
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

#if DATAWEB
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
        changeDispMode(DSP_RADIO);
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
        changeDispMode(DSP_PRESETNR);
        proc_digit(ircmd);
      }
// #endif
#else // AUTOSHUTDOWN defined
      if (!asdmode)
      {
        if (ircmd > 9)
        {
          changeDispMode(DSP_RADIO);
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
            changeDispMode(DSP_ASD);
            asdmode = true;
            dgt_count_asd = 0;
            dgt_asd = 0;
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
              drawIcon(PI_STOP);
              setMutepin(1, true);
              sendSDstat(0);
              prgrssbar(0, false);
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
              drawIcon(PI_RADIO);
              prgrssbar(0, true);
              clearLines();
              setPreset(reqpreset);
            }
            sendRadio();
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
          changeDispMode(DSP_PRESETNR);
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
              if (dispmode != DSP_LOWBATT)
              {
                dispmode = DSP_OTHER;
                changeDispMode(DSP_RADIO); // Restore screen
              }
            }
            else
            {
              pwofftime = 0;
              pwoffminutes = config->dasd;
              asdmode = false;
              sendAsd(NULL);
              enc_menu_mode = VOLUME; // Back to default mode
              if (dispmode != DSP_LOWBATT)
              {
                dispmode = DSP_OTHER;
                changeDispMode(DSP_RADIO); // Restore screen
              }
            }
            break;
          case IR_EX:
            enc_menu_mode = VOLUME; // Back to default mode
            if (dispmode != DSP_LOWBATT)
            {
              dispmode = DSP_OTHER;
              changeDispMode(DSP_RADIO); // Restore screen
            }
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

void stopAudioForUpdate()
{
  ESP_LOGW(TAG, "Preparing to receive configuration file. Stopping audio subsystem...");
  if (Task0 != NULL)
  {
    vTaskDelete(Task0);
    Task0 = NULL;
  }
  audio.stopSong();
  vTaskDelay(500 / portTICK_PERIOD_MS);
  ESP_LOGW(TAG, "Audio completely released. No memory collisions!");
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
    dispmode = DSP_OTHER;
    tft.fillScreen(TFT_BLACK);
    u8g2.setFont(u8g2_font_t0_17_me);
    uint8_t offset = 6 + u8g2.getFontDescent() + u8g2.getFontAscent();
    uint8_t linehgt = offset + 15;
    u8g2.setForegroundColor(TFT_WHITE);
    char charbuf[24];
    sprintf(charbuf, "RadioESP32 %s", STRINGIFY(VERSION));
    drawUTF8(0, offset, charbuf);
    drawUTF8(0, linehgt + offset, "SSID:");
    drawUTF8(0, 2 * linehgt + offset - 8, WiFi.SSID().c_str());
    drawUTF8(0, 3 * linehgt + offset, "IP Address:");
    IPAddress ip = WiFi.localIP();
    sprintf(charbuf, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    drawUTF8(0, 4 * linehgt + offset - 8, charbuf);
    if (reconnect)
    {
      vTaskDelay(8000 / portTICK_PERIOD_MS);
      if (pmode == PM_RADIO)
      {
        audpreset = 254;
      }
      reconnect = false;
    }
    else
    {
      vTaskDelay(4000 / portTICK_PERIOD_MS);
      reqpreset = enc_preset;
    }
    setMutepin(0, false);
    changeDispMode(DSP_RADIO);
    if (pmode == PM_RADIO)
    {
      char nr[6];
      sprintf(nr, "[%02d] ", presets[enc_preset].nr);
      cpycharar(station, nr, 5);
      cpycharar(station + 5, presets[enc_preset].name, BUFFLEN - 6);
      show_station(station);
    }
    gotIP = false;
  }
  if (APstart)
  {
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
    APstart = false;
  }
  if (clockReq)
  {
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
    clockReq = false;
  }

  chk_enc(); // Check rotary encoder functions
  if (updatemetadata)
  {
    if (enc_menu_mode == VOLUME)
    {
      show_title(title);
      show_artist(artist);
    }
#if defined(AUTOSHUTDOWN)
    if (dispmode != DSP_ASD)
    {
#endif
      changeDispMode(DSP_RADIO);
#if defined(AUTOSHUTDOWN)
    }
#endif
    if (pmode == PM_RADIO)
    {
      sendRadio();
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
      show_station(station);
    }
#if defined(AUTOSHUTDOWN)
    if (dispmode != DSP_ASD)
    {
#endif
      changeDispMode(DSP_RADIO);
#if defined(AUTOSHUTDOWN)
    }
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
      show_artist(artist);
    }
#if defined(AUTOSHUTDOWN)
    if (dispmode != DSP_ASD)
    {
#endif
      changeDispMode(DSP_RADIO);
#if defined(AUTOSHUTDOWN)
    }
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
      show_title(title);
    }
#if defined(AUTOSHUTDOWN)
    if (dispmode != DSP_ASD)
    {
#endif
      changeDispMode(DSP_RADIO);
#if defined(AUTOSHUTDOWN)
    }
#endif
#if defined(SDCARD)
    sendSDplayer(2);
#endif
    updatetitle = false;
  }
  if (time_req)
  {
    gettime(); // update timetxt (mandatory for big clock !)
    if (WiFi.status() == WL_CONNECTED)
    {
      time_req = false;
      if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
      {
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
#endif // auto
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
#endif // auto
      }
      else if (dispmode == DSP_CLOCK)
      {
        cpycharar(shorttimetxt, timetxt, 5);
        if ((strcmp(shorttimetxt, oldtimetxt) != 0) || (strcmp(datetxt, olddatetxt) != 0))
        {
          cpycharar(olddatetxt, datetxt, 15);
          cpycharar(oldtimetxt, shorttimetxt, 5);
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
            u8g2.drawUTF8(0, 60, SPACES); // clear line
            u8g2.drawUTF8((WID - cw) / 2, 60, config->wdays[Weekday]);
          }
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
          u8g2.setForegroundColor(TFT_RED);
          u8g2.setFont(u8g2_font_inb42_mn);
          cpycharar(sub, timetxt2, 2);
          uint8_t cw = u8g2.getUTF8Width(sub);
          uint8_t colw = min(cw / 2, (WID - 2 * cw));
          drawStr((WID - colw) / 2 - cw, HGT - 1, sub);
          cpycharar(sub, timetxt2 + 3, 2);
          drawStr((WID + colw) / 2, HGT - 1, sub);
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
  if (sdp_icons_req)
  {
    sdp_icons();
    sdp_icons_req = false;
  }
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
            prgrssbar(pstn, false);
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
    adcvalraw = read_bat_adc_input();
    // The following routine calculates the average value from the last FILTER_LEN measurements:
    // =========================================================================================
    Sum -= Adc1_Buffer[Adc1_i];        // Subtract the old value (of position Adc1_i) from the Sum
    Sum += adcvalraw;                  // Add the current value to the Sum
    Adc1_Buffer[Adc1_i++] = adcvalraw; // Store the current value at position Adc1_i and increment the pointer
    Adc1_i &= FILTER_MASK;             // Mask unnecessary bits
    adcval = Sum >> FILTER_SHIFT;      // Calculation of average value
    batbar();
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
    changeDispMode(DSP_OTHER);
    u8g2.setForegroundColor(TFT_RED);
    u8g2.setFont(u8g2_font_t0_22_me);
    u8g2.drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "   Bye, bye !");
    pwofftime = 0;
    pwoff_req = false;
    fade_req = true;
    lastfade = millis();
    uint8_t vol100 = audio.getVolume();
    audio.setVolumeSteps(100);
  }
  if (fade_req)
  {
    uint8_t fadevol = audio.getVolume();
    if (audio.getVolume() == 0)
    {
      powerOff();
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
          powerOff();
        }
      }
    }
  }
#endif

  if (shouldReboot)
  {
    ESP_LOGW(TAG, "System is going to reboot ...");
    tft.fillScreen(TFT_BLACK);
    u8g2.setCursor(30, 45);
    u8g2.setForegroundColor(TFT_WHITE);
    u8g2.print("Reboot ...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP.restart();
  }
  if (formatreq)
  {
    shouldReboot = true; // display ...
    ESP_LOGW(TAG, "Factory reset initiated ...");
#if DATAWEB
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
    tft.fillScreen(TFT_BLACK);
    u8g2.setCursor(0, 45);
    u8g2.setForegroundColor(TFT_WHITE);
    u8g2.print("Factory reset...");
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
    changeDispMode(DSP_RADIO);
    if (dgt_cmd != 0)
    {
      ix = getPresetByNr(dgt_cmd);
      if (ix != 255)
      {
        if (pmode != PM_RADIO)
        {
          reqpreset = 254;
          pmode = PM_RADIO;
        }
        enc_preset = ix;
        ESP_LOGW(TAG, "Remote IR number: %2d", ix); // For debugging
        setPreset(enc_preset);
      }
    }
  }
  if ((digtime != 0))
  {
    if ((millis() - digtime) > 1200)
    {
      if ((dispmode != DSP_RADIO) && (dispmode != DSP_DIMMED))
      {
        changeDispMode(DSP_RADIO);
      }
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
    drawIcon(PI_SDCARD);
    oldprgrssw = -1;
    audio.stopSong();
    clearLines();
    getSDFileName(SD_curindex);
    char *shortname = getShortSDFileName();
    SD_oldindex = 65534;
    updateTrack(0);
    oldsdix_req = false;
  }
#endif
  if (config_req)
  {
    stopAudioForUpdate();
    sendConfig_req();
    config_req = false;
  }
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
    if ((WF_MODE != WF_WAITSTA) || rcnnct)
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
