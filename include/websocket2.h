#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_task_wdt.h"

const char *WSTAG = "websocket"; // For debug lines
uint32_t ota_bytes_written = 0;  // This is where we will store the total size of the firmware

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
    free(client->_tempObject);
    client->_tempObject = NULL;
    return;
  }

  const char *command = root["command"];

  // =========================================================================
  // INITIALIZE OTA PROCESS VIA JSON COMMAND (With complete Watchdog bypass)
  // =========================================================================
  if (strcmp(command, "ota_start") == 0)
  {
    if (system_state != 0)
    {
      client->text("ERROR: Another process is already active!");
      free(client->_tempObject);
      client->_tempObject = NULL;
      return;
    }

    const char *target = root["target"].as<const char *>();
    if (strcmp(target, "RADIO") == 0)
    {
      update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "app0");
      client->text("STATUS: Preparing flash for Radio...");
    }
    else if (strcmp(target, "BTLS") == 0)
    {
      update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, "app1");
      client->text("STATUS: Preparing flash for Bluetooth...");
    }

    if (update_partition == NULL)
    {
      client->text("ERROR: Target partition layout not found!");
      free(client->_tempObject);
      client->_tempObject = NULL;
      return;
    }

    uint32_t ota_size = root["size"] | 0;
    if (ota_size == 0)
      ota_size = OTA_SIZE_UNKNOWN;

    // --- WATCHDOG INSURANCE: We will completely log out the current Task from Watchdog ---
    // This command tells the system: "Don't watch me for a while, I'm going to do a long operation"
    esp_task_wdt_delete(NULL);

    // We start erasing and initialization (now the processor has all the time in the world)
    esp_err_t err = esp_ota_begin(update_partition, ota_size, &ota_handle);
    ota_bytes_written = 0;
    if (err != ESP_OK)
    {
      ESP_LOGE("OTA", "esp_ota_begin failed! Error: %d", err);
      client->text("ERROR: Flash erase initialization failed!\nMaybe the file is too large.");
      update_partition = NULL;

      // In case of a Watchdog error, we will log back in just to be sure
      esp_task_wdt_add(NULL);

      free(client->_tempObject);
      client->_tempObject = NULL;
      return;
    }

    // --- RETURN OF THE WATCHDOG: After successful deletion, we register the task for monitoring again ---
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();

    system_state = 2; // Trigger RED display alert and hardware encoder lock
    client->text("STATUS: READY_FOR_DATA");

    free(client->_tempObject);
    client->_tempObject = NULL;
    return;
  }

  // =========================================================================
  // FINALIZE OTA PROCESS & UPDATE BINARIES.JSON (Fixed Structure & Size)
  // =========================================================================
  else if (strcmp(command, "ota_end") == 0)
  {
    if (system_state != 2 || ota_handle == 0 || update_partition == NULL)
    {
      free(client->_tempObject);
      client->_tempObject = NULL;
      return;
    }

    // 1. We will finalize and verify the OTA process in silicon
    esp_err_t err = esp_ota_end(ota_handle);
    if (err != ESP_OK)
    {
      ESP_LOGE("OTA", "esp_ota_end verification failed! Error: %d", err);
      client->text("ERROR: Binary signature validation failed!");
      ota_handle = 0;
      update_partition = NULL;
#if defined(BATTERY)
      if (config->batenabled)
      {
        if (battperc > config->critbatt)
        {
          system_state = 0;
        }
        else
        {
          system_state = 1;
        }
      }
#else
      system_state = 0;
#endif
      free(client->_tempObject);
      client->_tempObject = NULL;
      return;
    }

    // 2. UPDATING THE BINARIES.JSON FILE ON LITTLEFS
    const char *web_ver = root["new_version"] | "0.0.0";

    // We will prepare a buffer for the version with the letter "v" (e.g. v1.1.2)
    char formatted_version[32];
    if (web_ver[0] == 'v' || web_ver[0] == 'V')
    {
      // If it happened to come from a website with a "v", we'll copy it as is
      snprintf(formatted_version, sizeof(formatted_version), "%s", web_ver);
    }
    else
    {
      // If only "1.1.2" came, we add "v" to the beginning
      snprintf(formatted_version, sizeof(formatted_version), "v%s", web_ver);
    }

    File f = LittleFS.open("/binaries.json", "r");
    JsonDocument binDoc;
    if (f)
    {
      deserializeJson(binDoc, f);
      f.close();
    }

    if (update_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0)
    {
      binDoc["radio"]["version"] = formatted_version; // Writing with "v"
      binDoc["radio"]["size"] = ota_bytes_written;
      ESP_LOGW("OTA", "Updating binaries.json: Radio set to %s, size %u bytes", formatted_version, ota_bytes_written);
    }
    else if (update_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1)
    {
      binDoc["btls"]["version"] = formatted_version; // Writing with "v"
      binDoc["btls"]["size"] = ota_bytes_written;
      ESP_LOGW("OTA", "Updating binaries.json: Bluetooth set to %s, size %u bytes", formatted_version, ota_bytes_written);
    }

    // Writing the corrected JSON back to disk
    f = LittleFS.open("/binaries.json", "w");
    if (f)
    {
      serializeJson(binDoc, f);
      f.close();
    }

    // 3. Setting up a new boot partition for the next start
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK)
    {
      ESP_LOGE("OTA", "esp_ota_set_boot_partition failed!");
      client->text("ERROR: Failed to switch boot partition!");
      ota_handle = 0;
      update_partition = NULL;

#if defined(BATTERY)
      if (config->batenabled)
      {
        if (battperc > config->critbatt)
        {
          system_state = 0;
        }
        else
        {
          system_state = 1;
        }
      }
#else
      system_state = 0;
#endif

      free(client->_tempObject);
      client->_tempObject = NULL;
      return;
    }

    client->text("STATUS: SUCCESS");
    ESP_LOGW("OTA", "Firmware update completely successful!");

    ota_handle = 0;
    update_partition = NULL;

#if defined(BATTERY)
    if (config->batenabled)
    {
      if (battperc > config->critbatt)
      {
        system_state = 0;
      }
      else
      {
        system_state = 1;
      }
    }
#else
    system_state = 0;
#endif

    free(client->_tempObject);
    client->_tempObject = NULL;
    return;
  }

  else if (strcmp(command, "configfile") == 0)
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
  else if (strcmp(command, "status") == 0)
  {
    sendStatus(client);
  }
  else if (strcmp(command, "jump") == 0)
  {
    jump = root["target"].as<uint8_t>();
  }
  else if (strcmp(command, "restart") == 0)
  {
    shouldReboot = true;
  }

#if defined(AUTOSHUTDOWN)
  else if (strcmp(command, "shutdown") == 0)
  {
    pwoff_req = true;
  }
#endif

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
  free(client->_tempObject);
  client->_tempObject = NULL;
}

// Handles WebSocket Events (Enhanced for Binary OTA Streams)
void onWsEvent(AsyncWebSocket *server_, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    ESP_LOGW(WSTAG, "Websocket [%s] client [%u] connect", server_->url(), client->id());
  }
  else if (type == WS_EVT_ERROR)
  {
    ESP_LOGW(WSTAG, "Websocket [%s] client [%u] error(%u): %s", server_->url(), client->id(), *((uint16_t *)arg), (char *)data);
  }
  else if (type == WS_EVT_DISCONNECT)
  {
    ESP_LOGW(WSTAG, "Websocket [%s] client [%u] disconnect", server_->url(), client->id());
    // Safe fallback if client disconnects mid-air during flashing
    if (system_state == 2)
    {
      esp_ota_end(ota_handle);
      ota_handle = 0;
      update_partition = NULL;
#if defined(BATTERY)
      if (config->batenabled)
      {
        if (battperc > config->critbatt)
        {
          system_state = 0;
        }
        else
        {
          system_state = 1;
        }
      }
#else
      system_state = 0;
#endif
    }
  }
  else if (type == WS_EVT_DATA)
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;

    // =========================================================================
    // SECTION A: HANDLE INCOMING BINARY DATA (Raw Firmware Streaming)
    // =========================================================================
    if (info->opcode == WS_BINARY)
    {
      if (system_state != 2 || ota_handle == 0)
      {
        client->text("ERROR: No active OTA session initialized!");
        return;
      }

      // Write the incoming buffer chunk directly to the opened flash partition segment
      esp_err_t err = esp_ota_write(ota_handle, (const void *)data, len);
      if (err == ESP_OK)
      {
        ota_bytes_written += len; // We count written bytes in real time
      }

      else
      {
        ESP_LOGE("OTA", "Flash write error at index %llu! Error: %d", info->index, err);
        client->text("ERROR: Flash write failure! Update aborted.");
        esp_ota_end(ota_handle);
        ota_handle = 0;

#if defined(BATTERY)
        if (config->batenabled)
        {
          if (battperc > config->critbatt)
          {
            system_state = 0;
          }
          else
          {
            system_state = 1;
          }
        }
#else
        system_state = 0;
#endif
        return;
      }

      // Calculate individual frame boundaries and inform frontend to send next chunk
      if (info->index + len == info->len)
      {
        client->text("STATUS: CHUNK_OK");
      }
    }

    // ============================================================================
    // SECTION B: HANDLE TEXT MESSAGES (Your existing logic + OTA trigger commands)
    // ============================================================================
    else if (info->opcode == WS_TEXT)
    {
      uint64_t index = info->index;
      uint64_t infolen = info->len;

      if (info->final && info->index == 0 && infolen == len)
      {
        // The whole message is in a single frame and we got all of its data
        client->_tempObject = ps_malloc(len);
        if (client->_tempObject != NULL)
        {
          memcpy((uint8_t *)(client->_tempObject), data, len);
        }
        procMsg(client, infolen);
      }
      else
      {
        // Message is comprised of multiple frames or the frame is split into multiple packets
        if (index == 0)
        {
          if (info->num == 0 && client->_tempObject == NULL)
          {
            client->_tempObject = ps_malloc(infolen);
          }
        }
        if (client->_tempObject != NULL)
        {
          memcpy((uint8_t *)(client->_tempObject) + index, data, len);
        }
        if ((index + len) == infolen)
        {
          if (info->final)
          {
            procMsg(client, infolen);
          }
        }
      }
    }
  }
}
