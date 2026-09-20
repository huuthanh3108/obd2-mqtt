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
#pragma once
#ifdef USE_BLE
#include <BLEScanResultsSet.h>
#include "vlink_ble_stream.h"
#else
#include <BluetoothSerial.h>
#endif

#include <atomic>
#include <bitset>
#include <FS.h>
#include <OBDStates.h>

#include "ELMduino.h"

// ELM327
// https://cdn.sparkfun.com/assets/learn_tutorials/8/3/ELM327DS.pdf

// set default adapter name
#ifndef OBD_ADP_NAME
#define OBD_ADP_NAME        "OBDII"
#endif

#define STATES_FILE          "/states.json"

#define BT_DISCOVER_TIME    30000

// https://stackoverflow.com/questions/17170646/what-is-the-best-way-to-get-fuel-consumption-mpg-using-obd2-parameters
#define AF_RATIO_GAS        17.2
#define AF_RATIO_GASOLINE   14.7
#define AF_RATIO_PROPANE    15.5
#define AF_RATIO_ETHANOL    9.0
#define AF_RATIO_METHANOL   6.4
#define AF_RATIO_HYDROGEN   34.0
#define AF_RATIO_DIESEL     14.6

// https://kraftstoff-info.de/vergleich-der-kraftstoffe-nach-energiegehalt-und-dichte
#define DENSITY_GAS         540.0
#define DENSITY_GASOLINE    740.0
#define DENSITY_PROPANE     505.0
#define DENSITY_ETHANOL     789.0
#define DENSITY_METHANOL    792.0
#define DENSITY_HYDROGEN    70.0
#define DENSITY_DIESEL      830.0

// https://en.m.wikipedia.org/w/index.php?title=OBD-II_PIDs
// https://www.goingelectric.de/wiki/Liste-der-OBD2-Codes/
#define FUEL_TYPE_GASOLINE  1
#define FUEL_TYPE_METHANOL  2
#define FUEL_TYPE_ETHANOL   3
#define FUEL_TYPE_DIESEL    4
#define FUEL_TYPE_LPG       5
#define FUEL_TYPE_CNG       6
#define FUEL_TYPE_PROPANE   7
#define FUEL_TYPE_ELECTRIC  8

#define KPH_TO_MPH          1.60934f
#define LITER_TO_GALLON     3.7854f

class DTCs {
    std::vector<std::string> v_codes;

public:
    int getCount() const;

    std::string *getCode(int i);

    bool add(const std::string &code);

    void clear();
};

class OBDClass : public OBDStates {
#ifdef USE_BLE
    VLinkBLEStream serialBLE;
#else
    BluetoothSerial serialBt;
#endif
    ELM327 elm327;

    bool initDone = false;
    bool stopConnect = false;

    String devName;
    String devMac;
    char protocol;
    bool checkPidSupport = false;
    bool debug = false;
    bool specifyNumResponses = true;
    uint8_t rpmFailureCount = 0;
    unsigned long enginePidCooldownUntil = 0;
    unsigned long lastCooldownVoltageRead = 0;
    unsigned long lastCooldownPidProbe = 0;
    size_t cooldownPidProbeIndex = 0;

    DTCs dtcs;
    bool dtcsRead = false;

    std::string connectedBTAddress;

    std::function<void()> connectedCallback = nullptr;

    std::function<void()> connectErrorCallback = nullptr;

#ifdef USE_BLE
    std::function<void(BLEScanResultsSet *scanResult)> devDiscoveredCallback = nullptr;
#else
    std::function<void(BTScanResults *scanResult)> devDiscoveredCallback = nullptr;
#endif

#ifndef USE_BLE
    static void BTEvent(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);

    BTScanResults *discoverBtDevices();
#endif

#ifdef USE_BLE
    static void onBLEDisconnect();

    BLEScanResultsSet *discoverBLEDevices();
#endif

    template<typename T>
    void fromJSON(T *state, JsonDocument &doc);

    void readJSON(JsonDocument &doc);

    void writeJSON(JsonDocument &doc);

    template<typename T>
    T *setReadFuncByName(const char *funcName, T *state);

    template<typename T>
    T *setFormatFuncByName(const char *funcName, T *state);

    void probeNextCooldownPid();
    bool probeRpmForCooldownExit();

    // Guards the ELM327 transport against concurrent use by the polling
    // task and the diagnostic console.
    std::atomic_bool paused{false};
    std::atomic_bool busy{false};

    void loopInternal();

    // Cache of the last broadcast frame per CAN id (see readBroadcastFrame).
    // Small fixed table: only a handful of ids carry state we publish, and a
    // per-id cache keeps one read from invalidating another's data.
    struct CachedFrame {
        uint16_t id = 0;
        uint8_t data[8] = {0};
        bool valid = false;
        unsigned long at = 0;
    };

    // doors 0x620, odometer 0x611, lights 0x2C4, low beam 0x2C1 - four ids
    // already, so leave headroom rather than have one read evict another.
    static const uint8_t FRAME_CACHE_SIZE = 6;
    CachedFrame frameCache[FRAME_CACHE_SIZE];

    CachedFrame *frameSlot(uint16_t canId);

public:
    OBDClass();

    bool parseJSON(std::string &json);

    bool readStates(FS &fs);

    std::string buildJSON();

    bool writeStates(FS &fs);

    void begin(const String &devName, const String &devMac, char protocol = AUTOMATIC, bool checkPidSupport = false,
               bool debug = false, bool specifyNumResponses = true);

    void end();

    bool connect(bool reconnect = false);

    bool connected() const;

    void loop();

    void onConnected(const std::function<void()> &callback);

    void onConnectError(const std::function<void()> &callback);

    DTCs *getDTCs();

    bool resetDTCs();

#ifdef USE_BLE
    void onDevicesDiscovered(const std::function<void(BLEScanResultsSet *scanResult)> &callable);
#else
    void onDevicesDiscovered(const std::function<void(BTScanResults *scanResult)> &callable);
#endif

    std::string getConnectedBTAddress() const;

    uint16_t getPayloadLength() const;

    /**
     * Suspend state polling and wait until the read loop released the
     * ELM327 transport.
     *
     * @param timeoutMs how long to wait for the read loop to finish
     *
     * @return <code>true</code> if the transport is free to use
     */
    bool pause(unsigned long timeoutMs = 15000);

    /**
     * Resume state polling.
     */
    void resume();

    bool isPaused() const;

    /**
     * Whether the Bluetooth transport itself is up.
     *
     * Different from connected(), which reports ELMduino's own flag - that
     * one is cleared transiently while a blocking command is in flight, so
     * it is useless as a "may I talk now" gate.
     *
     * @return <code>true</code> if the serial link to the adapter is open
     */
    bool isLinkUp() const;

    /**
     * Direct access to the ELM327 instance, for the diagnostic console.
     * Only use it while the polling is paused.
     *
     * @return the ELM327 instance
     */
    ELM327 *getELM327();

    /**
     * Read one passively broadcast CAN frame.
     *
     * Door state on this vehicle is not answerable by any diagnostic
     * request - it is only broadcast. So instead of asking, narrow the
     * adapter's receive filter to a single id, listen briefly, then put the
     * filter back.
     *
     * Caller must already hold the OBD pause.
     *
     * @param canId the 11 bit id to listen for
     * @param timeoutMs how long to wait for one frame
     * @param minBytes reject a frame shorter than this. Frames are not all 8
     *                 bytes - 0x2C4 carries 4 - but accepting a truncated line
     *                 silently zero fills the missing bytes, which once made
     *                 the odometer publish 0.
     *
     * @return <code>true</code> if a frame was captured
     */
    bool readBroadcastFrame(uint16_t canId, unsigned long timeoutMs = 1200,
                            uint8_t minBytes = 8);

    /**
     * Refresh the cached frame for an id if it is older than maxAgeMs.
     *
     * @param canId the 11 bit id
     * @param maxAgeMs how stale the cache may be
     *
     * @return <code>true</code> if the cache holds a usable frame
     */
    bool refreshFrame(uint16_t canId, unsigned long maxAgeMs = 900, uint8_t minBytes = 8);

    /**
     * @param canId the 11 bit id
     * @param index data byte index, 0 based
     *
     * @return the cached byte, 0 if the cache is not valid
     */
    uint8_t getFrameByte(uint16_t canId, uint8_t index) const;

    bool isFrameValid(uint16_t canId) const;
};

extern OBDClass OBD;
