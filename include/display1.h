static const char *DSPTAG = "display"; // For debug lines
#define MAXGAP 16
#define BUFFLEN (255 - MAXGAP - 2)
#define ROWSNUM 3
#define ROW_LEN 14
#define CELLHGT 22
#define LINEOFFSET 17
#define CELLWID 11

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
  volatile uint8_t lock;
  uint8_t type = 0; // 0-none, 1-plane text, 2-scrolled text, 3-clear
  volatile bool updated = false;
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

volatile int dispVolume = 0;
volatile bool batbarflag = false;
volatile bool volbarflag = false;
volatile bool prgrssbarflag = false;
volatile bool startscreenflag = false;
volatile int8_t iconflag0 = -1;
volatile int8_t iconflag1 = -1;
volatile int8_t iconflag2 = -1;
volatile player_icons statusicon = PI_PLAY;
volatile player_icons btconnicon = PI_BT_OFF;
volatile bool clearscreenflag = false;
volatile bool drawbarsflag = false;
volatile bool critbattflag = false;
volatile uint8_t batbarperc = 100;
volatile int8_t menuflag = -1;
volatile bool jump2upmanflag = false;
volatile bool jump2radioflag = false;
volatile uint32_t p_pos = 0;

const int freq = 500;
const int resolution = 8;

/*
  Fontname: RadioESP32fonts
  Copyright: Created with Fony 1.4.7
  Glyphs: 11/11
  BBX Build Mode: 0
*/
const uint8_t RadioESP32fonts[283] U8G2_FONT_SECTION("RadioESP32fonts") =
    "\13\0\4\3\4\4\4\3\5\17\17\0\0\0\0\0\0\0\0\0\0\0\376\0\25\333\332\17A)b\222"
    "\304d\222*\42YD\272\311\320\274\2\1\35\277\350O\42\67\7\7#\63\26Z\264\220\60\231\220\70\210"
    "PQ\241\205\311\314\301\7\4\2\23\327\334\17aRC\64%\26\7\26%\64CRa\0\3\11\270\353"
    "\17#\376O\6\4\11\273\352\17\377\377\7\2\5\33\337\330\317\341\22s\65Q!R\61\351\42\242S\304"
    "\306\244J\42\61W,\35\2\6\32\317\350/\7D\221A\221Aq\265\323A\321\263uA\221A\221A"
    "\7$\0\7\21\370\313\177aRC\24\7\177PqD\65&\27\10\27\370\313\177aR\21A!\21\63"
    "b:\213\230\11\12\211\212\10\223\13\11$\372\312/\66#+\42$($&$VDH\224D\234\234"
    "DTDHLHLT\250\230\210\20Yc\2\12$\372\312/\66#+\42$($&$VDH"
    "\224D\234\234DTDHLHLT\250\230\210\20Yc\2\0\0\0\4\377\377\0";

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
const uint8_t *font = u8g2_font_t0_22_me;
const uint8_t *rowfont = font;
uint8_t xshift = 2; // display scroll step

RowData *rows = nullptr;

volatile disp_mode_t dispmode = DSP_OTHER;
char *station = nullptr;       //  PSRAM !
char *artist = nullptr;        //  PSRAM !
char *title = nullptr;         //  PSRAM !
char *station_input = nullptr; //  PSRAM !
char *artist_input = nullptr;  //  PSRAM !
char *title_input = nullptr;   //  PSRAM !
int8_t dispmuteflag = -1;
uint32_t lastredrw;
char oldtimetxt[6]; // Converted timeinfo
const char gaps[MAXGAP + 1] = "                ";
int16_t oldprgrssw = -1;

void drawUTF8(int16_t x, int16_t y, const char *str)
{
  if (dispmode != DSP_LOWBATT)
  {
    u8g2.drawUTF8(x, y, str);
  }
}

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
    drawUTF8(0, 35 + i * 24, menu_table[i]);
  }
  u8g2.setBackgroundColor(TFT_BLACK);
}

void drawStr(int16_t x, int16_t y, const char *str)
{
  if (dispmode != DSP_LOWBATT)
  {
    u8g2.drawStr(x, y, str);
  }
}

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

void drawIcon(uint8_t id)
{
  if (dispmode == DSP_RADIO)
  {
    tft.fillRect(PIparams[id].x, PIparams[id].y, 15, 15, TFT_BLACK);
    u8g2.setFont(RadioESP32fonts);
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

void drawRow(uint8_t row)
{
  u8h2[row].drawUTF8(0, LINEOFFSET, rows[row].input);
  sprite[row].pushSprite(0, rows[row].ypos);
}

void changeDispMode(disp_mode_t mode)
{
  if (dispmode == mode)
  {
    return;
  }
  if (mode != DSP_LOWBATT)
  {
#if defined(BATTERY)
    if (config->batenabled)
    {
      if (config->lowbatt)
      {
        if (batbarperc <= config->critbatt)
        {
          return;
        }
      }
    }
#endif
    dispmode = mode;
    clearscreenflag = true;
    switch (mode)
    {
    case DSP_RADIO:
    case DSP_DIMMED:
      drawbarsflag = true;
      prgrssbarflag = true;
      oldtimetxt[0] = '\0';
      iconflag0 = btconnicon;
      dispmuteflag = muteflag;
      iconflag2 = statusicon;
      if (config->bckpin != 255)
      {
        if (mode == DSP_RADIO)
        {
          ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight1 : config->backlight1);
        }
        else
        {
          ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight2 : config->backlight2);
        }
      }
#if defined(BATTERY)
      batbarflag = true;
#endif
      for (uint8_t row = 0; row < ROWSNUM; row++)
      {
        if (rows[row].type == 1)
        {
          rows[row].updated = true;
        }
      }
      enc_inactivity = 0;
      break;

    case DSP_CLOCK:
    case DSP_PWOFF:
      if (config->bckpin != 255)
      {
        ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight2 : config->backlight2);
      }
      break;
    case DSP_OFFED: // backlight OFF
      if (config->bckpin != 255)
      {
        ledcWrite(config->bckpin, (config->bckinv) ? 255 : 0);
      }
      break;
    case DSP_OTHER:
      if (config->bckpin != 255)
      {
        ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight1 : config->backlight1);
      }
      clearscreenflag = true;
      break;
    }
  }
  else
  {
    dispmode = mode;
    critbattflag = true;
  }
}

void displayloop(void)
{
  if (clearscreenflag)
  {
    tft.fillScreen(TFT_BLACK);
    clearscreenflag = false;
  }
  if (dispmode == DSP_OTHER)
  {
    if (startscreenflag)
    {
      tft.fillScreen(TFT_BLACK);
      u8g2.setFont(u8g2_font_t0_17_me);
      u8g2.setForegroundColor(TFT_WHITE);
      char charbuf[24];
      sprintf(charbuf, "RadioESP32 %s", STRINGIFY(VERSION));
      drawUTF8(0, 20, charbuf);
      drawUTF8(0, 55, "Bluetooth");
      drawUTF8(0, 75, "loadspeaker");
      u8g2.setForegroundColor(TFT_BLACK);
      u8g2.setBackgroundColor(TFT_WHITE);
      drawUTF8(0, HGT-8, config->btname);
      u8g2.setForegroundColor(TFT_WHITE);
      u8g2.setBackgroundColor(TFT_BLACK);
      startscreenflag = false;
    }
    if (menuflag != -1)
    {
      updateMenu(menuflag);
      menuflag = -1;
    }
    else if (jump2upmanflag)
    {
      tft.fillScreen(TFT_BLACK);
      u8g2.setFont(u8g2_font_t0_17_me);
      drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "Jump to");
      drawUTF8(0, HGT - CELLHGT - 2 + LINEOFFSET, "Update Manager");
      ESP_LOGW(DSPTAG, "Jump to Update Manager !"); //
      enc_menu_mode = MODECHANGE;                   // Swich to MODECHANGE mode
      jump2upmanflag = false;
    }
    else if (jump2radioflag)
    {
      tft.fillScreen(TFT_BLACK);
      u8g2.setFont(u8g2_font_t0_17_me);
      drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, "Jump to");
      drawUTF8(0, HGT - CELLHGT - 2 + LINEOFFSET, "Radio");
      ESP_LOGW(DSPTAG, "Jump to Radio !"); //
      enc_menu_mode = MODECHANGE;          // Swich to MODECHANGE mode
      jump2radioflag = false;
    }
    return;
  }
  if (dispmode == DSP_LOWBATT)
  {
    if (critbattflag)
    {
      if (config->bckpin != 255)
      {
        ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight1 : config->backlight1);
      }
      tft.fillScreen(TFT_BLACK);
      u8g2.setFont(font);
      u8g2.setForegroundColor(TFT_RED);
      u8g2.drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, " Low battery !");
      critbattflag = false;
    }
    return;
  }

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
  if (drawbarsflag)
  {
    u8g2.setFont(font);
    u8g2.setFontDirection(0);
    tft.drawRect(0, 0, WID, 6, TFT_WHITE);
    tft.drawRect(0, 25, WID, 9, TFT_WHITE);
    tft.drawRect(0, 118, WID, 9, TFT_WHITE);
    drawbarsflag = false;
  }
  if (dispmuteflag != -1)
  {
    if (dispmuteflag == 1)
    {
      dispVolume = 0;
    }
    else
    {
      dispVolume = volume;
    }
    volbarflag = true;
    drawIcon(muteflag + 7);
    dispmuteflag = -1;
  }
#if defined(BATTERY)
  if (batbarflag)
  {
    uint16_t w1 = uint16_t(batbarperc / 100.0 * (WID - 2));
    batsprite.fillSprite(TFT_RED);
    batsprite.fillRect(0, 0, w1, 6 - 2, TFT_GREEN);
    tft.pushImageDMA(1, 1, WID - 2, 6 - 2, batsprPtr);
    batbarflag = false;
  }
#endif
  if (volbarflag)
  {
    if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
    {
      float hundredpercent = 127.0;
      uint8_t w1 = uint8_t((dispVolume / hundredpercent) * (WID - 2));
      volsprite.fillSprite(TFT_RED);
      volsprite.fillRect(0, 0, w1, 9 - 2, TFT_GREEN);
      tft.pushImageDMA(1, 119, WID - 2, 9 - 2, volsprPtr);
    }
    volbarflag = false;
  }
  if (prgrssbarflag)
  {
    if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
    {
      uint8_t w1 = 0;
      uint32_t drtn = AudioFileDuration;
      if (drtn)
      {
        w1 = uint8_t((p_pos / (float)drtn) * (WID - 2));
      }
      if (w1 != oldprgrssw)
      {
        prgrsssprite.fillSprite(TFT_DARKGREY);
        prgrsssprite.fillRect(0, 0, w1, 9 - 2, TFT_BLUE);
        tft.pushImageDMA(1, 26, WID - 2, 9 - 2, prgrsssprPtr);
        oldprgrssw = w1;
      }
      uint32_t remtime = (uint32_t)round((AudioFileDuration - p_pos) / 1000.0);
      u8g2.setForegroundColor(TFT_YELLOW);
      u8g2.setFont(u8g2_font_t0_17_mn); // time font
      char tmtxt[9];
      secondsToHMS(remtime, tmtxt);
      drawStr(WID - 8 * 9, 21, tmtxt);
      u8g2.setForegroundColor(TFT_WHITE);
      u8g2.setFont(rowfont);
    }
    prgrssbarflag = false;
  }
  if (iconflag0 > -1)
  {
    drawIcon((uint8_t)iconflag0);
    iconflag0 = -1;
  }
  if (iconflag1 > -1)
  {
    drawIcon((uint8_t)iconflag1);
    iconflag1 = -1;
  }
  if (iconflag2 > -1)
  {
    drawIcon((uint8_t)iconflag2);
    iconflag2 = -1;
  }
}

void setupDisplay()
{
  station = (char *)ps_malloc(BUFFLEN * sizeof(char));
  artist = (char *)ps_malloc(BUFFLEN * sizeof(char));
  title = (char *)ps_malloc(BUFFLEN * sizeof(char));
  station_input = (char *)ps_malloc(BUFFLEN * sizeof(char));
  artist_input = (char *)ps_malloc(BUFFLEN * sizeof(char));
  title_input = (char *)ps_malloc(BUFFLEN * sizeof(char));
  station[0] = '\0';
  artist[0] = '\0';
  title[0] = '\0';

  PIparams = (pi_params *)ps_malloc(11 * sizeof(pi_params));
  rows = (RowData *)ps_malloc(ROWSNUM * sizeof(RowData));

  updateLine(0, (char *)"");
  updateLine(1, (char *)"");
  updateLine(2, (char *)"");
  for (int i = 0; i < ROWSNUM; i++)
  {
    rows[i].ypos = 40 + i * (CELLHGT + 5);
  }
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
  for (int i = 0; i < 11; i++)
  {
    PIparams[i].id = (player_icons)i;
    PIparams[i].x = 0;
    PIparams[i].y = 23 - 15;
    PIparams[i].y2 = 23;
    PIparams[i].idx = 0;
    PIparams[i].color = TFT_WHITE;
  }
  PIparams[2].x = 34;
  PIparams[3].x = 34;
  PIparams[4].x = 34;
  PIparams[5].x = 51;
  PIparams[6].x = 51;
  PIparams[7].x = 17;
  PIparams[8].x = 17;
  PIparams[0].idx = 2;
  PIparams[8].id = (player_icons)7;
  PIparams[8].color = TFT_RED;
  PIparams[9].color = TFT_LIGHTGREY;
  PIparams[10].color = TFT_BLUE;

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
    ESP_LOGW(DSPTAG, "GPIO %i is used to control display brightness (PWM).", config->bckpin);
  }
  else
  {
    ESP_LOGW(DSPTAG, "Display brightness control via PWM is not used (GPIO not configured).");
  }
  tft.fillScreen(TFT_BLACK);
}
