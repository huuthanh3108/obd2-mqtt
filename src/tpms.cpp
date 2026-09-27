#include "tpms.h"
#include "debug_log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <strings.h>

#ifdef TPMS_BLE_SCAN
#include <NimBLEDevice.h>
#endif

namespace {
constexpr uint8_t DJTPMS_CRC8_POLY = 0x2F;
constexpr uint8_t DJTPMS_CRC8_INIT = 0xDF;
constexpr int ATMOSPHERIC_PRESSURE_KPA = 101;
constexpr unsigned long TPMS_SIMULATION_INTERVAL_MS = 5000UL;
constexpr unsigned long TPMS_BLE_INITIAL_DELAY_MS = 30000UL;
constexpr unsigned long TPMS_BLE_SCAN_INTERVAL_MS = 60000UL;
// A wheel counts as current when its sensor was heard within this window (ten
// scans). Older values are withdrawn rather than shown as if they were live.
constexpr unsigned long TPMS_FRESH_MS = 600000UL;
constexpr uint32_t TPMS_BLE_SCAN_DURATION_MS = 20000;

struct TPMSFieldDescription {
    const char *suffix;
    const char *nameSuffix;
    const char *icon;
    const char *unit;
    const char *deviceClass;
    const char *stateClass;
    const char *entityCategory;
};

constexpr TPMSFieldDescription TPMS_FIELDS[] = {
    {"pressure_bar", "Pressure", "car-tire-alert", "bar", "pressure", SC_MEASUREMENT, ""},
    {"pressure_kpa", "Pressure kPa", "car-tire-alert", "kPa", "pressure", SC_MEASUREMENT, ""},
    {"temperature", "Temperature", "thermometer", "°C", "temperature", SC_MEASUREMENT, ""},
    {"battery", "Battery", "battery", "%", "battery", SC_MEASUREMENT, EC_DIAGNOSTIC},
    {"battery_voltage", "Battery Voltage", "battery", "V", "voltage", SC_MEASUREMENT, EC_DIAGNOSTIC},
    {"rssi", "RSSI", "signal", "dBm", "signal_strength", "", EC_DIAGNOSTIC},
    {"last_seen", "Last Seen", "timer", "s", "", SC_MEASUREMENT, EC_DIAGNOSTIC},
};

void makeFieldName(const TPMSReading &reading, const char *suffix, char *buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "tpms_%s_%s", reading.wheel, suffix);
}

const char *wheelDisplayName(const char *wheel) {
    if (strcmp(wheel, "front_left") == 0) {
        return "Trước trái";
    }
    if (strcmp(wheel, "front_right") == 0) {
        return "Trước phải";
    }
    if (strcmp(wheel, "rear_left") == 0) {
        return "Sau trái";
    }
    if (strcmp(wheel, "rear_right") == 0) {
        return "Sau phải";
    }
    return wheel;
}

void makeDisplayName(const TPMSReading &reading, const char *suffix, char *buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "Altis TPMS - %s - %s", wheelDisplayName(reading.wheel), suffix);
}

void formatFloat(float value, char *buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "%0.2f", static_cast<double>(value));
}

int toInt8(uint8_t value) {
    return value >= 128 ? static_cast<int>(value) - 256 : value;
}

void formatHex(const std::string &data, char *buffer, size_t bufferSize) {
    if (bufferSize == 0) {
        return;
    }

    size_t offset = 0;
    for (uint8_t value: data) {
        if (offset + 2 >= bufferSize) {
            break;
        }
        offset += snprintf(buffer + offset, bufferSize - offset, "%02X", value);
    }
}
}

TPMSManager TPMS;

void TPMSManager::begin() {
    const char *wheels[] = {"front_left", "front_right", "rear_left", "rear_right"};
    const char *macs[] = {
        "d0:0c:5e:5c:62:96",
        "d0:0c:5e:5c:7d:bf",
        "54:6c:50:63:ae:87",
        "d0:0c:5e:5c:5a:c6",
    };
    for (size_t i = 0; i < readings.size(); ++i) {
        readings[i].valid = true;
        strlcpy(readings[i].wheel, wheels[i], sizeof(readings[i].wheel));
        strlcpy(readings[i].mac, macs[i], sizeof(readings[i].mac));
    }

#ifdef TPMS_SIMULATION
    DBG_PRINTLN("TPMS: simulation mode enabled");
    updateSimulation();
#elif defined(TPMS_BLE_SCAN)
    nextBleScan = millis() + TPMS_BLE_INITIAL_DELAY_MS;
    DBG_PRINTLN("TPMS: BLE scan enabled; waiting before first passive scan");
#else
    DBG_PRINTLN("TPMS: enabled; BLE scanner integration pending real sensor capture");
#endif
}

void TPMSManager::loop() {
#ifdef TPMS_SIMULATION
    if (millis() >= nextSimulationUpdate) {
        updateSimulation();
    }
#elif defined(TPMS_BLE_SCAN)
    if (millis() >= nextBleScan) {
        scanBle();
        nextBleScan = millis() + TPMS_BLE_SCAN_INTERVAL_MS;
    }
#endif
}

bool TPMSManager::sendDiscovery(MQTT &mqtt, bool allowOffline) const {
    bool success = false;

    for (const auto &reading: readings) {
        if (!reading.valid) {
            continue;
        }

        for (const auto &field: TPMS_FIELDS) {
            char fieldName[48] = {'\0'};
            char displayName[80] = {'\0'};
            makeFieldName(reading, field.suffix, fieldName, sizeof(fieldName));
            makeDisplayName(reading, field.nameSuffix, displayName, sizeof(displayName));
            // last_seen stays tied to the device only: it is the one reading
            // that says how old the others are.
            char statusField[48] = {'\0'};
            if (strcmp(field.suffix, "last_seen") != 0) {
                makeFieldName(reading, "status", statusField, sizeof(statusField));
            }
            success |= mqtt.sendTopicConfig(fieldName,
                                            displayName,
                                            field.icon,
                                            field.unit,
                                            field.deviceClass,
                                            field.stateClass,
                                            field.entityCategory,
                                            TT_SENSOR,
                                            "",
                                            allowOffline,
                                            "",
                                            statusField);
        }
    }

    return success;
}

void TPMSManager::resetAvailability() {
    publishedStatus.fill(-1);
}

void TPMSManager::sendAvailability(MQTT &mqtt) {
    const unsigned long now = millis();
    for (size_t i = 0; i < readings.size(); ++i) {
        const TPMSReading &reading = readings[i];
        if (!reading.valid) {
            continue;
        }
        const bool fresh = reading.lastSeen != 0 && now - reading.lastSeen < TPMS_FRESH_MS;
        const int8_t status = fresh ? 1 : 0;
        if (publishedStatus[i] == status) {
            continue;
        }

        char fieldName[48] = {'\0'};
        makeFieldName(reading, "status", fieldName, sizeof(fieldName));
        if (!mqtt.sendTopicUpdate(fieldName, fresh ? OBD_STATUS_ONLINE : OBD_STATUS_OFFLINE)) {
            continue;
        }
        publishedStatus[i] = status;

        if (!fresh) {
            // Withdraw the retained values, or the broker keeps serving the
            // last pressure as if it were current.
            for (const auto &field: TPMS_FIELDS) {
                const bool isLastSeen = strcmp(field.suffix, "last_seen") == 0;
                if (isLastSeen && reading.lastSeen != 0) {
                    continue;
                }
                makeFieldName(reading, field.suffix, fieldName, sizeof(fieldName));
                mqtt.sendTopicUpdate(fieldName, "");
            }
            DBG_PRINTF("TPMS %s offline: cleared retained values\n", reading.wheel);
        }
    }
}

bool TPMSManager::sendState(MQTT &mqtt) const {
    bool success = false;
    bool sentAny = false;

    for (const auto &reading: readings) {
        if (!reading.valid || reading.lastSeen == 0) {
            continue;
        }

        sentAny = true;

        char fieldName[48] = {'\0'};
        char payload[32] = {'\0'};

        makeFieldName(reading, "last_seen", fieldName, sizeof(fieldName));
        snprintf(payload, sizeof(payload), "%lu", (millis() - reading.lastSeen) / 1000UL);
        success |= mqtt.sendTopicUpdate(fieldName, payload);

        // Stale wheel: its values were withdrawn by sendAvailability(), do not
        // put them back.
        if (millis() - reading.lastSeen >= TPMS_FRESH_MS) {
            continue;
        }

        makeFieldName(reading, "pressure_bar", fieldName, sizeof(fieldName));
        formatFloat(reading.gaugePressureBar, payload, sizeof(payload));
        success |= mqtt.sendTopicUpdate(fieldName, payload);

        makeFieldName(reading, "pressure_kpa", fieldName, sizeof(fieldName));
        snprintf(payload, sizeof(payload), "%d", reading.gaugePressureKpa);
        success |= mqtt.sendTopicUpdate(fieldName, payload);

        makeFieldName(reading, "temperature", fieldName, sizeof(fieldName));
        snprintf(payload, sizeof(payload), "%d", reading.temperatureC);
        success |= mqtt.sendTopicUpdate(fieldName, payload);

        makeFieldName(reading, "battery", fieldName, sizeof(fieldName));
        snprintf(payload, sizeof(payload), "%d", reading.batteryPercent);
        success |= mqtt.sendTopicUpdate(fieldName, payload);

        makeFieldName(reading, "battery_voltage", fieldName, sizeof(fieldName));
        formatFloat(reading.batteryVoltage, payload, sizeof(payload));
        success |= mqtt.sendTopicUpdate(fieldName, payload);

        makeFieldName(reading, "rssi", fieldName, sizeof(fieldName));
        snprintf(payload, sizeof(payload), "%d", reading.rssi);
        success |= mqtt.sendTopicUpdate(fieldName, payload);
    }

    return sentAny ? success : true;
}

bool TPMSManager::parseDjtpmsPayload(const uint8_t *payload,
                                     size_t length,
                                     uint16_t manufacturerId,
                                     bool hasManufacturerId,
                                     TPMSReading &reading) {
    if (payload == nullptr || length < 12) {
        return false;
    }

    if (hasManufacturerId) {
        const uint8_t cid0 = manufacturerId & 0xFF;
        const uint8_t cid1 = (manufacturerId >> 8) & 0xFF;

        if (length >= 14) {
            const uint8_t *frame14 = payload + length - 14;
            if (frame14[0] == cid0 && frame14[1] == cid1 && parseFrame(cid0, cid1, frame14 + 2, reading)) {
                return true;
            }
            if (cid0 != cid1 && frame14[0] == cid1 && frame14[1] == cid0 &&
                parseFrame(cid1, cid0, frame14 + 2, reading)) {
                return true;
            }
        }

        const uint8_t *frame12 = payload + length - 12;
        if (parseFrame(cid0, cid1, frame12, reading)) {
            return true;
        }
        if (cid0 != cid1 && parseFrame(cid1, cid0, frame12, reading)) {
            return true;
        }
        return false;
    }

    if (length >= 14) {
        const uint8_t *frame14 = payload + length - 14;
        return parseFrame(frame14[0], frame14[1], frame14 + 2, reading);
    }

    return false;
}

void TPMSManager::scanBle() {
#ifdef TPMS_BLE_SCAN
    if (!NimBLEDevice::isInitialized()) {
        DBG_PRINTLN("TPMS: BLE stack is not initialized yet; scan skipped");
        return;
    }

    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan == nullptr) {
        DBG_PRINTLN("TPMS: failed to get BLE scanner");
        return;
    }

    DBG_PRINTLN("TPMS: passive BLE scan start");
    scan->stop();
    scan->clearResults();
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->setMaxResults(64);

    int updated = 0;
    NimBLEScanResults results = scan->getResults(TPMS_BLE_SCAN_DURATION_MS, false);
    DBG_PRINTF("TPMS: scan found %d BLE device(s)\n", results.getCount());
    for (int i = 0; i < results.getCount(); ++i) {
        const NimBLEAdvertisedDevice *device = results.getDevice(i);
        if (device == nullptr) {
            continue;
        }

        const std::string macString = device->getAddress().toString();
#ifdef TPMS_VERBOSE_SCAN
        if (!device->getName().empty() ||
            device->getManufacturerDataCount() > 0 ||
            device->getServiceDataCount() > 0) {
            Serial.printf("TPMS: seen %s name=%s RSSI=%d mfg=%u svc=%u\n",
                          macString.c_str(),
                          device->getName().empty() ? "<no name>" : device->getName().c_str(),
                          device->getRSSI(),
                          device->getManufacturerDataCount(),
                          device->getServiceDataCount());
        }
#endif

        bool parsed = false;
        TPMSReading parsedReading;

        for (uint8_t dataIndex = 0; dataIndex < device->getManufacturerDataCount() && !parsed; ++dataIndex) {
            const std::string data = device->getManufacturerData(dataIndex);
#ifdef TPMS_VERBOSE_SCAN
            char hex[80] = {'\0'};
            formatHex(data, hex, sizeof(hex));
            Serial.printf("TPMS: manufacturer data[%u]=%s\n", dataIndex, hex);
#endif
            if (data.size() >= 2) {
                const auto *bytes = reinterpret_cast<const uint8_t *>(data.data());
                const uint16_t manufacturerId = static_cast<uint16_t>(bytes[0]) |
                                                (static_cast<uint16_t>(bytes[1]) << 8);
                parsed = parseDjtpmsPayload(bytes + 2, data.size() - 2, manufacturerId, true, parsedReading);
            }

            if (!parsed && data.size() >= 12) {
                const auto *bytes = reinterpret_cast<const uint8_t *>(data.data());
                parsed = parseDjtpmsPayload(bytes, data.size(), 0x0000, true, parsedReading);
            }

            if (!parsed && !data.empty()) {
                const auto *bytes = reinterpret_cast<const uint8_t *>(data.data());
                parsed = parseDjtpmsPayload(bytes, data.size(), 0, false, parsedReading);
            }
        }

        for (uint8_t dataIndex = 0; dataIndex < device->getServiceDataCount() && !parsed; ++dataIndex) {
            const std::string data = device->getServiceData(dataIndex);
#ifdef TPMS_VERBOSE_SCAN
            char hex[80] = {'\0'};
            formatHex(data, hex, sizeof(hex));
            Serial.printf("TPMS: service data[%u]=%s\n", dataIndex, hex);
#endif
            if (!data.empty()) {
                const auto *bytes = reinterpret_cast<const uint8_t *>(data.data());
                parsed = parseDjtpmsPayload(bytes, data.size(), 0, false, parsedReading);
            }
        }

        if (parsed) {
            const char *readingMac = parsedReading.mac[0] != '\0' ? parsedReading.mac : macString.c_str();
            TPMSReading *target = findReadingByMac(readingMac);
            if (target == nullptr) {
                DBG_PRINTF("TPMS: DJTPMS payload from %s contains unknown sensor MAC %s\n",
                           macString.c_str(),
                           readingMac);
                continue;
            }

            if (applyParsedReading(readingMac, parsedReading, device->getRSSI())) {
                ++updated;
                DBG_PRINTF("TPMS: %s %0.2f bar %d C %0.1f V RSSI %d adv=%s\n",
                           target->wheel,
                           static_cast<double>(target->gaugePressureBar),
                           target->temperatureC,
                           static_cast<double>(target->batteryVoltage),
                           target->rssi,
                           macString.c_str());
            }
        }
    }

    scan->clearResults();
    DBG_PRINTF("TPMS: passive BLE scan done, updated %d sensor(s)\n", updated);
#endif
}

bool TPMSManager::applyParsedReading(const char *mac, const TPMSReading &parsed, int rssi) {
    TPMSReading *target = findReadingByMac(mac);
    if (target == nullptr) {
        return false;
    }

    const bool changed = target->lastSeen == 0 ||
                         std::abs(target->gaugePressureKpa - parsed.gaugePressureKpa) >= 1 ||
                         target->temperatureC != parsed.temperatureC ||
                         std::abs(target->batteryVoltage - parsed.batteryVoltage) >= 0.05F ||
                         target->rssi != rssi;
    target->batteryVoltage = parsed.batteryVoltage;
    target->batteryPercent = parsed.batteryPercent;
    target->temperatureC = parsed.temperatureC;
    target->absolutePressureKpa = parsed.absolutePressureKpa;
    target->gaugePressureKpa = parsed.gaugePressureKpa;
    target->gaugePressureBar = parsed.gaugePressureBar;
    target->rssi = rssi;
    target->lastSeen = parsed.lastSeen;
    target->valid = true;

    return changed;
}

TPMSReading *TPMSManager::findReadingByMac(const char *mac) {
    if (mac == nullptr) {
        return nullptr;
    }

    for (auto &reading: readings) {
        if (strcasecmp(reading.mac, mac) == 0) {
            return &reading;
        }
    }

    return nullptr;
}

void TPMSManager::updateSimulation() {
    const unsigned long now = millis();
    const int phase = static_cast<int>((now / TPMS_SIMULATION_INTERVAL_MS) % 4);

    const int pressuresKpa[] = {200 + phase, 201, 209 - phase, 210};
    const int temperatures[] = {34, 34 + (phase % 2), 35, 35};
    const int rssiValues[] = {-61, -58, -67, -63};

    for (size_t i = 0; i < readings.size(); ++i) {
        readings[i].valid = true;
        readings[i].batteryVoltage = 3.0F;
        readings[i].batteryPercent = batteryPercentFromVoltage(readings[i].batteryVoltage);
        readings[i].temperatureC = temperatures[i];
        readings[i].gaugePressureKpa = pressuresKpa[i];
        readings[i].absolutePressureKpa = readings[i].gaugePressureKpa + ATMOSPHERIC_PRESSURE_KPA;
        readings[i].gaugePressureBar = readings[i].gaugePressureKpa / 100.0F;
        readings[i].rssi = rssiValues[i];
        readings[i].lastSeen = now;
    }

    nextSimulationUpdate = now + TPMS_SIMULATION_INTERVAL_MS;
}

int TPMSManager::batteryPercentFromVoltage(float voltage) {
    struct CurvePoint {
        float voltage;
        int percent;
    };

    constexpr CurvePoint curve[] = {
        {3.30F, 100},
        {3.05F, 97},
        {2.94F, 91},
        {2.90F, 75},
        {2.85F, 25},
        {2.80F, 17},
        {2.60F, 0},
    };

    if (voltage >= curve[0].voltage) {
        return 100;
    }
    constexpr size_t curveSize = sizeof(curve) / sizeof(curve[0]);
    if (voltage < curve[curveSize - 1].voltage) {
        return 0;
    }

    for (size_t i = 0; i < curveSize - 1; ++i) {
        const auto &high = curve[i];
        const auto &low = curve[i + 1];
        if (low.voltage < voltage && voltage <= high.voltage) {
            const float ratio = (voltage - low.voltage) / (high.voltage - low.voltage);
            return static_cast<int>(roundf(low.percent + ratio * (high.percent - low.percent)));
        }
    }

    return 0;
}

bool TPMSManager::parseFrame(uint8_t cid0, uint8_t cid1, const uint8_t *frame12, TPMSReading &reading) {
    if (frame12 == nullptr) {
        return false;
    }

    const uint8_t crcInput[] = {cid0, cid1, frame12[0], frame12[1], frame12[2], frame12[3]};
    if (crc8(crcInput, sizeof(crcInput)) != frame12[5]) {
        return false;
    }

    reading.batteryVoltage = frame12[0] / 10.0F;
    reading.batteryPercent = frame12[0] == 0 ? 100 : batteryPercentFromVoltage(reading.batteryVoltage);
    reading.temperatureC = toInt8(frame12[1]);
    reading.absolutePressureKpa = (static_cast<int>(frame12[2]) << 8) | frame12[3];
    reading.gaugePressureKpa = std::max(reading.absolutePressureKpa - ATMOSPHERIC_PRESSURE_KPA, 0);
    reading.gaugePressureBar = reading.gaugePressureKpa / 100.0F;
    snprintf(reading.mac,
             sizeof(reading.mac),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             frame12[6],
             frame12[7],
             frame12[8],
             frame12[9],
             frame12[10],
             frame12[11]);
    reading.valid = true;
    reading.lastSeen = millis();
    return true;
}

uint8_t TPMSManager::crc8(const uint8_t *data, size_t length) {
    uint8_t crc = DJTPMS_CRC8_INIT;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if ((crc & 0x80) != 0) {
                crc = static_cast<uint8_t>((crc << 1) ^ DJTPMS_CRC8_POLY);
            } else {
                crc = static_cast<uint8_t>(crc << 1);
            }
        }
    }
    return crc;
}
