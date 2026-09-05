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

bool volbarflag = false;
bool prgrssbarflag = false;
int16_t oldprgrssw = -1;

void volumebar(uint8_t val)
{
  if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
  {
    float hundredpercent = 100.0;
    uint8_t w1 = uint8_t((val / hundredpercent) * (WID - 2));
    volsprite.fillSprite(TFT_RED);
    volsprite.fillRect(0, 0, w1, 9 - 2, TFT_GREEN);
    volbarflag = true; // flag for display loop
  }
}

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
        prgrssbarflag = true; // flag for the display loop to render the volumebar
        oldprgrssw = w1;
      }
    }
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
  if ((dispmode == DSP_RADIO) || (dispmode == DSP_DIMMED))
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

void clearLines()
{
  station[0] = '\0';
  artist[0] = '\0';
  title[0] = '\0';
  for (uint8_t row = 0; row < ROWSNUM; row++)
  {
    updateLine(row, (char *)"");
  }
}

#if defined(BATTERY)
void batbar()
{
  if (config->batenabled)
  {
    uint16_t val = adcval;
    val = (val > config->bat0) ? val : config->bat0;
    val = (val < config->bat100) ? val : config->bat100;
    uint8_t perc = 100 * ((val - config->bat0) / (float)config->batw);
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
      uint16_t w1 = uint16_t(perc / 100.0 * (WID - 2));
      batsprite.fillSprite(TFT_RED);
      batsprite.fillRect(0, 0, w1, 6 - 2, TFT_GREEN);
      batbarflag = true; // flag for display loop
    }
  }
}
#endif // BATTERY

void changeDispMode(disp_mode_t mode)
{
#if defined(AUTOSHUTDOWN)
  if (fade_req)
  {
    return;
  }
#endif
  if (dispmode == mode)
  {
    idleTimer = 0;
    return;
  }
  if (mode != DSP_LOWBATT)
  {
#if defined(BATTERY)
    if (config->batenabled)
    {
      uint16_t val = adcval;
      val = (val > config->bat0) ? val : config->bat0;
      val = (val < config->bat100) ? val : config->bat100;
      uint8_t perc = 100 * ((val - config->bat0) / (float)config->batw);
      if (config->lowbatt)
      {
        if (perc <= config->critbatt)
        {
          return;
        }
      }
    }
#endif
    dispmode = mode;
    asdmode = false;
    tft.fillScreen(TFT_BLACK);
    switch (mode)
    {
    case DSP_RADIO:
    case DSP_DIMMED:
      oldtimetxt[0] = '\0';
      restoreIcons();
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
      if (config->bckpin != 255)
      {
        ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight1 : config->backlight1);
      }
      tft.fillScreen(TFT_BLACK);
      break;
    }
  }
  else
  {
    dispmode = mode;
    if (config->bckpin != 255)
    {
      ledcWrite(config->bckpin, (config->bckinv) ? 255 - config->backlight1 : config->backlight1);
    }
    tft.fillScreen(TFT_BLACK);
    u8g2.setFont(font);
    u8g2.setForegroundColor(TFT_RED);
    u8g2.drawUTF8(0, HGT - 2 * CELLHGT - 4 + LINEOFFSET, " Low battery !");
  }
}

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
    drawIcon(dispmuteflag + 7);
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
  // ODTUD dale to puvodne bylo za "if (configured)" !!!
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
