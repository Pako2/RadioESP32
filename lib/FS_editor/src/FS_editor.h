#ifndef FS_EDITOR_H
#define FS_EDITOR_H

#include <Arduino.h>
#include <FS.h>
#include <ESPAsyncWebServer.h>

class FS_editor: public AsyncWebHandler {
  private:
    fs::FS _fs;
    String _username;
    String _password; 
    bool _authenticated;
    uint32_t _startTime;
  public:
    FS_editor(const fs::FS& fs, const String& username=String(), const String& password=String());
    
    // ONLY canHandle has the const modifier!
    virtual bool canHandle(AsyncWebServerRequest *request) const override final;
    
    // These two methods MUST NOT be const because they write to LittleFS and change state!
    virtual void handleRequest(AsyncWebServerRequest *request) override final;
    virtual void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) override final;
    
    virtual bool isRequestHandlerTrivial() const override final { return false; }
};

#endif
