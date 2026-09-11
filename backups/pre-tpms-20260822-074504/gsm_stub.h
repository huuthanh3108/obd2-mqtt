#pragma once

#include <WiFi.h>
#include <WiFiClient.h>

#ifndef SQ_NOT_KNOWN
#define SQ_NOT_KNOWN 99
#endif

class GSM {
public:
    GSM(WiFiClient&) {}

    static int convertSQToRSSI(int sq) {
        return sq;
    }

    static void ulpInit() {}
    static void initBattery() {}
    static bool isUseGPRS() { return false; }
    static bool hasGSMLocation() { return false; }
    static bool hasGPSLocation() { return false; }
    static bool canDeepSleep() { return false; }
    static void deepSleep(int) {}

    int getSignalQuality() {
        return SQ_NOT_KNOWN;
    }

    void powerOff() {}

    void setNetworkMode(int) {}
    void connectToNetwork() {}
    void enableGPS() {}

    bool checkNetwork(bool force = false) {
        return WiFi.isConnected();
    }

    bool isNetworkConnected() {
        return WiFi.isConnected();
    }

    std::string getIpAddress() {
        return WiFi.localIP().toString().c_str();
    }

    WiFiClient& getClient(bool secure = false) {;
        static WiFiClient client;
        return client;
    }

};