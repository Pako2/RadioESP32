const char *WSTAG = "websocket"; // For debug lines


void procMsg(AsyncWebSocketClient *client, size_t sz)
{
  JsonDocument root;
  char json[sz + 1];
  memcpy(json, (char *)(client->_tempObject), sz);
  json[sz] = '\0';
  auto error = deserializeJson(root, json);
  if (error)
  {
    ESP_LOGW(WSTAG, "Couldn't parse WebSocket message");
    //free(client->_tempObject);
    //client->_tempObject = NULL;
    return;
  }
  const char *command = root["command"];
  if (strcmp(command, "configfile") == 0)
  {
    File f = LittleFS.open("/config.json", FILE_READ);
    if (f)
    {
      f.close();
      LittleFS.remove("/config.json");
    }
    f = LittleFS.open("/config.json", FILE_WRITE);
    if (f)
    {
      //serializeJsonPretty(root, Serial);
      vTaskDelay(5 / portTICK_PERIOD_MS);
      serializeJsonPretty(root, f);
      f.close();
      shouldReboot = true;
    }

  }
  else if (strcmp(command, "binaries") == 0)
  {
    sendBinaries(client);
  }
  else if (strcmp(command, "config_req") == 0)
  {
    config_req = true;
  }
  else if (strcmp(command, "jumptoupman") == 0)
  {
    jumptoupman();
  }
  else if (strcmp(command, "status") == 0)
  {
    sendStatus(client);
  }

  else if (strcmp(command, "pmode") == 0)
  {
    if (STAmode)
    {
      if (pmode == PM_RADIO)
      {
        sendRadio();
      }
#if defined(SDCARD)
      else
      {
        sendSDplayer(3);
      }
#endif
    }
    else
    {
      sendStatus(client);
    }
  }


  else if (strcmp(command, "radio") == 0)
  {
    if (pmode != PM_RADIO)
    {
      audio.stopSong();
      pmode = PM_RADIO;
      setMutepin(0, true);
      clearIcons(2);
      drawIcon(PI_RADIO);
      prgrssbar(0, true);
      clearLines();
      setPreset(reqpreset);
    }
    sendRadio();
  }

  else if (strcmp(command, "restart") == 0)
  {
    shouldReboot = true;
  }
  else if (strcmp(command, "destroy") == 0)
  {
    formatreq = true;
  }

  else if (strcmp(command, "test") == 0)
  {
    const char *url = root["url"];
    testUrl(url);
  }

#if defined(BATTERY)
  else if (strcmp(command, "getadcbat") == 0)
  {
    const char *val = root["val"];
    sendadcbat(val);
  }
#endif

  else if (strcmp(command, "volume") == 0)
  {
    reqvol = root["volume"];
    audio.setVolume(reqvol);
    changeDispMode(DSP_RADIO);
    volumebar(reqvol);
  }

#if defined(AUTOSHUTDOWN)
  else if (strcmp(command, "shutdown") == 0)
  {
    ESP_LOGW(WSTAG, "Shutdown command");
    pwoff_req = true;
  }

  else if (strcmp(command, "schedpwoff") == 0)
  {
    uint8_t val = root["val"];
    if (val > 0)
    {
      ESP_LOGW(WSTAG, "Automatic shutdown occurs after %i minutes", pwoffminutes);
      time(&now);
      pwofftime = now + 60 * val;
      if (dispmode == DSP_CLOCK)
      {
        changeDispMode(DSP_PWOFF);
      }
    }
    else
    {
      pwofftime = 0;
      pwoffminutes = config->dasd;
      ESP_LOGW(WSTAG, "Automatic shutdown mode canceled !");
      dispmode = DSP_OTHER;
      changeDispMode(DSP_CLOCK);
    }
  }
#endif

  else if (strcmp(command, "treble") == 0)
  {
    int8_t treble = root["treble"];
    audio.setTone(0, 0, treble);
  }
  else if (strcmp(command, "mute") == 0)
  {
    changeDispMode(DSP_RADIO);
    muteflag = root["mute"];
    mute(0);
  }

  else if (strcmp(command, "scan") == 0)
  {
    wifi_mode_t wm = WiFi.getMode();
    if ((wm == WIFI_MODE_STA) || (wm == WIFI_MODE_APSTA))
    {
      scanmode = 2; // callback is working !!
      WiFi.scanNetworks(true, true);
    }
  }
  else if (strcmp(command, "gettime") == 0)
  {
    sendTime(client);
  }
  else if (strcmp(command, "settime") == 0)
  {
    const char *tz = root["timezone"];
    time_t t = root["epoch"];
    setSystemTime(tz, t);
  }
  else if (strcmp(command, "getconf") == 0)
  {
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile)
    {
      size_t len = configFile.size();

      AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
      if (buffer)
      {
        configFile.readBytes((char *)buffer->get(), len + 1);
        client->text(buffer);
      }
      configFile.close();
    }
  }

  else if (strcmp(command, "steppreset") == 0)
  {
    changeDispMode(DSP_RADIO);
    int8_t val = root["val"];
    updatePreset(val, true);
  }
  else if (strcmp(command, "preset") == 0)
  {
    changeDispMode(DSP_RADIO);
    uint16_t prst = root["preset"];
    setPreset(prst - 1);
  }
#if defined(SDCARD)
  else if (strcmp(command, "sdplayer") == 0)
  {
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
    else
    {
      sendRadio();
    }
  }
  else if (strcmp(command, "steptrack") == 0)
  {
    changeDispMode(DSP_RADIO);
    int8_t val = root["val"];
    updateTrack(val);
  }
  else if (strcmp(command, "seek") == 0)
  {
    changeDispMode(DSP_RADIO);
    int8_t val = root["val"];
    audio.setTimeOffset(config->seekstep * val);
  }
  else if (strcmp(command, "playpause") == 0)
  {
    changeDispMode(DSP_RADIO);
    pausePlay();
  }
  else if (strcmp(command, "rndloop") == 0)
  {
    changeDispMode(DSP_RADIO);
    if (root["val"] == 0)
    {
      Random();
    }
    else
    {
      Repeat();
    }
  }
  else if (strcmp(command, "position") == 0)
  {
    changeDispMode(DSP_RADIO);
    uint32_t pos = root["position"].as<uint32_t>();
    audio.setAudioPlayTime(pos);
  }
  else if (strcmp(command, "stop") == 0)
  {
    changeDispMode(DSP_RADIO);
    audio.stopSong();
    drawIcon(PI_STOP);
    setMutepin(1, true);
    sendSDstat(0);
    prgrssbar(0, false);
  }
  else if (strcmp(command, "track") == 0)
  {
    changeDispMode(DSP_RADIO);
    SD_ix = root["track"];
    setTrack(SD_ix);
  }
#endif
}

// Handles WebSocket Events
void onWsEvent(AsyncWebSocket *server_, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    sendWebConfig(client);
    ESP_LOGW(WSTAG, "Websocket [%s] client [%u] connect", server_->url(), client->id());
  }
  else if (type == WS_EVT_ERROR)
  {
    ESP_LOGW(WSTAG, "Websocket [%s] client [%u] error(%u): %s", server_->url(), client->id(), *((uint16_t *)arg), (char *)data);
  }
  else if (type == WS_EVT_DISCONNECT)
  {
    ESP_LOGW(WSTAG, "Websocket [%s] client [%u] disconnect", server_->url(), client->id());
  }
  else if (type == WS_EVT_DATA)
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    
// For the entire message (even if it consists of multiple parts) we need to know the global position:
// info->index is the position in the current frame, info->num is the frame number.
// The library provides an absolute index within the entire message in info->index,
// if we read the message sequentially.
    
    if (info->final && info->index == 0 && info->len == len)
    {
      // the whole message is in a single frame and we got all of it's data
      client->_tempObject = ps_malloc(len);
      if (client->_tempObject != NULL)
      {
        memcpy((uint8_t *)(client->_tempObject), data, len);
        procMsg(client, len);
      }
    }
    else
    {
      // message is comprised of multiple frames or the frame is split into multiple packets
      if (info->index == 0)
      {
        // At the very beginning of the transmission (first packet of the first frame)
        // we allocate memory for the ENTIRE message (info->len)

        if (info->num == 0 && client->_tempObject == NULL)
        //if (client->_tempObject == NULL)
        {
          client->_tempObject = ps_malloc(info->len);
        }
      }
      
      // Secure writing to an absolute index within the entire message
      if (client->_tempObject != NULL)
      {
        // info->index for multiframe messages represents the global offset from the start of the message
        memcpy((uint8_t *)(client->_tempObject) + info->index, data, len);
      }
      
      // If we are at the end of the whole message ...
      if ((info->index + len) == info->len)
      {
        if (info->final)
        {
          procMsg(client, info->len);
        }
      }
    }
  }
}

