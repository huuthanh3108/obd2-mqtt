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
#ifdef TOYOTA_EXPLORER

#include "ElmConsole.h"

#include <ctype.h>
#include <stdlib.h>

#include "obd.h"

#define CONSOLE_PORT Serial

// Diagnostic services that only read data. Everything else is rejected,
// see chapter "Safety" - the explorer must never write, code or actuate.
static const uint8_t READ_ONLY_SERVICES[] = {
    0x01, // show current data
    0x02, // show freeze frame data
    0x03, // show stored DTCs
    0x06, // test results, on-board monitoring
    0x07, // show pending DTCs
    0x09, // request vehicle information
    0x0A, // show permanent DTCs
    0x21, // manufacturer specific read (KWP style)
    0x22, // read data by identifier (UDS style)
};

bool ElmResponse::isNoData() const {
    String upper = raw;
    upper.toUpperCase();
    return upper.indexOf("NO DATA") >= 0;
}

String ElmResponse::hexOnly() const {
    String out;
    out.reserve(raw.length());
    for (unsigned int i = 0; i < raw.length(); ++i) {
        const char c = raw.charAt(i);
        if (isxdigit(static_cast<unsigned char>(c))) {
            out += static_cast<char>(toupper(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

void ElmConsoleClass::printBanner() {
    CONSOLE_PORT.println();
    CONSOLE_PORT.println("========================================");
    CONSOLE_PORT.println("Toyota Enhanced PID Explorer");
    CONSOLE_PORT.println("========================================");
    CONSOLE_PORT.println("Vehicle : Toyota Corolla Altis 2009 1.8 MT");
    CONSOLE_PORT.println("Adapter : Vgate iCar Pro (BLE)");
    CONSOLE_PORT.println("Mode    : READ ONLY");
    CONSOLE_PORT.println("Type 'help' for commands.");
    CONSOLE_PORT.println("========================================");
}

void ElmConsoleClass::printHelp() {
    CONSOLE_PORT.println();
    CONSOLE_PORT.println("Commands");
    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.println("  help              this text");
    CONSOLE_PORT.println("  info              show probed adapter capabilities");
    CONSOLE_PORT.println("  probe             re-probe adapter capabilities");
    CONSOLE_PORT.println("  selftest          phase 1 - verify chain + 010C/010D");
    CONSOLE_PORT.println("  raw <cmd>         send one command, e.g. raw 010C");
    CONSOLE_PORT.println("  scan [a b [p]]    which addresses answer; default 7E0 7E7 0100");
    CONSOLE_PORT.println("                    e.g. scan 700 7EF 0100");
    CONSOLE_PORT.println("  mon [sec]         passive ATMA bus monitor, default 15s");
    CONSOLE_PORT.println("  live [sec]        live diff - prints only what CHANGES");
    CONSOLE_PORT.println("                    toggle one switch at a time, default 180s");
    CONSOLE_PORT.println("  delay [ms]        show/set scan pacing, default 100ms");
    CONSOLE_PORT.println("  headers on|off    toggle ATH1/ATH0");
    CONSOLE_PORT.println("  pause | resume    stop/start normal OBD polling");
    CONSOLE_PORT.println();
    CONSOLE_PORT.println("Write, coding and actuator services are rejected.");
    CONSOLE_PORT.println("scan and mon abort on ENTER.");
    CONSOLE_PORT.println("----------------------------------------");
}

void ElmConsoleClass::printCapabilities() const {
    CONSOLE_PORT.println();
    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.println("Adapter Capabilities");
    CONSOLE_PORT.println("----------------------------------------");
    if (!caps.probed) {
        CONSOLE_PORT.println("not probed yet - run 'probe'");
        return;
    }
    CONSOLE_PORT.printf("Version (ATI)      : %s\n", caps.version.c_str());
    CONSOLE_PORT.printf("Description (AT@1) : %s\n", caps.description.c_str());
    CONSOLE_PORT.printf("Protocol (ATDP)    : %s\n", caps.protocol.c_str());
    CONSOLE_PORT.printf("Protocol # (ATDPN) : %s\n", caps.protocolNumber.c_str());
    CONSOLE_PORT.printf("ATSH  set header   : %s\n", caps.supportsHeader ? "yes" : "no");
    CONSOLE_PORT.printf("ATCRA recv filter  : %s\n", caps.supportsReceiveAddress ? "yes" : "no");
    CONSOLE_PORT.printf("ATFC  flow control : %s\n", caps.supportsFlowControl ? "yes" : "no");
    CONSOLE_PORT.printf("Raw CAN addressing : %s\n", caps.supportsRawCanAddressing ? "yes" : "no");
    CONSOLE_PORT.println("----------------------------------------");
}

bool ElmConsoleClass::ensureReady() const {
    // Gate on the transport, not on ELMduino's connected flag: that flag is
    // cleared transiently while a blocking command runs, which made whole
    // commands bail out for no reason.
    if (!OBD.isLinkUp()) {
        CONSOLE_PORT.println("[ELM] adapter not connected - wait for the BLE link.");
        return false;
    }
    return true;
}

bool ElmConsoleClass::isReadOnlyCommand(const String &command, String &reason) {
    String cmd = command;
    cmd.trim();
    cmd.toUpperCase();

    if (cmd.length() == 0) {
        reason = "empty command";
        return false;
    }

    // AT commands configure the adapter, not the vehicle.
    if (cmd.startsWith("AT")) {
        return true;
    }

    String hex;
    for (unsigned int i = 0; i < cmd.length(); ++i) {
        const char c = cmd.charAt(i);
        if (c == ' ') {
            continue;
        }
        if (!isxdigit(static_cast<unsigned char>(c))) {
            reason = "not a hex request";
            return false;
        }
        hex += c;
    }

    if (hex.length() < 2) {
        reason = "request too short";
        return false;
    }

    const uint8_t service = strtoul(hex.substring(0, 2).c_str(), nullptr, 16);
    for (unsigned int i = 0; i < sizeof(READ_ONLY_SERVICES); ++i) {
        if (READ_ONLY_SERVICES[i] == service) {
            return true;
        }
    }

    reason = "service 0x" + String(service, HEX) + " is not in the read-only allowlist";
    return false;
}

bool ElmConsoleClass::sendRaw(const String &command, ElmResponse &response, const bool quiet) {
    // Check the link BEFORE taking the pause. The reconnect path in
    // readStatesTask is skipped while we hold it, so grabbing the pause on a
    // dead link starves the very reconnect we depend on.
    if (!ensureReady()) {
        return false;
    }

    // Keep the polling task away from the transport while we talk.
    if (!OBD.pause()) {
        CONSOLE_PORT.println("[ELM] busy - OBD polling did not yield, try again.");
        return false;
    }

    const bool ok = sendRawLocked(command, response, quiet);

    OBD.resume();
    return ok;
}

bool ElmConsoleClass::sendRawLocked(const String &command, ElmResponse &response, const bool quiet) {
    response.command = command;
    response.raw = "";
    response.ok = false;
    response.status = -1;
    response.durationMs = 0;

    if (!ensureReady()) {
        return false;
    }

    ELM327 *elm = OBD.getELM327();
    if (elm == nullptr) {
        CONSOLE_PORT.println("[ELM] no ELM327 instance.");
        return false;
    }

    // Defence in depth: the allowlist is enforced here, at the only place
    // that actually reaches the vehicle, not just in the command parser.
    String reason;
    if (!isReadOnlyCommand(command, reason)) {
        CONSOLE_PORT.printf("[ELM] BLOCKED '%s': %s\n", command.c_str(), reason.c_str());
        return false;
    }

    if (!quiet) {
        CONSOLE_PORT.printf("[ELM] TX: %s\n", command.c_str());
    }

    const unsigned long start = millis();
    response.status = elm->sendCommand_Blocking(command.c_str());
    response.durationMs = millis() - start;
    response.raw = elm->payload != nullptr ? String(elm->payload) : String("");
    response.ok = response.status == ELM_SUCCESS;

    if (!quiet) {
        if (response.raw.length() > 0) {
            CONSOLE_PORT.println("[ELM] RX RAW:");
            CONSOLE_PORT.println(response.raw);
        } else {
            CONSOLE_PORT.println("[ELM] RX RAW: <empty>");
        }
        CONSOLE_PORT.printf("[ELM] status=%d time=%lums\n\n", response.status, response.durationMs);
    }

    return response.ok;
}

const ElmCapabilities &ElmConsoleClass::probeCapabilities(const bool force) {
    if (caps.probed && !force) {
        return caps;
    }

    if (!OBD.pause()) {
        CONSOLE_PORT.println("[ELM] busy - cannot probe now, try again.");
        return caps;
    }

    ElmResponse res;

    if (sendRawLocked("ATI", res, true)) {
        caps.version = res.raw;
    }
    if (sendRawLocked("AT@1", res, true)) {
        caps.description = res.raw;
    }
    if (sendRawLocked("ATDP", res, true)) {
        caps.protocol = res.raw;
    }
    if (sendRawLocked("ATDPN", res, true)) {
        caps.protocolNumber = res.raw;
    }

    // Probe the advanced commands instead of assuming the firmware has them.
    // ATSH 7DF is the normal broadcast header, so restoring it is harmless.
    caps.supportsHeader = sendRawLocked("ATSH 7DF", res, true) && res.raw.indexOf("OK") >= 0;
    caps.supportsReceiveAddress = sendRawLocked("ATCRA", res, true) && res.raw.indexOf("OK") >= 0;
    caps.supportsFlowControl = sendRawLocked("ATFCSM0", res, true) && res.raw.indexOf("OK") >= 0;
    caps.supportsRawCanAddressing = caps.supportsHeader && caps.supportsReceiveAddress;

    caps.probed = true;

    OBD.resume();
    return caps;
}

bool ElmConsoleClass::setHeaders(const bool enable) {
    ElmResponse res;
    const bool ok = sendRaw(enable ? "ATH1" : "ATH0", res);
    if (ok) {
        headersEnabled = enable;
    }
    return ok;
}

bool ElmConsoleClass::areHeadersEnabled() const {
    return headersEnabled;
}

const ElmCapabilities &ElmConsoleClass::getCapabilities() const {
    return caps;
}

bool ElmConsoleClass::runSelfTest() {
    CONSOLE_PORT.println();
    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.println("Phase 1 - Chain Verification");
    CONSOLE_PORT.println("----------------------------------------");

    if (!ensureReady()) {
        return false;
    }

    // One pause for the whole sequence: a state read slipping in between
    // ATZ and ATE0 would talk to a freshly reset adapter with echo still on.
    if (!OBD.pause()) {
        CONSOLE_PORT.println("[ELM] busy - OBD polling did not yield, try again.");
        return false;
    }

    ElmResponse res;

    // ATZ resets the adapter and drops any header/echo setting, so the
    // whole init sequence is replayed afterwards.
    //
    // ATAL matters for Toyota: enhanced answers arrive as ISO-TP multi
    // frame (a real one seen in the wild is "7E8 10 09 61 25 ..." - PCI
    // 0x10 marks a First Frame of 9 bytes). Without "allow long messages"
    // the adapter truncates anything above 7 bytes.
    const char *initSequence[] = {"ATZ", "ATE0", "ATL0", "ATS0", "ATH1", "ATAL", "ATSP6"};
    for (unsigned int i = 0; i < sizeof(initSequence) / sizeof(initSequence[0]); ++i) {
        sendRawLocked(initSequence[i], res);
        delay(100);
    }
    headersEnabled = true;
    echoDisabled = true;

    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.println("Standard OBD Test");
    CONSOLE_PORT.println("----------------------------------------");

    const bool rpmOk = sendRawLocked("010C", res);
    const String rpmRaw = res.raw;
    const bool speedOk = sendRawLocked("010D", res);
    const String speedRaw = res.raw;

    OBD.resume();

    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.printf("RPM   010C -> %s\n", rpmOk ? rpmRaw.c_str() : "FAILED");
    CONSOLE_PORT.printf("Speed 010D -> %s\n", speedOk ? speedRaw.c_str() : "FAILED");
    CONSOLE_PORT.println("----------------------------------------");

    if (!rpmOk || !speedOk) {
        CONSOLE_PORT.println("Result: FAILED - engine may be off or the ECU is not answering.");
        CONSOLE_PORT.println("Turn the ignition on and repeat before moving to phase 2.");
        return false;
    }

    CONSOLE_PORT.println("Result: OK - chain verified down to the engine ECU.");
    return true;
}

ScanLimits &ElmConsoleClass::getLimits() {
    return limits;
}

unsigned int ElmConsoleClass::runEcuScan(const uint16_t fromId, const uint16_t toId, const String &probe) {
    String reason;
    if (!isReadOnlyCommand(probe, reason)) {
        CONSOLE_PORT.printf("[SCAN] REJECTED probe '%s': %s\n", probe.c_str(), reason.c_str());
        return 0;
    }

    if (!ensureReady()) {
        return 0;
    }

    CONSOLE_PORT.println();
    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.println("Phase 3 - Diagnostic Address Discovery");
    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.printf("Range   : 0x%03X .. 0x%03X\n", fromId, toId);
    CONSOLE_PORT.printf("Probe   : %s (read only)\n", probe.c_str());
    CONSOLE_PORT.printf("Pacing  : %lums between requests, %u retries\n",
                        limits.requestDelayMs, limits.maxRetries);
    CONSOLE_PORT.println("Press ENTER to abort.");
    CONSOLE_PORT.println("----------------------------------------");

    if (!OBD.pause()) {
        CONSOLE_PORT.println("[SCAN] busy - the polling task is stuck waiting on the bus.");
        return 0;
    }

    ElmResponse res;

    // Headers on, otherwise the responding CAN id is invisible and the scan
    // cannot tell us which ECU answered.
    sendRawLocked("ATH1", res, true);
    headersEnabled = true;

    unsigned int hits = 0;
    unsigned int aborted = 0;

    for (uint16_t id = fromId; id <= toId; ++id) {
        if (CONSOLE_PORT.available() > 0) {
            while (CONSOLE_PORT.available() > 0) {
                CONSOLE_PORT.read();
            }
            aborted = 1;
            break;
        }

        char header[8] = {'\0'};
        snprintf(header, sizeof(header), "ATSH%03X", id);
        if (!sendRawLocked(header, res, true)) {
            continue;
        }

        bool answered = false;
        for (uint8_t attempt = 0; attempt <= limits.maxRetries && !answered; ++attempt) {
            answered = sendRawLocked(probe, res, true) && !res.isNoData() && res.hexOnly().length() > 0;
            if (!answered) {
                delay(limits.requestDelayMs);
            }
        }

        if (answered) {
            ++hits;
            CONSOLE_PORT.printf("[SCAN] TX ID=0x%03X DATA=%s\n", id, probe.c_str());
            CONSOLE_PORT.printf("       RX %s  (%lums)\n", res.raw.c_str(), res.durationMs);
        }

        delay(limits.requestDelayMs);

        if (id == 0xFFFF) {
            break;
        }
    }

    // Back to the normal broadcast header so polling keeps working.
    sendRawLocked("ATSH7DF", res, true);

    OBD.resume();

    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.printf("%s - %u address(es) answered.\n", aborted ? "ABORTED" : "Done", hits);
    if (hits == 0) {
        CONSOLE_PORT.println("No address answered this probe. That is a result, not a bug:");
        CONSOLE_PORT.println("try a different probe or protocol before assuming a scan error.");
    }
    CONSOLE_PORT.println("----------------------------------------");

    return hits;
}

unsigned int ElmConsoleClass::runMonitor(const unsigned long seconds, const unsigned int maxLines) {
    if (!ensureReady()) {
        return 0;
    }

    ELM327 *elm = OBD.getELM327();
    if (elm == nullptr || elm->elm_port == nullptr) {
        CONSOLE_PORT.println("[MON] no ELM327 stream.");
        return 0;
    }

    CONSOLE_PORT.println();
    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.println("Passive Bus Monitor (ATMA)");
    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.printf("Listening %lus, max %u frames. Nothing is sent to the vehicle.\n",
                        seconds, maxLines);
    CONSOLE_PORT.println("Toggle a door or the light switch and watch which frame changes.");
    CONSOLE_PORT.println("Press ENTER to stop early.");
    CONSOLE_PORT.println("----------------------------------------");

    if (!OBD.pause()) {
        CONSOLE_PORT.println("[MON] busy - the polling task is stuck waiting on the bus.");
        CONSOLE_PORT.println("[MON] that usually means the ECUs are asleep; retry with the");
        CONSOLE_PORT.println("[MON] ignition on, or run 'pause' first and retry.");
        return 0;
    }

    // Pin the output format explicitly. Whatever ran before may have left
    // headers off or spaces on, and then ATMA emits data with no CAN id -
    // useless for telling frames apart. Verify instead of assuming.
    // The first command after taking the pause is flaky - it sometimes picks
    // up whatever the interrupted read loop left behind. Retry rather than
    // abort the whole capture on one bad reply.
    ElmResponse res;
    bool hdrOk = false;
    bool spcOk = false;
    for (uint8_t attempt = 0; attempt < 3 && !hdrOk; ++attempt) {
        hdrOk = sendRawLocked("ATH1", res, true) && res.raw.indexOf("OK") >= 0;
    }
    for (uint8_t attempt = 0; attempt < 3 && !spcOk; ++attempt) {
        spcOk = sendRawLocked("ATS0", res, true) && res.raw.indexOf("OK") >= 0;
    }
    sendRawLocked("ATL0", res, true);

    if (!hdrOk || !spcOk) {
        CONSOLE_PORT.printf("[MON] cannot pin output format (ATH1=%s ATS0=%s) - aborting,\n",
                            hdrOk ? "ok" : "FAILED", spcOk ? "ok" : "FAILED");
        CONSOLE_PORT.println("[MON] frames without a CAN id cannot be told apart.");
        OBD.resume();
        return 0;
    }
    headersEnabled = true;

    // Drain anything still pending, then start monitoring.
    while (elm->elm_port->available() > 0) {
        elm->elm_port->read();
    }
    elm->elm_port->print("ATMA\r");

    const unsigned long deadline = millis() + (seconds * 1000UL);
    unsigned int lines = 0;
    String current;

    while (millis() < deadline && lines < maxLines) {
        if (CONSOLE_PORT.available() > 0) {
            while (CONSOLE_PORT.available() > 0) {
                CONSOLE_PORT.read();
            }
            break;
        }

        while (elm->elm_port->available() > 0) {
            const char c = static_cast<char>(elm->elm_port->read());
            if (c == '\r' || c == '\n') {
                current.trim();
                if (current.length() > 0) {
                    CONSOLE_PORT.printf("[MON] %s\n", current.c_str());
                    ++lines;
                }
                current = "";
            } else if (c != '>') {
                if (current.length() < 96) {
                    current += c;
                }
            }
        }

        delay(2);
    }

    // Any character stops ATMA; then wait for the prompt to come back.
    elm->elm_port->print("\r");
    const unsigned long stopDeadline = millis() + 1000UL;
    while (millis() < stopDeadline) {
        while (elm->elm_port->available() > 0) {
            if (static_cast<char>(elm->elm_port->read()) == '>') {
                goto stopped;
            }
        }
        delay(5);
    }

stopped:
    OBD.resume();

    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.printf("Captured %u frame(s).\n", lines);
    if (lines == 0) {
        CONSOLE_PORT.println("Nothing seen. Either the bus is quiet with the ignition in this");
        CONSOLE_PORT.println("position, or this adapter does not forward monitored frames.");
    }
    CONSOLE_PORT.println("----------------------------------------");

    return lines;
}

bool ElmConsoleClass::pinMonitorFormat() {
    // Whatever ran before may have left headers off or spaces on, and then
    // ATMA emits data with no CAN id - useless for telling frames apart.
    // The first command after taking the pause is flaky, so retry.
    ElmResponse res;
    bool hdrOk = false;
    bool spcOk = false;
    for (uint8_t attempt = 0; attempt < 3 && !hdrOk; ++attempt) {
        hdrOk = sendRawLocked("ATH1", res, true) && res.raw.indexOf("OK") >= 0;
    }
    for (uint8_t attempt = 0; attempt < 3 && !spcOk; ++attempt) {
        spcOk = sendRawLocked("ATS0", res, true) && res.raw.indexOf("OK") >= 0;
    }
    sendRawLocked("ATL0", res, true);

    if (!hdrOk || !spcOk) {
        CONSOLE_PORT.printf("[LIVE] cannot pin output format (ATH1=%s ATS0=%s)\n",
                            hdrOk ? "ok" : "FAILED", spcOk ? "ok" : "FAILED");
        return false;
    }
    headersEnabled = true;
    return true;
}

void ElmConsoleClass::stopMonitor() {
    ELM327 *elm = OBD.getELM327();
    if (elm == nullptr || elm->elm_port == nullptr) {
        return;
    }
    // Any character stops ATMA; then drain up to the prompt.
    elm->elm_port->print("\r");
    const unsigned long stopBy = millis() + 1000;
    while (millis() < stopBy) {
        while (elm->elm_port->available() > 0) {
            if (static_cast<char>(elm->elm_port->read()) == '>') {
                return;
            }
        }
        delay(5);
    }
}

#define LIVE_MAX_IDS        64
#define LIVE_CHATTY_LIMIT   3

unsigned int ElmConsoleClass::runLiveDiff(const unsigned long seconds, const unsigned long settleSeconds) {
    if (!ensureReady()) {
        return 0;
    }

    ELM327 *elm = OBD.getELM327();
    if (elm == nullptr || elm->elm_port == nullptr) {
        CONSOLE_PORT.println("[LIVE] no ELM327 stream.");
        return 0;
    }

    CONSOLE_PORT.println();
    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.println("Live Bus Diff");
    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.printf("Learning for %lus, then reporting for %lus.\n",
                        settleSeconds, seconds > settleSeconds ? seconds - settleSeconds : 0);
    CONSOLE_PORT.println("Ids that keep changing on their own get muted.");
    CONSOLE_PORT.println("Toggle ONE thing at a time. Press ENTER to stop.");
    CONSOLE_PORT.println("----------------------------------------");

    if (!OBD.pause()) {
        CONSOLE_PORT.println("[LIVE] busy - the polling task is stuck waiting on the bus.");
        return 0;
    }

    if (!pinMonitorFormat()) {
        OBD.resume();
        return 0;
    }

    struct LiveEntry {
        uint16_t id;
        uint8_t len;
        uint8_t data[8];
        uint16_t changes;
        bool muted;
    };
    static LiveEntry table[LIVE_MAX_IDS];
    uint8_t used = 0;

    while (elm->elm_port->available() > 0) {
        elm->elm_port->read();
    }
    elm->elm_port->print("ATMA\r");

    const unsigned long start = millis();
    const unsigned long settleUntil = start + settleSeconds * 1000UL;
    const unsigned long deadline = start + seconds * 1000UL;

    char line[48] = {'\0'};
    size_t len = 0;
    unsigned int reported = 0;
    bool aborted = false;
    bool announced = false;

    while (millis() < deadline) {
        if (CONSOLE_PORT.available() > 0) {
            while (CONSOLE_PORT.available() > 0) {
                CONSOLE_PORT.read();
            }
            aborted = true;
            break;
        }

        const bool settling = millis() < settleUntil;
        if (!settling && !announced) {
            announced = true;
            uint8_t muted = 0;
            for (uint8_t i = 0; i < used; ++i) {
                if (table[i].changes > LIVE_CHATTY_LIMIT) {
                    table[i].muted = true;
                    ++muted;
                }
            }
            CONSOLE_PORT.printf("[LIVE] learnt %u id(s), muted %u chatty. Go ahead.\n",
                                used, muted);
        }

        while (elm->elm_port->available() > 0) {
            const char c = static_cast<char>(elm->elm_port->read());
            if (c != '\r' && c != '\n') {
                if (isxdigit(static_cast<unsigned char>(c)) && len < sizeof(line) - 1) {
                    line[len++] = static_cast<char>(toupper(static_cast<unsigned char>(c)));
                    line[len] = '\0';
                }
                continue;
            }

            // A complete line: 3 hex id + an even number of data nibbles.
            if (len >= 5 && (len - 3) % 2 == 0) {
                const uint16_t id = static_cast<uint16_t>(strtoul(String(line).substring(0, 3).c_str(), nullptr, 16));
                const uint8_t dlen = (len - 3) / 2 > 8 ? 8 : static_cast<uint8_t>((len - 3) / 2);
                uint8_t data[8] = {0};
                for (uint8_t i = 0; i < dlen; ++i) {
                    const char b[3] = {line[3 + i * 2], line[3 + i * 2 + 1], '\0'};
                    data[i] = static_cast<uint8_t>(strtoul(b, nullptr, 16));
                }

                int slot = -1;
                for (uint8_t i = 0; i < used; ++i) {
                    if (table[i].id == id) {
                        slot = i;
                        break;
                    }
                }
                if (slot < 0 && used < LIVE_MAX_IDS) {
                    slot = used++;
                    table[slot] = {id, dlen, {0}, 0, false};
                    memcpy(table[slot].data, data, dlen);
                } else if (slot >= 0) {
                    LiveEntry &e = table[slot];
                    if (e.len == dlen && memcmp(e.data, data, dlen) != 0) {
                        ++e.changes;
                        if (!settling && !e.muted) {
                            ++reported;
                            CONSOLE_PORT.printf("[LIVE] %03X t+%lus\n", id, (millis() - start) / 1000);
                            for (uint8_t i = 0; i < dlen; ++i) {
                                if (e.data[i] == data[i]) {
                                    continue;
                                }
                                CONSOLE_PORT.printf("       byte %u: %02X -> %02X", i, e.data[i], data[i]);
                                const uint8_t x = e.data[i] ^ data[i];
                                for (int8_t bit = 7; bit >= 0; --bit) {
                                    if (x & (1 << bit)) {
                                        CONSOLE_PORT.printf("  bit%d:%d->%d", bit,
                                                            (e.data[i] >> bit) & 1, (data[i] >> bit) & 1);
                                    }
                                }
                                CONSOLE_PORT.println();
                            }
                        }
                        memcpy(e.data, data, dlen);
                    }
                }
            }
            len = 0;
            line[0] = '\0';
        }

        delay(2);
    }

    stopMonitor();
    OBD.resume();

    CONSOLE_PORT.println("----------------------------------------");
    CONSOLE_PORT.printf("%s - %u change(s) reported, %u id(s) tracked.\n",
                        aborted ? "STOPPED" : "Done", reported, used);
    if (reported == 0) {
        CONSOLE_PORT.println("Nothing moved. Either the signal is not on this bus,");
        CONSOLE_PORT.println("or it sits in an id that was muted as chatty.");
    }
    CONSOLE_PORT.println("----------------------------------------");

    return reported;
}

void ElmConsoleClass::handleLine(const String &line) {
    String cmd = line;
    cmd.trim();
    if (cmd.length() == 0) {
        return;
    }

    String lower = cmd;
    lower.toLowerCase();

    if (lower == "help" || lower == "?") {
        printHelp();
        return;
    }

    if (lower == "info") {
        printCapabilities();
        return;
    }

    if (lower == "probe") {
        CONSOLE_PORT.println("[ELM] probing adapter...");
        probeCapabilities(true);
        printCapabilities();
        return;
    }

    if (lower == "selftest") {
        runSelfTest();
        return;
    }

    if (lower == "pause") {
        CONSOLE_PORT.println(OBD.pause() ? "[OBD] polling paused." : "[OBD] pause timed out.");
        return;
    }

    if (lower == "resume") {
        OBD.resume();
        CONSOLE_PORT.println("[OBD] polling resumed.");
        return;
    }

    if (lower.startsWith("headers")) {
        const String arg = lower.substring(7);
        if (arg.indexOf("on") >= 0) {
            setHeaders(true);
        } else if (arg.indexOf("off") >= 0) {
            setHeaders(false);
        } else {
            CONSOLE_PORT.printf("headers are %s\n", headersEnabled ? "on" : "off");
        }
        return;
    }

    if (lower.startsWith("scan")) {
        // scan [fromHex] [toHex] [probe]
        String args = cmd.substring(4);
        args.trim();

        uint16_t from = 0x7E0;
        uint16_t to = 0x7E7;
        String probe = "0100";

        if (args.length() > 0) {
            const int s1 = args.indexOf(' ');
            if (s1 < 0) {
                CONSOLE_PORT.println("usage: scan [fromHex toHex [probe]]   e.g. scan 700 7EF 0100");
                return;
            }
            from = static_cast<uint16_t>(strtoul(args.substring(0, s1).c_str(), nullptr, 16));
            String rest = args.substring(s1 + 1);
            rest.trim();
            const int s2 = rest.indexOf(' ');
            if (s2 < 0) {
                to = static_cast<uint16_t>(strtoul(rest.c_str(), nullptr, 16));
            } else {
                to = static_cast<uint16_t>(strtoul(rest.substring(0, s2).c_str(), nullptr, 16));
                probe = rest.substring(s2 + 1);
                probe.trim();
            }
        }

        if (from > to || to > 0x7FF) {
            CONSOLE_PORT.println("[SCAN] invalid range - 11 bit ids only, from <= to.");
            return;
        }

        runEcuScan(from, to, probe);
        return;
    }

    if (lower.startsWith("live")) {
        String args = cmd.substring(4);
        args.trim();
        const unsigned long secs = args.length() > 0 ? strtoul(args.c_str(), nullptr, 10) : 180UL;
        if (secs < 15 || secs > 900) {
            CONSOLE_PORT.println("usage: live [seconds]   15..900, default 180");
            return;
        }
        runLiveDiff(secs);
        return;
    }

    if (lower.startsWith("mon")) {
        String args = cmd.substring(3);
        args.trim();
        const unsigned long secs = args.length() > 0
                                       ? strtoul(args.c_str(), nullptr, 10)
                                       : 15UL;
        if (secs == 0 || secs > 300) {
            CONSOLE_PORT.println("usage: mon [seconds]   1..300, default 15");
            return;
        }
        // Let the time window govern, not a frame cap. At 400 the capture
        // ended after a couple of seconds and slow periodic frames were
        // missed at random, which made two captures of the same state look
        // different.
        runMonitor(secs, 20000);
        return;
    }

    if (lower.startsWith("delay")) {
        String args = cmd.substring(5);
        args.trim();
        if (args.length() > 0) {
            const unsigned long ms = strtoul(args.c_str(), nullptr, 10);
            if (ms < 10 || ms > 5000) {
                CONSOLE_PORT.println("usage: delay [10..5000]");
                return;
            }
            limits.requestDelayMs = ms;
        }
        CONSOLE_PORT.printf("scan pacing: %lums between requests\n", limits.requestDelayMs);
        return;
    }

    if (lower.startsWith("raw")) {
        String payload = cmd.substring(3);
        payload.trim();
        if (payload.length() == 0) {
            CONSOLE_PORT.println("usage: raw <command>");
            return;
        }

        String reason;
        if (!isReadOnlyCommand(payload, reason)) {
            CONSOLE_PORT.printf("[ELM] REJECTED: %s\n", reason.c_str());
            CONSOLE_PORT.println("[ELM] the explorer is read only.");
            return;
        }

        ElmResponse res;
        sendRaw(payload, res);
        return;
    }

    CONSOLE_PORT.printf("unknown command '%s' - type 'help'\n", cmd.c_str());
}

void ElmConsoleClass::begin() {
    printBanner();
}

void ElmConsoleClass::loop() {
    while (CONSOLE_PORT.available() > 0) {
        const char c = static_cast<char>(CONSOLE_PORT.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            const String line = inputBuffer;
            inputBuffer = "";
            handleLine(line);
            continue;
        }
        if (inputBuffer.length() < 128) {
            inputBuffer += c;
        }
    }
}

ElmConsoleClass ElmConsole;

#endif // TOYOTA_EXPLORER
