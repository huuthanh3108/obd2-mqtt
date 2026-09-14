/*
 * This program is free software; you can use it, redistribute it
 * and / or modify it under the terms of the GNU General Public License
 * (GPL) as published by the Free Software Foundation; either version 3
 * of the License or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program, in a file called gpl.txt or license.txt.
 *  If not, write to the Free Software Foundation Inc.,
 *  59 Temple Place - Suite 330, Boston, MA  02111-1307 USA
 */

#include "obd.h"
#include "debug_log.h"

#include <OBDStates.h>
#include <ExprParser.h>
#include <cstdlib>
#include <cctype>
#include "helper.h"

static constexpr uint8_t RPM_FAILURES_BEFORE_COOLDOWN = 3;
static constexpr unsigned long ENGINE_PID_COOLDOWN_MS = 60000UL;
static constexpr unsigned long COOLDOWN_VOLTAGE_INTERVAL_MS = 10000UL;
static constexpr unsigned long COOLDOWN_PID_PROBE_INTERVAL_MS = 15000UL;
static constexpr unsigned long NORMAL_PID_PACING_MS = 350UL;

OBDClass::OBDClass() : OBDStates(&elm327), elm327() {
    protocol = AUTOMATIC;

    addCustomFunction("afRatio", [](const double fuelType) {
        switch (static_cast<int>(fuelType)) {
            case FUEL_TYPE_METHANOL:
                return AF_RATIO_METHANOL;
            case FUEL_TYPE_ETHANOL:
                return AF_RATIO_ETHANOL;
            case FUEL_TYPE_DIESEL:
                return AF_RATIO_DIESEL;
            case FUEL_TYPE_LPG:
            case FUEL_TYPE_CNG:
                return AF_RATIO_GAS;
            case FUEL_TYPE_PROPANE:
                return AF_RATIO_PROPANE;
            case FUEL_TYPE_ELECTRIC:
                return 0.0;
            default:
                return AF_RATIO_GASOLINE;
        }
    });
    addCustomFunction("density", [](const double fuelType)-> double {
        switch (static_cast<int>(fuelType)) {
            case FUEL_TYPE_METHANOL:
                return DENSITY_METHANOL;
            case FUEL_TYPE_ETHANOL:
                return DENSITY_ETHANOL;
            case FUEL_TYPE_DIESEL:
                return DENSITY_DIESEL;
            case FUEL_TYPE_LPG:
            case FUEL_TYPE_CNG:
                return DENSITY_GAS;
            case FUEL_TYPE_PROPANE:
                return DENSITY_PROPANE;
            case FUEL_TYPE_ELECTRIC:
                return 0.0;
            default:
                return DENSITY_GASOLINE;
        }
    });
    addCustomFunction("numDTCs", [&](const double numCodes)-> double {
        int numDTCs = 0;
        if (static_cast<u_int8_t>(numCodes) > 0) {
            elm327.currentDTCCodes();
            if (elm327.nb_rx_state == ELM_SUCCESS) {
                dtcsRead = true;
                dtcs.clear();
                numDTCs = static_cast<int>(elm327.DTC_Response.codesFound);
                if (numDTCs > 0) {
                    for (int i = 0; i < numDTCs; i++) {
                        dtcs.add(elm327.DTC_Response.codes[i]);
                    }
                }
            }
        }
        return numDTCs;
    });

    setVariableResolveFunction([&](const char *varName)-> double {
        if (varName != nullptr) {
            if (varName[0] == '$') {
                varName++;
            }

            if (std::strcmp(varName, "millis") == 0) {
                return millis();
            }
            size_t pos = 0;
            string vstr = varName;
            if ((pos = vstr.find('.')) != string::npos) {
                string vn = vstr.substr(0, pos);
                string op = vstr.substr(pos + 1);

                auto *state = getStateByName(vn.c_str());
                if (state != nullptr) {
                    if (op == "pu") {
                        return state->getPreviousUpdate();
                    }
                    if (op == "lu") {
                        return state->getLastUpdate();
                    }

                    if (op.substr(0, 1) == "b" && op.length() > 1 && (
                            state->valueType() == OBD_STATE_TYPE_INT || state->valueType() == OBD_STATE_TYPE_FLOAT)) {
                        if (state->getPayload() != nullptr) {
                            size_t si = op.find(':');
                            int i = strtol(
                                (si != string::npos ? op.substr(1, si - 1) : op.substr(1)).c_str(),
                                nullptr,
                                10);
                            int j = si != string::npos ? strtol(op.substr(si + 1).c_str(), nullptr, 10) : i + 1;
                            if (j - i <= 8) {
                                int sidx = (i - 1) * 2;
                                int eidx = (j - 1) * 2;
                                if (sidx > 0 && eidx > sidx && sidx <= strlen(state->getPayload()) && eidx <= strlen(
                                        state->getPayload())) {
                                    string bstr = string(state->getPayload()).substr(sidx, eidx - sidx);
                                    return strtol(bstr.c_str(), nullptr, 16);
                                }
                                Serial.println("Index out of bound");
                            } else {
                                Serial.println("Range overflows double");
                            }
                        } else {
                            log_v("Payload was empty");
                        }
                    }

                    if (op == "ov" && state->valueType() == OBD_STATE_TYPE_INT) {
                        auto *is = reinterpret_cast<OBDStateInt *>(state);
                        return is->getOldValue();
                    }
                    if (op == "ov" && state->valueType() == OBD_STATE_TYPE_FLOAT) {
                        auto *is = reinterpret_cast<OBDStateFloat *>(state);
                        return is->getOldValue();
                    }
                    if (op == "ov" && state->valueType() == OBD_STATE_TYPE_BOOL) {
                        auto *is = reinterpret_cast<OBDStateBool *>(state);
                        return is->getOldValue();
                    }
                }
            } else {
                return getStateValue(varName);
            }
        }

        return 0.0;
    });
}

bool OBDClass::parseJSON(std::string &json) {
    bool success = false;
    JsonDocument doc;
    if (!deserializeJson(doc, json)) {
        readJSON(doc);
        success = true;
    }

    return success;
}

template<typename T>
void OBDClass::fromJSON(T *state, JsonDocument &doc) {
    state->setEnabled(doc["enabled"].as<bool>());
    state->setVisible(doc["visible"].as<bool>());

    if (state->getType() == obd::READ) {
        if (!doc["readFunc"].isNull()) {
            setReadFuncByName<T>(doc["readFunc"].as<std::string>().c_str(), state);
        } else if (!doc["pid"].isNull()) {
            state->setPIDSettings(
                doc["pid"]["service"].as<uint8_t>(),
                doc["pid"]["pid"].as<uint16_t>(),
                doc["pid"]["header"].as<uint32_t>(),
                doc["pid"]["numResponses"].as<uint8_t>(),
                doc["pid"]["numExpectedBytes"].as<uint8_t>(),
                doc["pid"]["responseFormat"].as<obd::OBDResponseFormat>(),
                !doc["pid"]["scaleFactor"].isNull() ? doc["pid"]["scaleFactor"].as<std::string>().c_str() : "1",
                doc["pid"]["bias"].as<float>()
            );
        }
    } else if (state->getType() == obd::CALC) {
        if (!doc["expr"].isNull()) {
            state->setCalcExpression(doc["expr"].as<std::string>().c_str());
        }
    }

    if (!doc["value"]["format"].isNull()) {
        state->setValueFormat(doc["value"]["format"].as<std::string>().c_str());
    }
    if (!doc["value"]["func"].isNull()) {
        setFormatFuncByName<T>(doc["value"]["func"].as<std::string>().c_str(), state);
    } else if (!doc["value"]["expr"].isNull()) {
        state->setValueFormatExpression(doc["value"]["expr"].as<std::string>().c_str());
    }

    // is reset by setPIDSettings
    state->setUpdateInterval(doc["interval"].as<long>());
}

bool OBDClass::readStates(FS &fs) {
    bool success = false;

    File file = fs.open(STATES_FILE, FILE_READ);
    if (file && !file.isDirectory()) {
        JsonDocument doc;
        if (!deserializeJson(doc, file)) {
            readJSON(doc);
            success = true;
        }
        file.close();
    }

    return success;
}

std::string OBDClass::buildJSON() {
    std::string payload;

    JsonDocument doc;
    writeJSON(doc);
    serializeJson(doc, payload);

    return payload;
}

void OBDClass::readJSON(JsonDocument &doc) {
    clearStates();
    const JsonArray array = doc.as<JsonArray>();
    for (JsonDocument stateObj: array) {
        if (stateObj["valueType"] == OBD_STATE_TYPE_BOOL) {
            auto *state = new OBDStateBool(
                stateObj["type"].as<obd::OBDStateType>(),
                stateObj["name"].as<std::string>().c_str(),
                stateObj["description"].as<std::string>().c_str(),
                !stateObj["icon"].isNull() ? stateObj["icon"].as<std::string>().c_str() : "",
                !stateObj["unit"].isNull() ? stateObj["unit"].as<std::string>().c_str() : "",
                !stateObj["deviceClass"].isNull() ? stateObj["deviceClass"].as<std::string>().c_str() : "",
                stateObj["measurement"].as<bool>(),
                stateObj["diagnostic"].as<bool>()
            );
            fromJSON(state, stateObj);
            addState(state);
        } else if (stateObj["valueType"] == OBD_STATE_TYPE_FLOAT) {
            auto *state = new OBDStateFloat(
                stateObj["type"].as<obd::OBDStateType>(),
                stateObj["name"].as<std::string>().c_str(),
                stateObj["description"].as<std::string>().c_str(),
                !stateObj["icon"].isNull() ? stateObj["icon"].as<std::string>().c_str() : "",
                !stateObj["unit"].isNull() ? stateObj["unit"].as<std::string>().c_str() : "",
                !stateObj["deviceClass"].isNull() ? stateObj["deviceClass"].as<std::string>().c_str() : "",
                stateObj["measurement"].as<bool>(),
                stateObj["diagnostic"].as<bool>()
            );
            fromJSON(state, stateObj);
            addState(state);
        } else if (stateObj["valueType"] == OBD_STATE_TYPE_INT) {
            auto *state = new OBDStateInt(
                stateObj["type"].as<obd::OBDStateType>(),
                stateObj["name"].as<std::string>().c_str(),
                stateObj["description"].as<std::string>().c_str(),
                !stateObj["icon"].isNull() ? stateObj["icon"].as<std::string>().c_str() : "",
                !stateObj["unit"].isNull() ? stateObj["unit"].as<std::string>().c_str() : "",
                !stateObj["deviceClass"].isNull() ? stateObj["deviceClass"].as<std::string>().c_str() : "",
                stateObj["measurement"].as<bool>(),
                stateObj["diagnostic"].as<bool>()
            );
            fromJSON(state, stateObj);
            addState(state);
        }
    }
}

void OBDClass::writeJSON(JsonDocument &doc) {
    std::vector<OBDState *> states{};
    getStates([](const OBDState *) {
        return true;
    }, states);
    for (OBDState *state: states) {
        JsonDocument stateObj;
        state->toJSON(stateObj);
        doc.add(stateObj);
    }
}

bool OBDClass::writeStates(FS &fs) {
    bool success = false;

    File file = fs.open(STATES_FILE, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file settings.json for writing.");
        return false;
    }

    JsonDocument doc;
    writeJSON(doc);
    success = serializeJson(doc, file);

    file.close();

    return success;
}

template<typename T>
T *OBDClass::setReadFuncByName(const char *funcName, T *state) {
    // Each door is its own READ state rather than a CALC over one shared
    // byte: a CALC cannot be marked stale, so if the frame is missing it
    // would quietly evaluate to 0 and report every door as closed.
    static const struct { const char *name; uint8_t mask; } DOOR_BITS[] = {
        {"doorDriver", 0x20}, {"doorPassenger", 0x10},
        {"doorRearRight", 0x08}, {"doorRearLeft", 0x04},
    };
    if (strcmp(state->valueType(), OBD_STATE_TYPE_BOOL) == 0) {
        for (const auto &d: DOOR_BITS) {
            if (strcmp(funcName, d.name) != 0) {
                continue;
            }
            const uint8_t mask = d.mask;
            state
                    ->withReadFuncName(d.name)
                    ->withReadFunc([this, mask]() {
                        if (refreshBodyFrame()) {
                            elm327.nb_rx_state = ELM_SUCCESS;
                            return (getBodyByte(5) & mask) != 0;
                        }
                        elm327.nb_rx_state = ELM_NO_DATA;
                        return false;
                    });
            return state;
        }
    }

    if (strcmp(funcName, "bodyDoorByte") == 0 && strcmp(state->valueType(), OBD_STATE_TYPE_INT) == 0) {
        state
                ->withReadFuncName("bodyDoorByte")
                ->withReadFunc([&]() {
                    if (refreshBodyFrame()) {
                        elm327.nb_rx_state = ELM_SUCCESS;
                        return static_cast<int>(getBodyByte(5));
                    }
                    // No frame -> report NO_DATA so the state is not published
                    // as a confident "all doors closed".
                    elm327.nb_rx_state = ELM_NO_DATA;
                    return 0;
                });
    }

    if (strcmp(funcName, "batteryVoltage") == 0 && strcmp(state->valueType(), OBD_STATE_TYPE_FLOAT) == 0) {
        state
                ->withReadFuncName("batteryVoltage")
                ->withReadFunc([&]() {
                    if (elm327.sendCommand_Blocking(READ_VOLTAGE) == ELM_SUCCESS) {
                        return static_cast<float>(strtof(elm327.payload, nullptr));
                    }
                    return 0.0F;
                });
    }

    return state;
}

template<typename T>
T *OBDClass::setFormatFuncByName(const char *funcName, T *state) {
    if (strcmp(state->valueType(), OBD_STATE_TYPE_INT) == 0) {
        auto *is = reinterpret_cast<OBDStateInt *>(state);
        if (strcmp(funcName, "toBitStr") == 0) {
            is
                    ->withValueFormatFuncName("toBitStr")
                    ->withValueFormatFunc([](const int value) {
                        char str[33];
                        snprintf(str, sizeof(str), "%s", std::bitset<32>(value).to_string().c_str());
                        return strdup(str);
                    });
        } else if (strcmp(funcName, "toMiles") == 0) {
            is
                    ->withValueFormatFuncName("toMiles")
                    ->withValueFormatFunc([](const int value) {
                        char str[16];
                        snprintf(str, sizeof(str), "%d", static_cast<int>(static_cast<float>(value) / KPH_TO_MPH));
                        return strdup(str);
                    });
        } else if (strcmp(funcName, "payload") == 0) {
            is
                    ->withValueFormatFuncName("payload")
                    ->withValueFormatFunc([is](const int value) {
                        return strdup(is->getPayload());
                    });
        }
    } else if (strcmp(state->valueType(), OBD_STATE_TYPE_FLOAT) == 0) {
        auto *is = reinterpret_cast<OBDStateFloat *>(state);
        if (strcmp(funcName, "toMiles") == 0) {
            is
                    ->withValueFormatFuncName("toMiles")
                    ->withValueFormatFunc([](const float value) {
                        char str[16];
                        snprintf(str, sizeof(str), "%4.2f", value / KPH_TO_MPH);
                        return strdup(str);
                    });
        } else if (strcmp(funcName, "toGallons") == 0) {
            is
                    ->withValueFormatFuncName("toGallons")
                    ->withValueFormatFunc([](const float value) {
                        char str[16];
                        snprintf(str, sizeof(str), "%4.2f", value / LITER_TO_GALLON);
                        return strdup(str);
                    });
        } else if (strcmp(funcName, "toMPG") == 0) {
            is
                    ->withValueFormatFuncName("toMPG")
                    ->withValueFormatFunc([&](const float value) {
                        char str[16];
                        snprintf(str, sizeof(str), "%4.2f", value == 0.0f ? 0.0f : 235.214583333333f / value);
                        return strdup(str);
                    });
        } else if (strcmp(funcName, "payload") == 0) {
            is
                    ->withValueFormatFuncName("payload")
                    ->withValueFormatFunc([is](const float value) {
                        return strdup(is->getPayload());
                    });
        }
    }
    return state;
}

int DTCs::getCount() const {
    return static_cast<int>(v_codes.size());
}

std::string *DTCs::getCode(int i) {
    if (i < 0) {
        return nullptr;
    }
    return &v_codes[i];
}

bool DTCs::add(const std::string &code) {
    bool found = false;
    for (const auto &c: v_codes) {
        if (c == code) return false;
    }

    v_codes.push_back(code);
    return true;
}

void DTCs::clear() {
    v_codes.clear();
}

#ifndef USE_BLE
void OBDClass::BTEvent(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
    if (event == ESP_SPP_CLOSE_EVT) {
        Serial.println("Bluetooth disconnected.");

        if (OBD.initDone && !OBD.stopConnect) {
            // FIXME get reconnect working - failed with "getChannels() failed timeout"
            // OBD.connect(true);
            ESP.restart();
        }
    }
}

BTScanResults *OBDClass::discoverBtDevices() {
    serialBt.discoverClear();

    Serial.println("Discover Bluetooth devices...");

    BTScanResults *btDeviceList = serialBt.getScanResults(); // maybe accessing from different threads!
    if (serialBt.discoverAsync([](BTAdvertisedDevice *pDevice) {
        Serial.printf(">>>>>>>>>>>Found a new device: %s\n", pDevice->toString().c_str());
    })) {
        delay(BT_DISCOVER_TIME);
        Serial.print("Stopping discover...");
        serialBt.discoverAsyncStop();
        Serial.println("stopped");
        delay(5000);

        if (btDeviceList->getCount() > 0) {
            return btDeviceList;
        }
    }

    return nullptr;
}
#endif

#ifdef USE_BLE
static BLEScanResultsSet rawBleDeviceList;

class RawBLEAdvertisedDeviceCallbacks final : public NimBLEScanCallbacks {
public:
    void onResult(const NimBLEAdvertisedDevice *pDevice) override {
        if (pDevice != nullptr && rawBleDeviceList.add(*pDevice)) {
            Serial.printf(">>>>>>>>>>>Found BLE device: %s\n", pDevice->toString().c_str());
        }
    }
};

static void dumpRemoteBLEGatt(const NimBLEAddress &addr) {
    Serial.printf("========== GATT explorer: %s ==========\n", addr.toString().c_str());

    NimBLEClient *client = NimBLEDevice::createClient();
    if (client == nullptr) {
        Serial.println("GATT explorer: failed to create BLE client");
        return;
    }

    client->setConnectionParams(12, 12, 0, 150);
    client->setConnectTimeout(5 * 1000);

    if (!client->connect(addr)) {
        Serial.println("GATT explorer: connect failed");
        NimBLEDevice::deleteClient(client);
        return;
    }

    Serial.printf("GATT explorer: connected, RSSI %d\n", client->getRssi());
    const std::vector<NimBLERemoteService *> &services = client->getServices(true);
    Serial.printf("GATT explorer: services found %u\n", static_cast<unsigned int>(services.size()));

    for (const auto *service: services) {
        if (service == nullptr) {
            continue;
        }

        Serial.printf("Service %s handle %u-%u\n",
                      service->getUUID().toString().c_str(),
                      service->getStartHandle(),
                      service->getEndHandle());

        const std::vector<NimBLERemoteCharacteristic *> &characteristics = service->getCharacteristics(true);
        for (const auto *characteristic: characteristics) {
            if (characteristic == nullptr) {
                continue;
            }

            Serial.printf("  Char %s handle %u props:%s%s%s%s%s\n",
                          characteristic->getUUID().toString().c_str(),
                          characteristic->getHandle(),
                          characteristic->canRead() ? " read" : "",
                          characteristic->canWrite() ? " write" : "",
                          characteristic->canWriteNoResponse() ? " write_nr" : "",
                          characteristic->canNotify() ? " notify" : "",
                          characteristic->canIndicate() ? " indicate" : "");
        }
    }

    client->disconnect();
    NimBLEDevice::deleteClient(client);
    Serial.println("=======================================");
}

void OBDClass::onBLEDisconnect() {
    Serial.println("Bluetooth LE disconnected.");

    if (OBD.initDone && !OBD.stopConnect) {
        OBD.elm327.connected = false;
        OBD.connectedBTAddress.clear();
        if (OBD.connectErrorCallback) {
            OBD.connectErrorCallback();
        }
    }
}

BLEScanResultsSet *OBDClass::discoverBLEDevices() {
    rawBleDeviceList.clear();
    Serial.println("Discover Bluetooth LE devices...");
    NimBLEScan *pBLEScan = NimBLEDevice::getScan();
    if (pBLEScan != nullptr) {
        pBLEScan->stop();
        pBLEScan->clearResults();
        pBLEScan->setScanCallbacks(new RawBLEAdvertisedDeviceCallbacks(), false);
        pBLEScan->setInterval(100);
        pBLEScan->setWindow(99);
        pBLEScan->setMaxResults(0);
        pBLEScan->setActiveScan(true);
        pBLEScan->start(BT_DISCOVER_TIME, false);
        delay(BT_DISCOVER_TIME);
        Serial.print("Stopping discover...");
        pBLEScan->stop();
        Serial.println("stopped");
        delay(1000);
        Serial.printf("BLE devices found: %d\n", rawBleDeviceList.getCount());

        if (rawBleDeviceList.getCount() > 0) {
            return &rawBleDeviceList;
        }
    }

    return nullptr;
}
#endif

void OBDClass::begin(const String &devName, const String &devMac, const char protocol,
                     const bool checkPidSupport, const bool debug, const bool specifyNumResponses) {
    this->devName = devName;
    this->devMac = devMac;
    this->protocol = protocol;
    this->checkPidSupport = checkPidSupport;
    this->debug = debug;
    this->specifyNumResponses = specifyNumResponses;
    stopConnect = false;
#ifdef USE_BLE
    serialBLE.onDisconnect(onBLEDisconnect);
#else
    serialBt.register_callback(BTEvent);
#endif
}

void OBDClass::end() {
    stopConnect = true;
#ifdef USE_BLE
    serialBLE.disconnect();
    serialBLE.end();
#else
    serialBt.disconnect();
    serialBt.end();
#endif
}

bool OBDClass::connect(bool reconnect) {
    stopConnect = false;
    connectedBTAddress.clear();
    elm327.connected = false;

connect:
    if (stopConnect || reconnect && !initDone) {
        return false;
    }

#ifdef USE_BLE
    const bool useIosVlinkProfile = devName.equalsIgnoreCase("IOS-Vlink") ||
                                    devName.equalsIgnoreCase("iOS-Vlink");
    const bool bleStarted = useIosVlinkProfile
                            ? serialBLE.begin("OBD2MQTT",
                                              "E7810A71-73AE-499D-8C15-FAA9AEF0C3F2",
                                              "BEF8D6C9-9C21-4C9E-B632-BD58C1009F9F",
                                              "BEF8D6C9-9C21-4C9E-B632-BD58C1009F9F")
                            : serialBLE.begin("OBD2MQTT");
    if (!bleStarted) {
        Serial.println("========== serialBLE failed!");
        ESP.restart();
    }
    if (useIosVlinkProfile) {
        Serial.println("Using IOS-Vlink BLE ELM327 profile");
    }
#else
    if (!serialBt.begin("OBD2MQTT", true)) {
        Serial.println("========== serialBT failed!");
        ESP.restart();
    }
#endif

    if (devMac.isEmpty()) {
#ifdef USE_BLE
        BLEScanResultsSet *bleDeviceList = discoverBLEDevices();

        if (bleDeviceList == nullptr) {
            Serial.println("Didn't find any devices");
            if (connectErrorCallback) {
                connectErrorCallback();
            }
        } else {
            NimBLEAddress addr = NimBLEAddress();

            if (devDiscoveredCallback != nullptr && bleDeviceList->getCount() != 0) {
                devDiscoveredCallback(bleDeviceList);
            }

            Serial.printf("Search device: %s\n", devName.c_str());
            for (int i = 0; i < bleDeviceList->getCount(); i++) {
                NimBLEAdvertisedDevice *device = bleDeviceList->getDevice(i);
                if (strcmp(device->getName().c_str(), devName.c_str()) == 0) {
                    Serial.printf(" ----- %s  %s %d\n", device->getAddress().toString().c_str(),
                                  device->getName().c_str(), device->getRSSI());
                    addr = NimBLEAddress(device->getAddress());
                }
            }

            if (!stopConnect && addr) {
                Serial.printf("connecting to %s\n", addr.toString().c_str());
                if (serialBLE.connect(addr)) {
                    connectedBTAddress = addr.toString();
                }
            }

            if (!stopConnect && connectedBTAddress.empty()) {
                Serial.println("No exact BLE name match; trying discovered BLE devices for OBD UART service...");
                for (int i = 0; !stopConnect && connectedBTAddress.empty() && i < bleDeviceList->getCount(); i++) {
                    NimBLEAdvertisedDevice *device = bleDeviceList->getDevice(i);
                    if (device == nullptr) {
                        continue;
                    }
                    addr = NimBLEAddress(device->getAddress());
                    Serial.printf("trying %s (%s, RSSI %d)\n",
                                  addr.toString().c_str(),
                                  device->getName().empty() ? "<no name>" : device->getName().c_str(),
                                  device->getRSSI());
                    if (serialBLE.connect(addr)) {
                        connectedBTAddress = addr.toString();
                    }
                    if (connectedBTAddress.empty()) {
                        serialBLE.disconnect();
                    }
                }
            }
        }
#else
        BTScanResults *btDeviceList = discoverBtDevices();

        if (btDeviceList == nullptr) {
            Serial.println("Didn't find any devices");
            if (connectErrorCallback) {
                connectErrorCallback();
            }
        } else {
            BTAddress addr;
            int channel = 0;

            if (devDiscoveredCallback != nullptr && btDeviceList->getCount() != 0) {
                devDiscoveredCallback(btDeviceList);
            }

            Serial.printf("Search device: %s\n", devName.c_str());
            for (int i = 0; i < btDeviceList->getCount(); i++) {
                BTAdvertisedDevice *device = btDeviceList->getDevice(i);
                if (strcmp(device->getName().c_str(), devName.c_str()) == 0) {
                    Serial.printf(" ----- %s  %s %d\n", device->getAddress().toString().c_str(),
                                  device->getName().c_str(), device->getRSSI());
                    std::map<int, std::string> channels = serialBt.getChannels(device->getAddress());
                    Serial.printf("scanned for services, found %d\n", channels.size());
                    for (auto const &entry: channels) {
                        Serial.printf("     channel %d (%s)\n", entry.first, entry.second.c_str());
                    }
                    if (!channels.empty()) {
                        addr = device->getAddress();
                        channel = channels.begin()->first;
                    }
                }
            }

            if (!stopConnect && addr) {
                Serial.printf("connecting to %s - %d\n", addr.toString().c_str(), channel);
                if (serialBt.connect(addr, channel, ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE)) {
                    connectedBTAddress = addr.toString().c_str();
                }
            }
        }
#endif
    } else {
        byte mac[6];
        parseBytes(devMac.c_str(), ':', mac, 6, 16);
#ifdef USE_BLE
        NimBLEAddress addr = NimBLEAddress(mac, 0);

        if (!stopConnect && addr) {
            if (useIosVlinkProfile) {
                Serial.println("Trying NimBLE VLink stream: IOS-Vlink E781/BEF8");
                if (serialBLE.connect(addr)) {
                    connectedBTAddress = addr.toString();
                }
            } else {
                Serial.printf("connecting to %s\n", addr.toString().c_str());
                if (serialBLE.connect(addr)) {
                    connectedBTAddress = addr.toString();
                } else {
                    serialBLE.disconnect();
                }
            }
        }
#else
        BTAddress addr = mac;
        int channel = 0;

        std::map<int, std::string> channels = serialBt.getChannels(addr);
        Serial.printf("scanned for services, found %d\n", channels.size());
        for (auto const &entry: channels) {
            Serial.printf("     channel %d (%s)\n", entry.first, entry.second.c_str());
        }

        if (!channels.empty()) {
            channel = channels.begin()->first;
        }

        if (!stopConnect && addr) {
            Serial.printf("connecting to %s - %d\n", addr.toString().c_str(), channel);
            if (serialBt.connect(addr, channel, ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE)) {
                connectedBTAddress = addr.toString().c_str();
            }
        }
#endif
    }

#ifdef USE_BLE
    if (!stopConnect && !connectedBTAddress.empty() && !serialBLE.isClosed() && serialBLE.connected()) {
        int retryCount = 0;
        while (!elm327.begin(serialBLE, debug, 5000, protocol) && retryCount < 3) {
            Serial.println("Couldn't connect to OBD scanner - Phase 2");
            delay(BT_DISCOVER_TIME);
            retryCount++;
        }
#else
    if (!stopConnect && !serialBt.isClosed() && serialBt.connected()) {
        int retryCount = 0;
        while (!elm327.begin(serialBt, debug, 5000, protocol) && retryCount < 3) {
            Serial.println("Couldn't connect to OBD scanner - Phase 2");
            delay(BT_DISCOVER_TIME);
            retryCount++;
        }
#endif
    } else if (!stopConnect) {
        Serial.println("Couldn't connect to OBD scanner - Phase 1");
    }

    // if connection stopped (AP connected) wait before reconnect
    while (stopConnect) {
        delay(BT_DISCOVER_TIME);
    }

    if (!elm327.connected) {
        Serial.println("OBD connect attempt failed.");
#ifdef USE_BLE
        serialBLE.end();
#else
        serialBt.end();
#endif
        if (connectErrorCallback) {
            connectErrorCallback();
        }
        return false;
    }

    Serial.println("Connected to ELM327");
    elm327.specifyNumResponses = this->specifyNumResponses;

#ifdef USE_BLE
    if (useIosVlinkProfile && protocol != AUTOMATIC) {
        char command[8] = {'\0'};
        snprintf(command, sizeof(command), "AT SP %c", protocol);
        Serial.printf("Locking IOS-Vlink ELM protocol: %s\n", command);
        elm327.sendCommand_Blocking(command);
    }
#endif

    if (connectedCallback) {
        connectedCallback();
    }

    if (!reconnect) {
        if (protocol == AUTOMATIC &&
            elm327.sendCommand_Blocking("AT DP") == ELM_SUCCESS &&
            strlen(elm327.payload) > 0) {
            auto protocol = String(elm327.payload);
            protocol.replace("AUTO", "");
            Serial.printf("ELM327 protocol: %s\n", protocol.c_str());
        }
        setCheckPidSupport(this->checkPidSupport);
        initDone = true;
    }

    return true;
}

void OBDClass::loop() {
    // Claim the transport first, then re-check the pause flag. Checking
    // before claiming leaves a window in which pause() can observe
    // busy == false and hand the transport out while this task is already
    // on its way into loopInternal().
    busy.store(true);

    if (paused.load()) {
        busy.store(false);
        delay(10);
        return;
    }

    loopInternal();
    busy.store(false);
}

bool OBDClass::pause(const unsigned long timeoutMs) {
    paused.store(true);

    const unsigned long start = millis();
    while (busy.load()) {
        if (millis() - start > timeoutMs) {
            // Do not leave the polling task suspended for good just because
            // we gave up waiting for it.
            paused.store(false);
            return false;
        }
        delay(5);
    }

    // The read loop was cut off mid-command, so its unread answer is still
    // sitting in the stream. Whoever takes the pause next would consume that
    // stale data as if it were their own reply - which made the first command
    // after every pause fail. Start the new owner on a clean stream.
    if (elm327.elm_port != nullptr) {
        while (elm327.elm_port->available() > 0) {
            elm327.elm_port->read();
        }
    }

    return true;
}

void OBDClass::resume() {
    paused.store(false);
}

bool OBDClass::isPaused() const {
    return paused.load();
}

bool OBDClass::isLinkUp() const {
#ifdef USE_BLE
    const bool link = !serialBLE.isClosed() && serialBLE.connected();
#else
    const bool link = !serialBt.isClosed() && serialBt.connected();
#endif
    // The transport alone is not enough: elm_port is only bound inside
    // ELM327::begin(), so there is a window where BLE is up but ELMduino
    // still has no stream to talk through.
    return link && elm327.elm_port != nullptr;
}

ELM327 *OBDClass::getELM327() {
    return &elm327;
}

// Body door state is broadcast on CAN and is not readable by any diagnostic
// request on this vehicle - see tools/toyota-explorer/README.md. Listening
// with a one-id receive filter keeps the burst short and the bus untouched.
#define BODY_DOOR_CAN_ID 0x620

bool OBDClass::readBroadcastFrame(const uint16_t canId, const unsigned long timeoutMs) {
    if (elm327.elm_port == nullptr || !isLinkUp()) {
        return false;
    }

    char filter[12] = {'\0'};
    snprintf(filter, sizeof(filter), "ATCRA%03X", canId);
    if (elm327.sendCommand_Blocking(filter) != ELM_SUCCESS) {
        return false;
    }

    while (elm327.elm_port->available() > 0) {
        elm327.elm_port->read();
    }
    elm327.elm_port->print("ATMA\r");

    char line[48] = {'\0'};
    size_t len = 0;
    bool got = false;
    const unsigned long deadline = millis() + timeoutMs;

    while (millis() < deadline && !got) {
        while (elm327.elm_port->available() > 0) {
            const char c = static_cast<char>(elm327.elm_port->read());
            if (c == '\r' || c == '\n') {
                if (len >= 16) {
                    got = true;
                }
                if (got) {
                    break;
                }
                len = 0;
                line[0] = '\0';
            } else if (isxdigit(static_cast<unsigned char>(c)) && len < sizeof(line) - 1) {
                line[len++] = static_cast<char>(toupper(static_cast<unsigned char>(c)));
                line[len] = '\0';
            }
        }
        if (!got) {
            delay(2);
        }
    }

    // Any character stops ATMA; then drain up to the prompt.
    elm327.elm_port->print("\r");
    const unsigned long stopBy = millis() + 400;
    while (millis() < stopBy) {
        while (elm327.elm_port->available() > 0) {
            if (static_cast<char>(elm327.elm_port->read()) == '>') {
                goto stopped;
            }
        }
        delay(2);
    }
stopped:
    // Putting the filter back matters: leaving it narrowed would silently
    // starve every normal PID read afterwards.
    elm327.sendCommand_Blocking("ATCRA");

    if (!got) {
        return false;
    }

    // With headers on the line carries the id first; strip it if present.
    const char *hex = line;
    char idStr[4] = {'\0'};
    snprintf(idStr, sizeof(idStr), "%03X", canId);
    if (len >= 19 && strncmp(line, idStr, 3) == 0) {
        hex += 3;
        len -= 3;
    }
    if (len < 16) {
        return false;
    }

    for (uint8_t i = 0; i < 8; ++i) {
        char b[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        bodyFrame[i] = static_cast<uint8_t>(strtoul(b, nullptr, 16));
    }
    bodyFrameValid = true;
    bodyFrameAt = millis();
    return true;
}

bool OBDClass::refreshBodyFrame(const unsigned long maxAgeMs) {
    if (bodyFrameValid && millis() - bodyFrameAt < maxAgeMs) {
        return true;
    }
    if (!readBroadcastFrame(BODY_DOOR_CAN_ID)) {
        // Do not keep serving a stale frame as if it were current.
        bodyFrameValid = false;
        return false;
    }
    return true;
}

uint8_t OBDClass::getBodyByte(const uint8_t index) const {
    return bodyFrameValid && index < 8 ? bodyFrame[index] : 0;
}

bool OBDClass::isBodyFrameValid() const {
    return bodyFrameValid;
}

void OBDClass::loopInternal() {
#ifdef USE_BLE
    if (!stopConnect && serialBLE && !serialBLE.isClosed()) {
#else
    if (!stopConnect && serialBt && !serialBt.isClosed()) {
#endif
        if (enginePidCooldownUntil > millis()) {
            if (lastCooldownVoltageRead == 0 || millis() - lastCooldownVoltageRead >= COOLDOWN_VOLTAGE_INTERVAL_MS) {
                const bool voltageRead = elm327.sendCommand_Blocking(READ_VOLTAGE) == ELM_SUCCESS;
                lastCooldownVoltageRead = millis();
                if (voltageRead) {
                    const float voltage = strtof(elm327.payload, nullptr);
                    setStateValue("batteryVoltage", voltage);
                    DBG_PRINTF("Engine PID cooldown active; battery voltage %.2fV\n", voltage);
                } else {
                    DBG_PRINTLN("Engine PID cooldown active; battery voltage unavailable");
                }
            }
            if (lastCooldownPidProbe == 0 || millis() - lastCooldownPidProbe >= COOLDOWN_PID_PROBE_INTERVAL_MS) {
                lastCooldownPidProbe = millis();
                probeNextCooldownPid();
            }
            delay(500);
            return;
        }

        if (enginePidCooldownUntil != 0) {
            DBG_PRINTLN("Engine PID cooldown ended; probing RPM before resuming full OBD PID reads.");
            if (!probeRpmForCooldownExit()) {
                enginePidCooldownUntil = millis() + ENGINE_PID_COOLDOWN_MS;
                lastCooldownVoltageRead = 0;
                lastCooldownPidProbe = 0;
                cooldownPidProbeIndex = 0;
                DBG_PRINTF("RPM still unavailable; keeping cooldown for another %lus\n",
                           ENGINE_PID_COOLDOWN_MS / 1000UL);
                delay(500);
                return;
            }
            DBG_PRINTLN("RPM recovered; resuming full OBD PID reads.");
            enginePidCooldownUntil = 0;
            lastCooldownVoltageRead = 0;
            lastCooldownPidProbe = 0;
            cooldownPidProbeIndex = 0;
        }

#ifdef DEBUG_OBDSTATE
        OBDState *state = nextState();
        if (state != nullptr && state->getType() == obd::READ && state->getLastUpdate() != -1 && state->isSupported()) {
            if (state->valueType() == OBD_STATE_TYPE_INT) {
                auto s = reinterpret_cast<TypedOBDState<int> *>(state);
                Serial.printf("%s : %d -> %d\n", s->getName(), s->getOldValue(), s->getValue());
            }
            if (state->valueType() == OBD_STATE_TYPE_FLOAT) {
                auto s = reinterpret_cast<TypedOBDState<float> *>(state);
                Serial.printf("%s : %4.2f -> %4.2f\n", s->getName(), s->getOldValue(), s->getValue());
            }
            if (state->valueType() == OBD_STATE_TYPE_BOOL) {
                auto s = reinterpret_cast<TypedOBDState<bool> *>(state);
                Serial.printf("%s %d -> %d\n", s->getName(), s->getOldValue(), s->getValue());
            }
        }
#else
        OBDState *state = nextState();
#endif
        if (state != nullptr && state->getType() == obd::READ) {
            if (state->getUpdateStatus() == ELM_SUCCESS) {
                const char *payload = state->getPayload() != nullptr ? state->getPayload() : "";
                if (state->valueType() == OBD_STATE_TYPE_INT) {
                    auto *is = reinterpret_cast<OBDStateInt *>(state);
                    DBG_PRINTF("OBD PID updated: %s=%d payload=%s\n", state->getName(), is->getValue(), payload);
                } else if (state->valueType() == OBD_STATE_TYPE_FLOAT) {
                    auto *fs = reinterpret_cast<OBDStateFloat *>(state);
                    DBG_PRINTF("OBD PID updated: %s=%4.2f payload=%s\n", state->getName(), fs->getValue(), payload);
                } else if (state->valueType() == OBD_STATE_TYPE_BOOL) {
                    auto *bs = reinterpret_cast<OBDStateBool *>(state);
                    DBG_PRINTF("OBD PID updated: %s=%d payload=%s\n", state->getName(), bs->getValue(), payload);
                } else {
                    DBG_PRINTF("OBD PID updated: %s payload=%s\n", state->getName(), payload);
                }
            } else if (state->getUpdateStatus() != ELM_GETTING_MSG) {
                DBG_PRINTF("OBD PID skipped: %s, status %d\n", state->getName(), state->getUpdateStatus());
            }
            delay(NORMAL_PID_PACING_MS);
        }
        if (state != nullptr && state->getType() == obd::READ &&
            state->getService() == SERVICE_01 && state->getPID() == ENGINE_RPM) {
            if (state->getUpdateStatus() == ELM_SUCCESS) {
                if (rpmFailureCount != 0) {
                    DBG_PRINTLN("RPM PID recovered; clearing engine PID cooldown counter.");
                }
                rpmFailureCount = 0;
            } else if (state->getUpdateStatus() == ELM_TIMEOUT || state->getUpdateStatus() == ELM_NO_DATA) {
                ++rpmFailureCount;
                DBG_PRINTF("RPM PID failed %u/%u with status %d\n",
                           rpmFailureCount,
                           RPM_FAILURES_BEFORE_COOLDOWN,
                           state->getUpdateStatus());
                if (rpmFailureCount >= RPM_FAILURES_BEFORE_COOLDOWN) {
                    rpmFailureCount = 0;
                    enginePidCooldownUntil = millis() + ENGINE_PID_COOLDOWN_MS;
                    lastCooldownVoltageRead = 0;
                    lastCooldownPidProbe = 0;
                    cooldownPidProbeIndex = 0;
                    DBG_PRINTF("RPM PID unavailable; pausing engine PID reads for %lus\n",
                               ENGINE_PID_COOLDOWN_MS / 1000UL);
                }
            }
        }
    } else {
        delay(500);
    }
}

void OBDClass::probeNextCooldownPid() {
    std::vector<OBDState *> cooldownProbeStates{};
    getStates([](OBDState *state) {
        return state->isEnabled() &&
               state->isVisible() &&
               !state->isDiagnostic() &&
               state->getType() == obd::READ &&
               state->getService() == SERVICE_01 &&
               state->getPID() != ENGINE_RPM &&
               strcmp(state->getName(), "batteryVoltage") != 0;
    }, cooldownProbeStates);

    if (cooldownProbeStates.empty()) {
        return;
    }

    if (cooldownPidProbeIndex >= cooldownProbeStates.size()) {
        cooldownPidProbeIndex = 0;
    }

    OBDState *state = cooldownProbeStates.at(cooldownPidProbeIndex++);
    DBG_PRINTF("Engine PID cooldown active; probing %s (01%02X)\n",
               state->getName(),
               state->getPID());
    state->readValue();
    if (state->getUpdateStatus() == ELM_SUCCESS) {
        DBG_PRINTF("Engine PID cooldown probe updated %s\n", state->getName());
    } else {
        DBG_PRINTF("Engine PID cooldown probe skipped %s, status %d\n",
                   state->getName(),
                   state->getUpdateStatus());
    }
}

bool OBDClass::probeRpmForCooldownExit() {
    std::vector<OBDState *> rpmStates{};
    getStates([](OBDState *state) {
        return state->isEnabled() &&
               state->getType() == obd::READ &&
               state->getService() == SERVICE_01 &&
               state->getPID() == ENGINE_RPM;
    }, rpmStates);

    if (rpmStates.empty()) {
        return true;
    }

    OBDState *state = rpmStates.at(0);
    state->readValue();
    return state->getUpdateStatus() == ELM_SUCCESS;
}

bool OBDClass::connected() const {
    return elm327.connected;
}

void OBDClass::onConnected(const std::function<void()> &callback) {
    connectedCallback = callback;
}

void OBDClass::onConnectError(const std::function<void()> &callback) {
    connectErrorCallback = callback;
}

DTCs *OBDClass::getDTCs() {
    return dtcsRead ? &dtcs : nullptr;
}

bool OBDClass::resetDTCs() {
    return elm327.resetDTC();
}

#ifdef USE_BLE
void OBDClass::onDevicesDiscovered(const std::function<void(BLEScanResultsSet * scanResult)> &callable) {
    devDiscoveredCallback = callable;
}
#else
void OBDClass::onDevicesDiscovered(const std::function<void(BTScanResults *scanResult)> &callable) {
    devDiscoveredCallback = callable;
}
#endif

std::string OBDClass::getConnectedBTAddress() const {
    return connectedBTAddress;
}

uint16_t OBDClass::getPayloadLength() const {
    return elm327.PAYLOAD_LEN;
}

OBDClass OBD;
