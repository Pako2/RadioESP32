const char *WTAG = "webserver"; // For debug lines

void setupWebServer()
{
  if (!MDNS.begin(config->hostnm))
  {
    ESP_LOGW(WTAG, "Error setting up MDNS responder !");
    while (1)
    {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }
  ESP_LOGW(WTAG, "mDNS responder started !");
  MDNS.addService("http", "tcp", 80);
  weso.onEvent(onWsEvent);
  server.addHandler(&weso);

#if DATAWEB
server.addHandler(new FS_editor(LittleFS, http_username, http_password));


  // Route for root / web page
  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request)
    { request->send(LittleFS, "/index.html", "text/html"); });
  // Route for root / web page
  server.on("/radioesp32.html", HTTP_GET, [](AsyncWebServerRequest *request)
    { request->send(LittleFS, "/radioesp32.html", "text/html"); });
  // Route to load font file
  server.on("/glyphicons-halflings-regular.woff", HTTP_GET, [](AsyncWebServerRequest *request)
    { request->send(LittleFS, "/glyphicons.woff", "font/woff"); });
  // Route to load style.css file
  server.on("/required.css", HTTP_GET, [](AsyncWebServerRequest *request)
    { request->send(LittleFS, "/required.css", "text/css"); });
  // Route to load js file
  server.on("/required.js", HTTP_GET, [](AsyncWebServerRequest *request)
    { request->send(LittleFS, "/required.js", "text/javascript"); });
  // Route to load js file
  server.on("/radioesp32.js", HTTP_GET, [](AsyncWebServerRequest *request)
    { request->send(LittleFS, "/radioesp32.js", "text/javascript"); });

#else
  server.on("/glyphicons-halflings-regular.woff", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    AsyncWebServerResponse *response = request->beginResponse(200, "font/woff", glyphicons_halflings_regular_woff_gz, glyphicons_halflings_regular_woff_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); 
  });
  server.on("/required.css", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/css", required_css_gz, required_css_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });
  server.on("/required.js", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/javascript", required_js_gz, required_js_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });
  server.on("/radioesp32.js", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/javascript", radioesp32_js_gz, radioesp32_js_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });
  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", index_html_gz, index_html_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });
  server.on("/radioesp32.html", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", radioesp32_html_gz, radioesp32_html_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });
#endif

  server.onNotFound([](AsyncWebServerRequest *request)
  {
    AsyncWebServerResponse *response = request->beginResponse(404, "text/plain", "Not found (404)");
    request->send(response);
  });
  #if defined(ROLE_RADIO)
  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    request->send(200, "text/plain", "Success");
  });
  #else
	server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
		if (!request->authenticate(http_username, http_password)) {
			return request->requestAuthentication();
		}
		request->send(200, "text/plain", "Success");
	});
  #endif
  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

#if defined(SDCARD)
  server.on("/mp3list", handle_mp3list); // Handle request for list of tracks
#endif
  server.rewrite("/", "/index.html");
  server.begin();

#if defined(ROLE_MENU)
#include "esp_partition.h"
#include <ArduinoJson.h>
#include "LittleFS.h"
// --- ENDPOINT FOR DOWNLOADING THE EXACT RADIO BACKUP (.BIN) ---
server.on("/download_radio", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate("admin", "kwUghLrp6hKqO72g")) {
        return request->requestAuthentication();
    }

    // Find the raw partition target for "app0"
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "app0"
    );

    if (partition == NULL) {
        request->send(404, "text/plain", "Partition app0 not found!");
        return;
    }

    // Read the exact compiled code size from binaries.json
    uint32_t exactSize = partition->size; // Fallback to full partition size
    if (LittleFS.exists("/binaries.json")) {
        File file = LittleFS.open("/binaries.json", "r");
        if (file) {
            JsonDocument sizeDoc;
            if (deserializeJson(sizeDoc, file) == DeserializationError::Ok) {
                uint32_t savedSize = sizeDoc["radio"]["size"] | 0;
                if (savedSize > 0 && savedSize <= partition->size) {
                    exactSize = savedSize; // Use the optimized exact size
                }
            }
            file.close();
        }
    }

    // Create an asynchronous chunked stream response with the exact size
    AsyncWebServerResponse *response = request->beginResponse(
        "application/octet-stream", exactSize,
        [partition, exactSize](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            size_t toRead = maxLen;
            if (index + toRead > exactSize) {
                toRead = exactSize - index;
            }
            if (toRead > 0) {
                esp_partition_read(partition, index, buffer, toRead);
            }
            return toRead;
        }
    );
    response->addHeader("Access-Control-Allow-Origin", "*"); 
    
    response->addHeader("Content-Disposition", "attachment; filename=radio_backup.bin");

    request->send(response);
});

// --- ENDPOINT FOR DOWNLOADING THE EXACT BLUETOOTH BACKUP (.BIN) ---
server.on("/download_bluetooth", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate("admin", "kwUghLrp6hKqO72g")) {
        return request->requestAuthentication();
    }

    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, "app1"
    );

    if (partition == NULL) {
        request->send(404, "text/plain", "Partition app1 not found!");
        return;
    }

    uint32_t exactSize = partition->size; // Fallback to full partition size
    if (LittleFS.exists("/binaries.json")) {
        File file = LittleFS.open("/binaries.json", "r");
        if (file) {
            JsonDocument sizeDoc;
            if (deserializeJson(sizeDoc, file) == DeserializationError::Ok) {
                uint32_t savedSize = sizeDoc["btls"]["size"] | 0;
                if (savedSize > 0 && savedSize <= partition->size) {
                    exactSize = savedSize;
                }
            }
            file.close();
        }
    }

    AsyncWebServerResponse *response = request->beginResponse(
        "application/octet-stream", exactSize,
        [partition, exactSize](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            size_t toRead = maxLen;
            if (index + toRead > exactSize) {
                toRead = exactSize - index;
            }
            if (toRead > 0) {
                esp_partition_read(partition, index, buffer, toRead);
            }
            return toRead;
        }
    );
    response->addHeader("Access-Control-Allow-Origin", "*"); 

    response->addHeader("Content-Disposition", "attachment; filename=bluetooth_backup.bin");

    request->send(response);
});

// // //
// --- ENDPOINT FOR DOWNLOADING THE EXACT UPMAN BACKUP (.BIN) ---
server.on("/download_upman", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate("admin", "kwUghLrp6hKqO72g")) {
        return request->requestAuthentication();
    }

    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory"
    );

    if (partition == NULL) {
        request->send(404, "text/plain", "Partition factory not found!");
        return;
    }

    uint32_t exactSize = partition->size; // Fallback to full partition size
    if (LittleFS.exists("/binaries.json")) {
        File file = LittleFS.open("/binaries.json", "r");
        if (file) {
            JsonDocument sizeDoc;
            if (deserializeJson(sizeDoc, file) == DeserializationError::Ok) {
                uint32_t savedSize = sizeDoc["upman"]["size"] | 0;
                if (savedSize > 0 && savedSize <= partition->size) {
                    exactSize = savedSize;
                }
            }
            file.close();
        }
    }

    AsyncWebServerResponse *response = request->beginResponse(
        "application/octet-stream", exactSize,
        [partition, exactSize](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            size_t toRead = maxLen;
            if (index + toRead > exactSize) {
                toRead = exactSize - index;
            }
            if (toRead > 0) {
                esp_partition_read(partition, index, buffer, toRead);
            }
            return toRead;
        }
    );
    response->addHeader("Access-Control-Allow-Origin", "*"); 

    response->addHeader("Content-Disposition", "attachment; filename=upman_backup.bin");

    request->send(response);
});
#endif
}
