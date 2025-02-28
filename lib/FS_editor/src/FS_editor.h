#ifndef FS_editor_H_
#define FS_editor_H_
#include <ESPAsyncWebServer.h>

class FS_editor: public AsyncWebHandler {
  private:
    fs::FS _fs;
    String _username;
    String _password; 
    bool _authenticated;
    uint32_t _startTime;
  public:
#ifdef ESP32
    FS_editor(const fs::FS& fs, const String& username=String(), const String& password=String());
#else
    FS_editor(const String& username=String(), const String& password=String(), const fs::FS& fs=SPIFFS);
#endif
    virtual bool canHandle(AsyncWebServerRequest *request) final;
//    virtual bool canHandle(AsyncWebServerRequest *request) const override final;
//  virtual bool canHandle(AsyncWebServerRequest* request __attribute__((unused))) const { return false; }

    virtual void handleRequest(AsyncWebServerRequest *request) override final;
//    virtual void handleRequest(__unused AsyncWebServerRequest* request) {}

    virtual void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) override final;
//    virtual void handleUpload(__unused AsyncWebServerRequest* request, __unused const String& filename, __unused size_t index, __unused uint8_t* data, __unused size_t len, __unused bool final) {}

//    virtual bool isRequestHandlerTrivial() override final {return false;};       //for old ESPAsyncWebserver !!!
    virtual bool isRequestHandlerTrivial() final {return false;};       //for old ESPAsyncWebserver !!!
//    virtual bool isRequestHandlerTrivial() const override final {return false;}; //for new ESPAsyncWebserver !!!
//    virtual bool isRequestHandlerTrivial() const { return true; }

};

#endif
