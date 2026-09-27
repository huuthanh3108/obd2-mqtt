/*
 * This program is free software; you can use it, redistribute it
 * and / or modify it under the terms of the GNU General Public License
 * (GPL) as published by the Free Software Foundation; either version 3
 * of the License or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program, in a file called gpl.txt or license.txt.
 * If not, write to the Free Software Foundation Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307 USA
 */
#include "mqtt.h"

#include <WiFi.h>
#include <atomic>

#ifdef USE_BLE

#if !defined(CONFIG_BT_ENABLED)
#error Bluetooth is not enabled
#endif

#else

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled
#endif

#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Bluetooth not available
#endif

#endif

#ifndef BUILD_GIT_BRANCH
#define BUILD_GIT_BRANCH ""
#endif
#ifndef BUILD_GIT_COMMIT_HASH
#define BUILD_GIT_COMMIT_HASH ""
#endif

#define MIN_VOLTAGE_LEVEL       3300
#define LOW_VOLTAGE_LEVEL       3600            // Sleep shutdown voltage

#include <LittleFS.h>

#define FORMAT_LITTLEFS_IF_FAILED true

#define DISCOVERED_DEVICES_FILE "/discovered_devices.json"

// Shown as the device name in Home Assistant; the entity names carry their own
// prefixes ("Altis OBD2 - " from states.json, "Altis TPMS - " from tpms.cpp).
#define DEVICE_DISPLAY_NAME     "Altis Gateway"

#define HA_T_CPUTEMP            "cpuTemp"
#define HA_T_FREEMEM            "freeMem"
#define HA_T_UPTIME             "uptime"
#define HA_T_RECONNECTS         "reconnects"
#define HA_T_IP_ADDR            "ipAddress"
#define HA_T_WIFI_SSID          "wifiSSID"
#define HA_T_SQ                 "signalQuality"
#define HA_T_BAT_VOL            "internalBatteryVoltage"
#define HA_T_BAT_LVL            "internalBatteryLevel"
#define HA_T_GSM_LOC            "gsmLocation"
#define HA_T_GPS_LOC            "gpsLocation"
#define HA_T_OBD_LAST_SEEN      "obdLastSeen"
#define HA_T_OBD_DATA_AGE       "obdDataAge"
#define HAT_T_DTC               "dtc"
#define HAT_T_CLEAR_DTC         "clearDTC"

#include <numeric>
#include <algorithm>

#include "settings.h"
#include "helper.h"
#include "obd.h"
#include "gsm.h"
#include "http.h"
#include "debug_log.h"
#include "esp_task_wdt.h"
#ifdef ENABLE_TPMS
#include "tpms.h"
#endif

#ifdef TOYOTA_EXPLORER
#include "ElmConsole.h"
#endif

HTTPServer server(80);

#define DEBUG_PORT Serial

#define SERIAL_CMD_MAX_LEN 32768
#define SERIAL_RX_BUFFER_SIZE 4096

// #define DUMP_AT_COMMANDS

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, Serial);
GSM gsm(debugger);
#else
#ifdef NO_MODEM
GSM gsm(Serial);
#else
GSM gsm(SerialAT);
#endif
#endif

MQTT mqtt = MQTT();

std::atomic_bool wifiAPStarted{false};
std::atomic_bool wifiAPInUse{false};
std::atomic<unsigned int> wifiAPStaConnected{0};

std::atomic_bool obdConnected{false};
std::atomic<int> obdConnectErrors{0};

std::atomic<unsigned long> startTime{0};

std::vector<uint32_t> batteryVoltages;
std::atomic<unsigned long> batteryTime{0};

std::atomic_bool allDiscoverySend{false};
std::atomic_bool allDiagnosticDiscoverySend{false};
std::atomic_bool allStaticDiagnosticDiscoverySend{false};

std::atomic<unsigned long> lastDebugOutput{0};
std::atomic<unsigned long> lastMQTTDiscoveryOutput{0};
std::atomic<unsigned long> lastMQTTDiagnosticDiscoveryOutput{0};
std::atomic<unsigned long> lastMQTTStaticDiagnosticDiscoveryOutput{0};
std::atomic<unsigned long> lastMQTTOutput{0};
std::atomic<unsigned long> lastMQTTDiagnosticOutput{0};
std::atomic<unsigned long> lastMQTTStaticDiagnosticOutput{0};
std::atomic<unsigned long> lastMQTTDTCDiagnosticOutput{0};
std::atomic<unsigned long> lastMQTTLocationOutput{0};

std::atomic<int> signalQuality{0};
std::atomic<float> gsmLatitude{0};
std::atomic<float> gsmLongitude{0};
std::atomic<float> gsmAccuracy{0};
std::atomic<float> gpsLatitude{0};
std::atomic<float> gpsLongitude{0};
std::atomic<float> gpsAccuracy{0};

std::atomic_bool clearDTC{false};

TaskHandle_t outputTaskHdl;
TaskHandle_t stateTaskHdl;

constexpr unsigned long OBD_RECONNECT_INITIAL_MS = 5000UL;
constexpr unsigned long OBD_RECONNECT_MAX_MS = 300000UL;
constexpr unsigned long MQTT_RECONNECT_INITIAL_MS = 5000UL;
constexpr unsigned long MQTT_RECONNECT_MAX_MS = 120000UL;
constexpr unsigned long APP_WATCHDOG_TIMEOUT_SEC = 90UL;
constexpr int MQTT_CONNECT_TIMEOUT_MS = 5000;

// Consecutive failed broker connects, while WiFi itself stays associated,
// before the current network is treated as the problem. WiFi status cannot
// tell a working hotspot from one that associates but carries no traffic.
constexpr uint8_t MQTT_HEALTH_MAX_FAILURES = 3;

std::atomic<unsigned long> nextOBDReconnect{0};
std::atomic<unsigned long> obdReconnectDelay{OBD_RECONNECT_INITIAL_MS};
std::atomic<unsigned long> nextMQTTReconnect{0};
std::atomic<unsigned long> mqttReconnectDelay{MQTT_RECONNECT_INITIAL_MS};
std::atomic<uint8_t> mqttHealthFailures{0};

size_t getESPHeapSize() {
    return heap_caps_get_free_size(MALLOC_CAP_8BIT);
}

void deepSleep(const int sec) {
    log_d("Prepare nap...");
    WiFi.disconnect(true);
    OBD.end();
    #ifndef NO_MODEM
    gsm.powerOff();
    #endif
    if (outputTaskHdl != nullptr) {
        vTaskDelete(outputTaskHdl);
    }
    if (stateTaskHdl != nullptr) {
        vTaskDelete(stateTaskHdl);
    }
    log_d("...ZzZzZz.");
    GSM::deepSleep(sec * 1000);
}

void consoleSendHeader(const char *str) {
    DBG_PRINTF("Send %s data...", str);
}

void consoleSendFooter(const bool success, const unsigned long time) {
    DBG_PRINTF("...%s (%lums)\n", success ? "done" : "failed", time);
}

std::string buildDTCPayload(DTCs *dtcs) {
    JsonDocument doc;
    JsonArray a = doc["dtc"].to<JsonArray>();
    for (int i = 0; i < dtcs->getCount(); ++i) {
        a.add(dtcs->getCode(i)->c_str());
    }
    std::string payload;
    serializeJson(doc, payload);
    return payload;
}

void WiFiAPStart(WiFiEvent_t event, WiFiEventInfo_t info) {
    wifiAPStarted = true;
    DEBUG_PORT.println("AP started.");

    DEBUG_PORT.printf("AP - IP address: %s\n", WiFi.softAPIP().toString().c_str());
}

void WiFiAPStop(WiFiEvent_t event, WiFiEventInfo_t info) {
    wifiAPStarted = false;
    wifiAPInUse = false;
    wifiAPStaConnected = 0;
    DEBUG_PORT.println("AP stopped.");
}

void WiFiAPStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    ++wifiAPStaConnected;
    wifiAPInUse = true;

    if (wifiAPStaConnected == 1) {
        DEBUG_PORT.println("AP in use.");
        OBD.end();
    }
}

void WiFiAPStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (wifiAPStaConnected != 0) {
        --wifiAPStaConnected;
    }

    if (wifiAPStaConnected == 0) {
        DEBUG_PORT.println("AP all clients disconnected.");
        OBD.begin(Settings.OBD2.getName(OBD_ADP_NAME), Settings.OBD2.getMAC(), Settings.OBD2.getProtocol(),
                  Settings.OBD2.getCheckPIDSupport(), Settings.OBD2.getDebug(), Settings.OBD2.getSpecifyNumResponses());
        OBD.connect(true);
        wifiAPInUse = false;
    }
}

void startWiFiAP() {
    DEBUG_PORT.print("Start AP...");

    WiFi.disconnect(true);

#ifdef NO_MODEM
    WiFi.mode(WIFI_AP_STA);
#else
    WiFi.mode(WIFI_AP);
#endif

    WiFi.onEvent(WiFiAPStart, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_START);
    WiFi.onEvent(WiFiAPStop, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STOP);
    WiFi.onEvent(WiFiAPStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STACONNECTED);
    WiFi.onEvent(WiFiAPStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);

    String ssid = Settings.WiFi.getAPSSID();
#ifdef NO_MODEM
    ssid = "OBD2-MQTT-" + String(stripChars(WiFi.macAddress().c_str()).c_str());
    WiFi.softAP(ssid.c_str(), "obd2mqtt");
#else
    if (ssid.isEmpty()) {
        ssid = "OBD2-MQTT-" + String(stripChars(WiFi.macAddress().c_str()).c_str());
        Settings.WiFi.setAPSSID(ssid.c_str());
    }
    WiFi.softAP(
        ssid.c_str(),
        Settings.WiFi.getAPPassword()
    );
#endif
}

void startHttpServer() {
    server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, MIME_TYPE_PLAIN, getVersion());
    });

    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, MIME_TYPE_JSON, Settings.buildJson().c_str());
    });

    server.on(
        "/api/settings",
        HTTP_PUT,
        [](AsyncWebServerRequest *request) {
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (request->contentType() == MIME_TYPE_JSON) {
                if (!index) {
                    request->_tempObject = malloc(total);
                }

                if (request->_tempObject != nullptr) {
                    memcpy(static_cast<uint8_t *>(request->_tempObject) + index, data, len);

                    if (index + len == total) {
                        auto json = std::string(static_cast<const char *>(request->_tempObject), total);
                        if (Settings.parseJson(json)) {
                            if (Settings.writeSettings(LittleFS)) {
                                request->send(200);
                            }
                        } else {
                            request->send(500);
                        }
                    }
                }
            } else {
                request->send(406);
            }
        }
    );

    server.on("/api/states", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, MIME_TYPE_JSON, OBD.buildJSON().c_str());
    });

    server.on(
        "/api/states",
        HTTP_PUT,
        [](AsyncWebServerRequest *request) {
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (request->contentType() != MIME_TYPE_JSON) {
                request->send(406);
                return;
            }

            if (!index) {
                // The body is ~17KB for a full profile. Take it from PSRAM like
                // the state objects do - the internal heap is the scarce one
                // with BLE, WiFi and MQTT all running.
                request->_tempObject = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
                if (request->_tempObject == nullptr) {
                    request->_tempObject = malloc(total);
                }

                if (request->_tempObject == nullptr) {
                    request->send(507, MIME_TYPE_PLAIN, "no buffer for request body");
                    return;
                }
            }

            if (request->_tempObject == nullptr) {
                // Allocation failed on the first chunk and was already reported.
                return;
            }

            memcpy(static_cast<uint8_t *>(request->_tempObject) + index, data, len);

            if (index + len != total) {
                return;
            }

            auto json = std::string(static_cast<const char *>(request->_tempObject), total);
            // Release the body buffer before parsing: writeStates() builds a
            // JsonDocument of its own and both do not have to be live at once.
            free(request->_tempObject);
            request->_tempObject = nullptr;

            if (!OBD.parseJSON(json)) {
                request->send(500, MIME_TYPE_PLAIN, "states JSON could not be parsed");
                return;
            }

            // Every failure path must answer. A silent close leaves the client
            // hanging and hides which of the two steps actually failed.
            if (!OBD.writeStates(LittleFS)) {
                request->send(500, MIME_TYPE_PLAIN, "states parsed but could not be written to flash");
                return;
            }

            request->send(200);
        }
    );

    server.on("/api/canDeepSleep", HTTP_GET, [](AsyncWebServerRequest *request) {
        std::string payload;
        JsonDocument doc;

        doc["canDeepSleep"] = GSM::canDeepSleep();

        serializeJson(doc, payload);

        request->send(200, MIME_TYPE_JSON, payload.c_str());
    });

    server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        std::string payload;
        JsonDocument wifiInfo;

        wifiInfo["hostname"] = WiFi.softAPgetHostname();
        wifiInfo["SSID"] = WiFi.softAPSSID();
        wifiInfo["ip"] = WiFi.softAPIP().toString();
        wifiInfo["mac"] = WiFi.macAddress();
#ifdef NO_MODEM
        wifiInfo["staConnected"] = WiFi.status() == WL_CONNECTED;
        wifiInfo["staSSID"] = WiFi.SSID();
        wifiInfo["staIP"] = WiFi.localIP().toString();
        wifiInfo["rssi"] = WiFi.RSSI();
#endif

        serializeJson(wifiInfo, payload);

        request->send(200, MIME_TYPE_JSON, payload.c_str());
    });
    server.on("/api/modem", HTTP_GET, [](AsyncWebServerRequest *request) {
        std::string payload;
        JsonDocument modemInfo;

#ifdef NO_MODEM
        modemInfo["name"] = "WiFi STA";
        modemInfo["info"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
        modemInfo["signalQuality"] = WiFi.RSSI();
        modemInfo["ip"] = WiFi.localIP().toString();
#else
        modemInfo["name"] = gsm.modem.getModemName();
        modemInfo["info"] = gsm.modem.getModemInfo();
        modemInfo["signalQuality"] = gsm.modem.getSignalQuality();
        modemInfo["ip"] = gsm.modem.getLocalIP();
        modemInfo["IMEI"] = gsm.modem.getIMEI();
        modemInfo["IMSI"] = gsm.modem.getIMSI();
        modemInfo["CCID"] = gsm.modem.getSimCCID();
        modemInfo["operator"] = gsm.modem.getOperator();
#endif

        serializeJson(modemInfo, payload);

        request->send(200, MIME_TYPE_JSON, payload.c_str());
    });

    server.on("/api/discoveredDevices", HTTP_GET, [](AsyncWebServerRequest *request) {
        File file = LittleFS.open(DISCOVERED_DEVICES_FILE, FILE_READ);
        if (file && !file.isDirectory()) {
            JsonDocument doc;
            if (!deserializeJson(doc, file)) {
                std::string payload;
                serializeJson(doc, payload);
                request->send(200, MIME_TYPE_JSON, payload.c_str());
            } else {
                request->send(500);
            }
            file.close();
        } else {
            request->send(404);
        }
    });

    server.on("/api/DTCs", HTTP_GET, [](AsyncWebServerRequest *request) {
        DTCs *dtcs = OBD.getDTCs();
        if (dtcs != nullptr && dtcs->getCount() != 0) {
            request->send(200, MIME_TYPE_JSON, buildDTCPayload(dtcs).c_str());
        } else {
            request->send(404);
        }
    });

    server.begin(LittleFS);
}

void onOBDConnected() {
    obdConnected = true;
    obdConnectErrors = 0;
    obdReconnectDelay = OBD_RECONNECT_INITIAL_MS;
    nextOBDReconnect = 0;
}

void onOBDConnectError() {
    obdConnected = false;
    ++obdConnectErrors;
#if DEVICE_CAN_DEEP_SLEEP && DEVICE_HAS_BATTERY
    if (GSM::isBatteryUsed() && obdConnectErrors > 5) {
        deepSleep(Settings.General.getSleepDuration());
    }
#endif
}

void scheduleNextOBDReconnect() {
    const unsigned long delayMs = obdReconnectDelay.load();
    nextOBDReconnect = millis() + delayMs;
    obdReconnectDelay = std::min(delayMs * 2, OBD_RECONNECT_MAX_MS);
    DEBUG_PORT.printf("Next OBD BLE reconnect in %lus\n", delayMs / 1000UL);
}

void scheduleNextMQTTReconnect() {
    const unsigned long delayMs = mqttReconnectDelay.load();
    nextMQTTReconnect = millis() + delayMs;
    mqttReconnectDelay = std::min(delayMs * 2, MQTT_RECONNECT_MAX_MS);
    DEBUG_PORT.printf("Next MQTT reconnect in %lus\n", delayMs / 1000UL);
}

#ifdef USE_BLE
void onBLEDevicesDiscovered(BLEScanResultsSet *btDeviceList) {
    JsonDocument devices;

    File file = LittleFS.open(DISCOVERED_DEVICES_FILE, FILE_WRITE);
    if (!file) {
        log_d("Failed to open file discovered_devices.json for writing.");
        return;
    }

    for (int i = 0; i < btDeviceList->getCount(); i++) {
        JsonDocument dev;
        BLEAdvertisedDevice *device = btDeviceList->getDevice(i);
        if (device && !device->getName().empty()) {
            dev["name"] = device->getName();
            dev["mac"] = device->getAddress().toString();
            devices["device"].add(dev);
        }
    }

    serializeJson(devices, file);

    file.close();
}

#else
void onBTDevicesDiscovered(BTScanResults *btDeviceList) {
    JsonDocument devices;

    File file = LittleFS.open(DISCOVERED_DEVICES_FILE, FILE_WRITE);
    if (!file) {
        log_d("Failed to open file discovered_devices.json for writing.");
        return;
    }

    for (int i = 0; i < btDeviceList->getCount(); i++) {
        JsonDocument dev;
        BTAdvertisedDevice *device = btDeviceList->getDevice(i);
        dev["name"] = device->getName();
        dev["mac"] = device->getAddress().toString();
        devices["device"].add(dev);
    }

    serializeJson(devices, file);

    file.close();
}
#endif

bool sendDiscoveryData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;
    bool allowOffline = Settings.MQTT.getAllowOffline();

    consoleSendHeader("discovery");

    std::vector<OBDState *> states{};
    OBD.getStates([](const OBDState *state) {
        return state->isVisible() && state->isEnabled() && state->isSupported() && !(
                   state->isDiagnostic() && state->getUpdateInterval() == -1);
    }, states);
    if (!states.empty()) {
        for (auto &state: states) {
            allSendsSuccessed |= mqtt.sendTopicConfig(state->getName(), state->getDescription(), state->getIcon(),
                                                      state->getUnit(), state->getDeviceClass(),
                                                      strlen(state->getStateClass()) != 0
                                                          ? state->getStateClass()
                                                          : (state->isMeasurement() ? SC_MEASUREMENT : ""),
                                                      state->isDiagnostic() ? EC_DIAGNOSTIC : "",
                                                      state->valueType() == OBD_STATE_TYPE_BOOL
                                                          ? TT_B_SENSOR
                                                          : TT_SENSOR,
                                                      "", allowOffline);
        }
    } else {
        allSendsSuccessed = true;
    }

#ifdef ENABLE_TPMS
    allSendsSuccessed |= TPMS.sendDiscovery(mqtt, allowOffline);
#endif

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

unsigned long latestOBDLastUpdate() {
    unsigned long latest = 0;
    std::vector<OBDState *> states{};
    OBD.getStates([](const OBDState *state) {
        return state->isVisible() &&
               state->isEnabled() &&
               state->isSupported() &&
               !state->isDiagnostic() &&
               state->getLastUpdate() > 0;
    }, states);

    for (auto &state: states) {
        latest = std::max(latest, static_cast<unsigned long>(state->getLastUpdate()));
    }

    return latest;
}

bool sendDiagnosticDiscoveryData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;

    consoleSendHeader("diagnostic discovery");

    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_CPUTEMP, "CPU Temperature", "thermometer", "°C", "temperature",
                                              SC_MEASUREMENT, EC_DIAGNOSTIC);
    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_FREEMEM, "Free Memory", "memory", "B", "", SC_MEASUREMENT,
                                              EC_DIAGNOSTIC);
    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_UPTIME, "Uptime", "timer-play", "sec", "", SC_MEASUREMENT,
                                              EC_DIAGNOSTIC);
    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_RECONNECTS, "Number of reconnects", "connection", "", "",
                                              SC_MEASUREMENT, EC_DIAGNOSTIC);
    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_OBD_LAST_SEEN, "OBD Last Seen", "timer", "s", "",
                                              SC_MEASUREMENT, EC_DIAGNOSTIC);
    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_OBD_DATA_AGE, "OBD Data Age", "timer-alert", "s", "",
                                              SC_MEASUREMENT, EC_DIAGNOSTIC);

    if (!gsm.getIpAddress().empty()) {
        allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_IP_ADDR, "IP Address", "network-outline", "", "", "",
                                                  EC_DIAGNOSTIC);
    }

#ifdef NO_MODEM
    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_WIFI_SSID, "WiFi SSID", "wifi", "", "", "",
                                              EC_DIAGNOSTIC);
#endif

    if (GSM::isUseGPRS()) {
        allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_SQ, "Signal Quality", "signal", "dBm",
                                                  "signal_strength", "", EC_DIAGNOSTIC);
    }

    if (GSM::hasGSMLocation()) {
        allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_GSM_LOC, "GSM Location", "crosshairs-gps", "", "", "",
                                                  EC_DIAGNOSTIC, TT_D_TRACKER, "gps", true);
    }

    if (GSM::hasGPSLocation()) {
        allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_GPS_LOC, "GPS Location", "crosshairs-gps", "", "", "",
                                                  EC_DIAGNOSTIC, TT_D_TRACKER, "gps", true);
    }

#if DEVICE_HAS_BATTERY
#if DEVICE_BATTERY_VOLTAGE
    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_BAT_VOL, "Internal Battery Voltage", "battery",
                                              "mV", "voltage", "", EC_DIAGNOSTIC);
#endif
#if DEVICE_BATTERY_LEVEL
    allSendsSuccessed |= mqtt.sendTopicConfig(HA_T_BAT_LVL, "Internal Battery Level", "battery",
                                              "%", "battery", "", EC_DIAGNOSTIC);
#endif
#endif

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

bool sendStaticDiagnosticDiscoveryData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;

    consoleSendHeader("static diagnostic discovery");

    std::vector<OBDState *> states{};
    OBD.getStates([](const OBDState *state) {
        return state->isVisible() && state->isEnabled() && state->isSupported() && state->isDiagnostic() && state->
               getUpdateInterval() == -1;
    }, states);
    if (!states.empty()) {
        for (auto &state: states) {
            allSendsSuccessed |= mqtt.sendTopicConfig(state->getName(), state->getDescription(), state->getIcon(),
                                                      state->getUnit(), state->getDeviceClass(),
                                                      strlen(state->getStateClass()) != 0
                                                          ? state->getStateClass()
                                                          : (state->isMeasurement() ? SC_MEASUREMENT : ""),
                                                      state->isDiagnostic() ? EC_DIAGNOSTIC : "",
                                                      state->valueType() == OBD_STATE_TYPE_BOOL
                                                          ? TT_B_SENSOR
                                                          : TT_SENSOR);
        }
    } else {
        allSendsSuccessed = true;
    }

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

bool sendStates(std::vector<OBDState *> &states, bool allSendsSuccessed) {
    if (!states.empty()) {
        for (auto &state: states) {
            // A state whose last read came back empty keeps its old value and
            // is not published. Sending it would report a value we do not
            // actually have - the "ESP32 offline -> headlight OFF" trap.
            //
            // Staying silent is not enough though: every value topic is
            // published retained, so the last good value would sit on the
            // broker for ever and Home Assistant would keep showing it as if
            // it were current. Clear the retained topic once, which makes the
            // entity read "unknown" instead of a number we no longer believe.
            if (state->isStale()) {
                if (!state->isRetainWhenStale() && !state->isStaleCleared()) {
                    mqtt.sendTopicUpdate(state->getName(), "");
                    state->setStaleCleared(true);
                    DBG_PRINTF("cleared retained value of %s (no data)\n", state->getName());
                }
                continue;
            }

            // A state that has never been read yet still holds its constructor
            // value of 0. Publishing that is the same lie as publishing a stale
            // one: after every boot or /api/states write the odometer would
            // announce 0 km until its first read lands, which for a 60s
            // interval on a starved read loop is a long time.
            if (state->getType() == obd::READ && state->getLastUpdate() == 0) {
                continue;
            }

            const size_t len = OBD.getPayloadLength() < 64 ? 64 : OBD.getPayloadLength() + 1;
            char tmp_char[len];

            if (state->valueType() == OBD_STATE_TYPE_INT) {
                auto *is = reinterpret_cast<OBDStateInt *>(state);
                char *str = is->formatValue();
                strncpy(tmp_char, str, len);
                free(str);
            } else if (state->valueType() == OBD_STATE_TYPE_FLOAT) {
                auto *is = reinterpret_cast<OBDStateFloat *>(state);
                char *str = is->formatValue();
                strncpy(tmp_char, str, len);
                free(str);
            } else if (state->valueType() == OBD_STATE_TYPE_BOOL) {
                auto *is = reinterpret_cast<OBDStateBool *>(state);
                char *str = is->formatValue();
                strncpy(tmp_char, str, len);
                free(str);
            }

            if (strcmp(state->getName(), "speed") == 0 ||
                strcmp(state->getName(), "engineCoolantTemp") == 0 ||
                strcmp(state->getName(), "engineLoad") == 0 ||
                strcmp(state->getName(), "throttle") == 0 ||
                strcmp(state->getName(), "batteryVoltage") == 0) {
                DBG_PRINTF("MQTT state update: %s=%s\n", state->getName(), tmp_char);
            }

            allSendsSuccessed |= mqtt.sendTopicUpdate(state->getName(), std::string(tmp_char));
        }
    } else {
        allSendsSuccessed = true;
    }

    return allSendsSuccessed;
}

bool sendOBDData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;

    consoleSendHeader("OBD");

    allSendsSuccessed |= mqtt.sendTopicUpdate(LWT_TOPIC, LWT_CONNECTED);

    std::vector<OBDState *> states{};
    OBD.getStates([](const OBDState *state) {
        return state->isVisible() && state->isEnabled() && state->isSupported() && !(
                   state->isDiagnostic() && state->getUpdateInterval() == -1);
    }, states);
    allSendsSuccessed = sendStates(states, allSendsSuccessed);

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

#ifdef ENABLE_TPMS
bool sendTPMSData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;

    consoleSendHeader("TPMS");
    allSendsSuccessed |= TPMS.sendState(mqtt);
    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}
#endif

bool sendDiagnosticData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;
    char tmp_char[50];

    consoleSendHeader("diagnostic");

    sprintf(tmp_char, "%d", static_cast<int>(temperatureRead()));
    allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_CPUTEMP, std::string(tmp_char));

    sprintf(tmp_char, "%lu", static_cast<long>(getESPHeapSize()));
    allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_FREEMEM, std::string(tmp_char));

    sprintf(tmp_char, "%lu", (millis() - startTime) / 1000);
    allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_UPTIME, std::string(tmp_char));

    sprintf(tmp_char, "%d", mqtt.reconnectAttemps());
    allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_RECONNECTS, std::string(tmp_char));

    const unsigned long obdLastSeen = latestOBDLastUpdate();
    const unsigned long obdDataAge = obdLastSeen == 0 ? 0 : (millis() - obdLastSeen) / 1000UL;
    sprintf(tmp_char, "%lu", obdLastSeen == 0 ? 0 : obdLastSeen / 1000UL);
    allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_OBD_LAST_SEEN, std::string(tmp_char));
    sprintf(tmp_char, "%lu", obdDataAge);
    allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_OBD_DATA_AGE, std::string(tmp_char));

    if (!gsm.getIpAddress().empty()) {
        sprintf(tmp_char, "%s", gsm.getIpAddress().c_str());
        allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_IP_ADDR, std::string(tmp_char));
    }

#ifdef NO_MODEM
    // Which hotspot of the ordered list actually carried the connection.
    const std::string ssid = gsm.getNetworkName();
    if (!ssid.empty()) {
        allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_WIFI_SSID, ssid);
    }
#endif

    if (GSM::isUseGPRS() && signalQuality != SQ_NOT_KNOWN) {
        sprintf(tmp_char, "%d", GSM::convertSQToRSSI(signalQuality));
        allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_SQ, std::string(tmp_char));
    }

#if DEVICE_HAS_BATTERY
#if DEVICE_BATTERY_VOLTAGE
    sprintf(tmp_char, "%d", GSM::getBatteryVoltage());
    allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_BAT_VOL, std::string(tmp_char));
#endif
#if DEVICE_BATTERY_LEVEL
    sprintf(tmp_char, "%d", static_cast<int>(GSM::getBatteryLevel()));
    allSendsSuccessed |= mqtt.sendTopicUpdate(HA_T_BAT_LVL, std::string(tmp_char));
#endif
#endif

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

bool sendStaticDiagnosticData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;

    consoleSendHeader("static diagnostic");

    std::vector<OBDState *> states{};
    OBD.getStates([](const OBDState *state) {
        return state->isVisible() && state->isEnabled() && state->isSupported() && state->isDiagnostic() && state->
               getUpdateInterval() == -1;
    }, states);
    allSendsSuccessed = sendStates(states, allSendsSuccessed);

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

bool sendDTCDiagnosticData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;

    consoleSendHeader("DTC diagnostic");

    DTCs *dtcs = OBD.getDTCs();
    if (dtcs != nullptr) {
        allSendsSuccessed |= mqtt.sendTopicConfig(HAT_T_DTC, "DTC", "engine",
                                                  "", "", "", EC_DIAGNOSTIC, TT_SENSOR, "", false,
                                                  "{{ value_json.dtc | join(\",\") }}");
        if (dtcs->getCount() != 0) {
            allSendsSuccessed |= mqtt.sendTopicConfig(HAT_T_CLEAR_DTC, "Clear DTC", "",
                                                      "", "", "", EC_DIAGNOSTIC, TT_BUTTON, "", false);

            mqtt.subscribe(HAT_T_CLEAR_DTC, [](const char *) {
                clearDTC = true;
            });
        }

        allSendsSuccessed |= mqtt.sendTopicUpdate(HAT_T_DTC, buildDTCPayload(dtcs));
    } else {
        allSendsSuccessed = true;
    }

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

std::string buildLocationAttrib(const float lat, const float lon, const float acc) {
    std::string payload;
    JsonDocument attribs;

    attribs["latitude"] = lat;
    attribs["longitude"] = lon;
    attribs["gps_accuracy"] = acc;

    serializeJson(attribs, payload);

    return payload;
}

bool sendLocationData() {
    const unsigned long start = millis();
    bool allSendsSuccessed = false;

    if (!GSM::hasGSMLocation() && !GSM::hasGPSLocation()) {
        return true;
    }

    consoleSendHeader("location");

    if (GSM::hasGSMLocation()) {
        allSendsSuccessed |= mqtt.sendTopicUpdate(
            HA_T_GSM_LOC,
            buildLocationAttrib(gsmLatitude, gsmLongitude, gsmAccuracy),
            true
        );
    }
    if (GSM::hasGPSLocation()) {
        allSendsSuccessed |= mqtt.sendTopicUpdate(
            HA_T_GPS_LOC,
            buildLocationAttrib(gpsLatitude, gpsLongitude, gpsAccuracy),
            true
        );
    }

    consoleSendFooter(allSendsSuccessed, millis() - start);

    return allSendsSuccessed;
}

unsigned long calcTimestamp(const unsigned int interval) {
    return millis() + interval * 1000L;
}

void mqttSendData() {
    if (millis() < lastMQTTOutput) {
        return;
    }

    if (mqtt.connected()) {
        if (millis() > lastMQTTDiscoveryOutput) {
            allDiscoverySend = false;
        }

        if (!allDiscoverySend) {
            if ((allDiscoverySend = sendDiscoveryData())) {
                lastMQTTDiscoveryOutput = calcTimestamp(Settings.MQTT.getDiscoveryInterval());
            } else {
                return;
            }
        }

        if (millis() > lastMQTTDiagnosticDiscoveryOutput) {
            allDiagnosticDiscoverySend = false;
        }

        if (!allDiagnosticDiscoverySend) {
            if ((allDiagnosticDiscoverySend = sendDiagnosticDiscoveryData())) {
                lastMQTTDiagnosticDiscoveryOutput = calcTimestamp(Settings.MQTT.getDiscoveryInterval());
            } else {
                return;
            }
        }

        if (millis() > lastMQTTStaticDiagnosticDiscoveryOutput) {
            allStaticDiagnosticDiscoverySend = false;
        }

        if (!allStaticDiagnosticDiscoverySend) {
            if ((allStaticDiagnosticDiscoverySend = sendStaticDiagnosticDiscoveryData())) {
                lastMQTTStaticDiagnosticDiscoveryOutput = calcTimestamp(Settings.MQTT.getDiscoveryInterval());
            } else {
                return;
            }
        }

        if (millis() > lastMQTTLocationOutput) {
            if (sendLocationData()) {
                lastMQTTLocationOutput = calcTimestamp(Settings.MQTT.getLocationInterval());
            } else {
                return;
            }
        }

        if (millis() > lastMQTTDiagnosticOutput) {
            if (sendDiagnosticData()) {
                lastMQTTDiagnosticOutput = calcTimestamp(Settings.MQTT.getDiagnosticInterval());
            } else {
                return;
            }
        }

            bool telemetrySent = false;

#ifdef ENABLE_TPMS
            if (sendTPMSData()) {
                telemetrySent = true;
            } else {
                return;
            }
#endif

            if (obdConnected) {
                if (millis() > lastMQTTStaticDiagnosticOutput) {
                    if (sendStaticDiagnosticData()) {
                        lastMQTTStaticDiagnosticOutput = calcTimestamp(Settings.MQTT.getDiagnosticInterval() * 2);
                } else {
                    return;
                }
            }

            if (millis() > lastMQTTDTCDiagnosticOutput) {
                if (sendDTCDiagnosticData()) {
                    lastMQTTDTCDiagnosticOutput = calcTimestamp(Settings.MQTT.getDiagnosticInterval());
                } else {
                    return;
                }
                }

                if (sendOBDData()) {
                    telemetrySent = true;
                } else {
                    return;
                }
            }

            if (telemetrySent) {
                lastMQTTOutput = calcTimestamp(Settings.MQTT.getDataInterval());
            } else if (mqtt.sendTopicUpdate(LWT_TOPIC, LWT_CONNECTED)) {
                const uint iv = Settings.MQTT.getDataInterval() * 5;
                lastMQTTOutput = calcTimestamp(iv < MQTT_KEEPALIVE ? iv : MQTT_KEEPALIVE - 1);
            }
    } else {
        delay(500);
    }
}

[[noreturn]] void readStatesTask(void *parameters) {
    esp_task_wdt_add(nullptr);
    for (;;) {
        if (!wifiAPInUse) {
            if (clearDTC) {
                DEBUG_PORT.print("DTC reset ");
                if (OBD.resetDTCs()) {
                    DEBUG_PORT.println("done.");
                } else {
                    DEBUG_PORT.println("failed.");
                }
                clearDTC = false;
            }

            if (OBD.connected()) {
                OBD.loop();
            } else if (!OBD.isPaused() && millis() >= nextOBDReconnect) {
                // Never tear the link down while someone else holds the
                // transport: ELMduino clears elm327.connected transiently
                // while a blocking command is in flight, and reconnecting
                // on that would kill the very command that is running.
                DEBUG_PORT.println("OBD BLE disconnected; trying reconnect.");
                if (!OBD.connect()) {
                    scheduleNextOBDReconnect();
                }
            }
        }
        esp_task_wdt_reset();
        delay(10);
    }
}

[[noreturn]] void outputTask(void *parameters) {
    esp_task_wdt_add(nullptr);
    unsigned long checkInterval = 0;
    bool networkWasConnected = gsm.isNetworkConnected();
    for (;;) {
        if (!wifiAPInUse) {
#ifdef ENABLE_TPMS
#ifdef TPMS_SIMULATION
            TPMS.loop();
#else
            if (obdConnected) {
                TPMS.loop();
            }
#endif
#endif
#if DEVICE_CAN_DEEP_SLEEP && DEVICE_HAS_BATTERY
            if (GSM::isBatteryUsed()) {
                const unsigned int batVoltage = GSM::getBatteryVoltage();
                if (batVoltage > MIN_VOLTAGE_LEVEL) {
                    const int sum = std::accumulate(batteryVoltages.begin(), batteryVoltages.end(), 0);
                    const double avgBat = static_cast<double>(sum) / batteryVoltages.size();
                    if (millis() > batteryTime + 5000) {
                        if (batteryVoltages.size() > 10) {
                            batteryVoltages.erase(batteryVoltages.begin());
                        }
                        batteryVoltages.push_back(batVoltage);
                        batteryTime = millis();
                    }
                    const bool drain = batteryVoltages.size() > 10 && batVoltage < (avgBat - 10);

                    const double avgLU = OBD.avgLastUpdate([](const OBDState *state) {
                        return state->isEnabled() &&
                               state->getType() == obd::READ && state->getUpdateInterval() > 0 &&
                               state->getUpdateInterval() <= 5 * 60 * 1000;
                    });

                    if (batVoltage < LOW_VOLTAGE_LEVEL || (
                            drain && avgLU > Settings.General.getSleepTimeout() * 1000)) {
                        if (batVoltage < LOW_VOLTAGE_LEVEL) {
                            log_d("Battery has low voltage.");
                        }
                        deepSleep(Settings.General.getSleepDuration());
                    }
                }
            }
#endif

            if (!gsm.checkNetwork()) {
                networkWasConnected = false;
                esp_task_wdt_reset();
                delay(1000);
                continue;
            }
            if (!networkWasConnected) {
                DEBUG_PORT.println("WiFi recovered; MQTT reconnect will run now.");
                nextMQTTReconnect = 0;
                networkWasConnected = true;
            }

            mqtt.loop();

            #ifndef NO_MODEM
            if ((GSM::hasGSMLocation() || GSM::hasGPSLocation()) && millis() > checkInterval) {
                unsigned long start = millis();
                bool allReadSuccessed = false;

                DEBUG_PORT.print("Read location...");

                if (gsm.isNetworkConnected()) {
                    float gsm_latitude = 0;
                    float gsm_longitude = 0;
                    float gsm_accuracy = 0;

                    if ((allReadSuccessed |= gsm.readGSMLocation(gsm_latitude, gsm_longitude, gsm_accuracy))) {
                        gsmLatitude = gsm_latitude;
                        gsmLongitude = gsm_longitude;
                        gsmAccuracy = gsm_accuracy;
                    }
                }

                if (GSM::hasGPSLocation()) {
                    float gps_latitude = 0;
                    float gps_longitude = 0;
                    float gps_accuracy = 0;

                    if (!(allReadSuccessed |= gsm.readGPSLocation(gps_latitude, gps_longitude, gps_accuracy))) {
                        gsm.checkGPS();
                    } else {
                        gpsLatitude = gps_latitude;
                        gpsLongitude = gps_longitude;
                        gpsAccuracy = gps_accuracy;
                    }
                }

                checkInterval = millis() + Settings.MQTT.getLocationInterval() * 1000L;
                consoleSendFooter(allReadSuccessed, millis() - start);
            }

            #endif

            #ifndef NO_MODEM
            if (GSM::isUseGPRS()) {
                signalQuality = gsm.getSignalQuality();
            }
            #endif

            if (!mqtt.connected()) {
                if (millis() >= nextMQTTReconnect) {
                    auto client_id = String(MQTT_CLIENT_ID) + "-" + stripChars(mqtt.getIdentifier()).c_str();
                    if (mqtt.connect(
                        client_id.c_str(),
                        Settings.MQTT.getHostname().c_str(),
                        Settings.MQTT.getPort(),
                        Settings.MQTT.getUsername().c_str(),
                        Settings.MQTT.getPassword().c_str(),
                        static_cast<mqttProtocol>(Settings.MQTT.getProtocol()),
                        MQTT_CONNECT_TIMEOUT_MS
                    )) {
                        mqttReconnectDelay = MQTT_RECONNECT_INITIAL_MS;
                        nextMQTTReconnect = 0;
                        mqttHealthFailures = 0;
                    } else {
                        scheduleNextMQTTReconnect();
#ifdef NO_MODEM
                        if (!gsm.isNetworkConnected()) {
                            // WiFi itself dropped - the ordered walk handles it.
                            gsm.checkNetwork(true);
                            mqttHealthFailures = 0;
                        } else if (++mqttHealthFailures >= MQTT_HEALTH_MAX_FAILURES) {
                            // Associated but the broker stays unreachable: this
                            // network is the problem, so move on to the next.
                            DEBUG_PORT.printf("MQTT unreachable %u times on the current WiFi\n",
                                              static_cast<unsigned>(mqttHealthFailures.load()));
                            mqttHealthFailures = 0;
                            gsm.switchToNextNetwork();
                        }
#else
                        gsm.checkNetwork(true);
#endif
                    }
                }
            } else {
                mqttSendData();
            }
        }
        esp_task_wdt_reset();
        delay(50);
    }
}

String buildIdentifier(const char *devMac) {
    String mID = "";
    if (static_cast<MQTTSettings::MQTTIdentifierType>(Settings.MQTT.getIdType()) ==
        MQTTSettings::MQTTIdentifierType::CUSTOM && Settings.MQTT.getIdSuffix().length() > 0) {
        mID = Settings.MQTT.getIdSuffix().c_str();
    } else if (devMac != nullptr && strlen(devMac) > 0) {
        mID = devMac;
        if (static_cast<MQTTSettings::MQTTIdentifierType>(Settings.MQTT.getIdType()) ==
            MQTTSettings::MQTTIdentifierType::MAC_IMEI) {
#ifdef NO_MODEM
            mID += "-S3";
#else
            mID += "-";
            mID += gsm.modem.getIMEI().substring(gsm.modem.getIMEI().length() - 4).c_str();
#endif
        }
    }
    return mID;
}

void startOutputTask(const char *id) {
    if (!Settings.MQTT.getHostname().isEmpty()) {
        mqtt.setClient(gsm.getClient(Settings.MQTT.getSecure()));
        mqtt.setIdentifier(id);
        mqtt.setIdentifierName(DEVICE_DISPLAY_NAME);

        xTaskCreatePinnedToCore(outputTask, "OutputTask", 9216, nullptr, 10, &outputTaskHdl, 0);
    }
}

void startReadTask() {
    if (!Settings.OBD2.getDisable()) {
#ifdef USE_BLE
        OBD.onDevicesDiscovered(onBLEDevicesDiscovered);
#else
        OBD.onDevicesDiscovered(onBTDevicesDiscovered);
#endif
        nextOBDReconnect = 0;
        obdReconnectDelay = OBD_RECONNECT_INITIAL_MS;

        xTaskCreatePinnedToCore(readStatesTask, "ReadStatesTask", 9216, nullptr, 1, &stateTaskHdl, 1);
    }
}

void setup() {
    startTime = millis();

    // The default 256 byte RX buffer overflows while loop() is starved by the
    // OBD task, and a states line for the serial commands is ~18KB.
    DEBUG_PORT.setRxBufferSize(SERIAL_RX_BUFFER_SIZE);
    DEBUG_PORT.begin(115200);

    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
        log_d("LittleFS Mount Failed");
        return;
    }

    // init the coprozessor
    GSM::ulpInit();

    // init battery if needed
    GSM::initBattery();

    Settings.readSettings(LittleFS);
    OBD.readStates(LittleFS);

    esp_task_wdt_init(APP_WATCHDOG_TIMEOUT_SEC, true);

    OBD.onConnected(onOBDConnected);
    OBD.onConnectError(onOBDConnectError);
    OBD.begin(Settings.OBD2.getName(OBD_ADP_NAME), Settings.OBD2.getMAC(), Settings.OBD2.getProtocol(),
              Settings.OBD2.getCheckPIDSupport(), Settings.OBD2.getDebug(), Settings.OBD2.getSpecifyNumResponses());

#ifdef ENABLE_TPMS
    TPMS.begin();
#endif

    String mID = buildIdentifier(Settings.OBD2.getMAC().c_str());
    if (!mID.isEmpty()) {
        startReadTask();
    } else {
        startReadTask();
        mID = buildIdentifier(OBD.getConnectedBTAddress().c_str());
    }

    startWiFiAP();
    startHttpServer();

    // will be ignored if the device does not support
    gsm.setNetworkMode(Settings.Mobile.getNetworkMode());
    gsm.connectToNetwork();
    gsm.enableGPS();

    if (!mID.isEmpty()) {
        startOutputTask(mID.c_str());
    }
}

void loop() {
#ifdef TOYOTA_EXPLORER
    // The Arduino loop task stays alive and serves the diagnostic console.
    static bool consoleStarted = false;
    if (!consoleStarted) {
        ElmConsole.begin();
        consoleStarted = true;
    }
    ElmConsole.loop();
    delay(20);
#else
    // Settings over USB serial - the way back in when no configured network
    // is reachable, so neither the STA address nor the API can be used.
    //   settings         prints the current settings as one JSON line
    //   settings {json}  replaces all settings, like PUT /api/settings
    //   states           prints the current states as one JSON line
    //   states {json}    replaces all states, like PUT /api/states
    //   reboot
    // A full states profile is ~18KB, so the line buffer lives in PSRAM.
    static char *line = static_cast<char *>(heap_caps_malloc(SERIAL_CMD_MAX_LEN, MALLOC_CAP_SPIRAM));
    static size_t lineLen = 0;
    static bool overflow = false;
    if (line == nullptr) {
        delay(1000);
        return;
    }

    while (DEBUG_PORT.available()) {
        const int c = DEBUG_PORT.read();
        if (c != '\n' && c != '\r') {
            if (lineLen < SERIAL_CMD_MAX_LEN) {
                line[lineLen++] = static_cast<char>(c);
            } else {
                overflow = true;
            }
            continue;
        }

        const std::string cmd(line, lineLen);
        if (overflow) {
            DEBUG_PORT.println("CMD ERR too long");
        } else if (cmd == "settings") {
            DEBUG_PORT.printf("SETTINGS %s\n", Settings.buildJson().c_str());
        } else if (cmd.rfind("settings ", 0) == 0) {
            if (!Settings.parseJson(cmd.substr(9))) {
                DEBUG_PORT.println("SETTINGS ERR parse");
            } else if (!Settings.writeSettings(LittleFS)) {
                DEBUG_PORT.println("SETTINGS ERR write");
            } else {
                DEBUG_PORT.println("SETTINGS OK");
            }
        } else if (cmd == "states") {
            DEBUG_PORT.print("STATES ");
            DEBUG_PORT.println(OBD.buildJSON().c_str());
        } else if (cmd.rfind("states ", 0) == 0) {
            std::string json = cmd.substr(7);
            if (!OBD.parseJSON(json)) {
                DEBUG_PORT.printf("STATES ERR parse (%u bytes)\n", static_cast<unsigned>(json.size()));
            } else if (!OBD.writeStates(LittleFS)) {
                DEBUG_PORT.println("STATES ERR write");
            } else {
                DEBUG_PORT.println("STATES OK");
            }
        } else if (cmd == "reboot") {
            DEBUG_PORT.println("REBOOT");
            DEBUG_PORT.flush();
            ESP.restart();
        }
        lineLen = 0;
        overflow = false;
    }
    delay(20);
#endif
}
