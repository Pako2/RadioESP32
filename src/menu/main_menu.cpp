#define MYSTR(A) #A
#define STRINGIFY(A) MYSTR(A)
#define REPLACEMENTCHARACTER '*'

#include "Arduino.h"
#include "WiFi.h"
#include <ArduinoJson.h>
#include "HW_Manager.h"

#include <Wire.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
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
#include "webh/menu_app/glyphicons-halflings-regular.woff.gz.h"
#include "webh/menu_app/required.css.gz.h"
#include "webh/menu_app/required.js.gz.h"
// custom, can be updated and changed - generated from files in the LittleFS partition using Custom scripts
#include "webh/menu_app/radioesp32.js.gz.h"
#include "webh/menu_app/radioesp32.html.gz.h"
#include "webh/menu_app/index.html.gz.h"
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

// Forward declaration
void WiFiEvent(WiFiEvent_t, arduino_event_info_t);

// Global variables
uint8_t battperc = 100;
esp_ota_handle_t ota_handle = 0;
const esp_partition_t *update_partition = NULL;
uint8_t old_system_state = 0;
uint32_t lastblick;
bool coloncolor = false;

uint32_t textcolor = 1;
uint32_t backcolor = 0;

int Weekday;
struct WLAN *wlans;

uint8_t *RESERVEDGPIOS;

uint8_t wlannum = 0;
bool shouldReboot = false;
bool formatreq = false;
uint8_t messageid = 0;
#if defined(AUTOSHUTDOWN)
#define MAXPWOFF 100
#define MINPWOFF 1
#endif

/*
Username and password are used in two cases:
============================================
1. to enter the website editor
2. to enter the Update Manager
*/

const char *http_username = "admin";
const char *http_password = "admin";

#define FSIF true // Format LittleFS if not existing
bool STAmode = true;
bool APstart = false;
bool gotIP = false;
uint8_t scanmode = 0;
unsigned long digtime = 0;
uint16_t dgt_cmd; // Buffer for digit input

bool reconnect = false;
bool rcnnct = false;
bool scanfinished = false;
int16_t nets;
AsyncWebServer server(80);
AsyncWebSocket weso("/ws");

uint8_t jump = 0;
Config *config;
#include "configManager.h"
#include "wsResponses2.h"
#include "websocket2.h"
#include "display2.h"
#include "webserver.h"

const char *TAG = "main"; // For debug lines

// Rotary encoder stuff
int16_t enc_preset = 0; // Selected preset

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

      // 3. Low-level hardware halt (ESP-IDF)
      // In ESP-IDF v5.x it is safer to stop the hardware first
      esp_wifi_stop();
      // 4. Deinitialization itself
      // We will use a condition so that deinit only happens if the stack is really active
      esp_err_t err = esp_wifi_deinit();
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "[WiFi] Low-level deinit status / note: %s\n", esp_err_to_name(err));
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
      while(1)
      {
        asm volatile("nop");
      }
    }
    else
    {
      ESP_LOGE(TAG, "ERROR writing to bootloader: %d\n", err);
    }
  }
  else
  {
    ESP_LOGE(TAG, "ERROR: Partition %s not found in memory!\n", name);
  }
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

  vTaskDelay(100 / portTICK_PERIOD_MS); // delay before PSRAM use
  //  maintask = xTaskGetCurrentTaskHandle(); // My taskhandle
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
  wlans = (WLAN *)ps_malloc(8 * sizeof(WLAN));

  RESERVEDGPIOS = (uint8_t *)ps_malloc(16 * sizeof(uint8_t));
  for (uint8_t i = 0; i < 16; i++)
  {
    RESERVEDGPIOS[i] = 0;
  }
  initConfig();

  //  uint8_t ypos = 0;

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
      updateBinariesJson();
    }
    else
    {
      // some ESP_LOGW(); ?
      jsoncfg.close();
    }
    configured = loadConfiguration();
  }
  ESP_LOGW(TAG, "Free PSRAM:     0x%X", ESP.getFreePsram());

  WiFi.onEvent(WiFiEvent);
  if (configured)
  {
    initHardwareManager(config);
    setupDisplay();

    uint16_t timeout_ms = 1000;
    uint16_t timeout_ms_ssl = 3000;
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
  }
  setupWebServer();
}

void jumpToApp(uint8_t app)
{
  tft.fillScreen(TFT_BLACK);
  u8g2.setFont(u8g2_font_t0_17_me);
  u8g2.drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "Jump to");
  u8g2.drawUTF8(0, HGT - CELLHGT - 2 + LINEOFFSET, (app == 1) ? "Radio" : "Bluetooth");
  ESP_LOGW(TAG, "Jump to %s !", (app == 1) ? "Radio" : "Bluetooth"); //
  enc_menu_mode = MODECHANGE;                                        // Swich to MODECHANGE mode
  sendJump(app);
  if (app == 1)
  {
    bootToPartition(ESP_PARTITION_SUBTYPE_APP_OTA_0, "Radio (app0)");
  }
  else
  {
    bootToPartition(ESP_PARTITION_SUBTYPE_APP_OTA_1, "Bluetooth (app1)");
  }

  if (jump > 0)
  {
    jumpToApp(jump);
    jump = 0;
  }
}

//**************************************************************************************************
//                                          E N C _ L O O P                                        *
//**************************************************************************************************
// See if rotary encoder is activated and perform its functions.                                   *
//**************************************************************************************************
void enc_loop()
{
  if (singleclick || doubleclick || tripleclick || longclick || (rotationcount != 0)) // Any activity
  {
  }
  else
  {
    return; // No, nothing to do
  }
  if (tripleclick) // Handle the tripleclick
  {
    ESP_LOGW(TAG, "Triple click");
    tripleclick = false;
  }
  if (doubleclick) // Handle the doubleclick
  {
    ESP_LOGW(TAG, "Double click");
    doubleclick = false;
  }
  if (singleclick)
  {
    ESP_LOGW(TAG, "Single click");
    singleclick = false;
    tft.fillScreen(TFT_BLACK);
    switch (purport[menuinx])
    {
    case 0:
#if defined(AUTOSHUTDOWN)
      pwoff_req = true;
      enc_menu_mode = MODECHANGE; // Swich to MODECHANGE mode
#endif
      break;
    case 1:
      break;
    case 2:
    case 3:
      jumpToApp(purport[menuinx] - 1);
      break;
    default:
      break;
    }
  }
  if (rotationcount == 0) // Any rotation?
  {
    return; // No, return
  }
  ESP_LOGW(TAG, "Rotation count %d", rotationcount);
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
  rotationcount = 0; // Reset
}

void basic_loop()
{
  if (gotIP)
  {
    WF_MODE = WF_STA;
    tft.fillScreen(TFT_BLACK);
    u8g2.setFont(u8g2_font_t0_17_me);
    uint8_t offset = 6 + u8g2.getFontDescent() + u8g2.getFontAscent();
    uint8_t linehgt = offset + 15;
    u8g2.setForegroundColor(TFT_WHITE);
    char charbuf[24];
    sprintf(charbuf, "RadioESP32 %s", STRINGIFY(VERSION));
    u8g2.drawUTF8(0, offset, charbuf);
    u8g2.drawUTF8(0, linehgt + offset, "SSID:");
    u8g2.drawUTF8(0, 2 * linehgt + offset - 8, WiFi.SSID().c_str());
    u8g2.drawUTF8(0, 3 * linehgt + offset, "IP Address:");
    IPAddress ip = WiFi.localIP();
    sprintf(charbuf, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    u8g2.drawUTF8(0, 4 * linehgt + offset - 8, charbuf);
    if (reconnect)
    {
      vTaskDelay(8000 / portTICK_PERIOD_MS);
      reconnect = false;
    }
    else
    {
      vTaskDelay(4000 / portTICK_PERIOD_MS);
    }
    enc_menu_mode = MENU;
    menuinx = 0;
    updateMenu(0);
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
    u8g2.drawUTF8(0, offset, charbuf);
    u8g2.setBackgroundColor(TFT_WHITE);
    u8g2.setForegroundColor(TFT_BLACK);
    u8g2.drawUTF8(0, linehgt + offset, "AP SSID:");
    u8g2.drawUTF8(0, 2 * linehgt + offset - 8, WiFi.softAPSSID().c_str());
    u8g2.drawUTF8(0, 3 * linehgt + offset, "AP IP Address:");
    IPAddress ip = WiFi.softAPIP();
    sprintf(charbuf, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    u8g2.drawUTF8(0, 4 * linehgt + offset - 8, charbuf);
    u8g2.setBackgroundColor(TFT_BLACK);
    u8g2.setForegroundColor(TFT_WHITE);
    APstart = false;
  }
#if defined(AUTOSHUTDOWN)
  if (pwoff_req)
  {
    ESP_LOGW(TAG, "It's time to shut down! GOOD BYE.");
    tft.fillScreen(TFT_BLACK);
    u8g2.setForegroundColor(TFT_RED);
    u8g2.setBackgroundColor(TFT_BLACK);
    u8g2.setFont(u8g2_font_t0_22_me);
    u8g2.drawUTF8(2 * CELLWID, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "Bye, bye !");
    pwoff_req = false;
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    powerOff();
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
}

void proc5s_loop()
{
  if (!proc5s_req)
  {
    return;
  }
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
  uint16_t val = adcval;
  val = (val > config->bat0) ? val : config->bat0;
  val = (val < config->bat100) ? val : config->bat100;
  battperc = 100 * ((val - config->bat0) / (float)config->batw);
  ESP_LOGW(TAG, "Free heap, free stack, battery percent: %s, %d, %d", String(ESP.getFreeHeap()), uxTaskGetStackHighWaterMark(NULL), battperc);

  if (config->batenabled)
  {
    if (config->lowbatt)
    {
      if (battperc <= config->critbatt)
      {
        if (system_state == 0)
        {
          system_state = 1;
        }
      }
      else if ((battperc > config->critbatt) && (system_state == 1))
      {
        system_state = 0;
      }
    }
  }
#else
  ESP_LOGW(TAG, "Free heap, free stack: %s, %d", String(ESP.getFreeHeap()), uxTaskGetStackHighWaterMark(NULL));
#endif
  proc5s_req = false;
}

void loop()
{
  if (system_state == 0)
  {
    if (old_system_state == 0)
    {
      enc_loop();
      if (jump > 0)
      {
        jumpToApp(jump);
        jump = 0;
      }
    }
    else if (old_system_state == 1)
    {
      old_system_state = 0;
      menuinx = 0;
      updateMenu(menuinx);
      sendBatteryLow(false);
    }
    else // old_system_state == 2
    {
      old_system_state = 0;
      menuinx = 0;
      updateMenu(menuinx);
    }
  }
  else if (system_state == 1)
  {
    if (old_system_state == 1)
    {
      // nedelat nic
    }
    else // old_system_state == 0 or old_system_state == 2
    {
      tft.fillScreen(TFT_RED);
      u8g2.setBackgroundColor(TFT_RED);
      u8g2.setForegroundColor(TFT_WHITE);
      u8g2.setFont(u8g2_font_t0_22_me);
      u8g2.drawUTF8(12, 30, "ATTENTION !");
      // u8g2.setFont(u8g2_font_t0_15_me);
      u8g2.drawUTF8(12, 70, "Low battery !");
      u8g2.setBackgroundColor(TFT_BLACK);
      sendBatteryLow(true);
      old_system_state = 1;
    }
  }
  else if (system_state == 2)
  {
    if (old_system_state == 2)
    {
      // to do nothing
    }
    else // old_system_state == 0 or old_system_state == 1 (The second option should not be reached)
    {
      old_system_state = 2;
      tft.fillScreen(TFT_RED);
      u8g2.setBackgroundColor(TFT_RED);
      u8g2.setForegroundColor(TFT_WHITE);
      u8g2.setFont(u8g2_font_t0_22_me);
      u8g2.drawUTF8(15, 30, "ATTENTION !");
      u8g2.setFont(u8g2_font_t0_15_me);
      u8g2.drawUTF8(0, 60, "Updating in progress");
      u8g2.drawUTF8(0, 80, "Please be patient !");
      u8g2.setBackgroundColor(TFT_BLACK);
    }
  }
  proc5s_loop();
  basic_loop();
  vTaskDelay(4 / portTICK_PERIOD_MS);
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
