const char *LCTAG = "loadConfig"; // For debug lines
#include "base64.hpp"

void decodeB64(const char *inputStr, char *output)
{
	unsigned char ibuff[65];
	memcpy(ibuff, inputStr, 65);
	unsigned char obuff[33];
	// decode_base64() does not place a null terminator, because the output is not always a string
	unsigned int string_length = decode_base64(ibuff, obuff);
	obuff[string_length] = '\0';
	memcpy(output, obuff, string_length + 1);
}

#if defined(ROLE_RADIO)
// used by qsort to sort presets
int cmpfunc(const void *a, const void *b)
{
	PRESET *presetA = (PRESET *)a;
	PRESET *presetB = (PRESET *)b;
	return (presetA->nr - presetB->nr);
}
#endif

void initConfig()                        //*
{                                        //*
  size_t cfgsize = sizeof(Config);       //*
  Config cfg_;                           // Only for initConfig() !!!                         //*
  config = (Config *)ps_malloc(cfgsize); // WARNING! Random memory contents !!!               //*
  memcpy(config, &cfg_, cfgsize);        // The default state of the structure is set !!!     //*
}


#include <ArduinoJson.h>
#include <LittleFS.h>

#include <ArduinoJson.h>
#include <LittleFS.h>


bool saveConfigToJSON() {
    // Checking if a pointer exists and is not empty
    if (config == nullptr) {
		ESP_LOGE(LCTAG, "ERROR: The config pointer is NULL!");
        return false;
    }

    const char* filename = "/config.json";
    JsonDocument doc;

    // Root
    doc["command"] = "configfile";
    doc["default"] = (bool)!config->default_;

    // NETWORK section
    JsonObject network = doc["network"].to<JsonObject>();
    network["apssid"] = config->apssid;
    network["apaddress"] = config->apaddress.toString();
    network["apsubnet"] = config->apsubnet.toString();
    network["networks"].to<JsonArray>();

    // HARDWARE section
    JsonObject hardware = doc["hardware"].to<JsonObject>();
    hardware["dsptype"] = config->dsptype;
    hardware["angle"] = config->angle;
    hardware["bclkpin"] = config->bclkpin;
    hardware["doutpin"] = config->doutpin;
    hardware["wspin"] = config->wspin;
    hardware["extpullup"] = config->extpullup;
    hardware["encclkpin"] = config->encclkpin;
    hardware["encdtpin"] = config->encdtpin;
    hardware["encswpin"] = config->encswpin;
    hardware["irpin"] = config->irpin;
    hardware["bckpin"] = config->bckpin;
    hardware["bckinv"] = config->bckinv;
    hardware["mutepin"] = config->mutepin;
#if defined(AUTOSHUTDOWN)
    hardware["onoffipin"] = config->onoffipin;
    hardware["onoffopin"] = config->onoffopin;
#endif
#if defined(SDCARD)
    hardware["sdpullup"] = config->sdpullup;
    hardware["sddpin"] = config->sddpin;
#endif

    // SDPLAYER section
    JsonObject sdplayer = doc["sdplayer"].to<JsonObject>();
    sdplayer["seekstep"] = config->seekstep;

    // BLUETOOTH section
    JsonObject bluetooth = doc["bluetooth"].to<JsonObject>();
    bluetooth["btname"] = config->btname;
    bluetooth["btauto"] = config->btauto;
    bluetooth["btcount"] = config->btcount;
    bluetooth["btaction"] = config->btaction;

    // GENERAL section
    JsonObject general = doc["general"].to<JsonObject>();
    general["hostnm"] = config->hostnm;
    general["restart"] = 0; 
    general["lwtimeout"] = 120; 
#if defined(AUTOSHUTDOWN)
    general["dasd"] = config->dasd;
#endif
#if defined(BATTERY)
    general["bat0"] = config->bat0;
    general["bat100"] = config->bat100;
    general["lowbatt"] = config->lowbatt;
    general["critbatt"] = config->critbatt;
#endif

    // DISPLAY section
    JsonObject display = doc["display"].to<JsonObject>();
    display["backlight1"] = config->backlight1;
    display["speed"] = config->refr; 
    display["scrollgap"] = config->scrollgap;
    display["backlight2"] = config->backlight2;
    display["idle"] = config->idle;
    display["tmode"] = config->tmode;
    display["sdclock"] = config->sdclock;
    display["calendar"] = config->calendar;
    display["dateformat"] = config->dateformat;
    
    // Using indices for days of the week from the wdays[0-6] array
    display["monday"]    = config->wdays[0];
    display["tuesday"]   = config->wdays[1];
    display["wednesday"] = config->wdays[2];
    display["thursday"]  = config->wdays[3];
    display["friday"]    = config->wdays[4];
    display["saturday"]  = config->wdays[5];
    display["sunday"]    = config->wdays[6];

    // NTP section
    JsonObject ntp = doc["ntp"].to<JsonObject>();
    ntp["server"] = config->ntpServer;
    ntp["interval"] = config->ntpInterval;
    ntp["timezone"] = config->timeZone;
    ntp["tzname"] = config->tzname;

    // RADIO section
    JsonObject radio = doc["radio"].to<JsonObject>();
    radio["defstat"] = config->defstat;
    radio["defvol"] = String(config->defvol);
    radio["bass"] = String(config->bass);
    radio["mid"] = String(config->mid);
    radio["treble"] = String(config->treble);
    radio["stations"].to<JsonArray>();

    // IRREMOTE section
    JsonObject irremote = doc["irremote"].to<JsonObject>();
    irremote["commands"].to<JsonArray>();

    // Opening a file and writing to LittleFS
    File configFile = LittleFS.open(filename, "w");
    if (!configFile) {
		ESP_LOGE(LCTAG, "ERROR: Failed to open file for writing!");
        return false;
    }

    if (serializeJsonPretty(doc, configFile) == 0) {
		ESP_LOGE(LCTAG, "ERROR: Failed to write JSON to file!");
        configFile.close();
        return false;
    }

    configFile.close();
	ESP_LOGW(LCTAG, "Configuration successfully saved to LittleFS.");
    return true;
}


bool loadConfiguration()
{
#if defined(ROLE_RADIO)
	uint8_t resix = 0;
#if defined(SDCARD)
	RESERVEDGPIOS[resix++] = 2;
	RESERVEDGPIOS[resix++] = 14;
	RESERVEDGPIOS[resix++] = 15;
#endif
#if defined(BOARD_HAS_PSRAM)
	RESERVEDGPIOS[resix++] = 16;
	RESERVEDGPIOS[resix++] = 17;
#endif
	RESERVEDGPIOS[resix++] = 18;
	RESERVEDGPIOS[resix++] = 21;
	RESERVEDGPIOS[resix++] = 22;
	RESERVEDGPIOS[resix++] = 23;
#if defined(BATTERY)
	RESERVEDGPIOS[resix++] = 36;
#endif
#endif
	// ToDo for other display versions ?
	File configFile = LittleFS.open("/config.json", "r");
	if (!configFile)
	{
		ESP_LOGW(LCTAG, "Failed to open config file");
		return false;
	}
	size_t size = configFile.size();
	std::unique_ptr<char[]> buf(new char[size]);
	configFile.readBytes(buf.get(), size);
	JsonDocument json;
	auto error = deserializeJson(json, buf.get());
	if (error)
	{
		ESP_LOGW(LCTAG, "Failed to parse config file");
		return false;
	}
	ESP_LOGW(LCTAG, "Config file found");

	config->default_ = json["default"];
	JsonObject hardware = json["hardware"];
#if defined(SDCARD)
	JsonObject sdplayer = json["sdplayer"];
	if (sdplayer["seekstep"].is<uint8_t>())
	{
		config->seekstep = sdplayer["seekstep"];
	}
	uint8_t sdmode = (config->sdpullup == 1) ? INPUT : INPUT_PULLUP;
	if (hardware["sddpin"].is<uint8_t>())
	{
		config->sddpin = hardware["sddpin"];
		if (config->sddpin != 255)
		{
			pinMode(config->sddpin, sdmode);
		}
	}
#endif
	if (hardware["extpullup"].is<uint8_t>())
	{
		config->extpullup = hardware["extpullup"];
	}
	if (hardware["encswpin"].is<uint8_t>())
	{
		config->encswpin = hardware["encswpin"];
	}
	JsonArray ircmds = json["irremote"]["commands"];
	JsonObject general = json["general"];
	JsonObject display = json["display"];
	JsonObject bluetooth = json["bluetooth"];
	config->btauto = bluetooth["btauto"].as<uint8_t>();
	config->btaction = bluetooth["btaction"].as<uint8_t>();
	config->btcount = bluetooth["btcount"].as<uint8_t>();
	cpycharar(config->btname, bluetooth["btname"].as<const char *>(), 32);
#if defined(AUTOSHUTDOWN)
	config->dasd = general["dasd"];
#endif
#if defined(BATTERY)
	config->bat0 = general["bat0"];
	config->bat100 = general["bat100"];
	config->batw = config->bat100 - config->bat0;
	config->lowbatt = general["lowbatt"];
	config->critbatt = general["critbatt"];
	config->batenabled = ((config->bat100 <= 4095) && (config->bat0 < (config->bat100-1)));
#endif
	if (hardware["encclkpin"].is<uint8_t>())
	{
		config->encclkpin = hardware["encclkpin"];
	}
	if (hardware["encdtpin"].is<uint8_t>())
	{
		config->encdtpin = hardware["encdtpin"];
	}
	if (hardware["mutepin"].is<uint8_t>())
	{
		config->mutepin = hardware["mutepin"];
	}
#if defined(AUTOSHUTDOWN)
	if (hardware["onoffipin"].is<uint8_t>())
	{
		config->onoffipin = hardware["onoffipin"];
	}
	if (hardware["onoffopin"].is<uint8_t>())
	{
		config->onoffopin = hardware["onoffopin"];
	}
#endif
	if (hardware["irpin"].is<uint8_t>())
	{
		config->irpin = hardware["irpin"];
	}
	if (hardware["angle"].is<uint8_t>())
	{
		config->angle = hardware["angle"];
	}
	config->dsptype = 128; // ToDo for other DISP drivers
	if (hardware["bckpin"].is<uint8_t>())
	{
		config->bckpin = hardware["bckpin"].as<uint8_t>();
	}
	if (hardware["bckinv"].is<uint8_t>())
	{
		config->bckinv = hardware["bckinv"].as<uint8_t>();
	}
	ESP_LOGW(LCTAG, "Trying to setup I2S hardware");
	if (hardware["bclkpin"].is<uint8_t>())
	{
		config->bclkpin = hardware["bclkpin"];
	}
	if (hardware["doutpin"].is<uint8_t>())
	{
		config->doutpin = hardware["doutpin"];
	}
	if (hardware["wspin"].is<uint8_t>())
	{
		config->wspin = hardware["wspin"];
	}
#if !defined(ROLE_MENU)
	// THERE SHOULD BE A CHECK EVERYWHERE THAT THE KEY EXISTS ?
	irnum = 0;
	for (JsonObject value : ircmds)
	{
		cpycharar(ir_cmds[irnum].descr, value["descr"].as<const char *>(), 24);
		ir_cmds[irnum].ircode = value["ircode"].as<uint32_t>();
		ir_cmds[irnum].ircmd = value["ircmd"].as<IRcmd>();
		irnum++;
	}
	ESP_LOGW(LCTAG, "Number of IR commands: %3d", irnum);
#endif
	config->backlight1 = display["backlight1"].as<uint8_t>();
	config->backlight2 = display["backlight2"].as<uint8_t>();
	config->defvol = json["radio"]["defvol"].as<uint8_t>();
#if !defined(ROLE_BTLS)
		JsonObject ntp = json["ntp"];
		JsonArray networks = json["network"]["networks"];
		JsonArray radio = json["radio"]["stations"];
		wlannum = 0;
		for (JsonObject value : networks)
		{
			cpycharar(wlans[wlannum].name, value["location"].as<const char *>(), 16);
			cpycharar(wlans[wlannum].ssid, value["ssid"].as<const char *>(), 32);
			decodeB64(value["wifipass"].as<const char *>(), wlans[wlannum].pass);
			char bssid[18];
			cpycharar(bssid, value["wifibssid"].as<const char *>(), 17);
			wlans[wlannum].dhcp = value["dhcp"].as<uint8_t>();
			if (strlen(bssid) == 0)
			{
				strcat(bssid, "00:00:00:00:00:00");
			}
			sscanf(bssid, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
				   &wlans[wlannum].bssid[0], &wlans[wlannum].bssid[1],
				   &wlans[wlannum].bssid[2], &wlans[wlannum].bssid[3],
				   &wlans[wlannum].bssid[4], &wlans[wlannum].bssid[5]);
			if (wlans[wlannum].dhcp == 0)
			{
				wlans[wlannum].ipaddress.fromString(value["ipaddress"].as<const char *>());
				wlans[wlannum].subnet.fromString(value["subnet"].as<const char *>());
				wlans[wlannum].dnsadd.fromString(value["dnsadd"].as<const char *>());
				wlans[wlannum].gateway.fromString(value["gateway"].as<const char *>());
			}
			wlannum++;
		}
		ESP_LOGW(LCTAG, "Number of wlans: %3d", wlannum);
#endif
#if defined(ROLE_RADIO)
		presetnum = 0;
		for (JsonObject value : radio)
		{
			cpycharar(presets[presetnum].name, value["name"].as<const char *>(), 32);
			cpycharar(presets[presetnum].url, value["url"].as<const char *>(), 96);
			presets[presetnum].nr = value["preset"].as<uint8_t>();
			presetnum++;
		}
		ESP_LOGW(LCTAG, "Number of presets: %3d", presetnum);
		// sort the presets !
		size_t l = sizeof(presets) / sizeof(presets[0]);
		qsort(presets, l, sizeof(presets[0]), cmpfunc);
		config->ntpInterval = ntp["interval"].as<int>();
		cpycharar(config->timeZone, ntp["timezone"].as<const char *>(), 39);
		cpycharar(config->tzname, ntp["tzname"].as<const char *>(), 23);
		cpycharar(config->ntpServer, ntp["server"].as<const char *>(), 23);
		cpycharar(config->hostnm, general["hostnm"].as<const char *>(), 23);
		cpycharar(config->apssid, json["network"]["apssid"].as<const char *>(), 32);
		config->apaddress.fromString(json["network"]["apaddress"].as<const char *>());
		config->apsubnet.fromString(json["network"]["apsubnet"].as<const char *>());
		cpycharar(config->dateformat, display["dateformat"].as<const char *>(), 10);
		cpycharar(config->wdays[0], display["monday"].as<const char *>(), 23);
		cpycharar(config->wdays[1], display["tuesday"].as<const char *>(), 23);
		cpycharar(config->wdays[2], display["wednesday"].as<const char *>(), 23);
		cpycharar(config->wdays[3], display["thursday"].as<const char *>(), 23);
		cpycharar(config->wdays[4], display["friday"].as<const char *>(), 23);
		cpycharar(config->wdays[5], display["saturday"].as<const char *>(), 23);
		cpycharar(config->wdays[6], display["sunday"].as<const char *>(), 23);
#endif
#if !defined(ROLE_MENU)
//xshift calculation !
		uint8_t spd = display["speed"].as<uint8_t>();
		uint8_t r;
		for (xshift = 1; xshift < 6; xshift++)
		{
			r = (1000 * xshift) / (CELLWID * spd);
			if (r >= 20)
			{
				break;
			}
		}
		config->refr = 1000 * xshift / CELLWID / spd;
		config->scrollgap = display["scrollgap"].as<uint8_t>();
		config->tmode = display["tmode"].as<uint8_t>();
		config->sdclock = display["sdclock"].as<uint8_t>();
		config->calendar = display["calendar"].as<uint8_t>();
		config->idle = 10 * display["idle"].as<uint8_t>();
		config->bass = json["radio"]["bass"].as<int8_t>();
		config->mid = json["radio"]["mid"].as<int8_t>();
		config->treble = json["radio"]["treble"].as<int8_t>();
		config->defstat = json["radio"]["defstat"].as<uint8_t>();
#endif		
	ESP_LOGW(LCTAG, "Configuration done");
	configFile.close();
	return true;
}
