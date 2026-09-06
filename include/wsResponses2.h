#include "esp_partition.h"
#include <ArduinoJson.h>
#include "LittleFS.h"


const char *WSRTAG = "wsResponses"; // For debug lines

void sendJsonToClient(AsyncWebSocketClient *cl, JsonDocument &root)
{
	if (cl != nullptr && cl->status() == WS_CONNECTED)
	{
		size_t len = 0;
		len = measureJson(root);

		if (len > 0)
		{
			char *jsonBuffer = (char *)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
			if (jsonBuffer != nullptr)
			{
				serializeJson(root, jsonBuffer, len + 1);

				AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer((uint8_t *)jsonBuffer, len);
				if (buffer)
				{
					cl->text(buffer);
				}
				heap_caps_free(jsonBuffer);
			}
		}
	}
}

void sendJsonToAll(JsonDocument &root, bool logmssg = false)
{
	if (weso.count() > 0)
	{
		size_t len = 0;
		len = measureJson(root);

		if (len > 0)
		{
			char *jsonBuffer = (char *)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
			if (jsonBuffer != nullptr)
			{
				serializeJson(root, jsonBuffer, len + 1);
				AsyncWebSocketMessageBuffer *buffer = weso.makeBuffer((uint8_t *)jsonBuffer, len);
				if (buffer)
				{
					weso.textAll(buffer);
					if (logmssg)
					{
						ESP_LOGW(WSRTAG, "Heartbeat message sent !");
					}
				}
				heap_caps_free(jsonBuffer);
			}
		}
	}
}

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
	uptime.weeks = (uint32_t)(currentsecs / 604800); // Tady klidně nechte uint32_t
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


uint32_t getPartitionSizeBySubtype(esp_partition_subtype_t subtype)
{
	const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, NULL);
	return (part != NULL) ? part->size : 0;
}

void sendHeartBeat()
{
	JsonDocument root;
	root["command"] = "heartbeat";
	root["messageid"] = ++messageid;
	if (weso.count() > 0)
	{
		sendJsonToAll(root, true);
	}
}
#if defined(BATTERY)
void sendBatteryLow(bool battlow)
{
	JsonDocument root;
	root["command"] = "battlow";
	root["value"] = battlow;
	if (weso.count() > 0)
	{
		sendJsonToAll(root, true);
	}
}
#endif

void sendBinaries(AsyncWebSocketClient *cl)
{
	File bins = LittleFS.open("/binaries.json", "r");
	if (bins)
	{
		JsonDocument root;
		root["command"] = "binaries";
		// Handle potential JSON corruption or invalid data formats
		DeserializationError error = deserializeJson(root["binaries"], bins);
		bins.close();
		if (error)
		{
			ESP_LOGW(WSRTAG, "File binaries.json contains invalid JSON data. ToDo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
		}
		if (cl != NULL)
		{
			sendJsonToClient(cl, root);
		}
	}
}

void sendStatus(AsyncWebSocketClient *cl)
{
	if (cl != NULL)
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
		uint32_t totalPsram = ESP.getPsramSize();
		uint32_t freePsram = ESP.getFreePsram();
		if (totalBytes <= 0)
		{
			ESP_LOGE(WSRTAG, "Error getting info on LittleFS");
		}
		JsonDocument root;
		root["command"] = "status";
		root["version"] = STRINGIFY(VERSION);
		root["heap"] = ESP.getFreeHeap();
		root["totalheap"] = ESP.getHeapSize();
		ESP_LOGW(WSRTAG, "Total PSRAM: %d", totalPsram);
		ESP_LOGW(WSRTAG, "Free PSRAM: %d", freePsram);
		root["psram"] = ESP.getFreePsram();
		root["totalpsram"] = ESP.getPsramSize();
		uint64_t chipid = ESP.getEfuseMac();
		uint16_t chip = (uint16_t)(chipid >> 32);
		snprintf(charchipid, 19, "ESP32-%04X%08X", chip, (uint32_t)chipid);
		snprintf(charchipmodel, 20, "%s Rev %d", ESP.getChipModel(), ESP.getChipRevision());
		root["chipid"] = charchipid;
		root["chipmodel"] = charchipmodel;
		root["cpu"] = ESP.getCpuFreqMHz();
		root["partsize"] = getPartitionSizeBySubtype(ESP_PARTITION_SUBTYPE_APP_FACTORY);
		// 2. Getting the total partition sizes for Radio and BT from the table
		root["partsize0"] = getPartitionSizeBySubtype(ESP_PARTITION_SUBTYPE_APP_OTA_0);
		root["partsize1"] = getPartitionSizeBySubtype(ESP_PARTITION_SUBTYPE_APP_OTA_1);
		root["sketchsize"] = ESP.getSketchSize();

		// 3. Loading code sizes (sketchsize) from LittleFS file
		uint32_t radioSketch = 0;
		uint32_t btlsSketch = 0;

		if (LittleFS.exists("/binaries.json"))
		{
			File file = LittleFS.open("/binaries.json", "r");
			if (file)
			{
				JsonDocument sizeDoc;
				deserializeJson(sizeDoc, file);
				radioSketch = sizeDoc["radio"]["size"] | 0;
				btlsSketch = sizeDoc["btls"]["size"] | 0;
				file.close();
			}
		}
		root["sketchsize0"] = radioSketch;
		root["sketchsize1"] = btlsSketch;
		root["availspiffs"] = totalBytes - usedBytes;
		root["spiffssize"] = totalBytes;
		root["psramsize"] = totalPsram;
		root["availpsram"] = freePsram;
		getDeviceUptimeString(dus);
		root["uptime"] = dus;
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
		uint16_t adcval_ = adcval;
		adcval_ = (adcval_ > config->bat0) ? adcval_ : config->bat0;
		adcval_ = (adcval_ < config->bat100) ? adcval_ : config->bat100;
		root["battery"] = (uint8_t)(0.5 + (100 * (float)(adcval_ - config->bat0) / config->batw));

#endif
		sendJsonToClient(cl, root);
	}
}

void sendJump(uint8_t val)
{
	if (weso.count() > 0)
	{
		JsonDocument root;
		root["command"] = "jump";
		root["jump"] = val;
		sendJsonToAll(root);
	}
}


