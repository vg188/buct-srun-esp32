#pragma once
#include "Arduino.h"
enum wl_status_t { WL_IDLE_STATUS=0, WL_CONNECTED=3, WL_DISCONNECTED=6 };
#define WIFI_STA 1
extern bool testAssociated;
struct IPAddress {
    uint32_t value=0;
    operator uint32_t() const { return value; }
    String toString() const { return value ? "192.0.2.1" : "0.0.0.0"; }
};
struct WiFiMock {
    wl_status_t statusValue=WL_DISCONNECTED;
    IPAddress ip, gateway;
    bool persistentEnabled=true, autoReconnect=true, sleep=false;
    bool disconnectOK=true, radioOffRequested=false, eraseRequested=false;
    int starts=0, disconnects=0, modeValue=0;
    wl_status_t status() const { return statusValue; }
    wl_status_t begin(const char*,const char*) { ++starts; return statusValue; }
    bool disconnect(bool radioOff,bool erase) {
        ++disconnects; radioOffRequested=radioOff; eraseRequested=erase;
        if(disconnectOK) { statusValue=WL_DISCONNECTED; ip.value=0; gateway.value=0; testAssociated=false; }
        return disconnectOK;
    }
    IPAddress localIP() const { return ip; }
    IPAddress gatewayIP() const { return gateway; }
    IPAddress dnsIP() const { return gateway; }
    void persistent(bool v) { persistentEnabled=v; }
    void mode(int v) { modeValue=v; }
    void setAutoReconnect(bool v) { autoReconnect=v; }
    void setSleep(bool v) { sleep=v; }
};
extern WiFiMock WiFi;
