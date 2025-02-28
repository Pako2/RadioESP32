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

#if defined(DATAWEB)
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
    AsyncWebServerResponse *response = request->beginResponse_P(200, "font/woff", glyphicons_halflings_regular_woff_gz, glyphicons_halflings_regular_woff_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });
  server.on("/required.css", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/css", required_css_gz, required_css_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });
  server.on("/required.js", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/javascript", required_js_gz, required_js_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });
  server.on("/radioesp32.js", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/javascript", radioesp32_js_gz, radioesp32_js_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });

  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_html_gz, index_html_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });

  server.on("/radioesp32.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", radioesp32_html_gz, radioesp32_html_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response); });
#endif

#if defined(OTA)
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request)
            {
		AsyncWebServerResponse * response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
		response->addHeader("Connection", "close");
		request->send(response); }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
            {
		if (!request->authenticate(http_username, http_password))
    {
      return request->requestAuthentication();
		}
		if (!index)
    {
      ESP_LOGW(WTAG, "Firmware update begin ...");
			if (!Update.begin())
      {
#if defined(DEBUG)
				Update.printError(Serial);
#endif
			}
		}
		if (!Update.hasError())
    {
			if (Update.write(data, len) != len)
      {
#if defined(DEBUG)
				Update.printError(Serial);
#endif
			}
		}
		if (final)
    {
			if (Update.end(true))
      {
        ESP_LOGW(WTAG, "Firmware update finished: %uB\n", index + len);
				shouldReboot = !Update.hasError();
			}
#if defined(DEBUG)
      else 
      {
				Update.printError(Serial);
			}
#endif
		}
  });
#endif

  server.onNotFound([](AsyncWebServerRequest *request)
                    {
    AsyncWebServerResponse *response = request->beginResponse(404, "text/plain", "Not found (404)");
    request->send(response); });

  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", "Success"); });

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", String(ESP.getFreeHeap())); });

#if defined(SDCARD)
  server.on("/mp3list", handle_mp3list); // Handle request for list of tracks
#endif

  server.rewrite("/", "/index.html");
  server.begin();
}
