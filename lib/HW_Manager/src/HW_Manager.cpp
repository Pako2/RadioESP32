#include "HW_Manager.h"

static const char *HWTAG = "HW_Manager"; // For debug lines

// Global variables
#if defined(ROLE_BTLS)
#endif
volatile bool sw_state = true; // not pushed (HIGH logic level)

volatile bool proc1s_req = false; // Set proc1s requested
volatile bool proc5s_req = false; // Set proc5s requested
volatile bool time_req = false;   // Set time requested
volatile bool digitsReq = false;
volatile bool pwoff_req = false;  // Set on/off requested
volatile bool pwoffclick = false; // True if power off click detected

volatile bool singleclick = false;
volatile bool doubleclick = false;
volatile bool tripleclick = false;

volatile bool longclick = false;
volatile int16_t clickcount = 0;
volatile int16_t rotationcount = 0;
volatile uint16_t enc_inactivity = 0;
volatile uint16_t idleTimer = 0;

volatile bool clockReq = false;
volatile uint16_t dgt_inactivity = 255; // Digit input inactive
volatile uint8_t dgt_count = 0;         // Digit input count

#if defined(BATTERY)
#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"
#define CHANNEL_BAT ADC_CHANNEL_0 // adc1_0, GPIO36
adc_oneshot_unit_handle_t adc1_handle;
int adcvalraw = 0;
uint16_t adcval = 0;
#define LOG_2(n) ((n == 8) ? 3 : ((n == 16) ? 4 : ((n == 32) ? 5 : 6)))
#define FILTER_LEN 16 // allowed values: 8, 16, 32, 64
const uint8_t FILTER_SHIFT = LOG_2(FILTER_LEN);
const uint8_t FILTER_MASK = FILTER_LEN - 1;
uint16_t *Adc1_Buffer; // PSRAM !
uint8_t Adc1_i = 0;    // Index
uint32_t Sum = 0;
#endif
#if defined(ROLE_MENU)
volatile uint8_t system_state = 0; // 0 = Normal, 1 = low battery, 2 = Flashing (Shared with display/encoder)
#endif

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

const char state0[] PROGMEM = "Turn OFF";
const char state1[] PROGMEM = "Update Manager";
const char state2[] PROGMEM = "Radio";
const char state3[] PROGMEM = "Bluetooth";
const char *const menu_table[] PROGMEM =
    {
#if defined(AUTOSHUTDOWN)
        state0,
#endif
#if !defined(ROLE_MENU)
        state1,
#endif
        state2,
        state3,
};
#if defined(ROLE_MENU)
#if defined(AUTOSHUTDOWN)
const uint8_t menu_table_len = 3;
uint8_t purport[] = {0, 2, 3};
#else
const uint8_t menu_table_len = 2;
uint8_t purport[] = {2, 3};
#endif
#else // v jinych rolich, nez menu
#if defined(AUTOSHUTDOWN)
const uint8_t menu_table_len = 4;
uint8_t purport[] = {0, 1, 2, 3};
#else
const uint8_t menu_table_len = 3;
uint8_t purport[] = {1, 2, 3};
#endif
#endif

uint8_t irnum = 0;
struct IR_CMD *ir_cmds;
enc_menu_t enc_menu_mode = VOLUME; // Default is VOLUME mode
int8_t menuinx = 0;

uint32_t AudioFileDuration = 0;

time_t pwofftime = 0;

bool random_ = false;
bool loop_ = true;
bool muteflag = false; // Mute output
player_mode pmode = PM_RADIO;

uint8_t reqvol = 50; // Requested volume
bool asdmode = false;

#if !defined(ROLE_BTLS)
static volatile tm *p_timeinfo = nullptr;
#endif
#if defined(ROLE_RADIO)
static Audio *p_audio = nullptr;
#endif

static Config *p_config = nullptr;

volatile int16_t eqcount = 0;       // Counter for equal number of clicks
volatile int16_t oldclickcount = 0; // To detect difference
hw_timer_t *timer = NULL;
uint32_t oldtime = 0; // Time in millis previous interrupt

// Buxtronix FSM table
#define DIR_CW 0x10
#define DIR_CCW 0x20
#define R_START 0x0
#define F_CW_FINAL 0x1
#define F_CW_BEGIN 0x2
#define F_CW_NEXT 0x3
#define F_CCW_BEGIN 0x4
#define F_CCW_FINAL 0x5
#define F_CCW_NEXT 0x6

static uint8_t table_state = R_START;

const uint8_t _ttable_full[7][4] = {
    // 00        01           10           11              // BA
    {R_START, F_CW_BEGIN, F_CCW_BEGIN, R_START},           // R_START
    {F_CW_NEXT, R_START, F_CW_FINAL, R_START | DIR_CW},    // F_CW_FINAL
    {F_CW_NEXT, F_CW_BEGIN, R_START, R_START},             // F_CW_BEGIN
    {F_CW_NEXT, F_CW_BEGIN, F_CW_FINAL, R_START},          // F_CW_NEXT
    {F_CCW_NEXT, R_START, F_CCW_BEGIN, R_START},           // F_CCW_BEGIN
    {F_CCW_NEXT, F_CCW_FINAL, R_START, R_START | DIR_CCW}, // F_CCW_FINAL
    {F_CCW_NEXT, F_CCW_FINAL, F_CCW_BEGIN, R_START},       // F_CCW_NEXT
};

//**************************************************************************************************
//                                          I S R _ E N C _ S W I T C H                            *
//**************************************************************************************************
// Interrupts received from rotary encoder switch                                                  *
//**************************************************************************************************
void IRAM_ATTR isr_enc_switch()
{
  #if defined(ROLE_MENU)
  if (system_state > 0)
  {
    return;
  }
  #endif
  bool newstate;    // Current state of input signal
  uint32_t newtime; // Current timestamp
  uint32_t dtime;   // Time difference with previous interrupt

  newstate = (bool)digitalRead(p_config->encswpin); // Active state is LOW !!!
  newtime = xTaskGetTickCount();                    // Time of last interrupt
  dtime = (newtime - oldtime) & 0xFFFF;             // Compute delta
  if (dtime < 50)                                   // Debounce
  {
    return; // Ignore bouncing
  }
  if (newstate != sw_state) // State changed?
  {
    oldtime = newtime;   // Time of change for next compare
    sw_state = newstate; // Yes, set current (new) state
    if (sw_state)        // SW released (HIGH level)?
    {
      if ((dtime) > 2000) // More than 2 second?
      {
        longclick = true; // Yes, register longclick
        clickcount = 0;   // Forget normal count
      }
      else
      {
        clickcount = clickcount + 1; // Yes, click detected
      }
      enc_inactivity = 0; // Not inactive anymore
    }
  }
}

#if !defined(ROLE_BTLS)
//**************************************************************************************************
//                                          T I M E R 1 0 0                                        *
//**************************************************************************************************
// Called every 100 msec on interrupt level, so must be in IRAM and no lengthy operations          *
// allowed.                                                                                        *
//**************************************************************************************************
void IRAM_ATTR isr_timer100()
{
  DRAM_ATTR static volatile bool shiftflag = false;
  DRAM_ATTR static volatile int16_t count1sec = 0;     // Counter for activatie 5 seconds process
  DRAM_ATTR static volatile int16_t count5sec = 0;     // Counter for activatie 5 seconds process
  DRAM_ATTR static volatile int16_t oldclickcount = 0; // To detect difference

  if (!shiftflag)
  {
    if (count1sec == 5)
    {
      shiftflag = true;
    }
  }

  count1sec = count1sec + 1;
  if (count1sec == 10) // 1 second passed?
  {
    proc1s_req = true;
    count1sec = 0; // Reset count
  }

  count5sec = count5sec + 1;
  if (shiftflag && (count5sec == 50)) // 5 seconds passed?
  {
    proc5s_req = true;
    count5sec = 0; // Reset count
  }

  if (p_config->tmode > 0)
  {
    if ((dispmode == DSP_RADIO) && !clockReq)
    {
      idleTimer = idleTimer + 1;
      if (idleTimer >= p_config->idle)
      {
        clockReq = true;
      }
    }
  }

  if (dispmode != DSP_PRESETNR)
  {
    if ((count5sec % 10) == 0) // One second over?
    {

      if (p_timeinfo != nullptr)
      {
        p_timeinfo->tm_sec = p_timeinfo->tm_sec + 1;
        if (p_timeinfo->tm_sec >= 60) // Yes, update number of seconds
        {
          p_timeinfo->tm_sec = 0; // Wrap after 60 seconds
          p_timeinfo->tm_min = p_timeinfo->tm_min + 1;
          if (p_timeinfo->tm_min >= 60)
          {
            p_timeinfo->tm_min = 0; // Wrap after 60 minutes
            p_timeinfo->tm_hour = p_timeinfo->tm_hour + 1;
            if (p_timeinfo->tm_hour >= 24)
            {
              p_timeinfo->tm_hour = 0; // Wrap after 24 hours
            }
          }
        }
      }
      time_req = true; // Yes, show current time request
    }
  }

  // Handle rotary encoder. Inactivity counter will be reset by encoder interrupt
  if (enc_inactivity < 36000) // Count inactivity time, but limit to 36000
  {
    enc_inactivity = enc_inactivity + 1;
  }
  if (dgt_inactivity < 24) // Count inactivity time, but limit 2.4 sec
  {
    dgt_inactivity = dgt_inactivity + 1;
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
      eqcount = eqcount + 1;
      if (eqcount == 6) // Long time stable?
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

#else

//**************************************************************************************************
//                                          T I M E R 1 0 0                                        *
//**************************************************************************************************
// Called every 100 msec on interrupt level, so must be in IRAM and no lengthy operations          *
// allowed.                                                                                        *
//**************************************************************************************************
void IRAM_ATTR isr_timer100()
{
  DRAM_ATTR static volatile uint16_t count5sec = 0; // Counter for activatie 5 seconds process

  // Handle rotary encoder. Inactivity counter will be reset by encoder interrupt
  if (enc_inactivity < 36000) // Count inactivity time, but limit to 36000
  {
    enc_inactivity = enc_inactivity + 1;
  }

  // Now detection of single/double/triple click of rotary encoder
  if (clickcount) // Any click?
  {
    if (oldclickcount == clickcount) // Yes, stable situation?
    {
      eqcount = eqcount + 1;
      if (eqcount == 6) // Long time stable?
      {
        eqcount = 0;
        if (clickcount == 2) // Double click?
        {
          doubleclick = true; // Yes, set result
        }
        else if (clickcount == 3) // Triple click?
        {
          tripleclick = true; // Yes, set result
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
  count5sec = count5sec + 1;
  if (count5sec >= 50) // 5 seconds passed?
  {
    proc5s_req = true;
    count5sec = 0; // Reset count
  }
}

#endif

//**************************************************************************************************
//                                          I S R _ E N C _ T U R N                                *
//**************************************************************************************************
// Interrupts received from rotary encoder (clk signal) knob turn.                                 *
// The encoder is a Manchester coded device, the outcomes (-1,0,1) of all the previous state and   *
// actual state are stored in the enc_states[].                                                    *
// Full_status is a 4 bit variable, the upper 2 bits are the previous encoder values, the lower    *
// ones are the actual ones.                                                                       *
// 4 bits cover all the possible previous and actual states of the 2 PINs, so this variable is     *
// the index enc_states[].                                                                         *
// No debouncing is needed, because only the valid states produce values different from 0.         *
// Rotation is 4 if position is moved from one fixed position to the next, so it is devided by 4.  *
//**************************************************************************************************
void IRAM_ATTR isr_enc_turn()
{
  #if defined(ROLE_MENU)
  if (system_state > 0)
  {
    return;
  }
  #endif
  uint8_t pin_state = (digitalRead(p_config->encclkpin) << 1) + digitalRead(p_config->encdtpin);
  table_state = _ttable_full[table_state & 0xf][pin_state];

  uint8_t event = table_state & 0x30;
  switch (event)
  {
  case DIR_CW:
    rotationcount = rotationcount + 1;
    break;
  case DIR_CCW:
    rotationcount = rotationcount - 1;
    break;
  default:
    break;
  }
  enc_inactivity = 0;
  idleTimer = 0;
}

#if defined(AUTOSHUTDOWN)
//**************************************************************************************************
//                                          P W _ O F F                                            *
//**************************************************************************************************
// Interrupts received from ON/OFF switch.                                                         *
//**************************************************************************************************
void IRAM_ATTR isr_pw_OFF()
{
  DRAM_ATTR static volatile uint32_t pwoldtime = 0; // Time in millis previous interrupt
  DRAM_ATTR static volatile bool pwsw_state;        // True is pushed (HIGH)

  bool pwnewstate;    // Current state of input signal
  uint32_t pwnewtime; // Current timestamp
  uint32_t pwdtime;   // Time difference with previous interrupt
  pwnewstate = (digitalRead(p_config->onoffipin) == HIGH);
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

char *cpycharar(char *destination, const char *source, size_t num)
{
  destination[0] = '\0';
  return strncat(destination, source, num);
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

void setMutepin(uint8_t mute_, bool test)
{
  if (p_config->mutepin != 255)
  {
    if (test && muteflag)
    {
      return;
    }
    digitalWrite(p_config->mutepin, mute_); // turn on/off (unmute/mute) the amplifier
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

void powerOff()
{
  if (p_config->onoffopin != 255)
  {
  digitalWrite(p_config->onoffopin, HIGH); // Power off !
  }
}

int read_bat_adc_input()
{
  adc_channel_t channel = CHANNEL_BAT;
  int adc_raw;
  ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, channel, &adc_raw));
  return adc_raw;
}
#endif

void updateBinariesJson()
{
  File file;
  uint32_t currentSize = ESP.getSketchSize();

#if defined(ROLE_RADIO)
  const char key[] = "radio";
  const char ke2[] = "btls";
  const char ke3[] = "upman";
#elif defined(ROLE_BTLS)
  const char key[] = "btls";
  const char ke2[] = "radio";
  const char ke3[] = "upman";
#else
  const char key[] = "upman";
  const char ke2[] = "radio";
  const char ke3[] = "btls";
#endif
  JsonDocument doc;
  bool needs_update = false;
  char charbuf[16];
  sprintf(charbuf, "v%s", STRINGIFY(VERSION));

  // 1. Check if the configuration file already exists in LittleFS
  if (LittleFS.exists("/binaries.json"))
  {
    file = LittleFS.open("/binaries.json", "r");
    if (file)
    {
      // Handle potential JSON corruption or invalid data formats
      DeserializationError error = deserializeJson(doc, file);
      file.close();

      if (error)
      {
        ESP_LOGW(HWTAG, "File binaries.json contains invalid JSON data. Re-creating...");
        doc.clear(); // Discard corrupted data and start fresh
      }
    }
    else 
    {
      // Handle the rare case where file exists but failed to open for reading
      ESP_LOGE(HWTAG, "Error: File binaries.json exists but failed to open!");
      return; // Safe abort to prevent overwriting the other app's stored size
    }

    // 2. Compare the current sketch size with the previously saved value

    const char* stored_version = doc[key]["version"] | "";
    uint32_t stored_size = doc[key]["size"] | 0;

    // --- TIGHTENING CONTROL: SIZE OR VERSION MISMATCH ---
    if (currentSize != stored_size || strcmp(stored_version, charbuf) != 0)
    {
        ESP_LOGW(HWTAG, "Mismatch detected in binaries.json! Updating records...");
        doc[key]["version"] = charbuf;
        doc[key]["size"] = currentSize;
        needs_update = true;
    }
    // If there was a change, we save the corrected binaries.json back
    if (needs_update)
    {
        file = LittleFS.open("/binaries.json", "w");
        if (file)
        {
            serializeJson(doc, file);
            file.close();
            ESP_LOGW(HWTAG, "Saved new size and version for %s: %u bytes, %s\n", key, currentSize, charbuf);
        }
    }
    else
    {
      ESP_LOGW(HWTAG, "Size and version for %s remains unchanged. Flash write skipped.\n", key);
    }
  }
  else // File does not exist yet (e.g., first boot after a factory reset)
  {
    file = LittleFS.open("/binaries.json", "w"); // "w" creates a new file or overwrites
    if (file)
    {
      ESP_LOGW(HWTAG, "File binaries.json not found. Initializing a new one.");
      doc[key]["size"] = currentSize;
      doc[ke2]["size"] = 0; // The second app will populate its own field upon its first boot
      doc[ke3]["size"] = 0; // The third app will populate its own field upon its first boot
      doc[key]["version"] = charbuf;
      doc[ke2]["version"] = "v0.0.0";
      doc[ke3]["version"] = "v0.0.0";
      serializeJson(doc, file);
      file.close();
    }
    else
    {
      ESP_LOGE(HWTAG, "Failed to create file binaries.json!");
    }
  }
}

#if defined(ROLE_RADIO)
void initHardwareManager(Audio &shared_audio, Config *shared_config, tm &shared_time)
{
  p_config = shared_config;
  p_audio = &shared_audio;
  p_timeinfo = &shared_time;
//#elif defined(ROLE_BTLS)
#else
void initHardwareManager(Config *shared_config)
{
  p_config = shared_config;
#endif
  timer = timerBegin(100000);                 // 100 kHz (period = 10 us)
  timerAttachInterrupt(timer, &isr_timer100); // Call isr_timer100() on timer alarm
  timerAlarm(timer, 10000, true, 0);          // 10000us * 10 = 100ms

  ESP_LOGW(HWTAG, "Trying to setup rotary encoder hardware");

  if (p_config->encclkpin != 255 && p_config->encdtpin != 255 && p_config->encswpin != 255)
  {
    ESP_LOGW(HWTAG, "Rotary encoder pin mode = \"%s\"", (p_config->extpullup == INPUT) ? "INPUT" : "INPUT_PULLUP");
    uint8_t inp_mode = (p_config->extpullup == 1) ? INPUT : INPUT_PULLUP;
    pinMode(p_config->encclkpin, inp_mode);
    pinMode(p_config->encdtpin, inp_mode);
    pinMode(p_config->encswpin, inp_mode);

    // Connecting interrupts for both pins (change of state)
    attachInterrupt(digitalPinToInterrupt(p_config->encclkpin), isr_enc_turn, CHANGE);
    attachInterrupt(digitalPinToInterrupt(p_config->encdtpin), isr_enc_turn, CHANGE);
    attachInterrupt(digitalPinToInterrupt(p_config->encswpin), isr_enc_switch, CHANGE);
    ESP_LOGW(HWTAG, "Rotary encoder initialized using GPIOS %i, %i and %i.", p_config->encclkpin, p_config->encdtpin, p_config->encswpin);
  }
  else
  {
    ESP_LOGW(HWTAG, "Rotary encoder not initialized (bad GPIOS %i, %i and %i) !", p_config->encclkpin, p_config->encdtpin, p_config->encswpin);
  }
  if (p_config->mutepin != 255)
  {
    pinMode(p_config->mutepin, OUTPUT);
    digitalWrite(p_config->mutepin, HIGH); // turn off (mute) the amplifier until everything is ready
    ESP_LOGW(HWTAG, "GPIO %i is used for hardware control of amplifier muting.", p_config->mutepin);
  }
  else
  {
    ESP_LOGW(HWTAG, "Hardware control of amplifier muting is not enabled !");
  }
#if defined(AUTOSHUTDOWN)
  if (p_config->onoffipin != 255 && p_config->onoffopin != 255)
  {
    pinMode(p_config->onoffipin, INPUT);
    attachInterrupt(p_config->onoffipin, isr_pw_OFF, CHANGE); // Interrupts will be handle by ON/OFF button
    pinMode(p_config->onoffopin, OUTPUT);
    digitalWrite(p_config->onoffopin, LOW);
    ESP_LOGW(HWTAG, "One-button power on/off circuit is connected to GPIOS %i and %i.", p_config->onoffipin, p_config->onoffopin);
  }
  else
  {
    ESP_LOGW(HWTAG, "GPIOS for one-button power control is not configured !");
  }
#else
  ESP_LOGW(HWTAG, "One-button power on/off function is not enabled !");
#endif

#if defined(BATTERY)
  adc_Init();
  Adc1_Buffer = (uint16_t *)ps_malloc(FILTER_LEN * sizeof(uint16_t));
  adcvalraw = read_bat_adc_input();
  Sum = adcvalraw << FILTER_SHIFT;
  adcval = adcvalraw;
  for (uint8_t i = 0; i < FILTER_LEN; i++)
  {
    Adc1_Buffer[i] = adcvalraw;
  }
#endif
}
