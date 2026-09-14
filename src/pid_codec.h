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

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/**
 * Framing and parsing of OBD-II / UDS requests.
 *
 * Kept free of Arduino and ELM327 dependencies so it can be unit tested on
 * the host - this is where the 8 bit versus 16 bit identifier mistakes live.
 */
namespace pidcodec {
    /** Longest request or marker produced here: "221234" plus NUL. */
    static const size_t MIN_BUFFER = 7;

    /** Data bytes that still fit into the uint64_t accumulator. */
    static const uint8_t MAX_VALUE_BYTES = 8;

    /** Upper bound of hex characters taken from one answer. */
    static const size_t MAX_HEX_CHARS = 256;

    /**
     * UDS readDataByIdentifier (0x22) addresses a two byte DID, every other
     * read service used here takes a single byte PID.
     *
     * Deciding on the service instead of the PID magnitude matters: a DID
     * like 0x0042 is a genuine two byte identifier although it fits into a
     * single byte, so a "pid > 0xFF" test would truncate it.
     *
     * @param service the diagnostic service
     *
     * @return <code>true</code> if the identifier is two bytes wide
     */
    inline bool usesTwoBytePid(const uint8_t service) {
        return service == 0x22;
    }

    /**
     * Write the request as the ELM327 expects it, e.g. "010C" or "221234".
     *
     * @param out receives the request, must hold at least MIN_BUFFER bytes
     * @param outSize size of out
     * @param service the diagnostic service
     * @param pid the PID or DID
     *
     * @return number of characters written, 0 on failure
     */
    inline size_t buildRequest(char *out, const size_t outSize,
                               const uint8_t service, const uint16_t pid) {
        if (out == nullptr || outSize < MIN_BUFFER) {
            return 0;
        }

        const int written = usesTwoBytePid(service)
                                ? snprintf(out, outSize, "%02X%04X", service, pid)
                                : snprintf(out, outSize, "%02X%02X", service, pid & 0xFF);

        return written > 0 && static_cast<size_t>(written) < outSize
                   ? static_cast<size_t>(written)
                   : 0;
    }

    /**
     * Write the marker a positive response must contain, e.g. "410C" or
     * "621234". The response service is the request service plus 0x40.
     *
     * @param out receives the marker, must hold at least MIN_BUFFER bytes
     * @param outSize size of out
     * @param service the diagnostic service of the request
     * @param pid the PID or DID
     *
     * @return number of characters written, 0 on failure
     */
    inline size_t buildResponseMarker(char *out, const size_t outSize,
                                      const uint8_t service, const uint16_t pid) {
        if (out == nullptr || outSize < MIN_BUFFER) {
            return 0;
        }

        const int written = usesTwoBytePid(service)
                                ? snprintf(out, outSize, "%02X%04X", service + 0x40, pid)
                                : snprintf(out, outSize, "%02X%02X", service + 0x40, pid & 0xFF);

        return written > 0 && static_cast<size_t>(written) < outSize
                   ? static_cast<size_t>(written)
                   : 0;
    }

    /**
     * Locate the positive response for a request and read its data bytes.
     *
     * Every non hex character is dropped first, so the answer may or may not
     * carry CAN headers and whitespace.
     *
     * At most MAX_VALUE_BYTES bytes end up in <code>rawValue</code>. A longer
     * answer is still reported as parsed rather than rejected, because the
     * caller only stores the untouched payload when this succeeds - and the
     * raw frame is what byte and bit discovery works on.
     *
     * @param payload the adapter answer
     * @param service the diagnostic service of the request
     * @param pid the PID or DID
     * @param numExpectedBytes data bytes the response must carry
     * @param rawValue receives the big endian value of the data bytes
     *
     * @return <code>true</code> if a matching response was found
     */
    inline bool parseResponse(const char *payload,
                              const uint8_t service,
                              const uint16_t pid,
                              const uint8_t numExpectedBytes,
                              uint64_t &rawValue) {
        if (payload == nullptr || numExpectedBytes == 0) {
            return false;
        }

        char hex[MAX_HEX_CHARS + 1] = {'\0'};
        size_t hexLen = 0;
        for (const char *p = payload; *p != '\0' && hexLen < MAX_HEX_CHARS; ++p) {
            if (isxdigit(static_cast<unsigned char>(*p))) {
                hex[hexLen++] = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
            }
        }

        char marker[MIN_BUFFER] = {'\0'};
        const size_t markerLen = buildResponseMarker(marker, sizeof(marker), service, pid);
        if (markerLen == 0) {
            return false;
        }

        const size_t neededLen = markerLen + (static_cast<size_t>(numExpectedBytes) * 2);
        if (hexLen < neededLen) {
            return false;
        }

        const uint8_t valueBytes = numExpectedBytes > MAX_VALUE_BYTES
                                       ? MAX_VALUE_BYTES
                                       : numExpectedBytes;

        for (size_t i = 0; i + neededLen <= hexLen; ++i) {
            if (strncmp(hex + i, marker, markerLen) != 0) {
                continue;
            }

            rawValue = 0;
            size_t pos = i + markerLen;
            for (uint8_t byteIndex = 0; byteIndex < valueBytes; ++byteIndex) {
                const char byteText[3] = {hex[pos], hex[pos + 1], '\0'};
                rawValue = (rawValue << 8) | strtoul(byteText, nullptr, 16);
                pos += 2;
            }
            return true;
        }

        return false;
    }
}
