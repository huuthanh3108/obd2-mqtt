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
#pragma once

#include <Arduino.h>

/**
 * Result of a single ELM327 command.
 *
 * <code>raw</code> keeps the adapter answer as received (minus the trailing
 * prompt), so CAN headers stay intact when ATH1 is active.
 */
struct ElmResponse {
    String command;
    String raw;
    int8_t status = -1;
    unsigned long durationMs = 0;
    bool ok = false;

    /**
     * @return <code>true</code> if the adapter answered "NO DATA"
     */
    bool isNoData() const;

    /**
     * @return the answer with all non hex characters removed
     */
    String hexOnly() const;
};

/**
 * Detected adapter features.
 *
 * Everything here is probed at runtime - no Vgate firmware revision is
 * assumed to support a given command.
 */
struct ElmCapabilities {
    String version;
    String description;
    String protocol;
    String protocolNumber;
    bool supportsHeader = false;
    bool supportsReceiveAddress = false;
    bool supportsFlowControl = false;
    bool supportsRawCanAddressing = false;
    bool probed = false;
};

/**
 * Rate limits for any sweep over the vehicle bus.
 *
 * Automotive CAN must not be flooded, so every scan paces itself and can
 * be slowed down further if ECUs start dropping answers.
 */
struct ScanLimits {
    unsigned long requestDelayMs = 100;
    uint8_t maxRetries = 1;
};

/**
 * One answering diagnostic address found by the ECU scan.
 */
struct EcuScanHit {
    uint16_t txId = 0;
    String rxRaw;
    unsigned long responseMs = 0;
};

/**
 * Serial driven ELM327 console.
 *
 * Phase 1 verifies the ESP32 -> BLE -> Vgate -> ELM327 -> OBD chain,
 * phase 2 exposes an arbitrary command interface, phase 3 discovers which
 * diagnostic addresses answer at all. Everything reuses the ELM327 instance
 * owned by OBDClass instead of opening a second transport.
 */
class ElmConsoleClass {
    ElmCapabilities caps;
    ScanLimits limits;
    bool headersEnabled = false;
    bool echoDisabled = true;
    String inputBuffer;

    static void printBanner();

    static void printHelp();

    void printCapabilities() const;

    bool ensureReady() const;

    /**
     * Reject anything that is not a read only diagnostic request.
     *
     * AT commands address the adapter itself and are always allowed.
     * OBD requests are checked against a hard coded service allowlist.
     *
     * @param command the command to check
     * @param reason receives the rejection reason
     *
     * @return <code>true</code> if the command may be sent
     */
    static bool isReadOnlyCommand(const String &command, String &reason);

    /**
     * Send a command assuming the caller already holds the OBD pause.
     *
     * Used for multi step sequences: a state read must not slip in between
     * ATZ and ATE0, so the sequence holds one pause instead of taking and
     * releasing it per command.
     *
     * @param command the command without CR
     * @param response receives the result
     * @param quiet suppress the TX/RX log
     *
     * @return <code>true</code> on a successful transfer
     */
    bool sendRawLocked(const String &command, ElmResponse &response, bool quiet = false);

    void handleLine(const String &line);

public:
    /**
     * Print the banner - call once after the OBD connection is up.
     */
    void begin();

    /**
     * Poll the serial port for commands, call from loop().
     */
    void loop();

    /**
     * Send a single command and log TX/RX.
     *
     * Pauses the OBD polling task for the duration of the call so the
     * request and the answer cannot interleave with a state read.
     *
     * @param command the command without CR
     * @param response receives the result
     * @param quiet suppress the TX/RX log
     *
     * @return <code>true</code> on a successful transfer
     */
    bool sendRaw(const String &command, ElmResponse &response, bool quiet = false);

    /**
     * Probe adapter identity and which advanced commands it accepts.
     *
     * @param force re-probe even if already done
     */
    const ElmCapabilities &probeCapabilities(bool force = false);

    /**
     * Phase 1 - verify the whole chain down to standard OBD-II PIDs.
     *
     * Sends the documented init sequence and then reads 010C/010D.
     *
     * @return <code>true</code> if both standard PIDs answered
     */
    bool runSelfTest();

    /**
     * Enable or disable CAN headers (ATH1/ATH0).
     *
     * @param enable <code>true</code> to show headers
     *
     * @return <code>true</code> on success
     */
    bool setHeaders(bool enable);

    bool areHeadersEnabled() const;

    const ElmCapabilities &getCapabilities() const;

    /**
     * Phase 3 - find which diagnostic addresses answer at all.
     *
     * Sets ATSH to every address in the range and sends one service 01
     * request. Only read services are used and the sweep is paced by
     * ScanLimits. Any serial input aborts it.
     *
     * Nothing here assumes a Toyota address map: the range is given by the
     * caller and every answer is reported with its own CAN id.
     *
     * @param fromId first 11 bit request id
     * @param toId last 11 bit request id
     * @param probe the read request to send, e.g. "0100"
     *
     * @return number of addresses that answered
     */
    unsigned int runEcuScan(uint16_t fromId, uint16_t toId, const String &probe);

    /**
     * Passive bus monitor (ATMA).
     *
     * Streams every frame the adapter sees without sending a single request
     * to the vehicle. Useful to find broadcast body signals: toggle a door
     * and watch which frame changes.
     *
     * Needs direct stream access because ATMA never returns a prompt until
     * it is interrupted, so sendCommand_Blocking() cannot be used.
     *
     * @param seconds how long to listen
     * @param maxLines stop after this many frames
     *
     * @return number of lines captured
     */
    unsigned int runMonitor(unsigned long seconds, unsigned int maxLines);

    /**
     * Live bus diff: stream frames and report only what changes.
     *
     * A capture-and-compare round trip costs about a minute per vehicle
     * state, which is far too slow to sweep a list of switches. This keeps
     * the last payload per CAN id and prints a line the moment one moves,
     * so a switch can be toggled and identified immediately.
     *
     * Chatty ids (wheel speeds, counters) are learnt during a short settle
     * phase and muted, rather than hard coded - what counts as chatty
     * differs between parked and driving.
     *
     * @param seconds total run time
     * @param settleSeconds learning phase before reporting starts
     *
     * @return number of changes reported
     */
    unsigned int runLiveDiff(unsigned long seconds, unsigned long settleSeconds = 8);

    ScanLimits &getLimits();

private:
    /**
     * Put the adapter into a deterministic monitoring format.
     *
     * @return <code>true</code> if headers are on and spaces are off
     */
    bool pinMonitorFormat();

    /**
     * Stop a running ATMA and wait for the prompt.
     */
    void stopMonitor();

public:
};

extern ElmConsoleClass ElmConsole;
