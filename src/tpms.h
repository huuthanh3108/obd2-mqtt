#pragma once

#include <Arduino.h>
#include <array>
#include <cstddef>
#include <cstdint>

#include "mqtt.h"

struct TPMSReading {
    bool valid = false;
    char wheel[17] = "";
    char mac[18] = "";
    float batteryVoltage = 0.0F;
    int batteryPercent = 0;
    int temperatureC = 0;
    int absolutePressureKpa = 0;
    int gaugePressureKpa = 0;
    float gaugePressureBar = 0.0F;
    int rssi = 0;
    unsigned long lastSeen = 0;
};

class TPMSManager {
public:
    void begin();
    void loop();
    bool sendDiscovery(MQTT &mqtt, bool allowOffline) const;
    bool sendState(MQTT &mqtt) const;

    static bool parseDjtpmsPayload(const uint8_t *payload,
                                   size_t length,
                                   uint16_t manufacturerId,
                                   bool hasManufacturerId,
                                   TPMSReading &reading);

private:
    std::array<TPMSReading, 4> readings{};
    unsigned long nextSimulationUpdate = 0;
    unsigned long nextBleScan = 0;

    void updateSimulation();
    void scanBle();
    bool applyParsedReading(const char *mac, const TPMSReading &parsed, int rssi);
    TPMSReading *findReadingByMac(const char *mac);
    static int batteryPercentFromVoltage(float voltage);
    static bool parseFrame(uint8_t cid0,
                           uint8_t cid1,
                           const uint8_t *frame12,
                           TPMSReading &reading);
    static uint8_t crc8(const uint8_t *data, size_t length);
};

extern TPMSManager TPMS;
