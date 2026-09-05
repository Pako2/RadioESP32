#ifndef HW_Manager_h
#define HW_Manager_h
#define MYSTR(A) #A
#define STRINGIFY(A) MYSTR(A)
#include "Arduino.h"
#include <ArduinoJson.h>
#include "LittleFS.h"
#if defined(ROLE_RADIO)
#include "Audio.h"
#include "../../include/config.h"
#else
#include "../../../include/config.h"
#endif
#include "esp_log.h"
#include <time.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <u8g2_for_TFT_eSPI.h>

// Global variables
extern volatile bool sw_state;
extern volatile bool proc1s_req; // Set proc1s requested
extern volatile bool proc5s_req; // Set proc5s requested
extern struct tm timeinfo;      // Will be filled by NTP server
extern volatile bool time_req;   // Set time requested
extern volatile bool digitsReq;
extern volatile bool pwoff_req; // Set on/off requested
extern volatile bool pwoffclick; // True if power off click detected

extern volatile bool singleclick;
extern volatile bool doubleclick;
extern volatile bool tripleclick;

extern volatile bool longclick;
extern volatile int16_t clickcount;
extern volatile int16_t rotationcount;
extern volatile uint16_t enc_inactivity;
extern volatile uint16_t idleTimer;

extern volatile bool clockReq;
extern volatile uint16_t dgt_inactivity; // Digit input inactive
extern volatile uint8_t dgt_count;         // Digit input count

#define MAXGAP 16
#define BUFFLEN (255 - MAXGAP - 2)
#define ROWSNUM     3
#define ROW_LEN    14
#define CELLHGT    22
#define LINEOFFSET 17
#define CELLWID    11

extern uint8_t reqvol;   // Requested volume
extern bool asdmode;
extern time_t pwofftime;
#if defined(BATTERY)
extern const uint8_t FILTER_SHIFT;
extern const uint8_t FILTER_MASK;
extern uint16_t adcval;
extern int adcvalraw;
extern uint16_t *Adc1_Buffer; // PSRAM !
extern uint8_t Adc1_i;    // Index
extern uint32_t Sum;
#endif
#if defined(ROLE_MENU)
extern volatile uint8_t system_state; // 0 = Normal, 2 = Flashing (Shared with display/encoder)
#endif

extern const char *const cmd_table[] PROGMEM;
extern uint8_t irnum;

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

struct IR_CMD
{
  char descr[25];
  uint32_t ircode;
  IRcmd ircmd;
};
extern struct IR_CMD *ir_cmds;

enum enc_menu_t
{
  VOLUME,
  STATIONS,
  TRACKS,
  MODECHANGE,
  COMMAND,
#if defined(AUTOSHUTDOWN)
  AUTOPWOFF,
#endif
  MENU
}; // State for rotary encoder menu
extern enc_menu_t enc_menu_mode; // Default is VOLUME mode

extern const char state0[] PROGMEM;
extern const char state1[] PROGMEM;
extern const char state2[] PROGMEM;
extern const char state3[] PROGMEM;
extern const char *const menu_table[] PROGMEM;

extern const uint8_t menu_table_len;
extern int8_t menuinx;
extern uint8_t purport[];
extern uint32_t AudioFileDuration;

enum player_mode
{
  PM_RADIO,
  PM_SDCARD
};
extern player_mode pmode;
extern bool random_;
extern bool loop_;
extern bool muteflag; // Mute output

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
extern volatile  disp_mode_t dispmode;

char *cpycharar(char *destination, const char *source, size_t num);
uint8_t getCmdByCode(uint32_t val);
void setMutepin(uint8_t mute_, bool test);
void secondsToHMS(uint32_t seconds, char timestr[]);
void updateBinariesJson();
#if defined(ROLE_RADIO) 
void initHardwareManager(Audio &sdilene_audio, Config *shared_config, tm &shared_time);
#else
void initHardwareManager(Config *shared_config);
#endif
void powerOff();
#if defined(BATTERY)
int read_bat_adc_input();
#endif
#endif //#ifndef HW_Manager_h
