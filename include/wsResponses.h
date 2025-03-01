const char *WSRTAG = "wsResponses"; // For debug lines
struct deviceUptime
{
	uint32_t weeks;
	uint8_t days;
	uint8_t hours;
	uint8_t mins;
	uint8_t secs;
};

void setSystemTime(const char *tz, time_t epch)
{
  setenv("TZ", tz, 1);
  tzset();
  timeval tv = {epch, 0};
  settimeofday(&tv, nullptr);
}

deviceUptime getDeviceUptime()
{
	uint64_t currentsecs = esp_timer_get_time() / 1000000;
	deviceUptime uptime;
	uptime.secs = (uint8_t)(currentsecs % 60);
	uptime.mins = (uint8_t)((currentsecs / 60) % 60);
	uptime.hours = (uint8_t)((currentsecs / 3600) % 24);
	uptime.days = (uint8_t)((currentsecs / 86400) % 7);
	uptime.weeks = (uint32_t)((currentsecs / 604800));
	return uptime;
}

void getDeviceUptimeString(char *uptimestr)
{
	deviceUptime uptime = getDeviceUptime();
	sprintf(uptimestr, "%ld weeks, %ld days, %ld hours, %ld mins, %ld secs", uptime.weeks, uptime.days, uptime.hours, uptime.mins, uptime.secs);
}

void IPtoChars(IPAddress adress, char *ipadress)
{
	sprintf(ipadress, "%s", adress.toString().c_str());
}

void sendHeartBeat()
{
	JsonDocument root;
	root["command"] = "heartbeat";
	root["messageid"] = ++messageid;
	size_t len = 0;
	len = measureJson(root);
	if (len)
	{
		AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
		if (buffer && weso.count() > 0)
		{
			serializeJson(root, (char *)buffer->get(), len + 1);
			weso.textAll(buffer);
			ESP_LOGW(WSRTAG, "Heartbeat message sent !");
		}
	}
}

void sendStatus(AsyncWebSocketClient *cl)
{
	if (weso.count() > 0)
	{
		char ip1[16] = "0.0.0.0";
		char ip2[16] = "0.0.0.0";
		char ip3[16] = "0.0.0.0";
		char ip4[16] = "0.0.0.0";
		char charchipid[19];
		char charchipmodel[20];
		char dus[64];
		unsigned int totalBytes = LittleFS.totalBytes();
		unsigned int usedBytes = LittleFS.usedBytes();
		if (totalBytes <= 0)
		{
			ESP_LOGE(WSRTAG, "Error getting info on LittleFS");
		}
		JsonDocument root;
		root["command"] = "status";
		root["heap"] = ESP.getFreeHeap();
		root["totalheap"] = ESP.getHeapSize();
		ESP_LOGW(WSRTAG, "Total PSRAM: %d", ESP.getPsramSize());
		ESP_LOGW(WSRTAG, "Free PSRAM: %d", ESP.getFreePsram());
		root["psram"] = ESP.getFreePsram();
		root["totalpsram"] = ESP.getPsramSize();
		uint64_t chipid = ESP.getEfuseMac();
		uint16_t chip = (uint16_t)(chipid >> 32);
		snprintf(charchipid, 19, "ESP32-%04X%08X", chip, (uint32_t)chipid);
		snprintf(charchipmodel, 20, "%s Rev %d", ESP.getChipModel(), ESP.getChipRevision());
		root["chipid"] = charchipid;
		root["chipmodel"] = charchipmodel;
		root["cpu"] = ESP.getCpuFreqMHz();
		root["sketchsize"] = ESP.getSketchSize();
		root["partsize"] = ESP.getFreeSketchSpace(); // esp library bug !
		root["availspiffs"] = totalBytes - usedBytes;
		root["spiffssize"] = totalBytes;
		getDeviceUptimeString(dus);
		root["uptime"] = dus;
		int ss1 = uxTaskGetStackHighWaterMark(maintask);
		int ss0 = uxTaskGetStackHighWaterMark(Task0);
		root["stacksize0"] = ss0;
		root["stacksize1"] = ss1;
		ESP_LOGW(WSRTAG, "Stack audio    is %5d", ss0);
		ESP_LOGW(WSRTAG, "Stack loopTask is %5d", ss1);
		if (WiFi.getMode() == WIFI_STA)
		{
			root["ssid"] = WiFi.SSID();
			root["mac"] = WiFi.macAddress();
			IPtoChars(WiFi.dnsIP(), ip1);
			IPtoChars(WiFi.localIP(), ip2);
			IPtoChars(WiFi.gatewayIP(), ip3);
			IPtoChars(WiFi.subnetMask(), ip4);
		}
		else if (WiFi.getMode() == WIFI_AP_STA)
		{
			root["ssid"] = WiFi.softAPSSID();
			root["mac"] = WiFi.softAPmacAddress();
			IPtoChars(WiFi.softAPIP(), ip1);
			IPtoChars(WiFi.softAPIP(), ip2);
			IPtoChars(WiFi.softAPIP(), ip3);
			IPtoChars(config->apsubnet, ip4);
		}
		root["dns"] = ip1;
		root["ip"] = ip2;
		root["gateway"] = ip3;
		root["netmask"] = ip4;
#if defined(BATTERY)
		uint16_t adcval_ = (adcval > config->bat0) ? adcval - config->bat0 : 0;
		adcval_ = (adcval < config->bat100) ? adcval_ : config->batw;
		root["battery"] = (uint8_t)(0.5 + (100 * (float)adcval_ / config->batw));
#endif
		size_t len = 0;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				cl->text(buffer);
			}
		}
	}
}

void sendRadio(AsyncWebSocketClient *cl)
{
	if (weso.count() > 0)
	{
		JsonDocument root;
		root["command"] = "radio";
		if (reqpreset >= 254)
		{
			root["station"] = station;
		}
		else
		{
			root["station"] = presets[reqpreset].name;
		}
		root["artist"] = artist;
		root["title"] = title;
		root["mute"] = muteflag;
		root["volume"] = reqvol;
		root["running"] = audio.isRunning();
		size_t len = 0;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				if (cl == NULL)
				{
					weso.textAll(buffer);
				}
				else
				{
					cl->text(buffer);
				}
			}
		}
	}
}

void sendMute(bool val)
{
	if (weso.count() > 0)
	{
		JsonDocument root;
		root["command"] = "mute";
		root["mute"] = val;
		size_t len = len;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				weso.textAll(buffer);
			}
		}
	}
}

void sendIRcode(uint32_t val)
{
	if (weso.count() > 0)
	{
		JsonDocument root;
		root["command"] = "ircode";
		root["ircode"] = val;
		size_t len = 0;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				weso.textAll(buffer);
			}
		}
	}
}

void sendVolume(uint8_t val)
{
	if (weso.count() > 0)
	{
		JsonDocument root;
		root["command"] = "volume";
		root["volume"] = val;
		size_t len = 0;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				weso.textAll(buffer);
			}
		}
	}
}

void sendScanResult(int networksFound, AsyncWebSocketClient *cl)
{
	if (weso.count() > 0)
	{
		int n = networksFound;
		int indices[n];
		int skip[n];
		for (int i = 0; i < networksFound; i++)
		{
			indices[i] = i;
		}
		for (int i = 0; i < networksFound; i++)
		{
			for (int j = i + 1; j < networksFound; j++)
			{
				if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i]))
				{
					std::swap(indices[i], indices[j]);
					std::swap(skip[i], skip[j]);
				}
			}
		}
		JsonDocument root;
		root["command"] = "ssidlist";
		JsonArray scan = root["list"].to<JsonArray>();
		for (int i = 0; i < 5 && i < networksFound; ++i)
		{
			JsonObject item = scan.add<JsonObject>();
			item["ssid"] = WiFi.SSID(indices[i]);
			item["bssid"] = WiFi.BSSIDstr(indices[i]);
			item["rssi"] = WiFi.RSSI(indices[i]);
			item["channel"] = WiFi.channel(indices[i]);
			item["enctype"] = WiFi.encryptionType(indices[i]);
		}
		size_t len = 0;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len); //  creates a buffer (len + 1) for you.
			if (buffer && weso.count() > 0)
			{
				JsonObject documentRoot = root["list"].as<JsonObject>();
				serializeJson(root, (char *)buffer->get(), len + 1);
				if (cl == NULL)
				{
					weso.textAll(buffer);
				}
				else
				{
					cl->text(buffer);
				}
			}
			WiFi.scanDelete();
		}
	}
}

void sendWebConfig(AsyncWebSocketClient *cl)
{
	if (weso.count() > 0)
	{
		char cmdbuffer[32];
		JsonDocument root;
		root["command"] = "webconfig";
		root["version"] = STRINGIFY(VERSION);
		root["rowsnum"] = ROWSNUM;
#if defined(AUTOSHUTDOWN)
		root["autoshutdown"] = true;
		root["maxpwoff"] = MAXPWOFF;
		root["minpwoff"] = MINPWOFF;
		root["pwofftime"] = pwofftime;
#else
		root["autoshutdown"] = false;
#endif
		bool ota = false;
		bool oled = false;
		bool disp = false;
#if defined(OTA)
		ota = OTA;
#endif
		root["ota"] = ota;
#if defined(SDCARD)
		root["sdcard"] = true;
		root["sdready"] = SD_okay;
#else
		root["sdcard"] = false;
#endif
#if defined(DISP)
		root["disp"] = true;
#else
		root["disp"] = false;
#endif
#if defined(BATTERY)
        root["battery"] = true;
#else
        root["battery"] = false;
#endif
		JsonArray ircommands = root["ircommands"].to<JsonArray>();
		for (int i = 0; i < cmd_table_len; i++)
		{
			strcpy_P(cmdbuffer, (char *)pgm_read_ptr(&(cmd_table[i]))); // Necessary casts and dereferencing, just copy.
			ircommands.add(cmdbuffer);
		}
		JsonArray reservedgpios = root["reservedgpios"].to<JsonArray>();
		for (int i = 0; i < 16; i++)
		{
			if (RESERVEDGPIOS[i] > 0)
			{
				reservedgpios.add(RESERVEDGPIOS[i]);
			}
			else
			{
				break;
			}
		}
		size_t len = 0;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len); //  creates a buffer (len + 1) for you.
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				cl->text(buffer);
			}
		}
	}
}

int getTZoffset(time_t tt)
{
	tm *LOC_TM; // pointer to a tm struct;
	LOC_TM = localtime(&tt);
	int minl = LOC_TM->tm_min;
	int hourl = LOC_TM->tm_hour;
	int yearl = LOC_TM->tm_year;
	int ydayl = LOC_TM->tm_yday;
	char *pattern = (char *)"%d-%m-%Y %H:%M:%S";
	tm *gt;
	gt = gmtime(&tt);
	char bffr[30];
	strftime(bffr, 30, pattern, gt);
	struct tm GMTM = {0};
	strptime(bffr, pattern, &GMTM);
	int ming = GMTM.tm_min;
	int hourg = GMTM.tm_hour;
	int yearg = GMTM.tm_year;
	int ydayg = GMTM.tm_yday;
	if (yearg != yearl)
	{
		if (yearg + 1 == yearl && ydayl == 0)
		{
			ydayg = -1;
		}
		else if (yearl + 1 == yearg && ydayg == 0)
		{
			ydayl = -1;
		}
		else
		{
			return 0;
		}
	}
	return minl - ming + 60 * (hourl - hourg) + 1440 * (ydayl - ydayg);
}

void sendTime(AsyncWebSocketClient *cl)
{
	if (weso.count() > 0)
	{
		time_t now_ = time(nullptr);
		JsonDocument root;
		root["command"] = "gettime";
		root["epoch"] = (long long int)now_;
		root["tzoffset"] = getTZoffset(now_);
		size_t len = 0;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				cl->text(buffer);
			}
		}
	}
}

void  sendLoginpassw(AsyncWebSocketClient *cl, bool valid)
{
	if (weso.count() > 0)
	{
		JsonDocument root;
		root["command"] = "loginpassw";
		root["valid"] = valid;
		size_t len = 0;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				cl->text(buffer);
			}
		}
	}
}

#if defined(SDCARD)
void sendSDplayer(uint8_t kind)
{
	if (weso.count() > 0)
	{
		JsonDocument root;
		root["command"] = "sdplayer";
		root["random"] = random_;
		root["loop"] = loop_;
		switch (kind)
		{
		case 0:
			root["kind"] = "album";
			root["album"] = station;
			break;
		case 1:
			root["kind"] = "artist";
			root["artist"] = artist;
			break;
		case 2:
			root["kind"] = "title";
			root["title"] = title;
			break;
		case 3:
			root["kind"] = "all";
			root["album"] = station;
			root["artist"] = artist;
			root["title"] = title;
			break;
		case 4:
			root["kind"] = "none";
			break;
		default:
			break;
		}
		root["mute"] = muteflag;
		root["volume"] = reqvol;
		root["sdready"] = SD_okay;
		root["count"] = SD_filecount;
		root["index"] = SD_curindex;
		root["running"] = audio.isRunning();
		size_t len = 0;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				weso.textAll(buffer);
			}
		}
	}
}

void sendSDtrack(int ix, char *fnm)
{
	if (weso.count() > 0)
	{
		JsonDocument root;
		root["command"] = "sdtrack";
		root["sdtrack"] = fnm;
		root["index"] = ix;
		root["sdready"] = SD_okay;
		root["count"] = SD_filecount;
		size_t len = 0;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				weso.textAll(buffer);
			}
		}
	}
}

void sendRndLoop()
{
	if (weso.count() > 0)
	{
		JsonDocument root;
		root["command"] = "rndloop";
		root["random"] = random_;
		root["loop"] = loop_;
		size_t len = len;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				weso.textAll(buffer);
			}
		}
	}
}

void sendSDready()
{
	if (weso.count() > 0)
	{
		JsonDocument root;
		root["command"] = "sdready";
		root["sdready"] = SD_okay;
		root["count"] = SD_filecount;
		size_t len = 0;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				weso.textAll(buffer);
			}
		}
	}
}

void sendSDstat(uint32_t pstn)
{
	JsonDocument root;
	root["command"] = "sdstatus";
	root["running"] = audio.isRunning();
	root["duration"] = audio.getAudioFileDuration();
	root["position"] = pstn;
	size_t len = 0;
	len = measureJson(root);
	if (len)
	{
		AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
		if (buffer && weso.count() > 0)
		{
			serializeJson(root, (char *)buffer->get(), len + 1);
			weso.textAll(buffer);
		}
	}
}
#endif

#if defined(BATTERY)
void sendadcbat(const char *val)
{
	if (weso.count() > 0)
	{
		JsonDocument root;
		root["command"] = "adcbat";
		root["kind"] = val;
		root["val"] = adcval;
		size_t len = 0;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				weso.textAll(buffer);
			}
		}
	}
}
#endif

#if defined(AUTOSHUTDOWN)
void sendAsd(AsyncWebSocketClient *cl)
{
	if (weso.count() > 0)
	{
		JsonDocument root;
		root["command"] = "asd";
		root["pwoffminutes"] = pwoffminutes;
		root["pwofftime"] = pwofftime;
		size_t len = 0;
		len = measureJson(root);
		if (len)
		{
			AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer(len);
			if (buffer && weso.count() > 0)
			{
				serializeJson(root, (char *)buffer->get(), len + 1);
				if (cl == NULL)
				{
					weso.textAll(buffer);
				}
				else
				{
					cl->text(buffer);
				}
			}
		}
	}
}
#endif

