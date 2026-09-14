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
#include <unity.h>

#include "pid_codec.h"

void setUp(void) {
}

void tearDown(void) {
}

// ---------------------------------------------------------------------------
// identifier width
// ---------------------------------------------------------------------------

static void test_only_service_22_uses_two_byte_id(void) {
    TEST_ASSERT_TRUE(pidcodec::usesTwoBytePid(0x22));

    TEST_ASSERT_FALSE(pidcodec::usesTwoBytePid(0x01));
    TEST_ASSERT_FALSE(pidcodec::usesTwoBytePid(0x09));
    TEST_ASSERT_FALSE(pidcodec::usesTwoBytePid(0x21));
    TEST_ASSERT_FALSE(pidcodec::usesTwoBytePid(0x0A));
}

// ---------------------------------------------------------------------------
// request framing
// ---------------------------------------------------------------------------

static void test_request_standard_pid(void) {
    char out[pidcodec::MIN_BUFFER] = {'\0'};

    TEST_ASSERT_EQUAL_UINT(4, pidcodec::buildRequest(out, sizeof(out), 0x01, 0x0C));
    TEST_ASSERT_EQUAL_STRING("010C", out);

    TEST_ASSERT_EQUAL_UINT(4, pidcodec::buildRequest(out, sizeof(out), 0x01, 0x0D));
    TEST_ASSERT_EQUAL_STRING("010D", out);
}

static void test_request_uds_did_is_four_hex_digits(void) {
    char out[pidcodec::MIN_BUFFER] = {'\0'};

    // The bug this guards: masking the DID to 8 bit produced "2234".
    TEST_ASSERT_EQUAL_UINT(6, pidcodec::buildRequest(out, sizeof(out), 0x22, 0x1234));
    TEST_ASSERT_EQUAL_STRING("221234", out);
}

static void test_request_uds_did_below_256_keeps_leading_zeros(void) {
    char out[pidcodec::MIN_BUFFER] = {'\0'};

    // A "pid > 0xFF" discriminator would wrongly emit "2242" here.
    TEST_ASSERT_EQUAL_UINT(6, pidcodec::buildRequest(out, sizeof(out), 0x22, 0x0042));
    TEST_ASSERT_EQUAL_STRING("220042", out);
}

static void test_request_rejects_short_buffer(void) {
    char tiny[5] = {'\0'};

    TEST_ASSERT_EQUAL_UINT(0, pidcodec::buildRequest(tiny, sizeof(tiny), 0x22, 0x1234));
    TEST_ASSERT_EQUAL_UINT(0, pidcodec::buildRequest(nullptr, 16, 0x01, 0x0C));
}

// ---------------------------------------------------------------------------
// response marker
// ---------------------------------------------------------------------------

static void test_marker_adds_0x40_to_service(void) {
    char out[pidcodec::MIN_BUFFER] = {'\0'};

    TEST_ASSERT_EQUAL_UINT(4, pidcodec::buildResponseMarker(out, sizeof(out), 0x01, 0x0C));
    TEST_ASSERT_EQUAL_STRING("410C", out);

    // The old marker[5] buffer could not hold this six character marker.
    TEST_ASSERT_EQUAL_UINT(6, pidcodec::buildResponseMarker(out, sizeof(out), 0x22, 0x1234));
    TEST_ASSERT_EQUAL_STRING("621234", out);
}

// ---------------------------------------------------------------------------
// response parsing - standard PIDs
// ---------------------------------------------------------------------------

static void test_parse_rpm_without_headers(void) {
    uint64_t raw = 0;

    TEST_ASSERT_TRUE(pidcodec::parseResponse("41 0C 1A F8", 0x01, 0x0C, 2, raw));
    TEST_ASSERT_EQUAL_UINT64(0x1AF8, raw);
}

static void test_parse_rpm_with_can_headers(void) {
    uint64_t raw = 0;

    // ATH1 is on, so the answer carries the CAN id and the length byte.
    TEST_ASSERT_TRUE(pidcodec::parseResponse("7E8 04 41 0C 1A F8", 0x01, 0x0C, 2, raw));
    TEST_ASSERT_EQUAL_UINT64(0x1AF8, raw);
}

static void test_parse_single_byte_pid(void) {
    uint64_t raw = 0;

    TEST_ASSERT_TRUE(pidcodec::parseResponse("41 0D 28", 0x01, 0x0D, 1, raw));
    TEST_ASSERT_EQUAL_UINT64(0x28, raw);
}

// ---------------------------------------------------------------------------
// response parsing - Toyota Enhanced style DIDs
// ---------------------------------------------------------------------------

static void test_parse_uds_did_response(void) {
    uint64_t raw = 0;

    TEST_ASSERT_TRUE(pidcodec::parseResponse("7EA 06 62 12 34 00 01", 0x22, 0x1234, 2, raw));
    TEST_ASSERT_EQUAL_UINT64(0x0001, raw);
}

static void test_parse_uds_did_does_not_match_truncated_marker(void) {
    uint64_t raw = 0;

    // "6234" must not be accepted as the answer to DID 0x1234.
    TEST_ASSERT_FALSE(pidcodec::parseResponse("62 34 00 01", 0x22, 0x1234, 2, raw));
}

static void test_parse_negative_response_is_not_a_match(void) {
    uint64_t raw = 0xDEAD;

    // 7F 22 31 = requestOutOfRange, must not be mistaken for data.
    TEST_ASSERT_FALSE(pidcodec::parseResponse("7EA 03 7F 22 31", 0x22, 0x1234, 2, raw));
    TEST_ASSERT_EQUAL_UINT64(0xDEAD, raw);
}

// ---------------------------------------------------------------------------
// robustness
// ---------------------------------------------------------------------------

static void test_parse_rejects_truncated_frame(void) {
    uint64_t raw = 0;

    // Marker is present but only one of the two data bytes arrived.
    TEST_ASSERT_FALSE(pidcodec::parseResponse("41 0C 1A", 0x01, 0x0C, 2, raw));
}

static void test_parse_rejects_no_data_and_null(void) {
    uint64_t raw = 0;

    TEST_ASSERT_FALSE(pidcodec::parseResponse("NO DATA", 0x01, 0x0C, 2, raw));
    TEST_ASSERT_FALSE(pidcodec::parseResponse("", 0x01, 0x0C, 2, raw));
    TEST_ASSERT_FALSE(pidcodec::parseResponse(nullptr, 0x01, 0x0C, 2, raw));
    TEST_ASSERT_FALSE(pidcodec::parseResponse("41 0C 1A F8", 0x01, 0x0C, 0, raw));
}

static void test_parse_clamps_long_response_instead_of_failing(void) {
    uint64_t raw = 0;

    // Ten data bytes: a body ECU frame can be longer than the uint64
    // accumulator. Parsing must still succeed, otherwise readValue() skips
    // setPayload() and the raw frame needed for bit discovery is lost.
    const char *answer = "62 12 34 01 02 03 04 05 06 07 08 09 0A";
    TEST_ASSERT_TRUE(pidcodec::parseResponse(answer, 0x22, 0x1234, 10, raw));
    TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ULL, raw);
}

static void test_parse_finds_marker_after_leading_noise(void) {
    uint64_t raw = 0;

    // Echo or a previous line may still sit in the buffer.
    TEST_ASSERT_TRUE(pidcodec::parseResponse("010C 7E8 04 41 0C 0B B8", 0x01, 0x0C, 2, raw));
    TEST_ASSERT_EQUAL_UINT64(0x0BB8, raw);
}

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_only_service_22_uses_two_byte_id);

    RUN_TEST(test_request_standard_pid);
    RUN_TEST(test_request_uds_did_is_four_hex_digits);
    RUN_TEST(test_request_uds_did_below_256_keeps_leading_zeros);
    RUN_TEST(test_request_rejects_short_buffer);

    RUN_TEST(test_marker_adds_0x40_to_service);

    RUN_TEST(test_parse_rpm_without_headers);
    RUN_TEST(test_parse_rpm_with_can_headers);
    RUN_TEST(test_parse_single_byte_pid);

    RUN_TEST(test_parse_uds_did_response);
    RUN_TEST(test_parse_uds_did_does_not_match_truncated_marker);
    RUN_TEST(test_parse_negative_response_is_not_a_match);

    RUN_TEST(test_parse_rejects_truncated_frame);
    RUN_TEST(test_parse_rejects_no_data_and_null);
    RUN_TEST(test_parse_clamps_long_response_instead_of_failing);
    RUN_TEST(test_parse_finds_marker_after_leading_noise);

    return UNITY_END();
}
