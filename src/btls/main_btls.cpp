#define MYSTR(A) #A
#define STRINGIFY(A) MYSTR(A)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Arduino.h"
#include <ArduinoJson.h>
#define NO_LED_FEEDBACK_CODE        // saves 92 bytes program memory
#define EXCLUDE_UNIVERSAL_PROTOCOLS // Saves up to 1000 bytes program memory.
#define EXCLUDE_EXOTIC_PROTOCOLS    // saves around 650 bytes program memory if all other protocols are active
#include <IRremote.hpp>
#include "HW_Manager.h"
Config *config;

// Global variables
bool pauseflag = false;
int volume = 0;

#include "display1.h"
#define A2DP_I2S_AUDIOTOOLS 0 // Suppress warning "AudioTools library is not included first or installed"
#include "BluetoothA2DPSink.h"
#undef A2DP_I2S_AUDIOTOOLS
#include "ESP_I2S.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include <LittleFS.h>

#define FSIF false // Not format LittleFS if not existing !!!

const char *TAG = "MAIN";

#include "configManager.h"



I2SClass i2s;
BluetoothA2DPSink a2dp_sink(i2s);

char *convert_to_c_data_buffer(uint8_t const *ptr)
{
  return reinterpret_cast<char *>(const_cast<uint8_t *>(ptr));
}

void avrc_metadata_callback(uint8_t id, const uint8_t *text)
{
  if ((dispmode == DSP_OTHER) || (dispmode == DSP_LOWBATT))
  {
    return;
  }
  // 1. For sure: If the text does not exist or is empty, immediately jump out
  if (text == NULL || text[0] == '\0')
    return;

  if (id == ESP_AVRC_MD_ATTR_PLAYING_TIME)
  {
    // 2. Check: Live streams often send non-numeric texts.
    // We will check whether there is really a digit in the first place.
    if (text[0] >= '0' && text[0] <= '9')
    {
      AudioFileDuration = strtoul((const char *)text, nullptr, 10);
    }
    else
    {
      AudioFileDuration = 0; // For a live stream, we set the length to 0
    }
    return; // Done, we don't need to continue with the lyrics for the playing time
  }

  char *constu_ptr = convert_to_c_data_buffer(text);
  if (strlen(constu_ptr) > (BUFFLEN - 1))
  {
    ESP_LOGE(TAG, "Callback text too long: %i", strlen(constu_ptr));
    return;
  }

  if (id == ESP_AVRC_MD_ATTR_ALBUM)
  {
    cpycharar(station_input, constu_ptr, BUFFLEN - 1);
    updateLine(0, station_input);
  }
  else if (id == ESP_AVRC_MD_ATTR_TITLE)
  {
    cpycharar(title_input, constu_ptr, BUFFLEN - 1);
    updateLine(1, title_input);
  }
  else if (id == ESP_AVRC_MD_ATTR_ARTIST)
  {
    cpycharar(artist_input, constu_ptr, BUFFLEN - 1);
    updateLine(2, artist_input);
  }
}

void avrc_rn_play_pos_callback(uint32_t play_pos)
{
  if ((dispmode == DSP_OTHER) || (dispmode == DSP_LOWBATT))
  {
    return;
  }
  if (AudioFileDuration >= play_pos)
  {
    p_pos = play_pos;
  }

  prgrssbarflag = true;
}

// Connection state callback function
void connection_state_changed(esp_a2d_connection_state_t state, void *ptr)
{
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED)
  {
    volume = a2dp_sink.get_volume();
    dispVolume = volume;
    ESP_LOGW(TAG, "volume = %i", volume);
    iconflag0 = PI_BT_ON;
    btconnicon = PI_BT_ON;
    volbarflag = true;
  }
  else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
  {
    iconflag0 = PI_BT_OFF;
    btconnicon = PI_BT_OFF;
    station_input[0] = '\0';
    updateLine(0, station_input);
    title_input[0] = '\0';
    updateLine(1, title_input);
    artist_input[0] = '\0';
    updateLine(2, artist_input);
  }
}

void avrc_rn_playstatus_callback(esp_avrc_playback_stat_t playback)
{
  switch (playback)
  {
  case esp_avrc_playback_stat_t::ESP_AVRC_PLAYBACK_STOPPED:
    iconflag2 = PI_STOP;
    statusicon = PI_STOP;
    break;
  case esp_avrc_playback_stat_t::ESP_AVRC_PLAYBACK_PLAYING:
    pauseflag = false;
    iconflag2 = PI_PLAY;
    statusicon = PI_PLAY;
    break;
  case esp_avrc_playback_stat_t::ESP_AVRC_PLAYBACK_PAUSED:
    pauseflag = true;
    iconflag2 = PI_PAUSE;
    statusicon = PI_PAUSE;
    break;
  default:
    ESP_LOGW(TAG, "Got unknown playback status %d\n", playback);
    break;
  }
}

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

      // 1. Clean shutdown of a running wireless subsystem
      ESP_LOGW(TAG, "Deinitializing Bluetooth A2DP & I2S...");
      // .end(true) disconnects BT, turns off the radio and frees I2S DMA memory
      a2dp_sink.end(true);
      delay(150); // Short pause to safely complete I2S deinitialization

      // 2. NVS deinitialization (flash write and close)
      nvs_flash_deinit();
      delay(500);
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
      ESP_LOGW(TAG, "ERROR writing to bootloader: %d\n", err);
    }
  }
  else
  {
    ESP_LOGE(TAG, "ERROR: Partition %s not found in memory!\n", name);
  }
}

void pausePlay()
{
  pauseflag = !pauseflag;
  if (pauseflag)
  {
    a2dp_sink.pause();
  }
  else
  {
    a2dp_sink.play();
  }
}

void mute()
{
  muteflag = !muteflag; // Mute/unmute REQUEST
  setMutepin(muteflag, false);
  iconflag1 = (muteflag + 7);
  ESP_LOGW(TAG, "Mute set to %d", muteflag);
  if (muteflag)
  {
    ESP_LOGW(TAG, "volume = %i", volume);
    a2dp_sink.set_volume(0);
    dispVolume = 0;
    volbarflag = true;
  }
  else
  {
    a2dp_sink.set_volume(volume);
    dispVolume = volume;
    volbarflag = true;
  }
}
void updateVolume(int step)
{
  int voloffset = 5 * step; // Step by 4 percent
  volume = volume + voloffset;

  if (volume < 0) // Limit volume
  {
    volume = 0; // Limit to normal values
  }
  else if (volume > 127)
  {
    volume = 127; // Limit to normal values
  }
  a2dp_sink.set_volume(volume);
  dispVolume = volume;
  volbarflag = true;
  ESP_LOGW(TAG, "volume = %i", volume);
  muteflag = false; // Mute off
  setMutepin(muteflag, false);
  iconflag1 = (muteflag + 7);
}

//**************************************************************************************************
//                                          E N C _ L O O P                                        *
//**************************************************************************************************
// See if rotary encoder is activated and perform its functions.                                   *
//**************************************************************************************************
void enc_loop()
{
  if (enc_menu_mode != VOLUME) // In default mode?
  {
    if (enc_inactivity > 50) // No, more than 5 seconds inactive
    {
      enc_inactivity = 0;
      enc_menu_mode = VOLUME; // Return to VOLUME mode
      ESP_LOGW(TAG, "Encoder mode back to VOLUME");
    }
  }

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
  }
  if (doubleclick) // Handle the doubleclick
  {
    ESP_LOGW(TAG, "Double click");
    doubleclick = false;
    enc_menu_mode = COMMAND; // Swich to PRESET mode
    ESP_LOGW(TAG, "Encoder mode set to COMMAND");
  }
  if (singleclick)
  {
    ESP_LOGW(TAG, "Single click");
    singleclick = false;
    switch (enc_menu_mode) // Which mode (VOLUME, PRESET, SDCARD)?
    {
    case VOLUME:
      if (config->btaction)
      {
        pausePlay();
      }
      else
      {
        mute();
      }
      break;
    case COMMAND:
      if (!config->btaction)
      {
        mute();
      }
      else
      {
        pausePlay();
      }
      enc_menu_mode = VOLUME; // Back to default mode
      break;
    case MENU:
      switch (purport[menuinx])
      {
      case 0:
        pwoff_req = true;
        enc_menu_mode = MODECHANGE; // Swich to MODECHANGE mode
        break;
      case 1:
        jump2upmanflag = true;
        bootToPartition(ESP_PARTITION_SUBTYPE_APP_FACTORY, "Update Manager (factory)");
        break;
      case 2:
        jump2radioflag = true;
        bootToPartition(ESP_PARTITION_SUBTYPE_APP_OTA_0, "Radio (app1)");
        break;
      case 3:
        dispmode = DSP_OTHER;
        changeDispMode(DSP_RADIO); // Restore screen
        break;
      default:
        break;
      }
      break;
    }
  }
  if (longclick) // Check for long click
  {
    changeDispMode(DSP_OTHER);
    enc_menu_mode = MENU;
    menuinx = 0;
    menuflag = 0;
    longclick = false; // Reset flag
  }
  if (rotationcount == 0) // Any rotation?
  {
    return; // No, return
  }
  ESP_LOGW(TAG, "Rotation count %d", rotationcount);
  switch (enc_menu_mode) // Which mode (VOLUME, PRESET, SDCARD)?
  {
  case VOLUME:
  {
    updateVolume(rotationcount);
    break;
  }
  case COMMAND:
    if (rotationcount > 0)
    {
      a2dp_sink.next();
    }
    else
    {
      a2dp_sink.previous();
    }
    break;

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
      menuflag = menuinx;
    }
    break;

  default:
    break;
  }
  rotationcount = 0; // Reset
}

void volumeChanged(int volume_) // callback!
{
  volume = volume_;
  dispVolume = volume_;
  volbarflag = true;
}

void displayTask(void *pvParameters)
{
  for (;;)
  {
    displayloop();
    vTaskDelay(4 / portTICK_PERIOD_MS);
  }
}

void setup()
{
  esp_ota_mark_app_valid_cancel_rollback();

  vTaskDelay(200 / portTICK_PERIOD_MS); // delay before PSRAM use
  // DEBUG !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  // vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait for PlatformIO monitor to start
  Serial.begin(115200);
  ESP_LOGW(TAG, "Starting Bluetooth speaker ...");

  ESP_LOGW(TAG, "SketchSize:     0x%X", ESP.getSketchSize());
  ESP_LOGW(TAG, "MaxSketchSpace: 0x%X", ESP.getFreeSketchSpace());
  if (psramFound())
  {
    ESP_LOGW(TAG, "Total PSRAM:    0x%X", ESP.getPsramSize());
    ESP_LOGW(TAG, "Free PSRAM:     0x%X", ESP.getFreePsram());
  }
  initConfig();
  ir_cmds = (IR_CMD *)ps_malloc(100 * sizeof(IR_CMD));
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
      configured = loadConfiguration();
      updateBinariesJson();
    }
    else
    {
      // some ESP_LOGW(); ??
      jsoncfg.close();
    }
  }
  ESP_LOGW(TAG, "Free PSRAM:     0x%X", ESP.getFreePsram());
  if (configured)
  {
    i2s.setPins(config->bclkpin, config->wspin, config->doutpin);
    if (!i2s.begin(I2S_MODE_STD, 44100, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH))
    {
      ESP_LOGW(TAG, "Failed to initialize I2S!");
      // Print to display ??? No! (display not configured)
      while (1)
        ; // do nothing
    }
    a2dp_sink.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST | ESP_AVRC_MD_ATTR_ALBUM | ESP_AVRC_MD_ATTR_PLAYING_TIME);
    a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
    a2dp_sink.set_avrc_rn_playstatus_callback(avrc_rn_playstatus_callback);
    a2dp_sink.set_avrc_rn_play_pos_callback(avrc_rn_play_pos_callback);
    a2dp_sink.set_on_connection_state_changed(connection_state_changed);
    a2dp_sink.set_on_volumechange(volumeChanged);
    a2dp_sink.set_auto_reconnect(config->btauto, config->btcount);

    initHardwareManager(config);
    uint16_t val = adcval;
    val = (val > config->bat0) ? val : config->bat0;
    val = (val < config->bat100) ? val : config->bat100;
    batbarperc = 100 * ((val - config->bat0) / (float)config->batw);
    setupDisplay();
    if (config->irpin != 255)
    {
      IrReceiver.begin(config->irpin, DISABLE_LED_FEEDBACK);
    }
    if (config->mutepin != 255)
    {
      pinMode(config->mutepin, OUTPUT);
      digitalWrite(config->mutepin, LOW); // turn on the amplifier (mute off) until everything is ready
      // ToDo !!!!!!!!!!!!!!!!!!!!!! mutepin by mel byt ovladan, kdyz je zapnute/vypnute mute !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    }
    dispmode = DSP_RADIO;
    changeDispMode(DSP_OTHER);
    startscreenflag = true;

    xTaskCreatePinnedToCore(displayTask, "display", 4096, NULL, 1, NULL, 1);
    vTaskDelay(8000 / portTICK_PERIOD_MS);
    changeDispMode(DSP_RADIO);

    a2dp_sink.start(config->btname);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    esp_a2d_connection_state_t state = a2dp_sink.get_connection_state();
    if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
    {
      iconflag0 = PI_BT_OFF;
      btconnicon = PI_BT_OFF;
    }
    else
    {
      volume = a2dp_sink.get_volume();
      dispVolume = volume;
      volbarflag = true;
      ESP_LOGW(TAG, "volume = %i", volume);
      iconflag0 = PI_BT_ON;
      btconnicon = PI_BT_ON;
      esp_a2d_audio_state_t astate = a2dp_sink.get_audio_state();
      switch (astate)
      {
      case esp_a2d_audio_state_t::ESP_A2D_AUDIO_STATE_STARTED:
        statusicon = PI_PLAY;
        break;
      case esp_a2d_audio_state_t::ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND:
        statusicon = PI_PAUSE;
        break;
      default:
        break;
      }
    }
  }
  else
  {
    ESP_LOGW(TAG, "Configution not loaded !!!");
  }
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
      switch (ircmd)
      {
      case IR_MUTE:
        mute();
        break;
      case IR_VOLP:
        updateVolume(1);
        break;
      case IR_VOLM:
        updateVolume(-1);
        break;
      case IR_PP:
        pausePlay();
        break;
      case IR_STOP:
        a2dp_sink.stop();
        break;
      case IR_FORW:
        a2dp_sink.next();
        break;
      case IR_BACKW:
        a2dp_sink.previous();
        break;
      case IR_ISD:
        pwoff_req = true;
        break;
      case IR_RADIO:
        bootToPartition(ESP_PARTITION_SUBTYPE_APP_OTA_0, "RADIO (app0)");
        break;
      default:
        ESP_LOGW(TAG, "Unsupported IR command %08X", rawdata);
        // sendIRcode(rawdata);
        break;
      }
    }
    else
    {
      ESP_LOGW(TAG, "Unknown IR command %08X", rawdata);
    }
  }
  IrReceiver.resume();
}

char dus[64];
struct deviceUptime
{
  uint32_t weeks;
  uint8_t days;
  uint8_t hours;
  uint8_t mins;
  uint8_t secs;
};

deviceUptime getDeviceUptime()
{
  uint64_t currentsecs = esp_timer_get_time() / 1000000;
  deviceUptime uptime;
  uptime.secs = (uint8_t)(currentsecs % 60);
  uptime.mins = (uint8_t)((currentsecs / 60) % 60);
  uptime.hours = (uint8_t)((currentsecs / 3600) % 24);
  uptime.days = (uint8_t)((currentsecs / 86400) % 7);
  uptime.weeks = (uint32_t)(currentsecs / 604800);
  return uptime;
}

void getDeviceUptimeString(char *uptimestr)
{
  deviceUptime uptime = getDeviceUptime();
  sprintf(uptimestr, "%ld weeks, %ld days, %ld hours, %ld mins, %ld secs", uptime.weeks, uptime.days, uptime.hours, uptime.mins, uptime.secs);
}

void loop()
{
  enc_loop(); // Check rotary encoder functions
  if (IrReceiver.decode())
  {
    irloop();
  }
  if (proc5s_req)
  {
    ESP_LOGW(TAG, "Free heap, free stack: %s, %d", String(ESP.getFreeHeap()), uxTaskGetStackHighWaterMark(NULL));
    getDeviceUptimeString(dus);
    ESP_LOGW(TAG, "Device uptime: %s", dus);

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
    batbarperc = 100 * ((val - config->bat0) / (float)config->batw);
    if (config->batenabled)
    {
      if (config->lowbatt)
      {
        if ((batbarperc <= config->critbatt) && (dispmode != DSP_LOWBATT))
        {
          changeDispMode(DSP_LOWBATT);
        }
        else if ((batbarperc > config->critbatt) && (dispmode == DSP_LOWBATT))
        {
          changeDispMode(DSP_RADIO);
        }
      }
    }
    batbarflag = true;
#endif
    proc5s_req = false;
  }
  if (pwoff_req)
  {
    ESP_LOGW(TAG, "Power-off request. Bye, bye !!!");
    powerOff();
    pwoff_req = false;
  }
  vTaskDelay(8 / portTICK_PERIOD_MS);
}