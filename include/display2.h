static const char *DSPTAG = "display"; // For debug lines
#define MAXGAP 16
#define BUFFLEN (255 - MAXGAP - 2)
#define ROWSNUM     3
#define ROW_LEN    14
#define CELLHGT    22
#define LINEOFFSET 17
#define CELLWID    11

uint16_t WID = 160;
uint16_t HGT = 128;

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
extern RowData *rows;

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
  PI_MUTE_ON,
  PI_BT_OFF,
  PI_BT_ON
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
struct pi_params *PIparams = nullptr;

void changeDispMode(disp_mode_t mode);

volatile bool batbarflag;
const int freq = 500;
const int resolution = 8;

TFT_eSPI tft = TFT_eSPI(); // tft instance
U8g2_for_TFT_eSPI u8g2;    // u8f2 font instance
U8g2_for_TFT_eSPI u8h2[3]; // u8f2 font instance for rows
TFT_eSprite sprite[3] = {TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft)};
#if defined(BATTERY)
TFT_eSprite batsprite = TFT_eSprite(&tft);
uint16_t *batsprPtr;
#endif

const uint8_t *font = u8g2_font_t0_22_me;
const uint8_t *rowfont = font;
uint8_t xshift = 2; // display scroll step

volatile disp_mode_t dispmode = DSP_OTHER;

void updateMenu(uint8_t state)
{
  ESP_LOGW(DSPTAG, "Current menu target: %i", purport[state]);
  tft.fillScreen(TFT_BLACK);
  u8g2.setFont(rowfont);
  for (uint8_t i = 0; i < menu_table_len; i++)
  {
    u8g2.setBackgroundColor(TFT_BLACK);
    u8g2.setForegroundColor(TFT_WHITE);
    if (i == state)
    {
      tft.fillRect(0, 16 + i * 24, 160, 24, TFT_WHITE);
      u8g2.setBackgroundColor(TFT_WHITE);
      u8g2.setForegroundColor(TFT_BLACK);
    }
    u8g2.drawUTF8(0, 35 + i * 24, menu_table[i]);
  }
  u8g2.setBackgroundColor(TFT_BLACK);
}


void setupDisplay()
{
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
  case 128: // DISP ST7735
            //      LINEOFFSET = 17; // ToDo
            //      ROW_LEN = 14;
            //      CELLWID = 11;
            //      CELLHGT = 22;
            //      ROWSNUM = 3;
    break;
  default:
    //      LINEOFFSET = 17; // ToDo
    //      ROW_LEN = 14;
    //      CELLWID = 11;
    //      CELLHGT = 22;
    //      ROWSNUM = 3;
    break;
  }
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
#if defined(BATTERY)
  batsprite.setColorDepth(16);
  batsprPtr = (uint16_t *)batsprite.createSprite(WID - 2, 6 - 2);
  batsprite.fillSprite(TFT_RED);
#endif
  // PWM settings - ATTENTION! - it must be AFTER tft.init() !!!
  if (config->bckpin != 255)
  {
    ledcAttach(config->bckpin, freq, resolution);
    ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight1 : config->backlight1);
    ESP_LOGW(DSPTAG, "GPIO %i is used to control display brightness (PWM).", config->bckpin);
  }
  else
  {
    ESP_LOGW(DSPTAG, "Display brightness control via PWM is not used (GPIO not configured).");
  }
  tft.fillScreen(TFT_BLACK);
}
