#include <unity.h>
#include "protocol.h"

// This file only tests protocol.h, so the prefix is left out here.
using namespace protocol;

// Scratch buffers reused across tests. Sized to the protocol's own maxima so a
// valid frame can never overflow them -- the same constants the real code uses.
static uint8_t encoded[MAX_ENCODED_FRAME];
static uint8_t scratch[MAX_DECODED_FRAME];

// Unity calls these before/after every test. Nothing to set up here, but they
// must exist.
void setUp(void)    {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Round trip: build a frame, parse it back, and confirm every field survives.
// This is the single most valuable test -- if it passes, encode, checksum,
// COBS, decode, and validation all agree with each other.
// ---------------------------------------------------------------------------
void test_clock_init_round_trip(void) {
    ClockInitData cfg{};
    cfg.hand1Color    = 0xF800;   // has a zero byte in the low half -> exercises COBS
    cfg.hand2Color    = 0x07E0;
    cfg.bgColor       = 0x0000;   // all-zero field -> two zero bytes in a row
    cfg.hand1LengthPct = 90;
    cfg.hand2LengthPct = 45;
    cfg.hand1Thickness = 3;
    cfg.hand2Thickness = 2;

    size_t n = buildFrame(CMD_CLOCK_INIT, 5, 42, &cfg, sizeof(cfg),
                          encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, n);                 // build succeeded
    TEST_ASSERT_EQUAL_UINT8(0x00, encoded[n-1]); // frame ends with the delimiter

    Frame f{};
    ParseResult r = parseFrame(encoded, n - 1,   // strip the delimiter, as the RX loop would
                               scratch, sizeof(scratch), &f);
    TEST_ASSERT_EQUAL(PARSE_OK, r);
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_VERSION, f.header.version);
    TEST_ASSERT_EQUAL_UINT8(CMD_CLOCK_INIT, f.header.command);
    TEST_ASSERT_EQUAL_UINT8(5,  f.header.targetNode);
    TEST_ASSERT_EQUAL_UINT8(42, f.header.sequence);
    TEST_ASSERT_EQUAL_UINT32(sizeof(ClockInitData), f.payloadLen);

    // The payload bytes must match what we sent, byte for byte.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&cfg, f.payload, sizeof(cfg));
}

// A payload that is ALL zeros is the hardest case for the encoder's zero
// handling -- every single byte needs escaping. Note this is NOT the largest
// a frame can get: zeros are the cheap case for COBS (see the worst-case test
// below), but if this round-trips the escaping logic is solid.
void test_all_zero_payload_round_trip(void) {
    GlobalClockUpdate upd{};   // 288 bytes, all zero
    size_t n = buildFrame(CMD_CLOCK_UPDATE, GLOBAL_BROADCAST_ID, 1, &upd, sizeof(upd),
                          encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, n);

    Frame f{};
    TEST_ASSERT_EQUAL(PARSE_OK,
        parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));
    TEST_ASSERT_EQUAL_UINT32(sizeof(GlobalClockUpdate), f.payloadLen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&upd, f.payload, sizeof(upd));
}

// The actual worst case for COBS: a full-size payload containing NO zero bytes
// at all. Two things only this test can prove:
//   1. It is the only case long enough to cross COBS's 254-byte block boundary,
//      forcing the encoder to close one block and open another mid-payload.
//   2. It produces the largest frame the protocol can ever emit, which lands
//      exactly on MAX_ENCODED_FRAME -- so this pins the buffer sizing that
//      every caller relies on. One byte of drift here is a buffer overrun.
void test_cobs_worst_case_round_trip(void) {
    GlobalClockUpdate upd{};
    for (uint8_t i = 0; i < TOTAL_MATRIX_NODES; i++) {
        upd.nodes[i].angle1DeciDeg = 0x0101;   // every byte non-zero
        upd.nodes[i].angle2DeciDeg = 0x0202;
    }

    size_t n = buildFrame(CMD_CLOCK_UPDATE, GLOBAL_BROADCAST_ID, 1, &upd, sizeof(upd),
                          encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, n);
    TEST_ASSERT_EQUAL_UINT32(MAX_ENCODED_FRAME, n);   // exactly fills the ceiling

    Frame f{};
    TEST_ASSERT_EQUAL(PARSE_OK,
        parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));
    TEST_ASSERT_EQUAL_UINT32(sizeof(GlobalClockUpdate), f.payloadLen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&upd, f.payload, sizeof(upd));
}

// The variable-length case: send fewer data bytes than the struct can hold and
// confirm the receiver recovers exactly the used length AND the right bytes.
void test_widget_update_variable_length(void) {
    WidgetUpdateData w{};
    w.fieldId = MAX_WIDGET_FIELDS - 1;
    w.data[0] = 0xAB;
    w.data[1] = 0xCD;
    size_t used = 1 + 2;   // fieldId + two data bytes

    size_t n = buildFrame(CMD_WIDGET_UPDATE, 3, 9, &w, used,
                          encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, n);

    Frame f{};
    TEST_ASSERT_EQUAL(PARSE_OK,
        parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));
    TEST_ASSERT_EQUAL_UINT32(used, f.payloadLen);   // recovered, not the full struct size
    TEST_ASSERT_EQUAL_UINT8(MAX_WIDGET_FIELDS - 1, f.payload[0]);    // fieldId
    TEST_ASSERT_EQUAL_UINT8(0xAB, f.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCD, f.payload[2]);
}

// CMD_WIDGET_UPDATE is the only command with a variable payload, so it is the
// only one with a length RANGE to get wrong. Walk both edges of that range.
void test_widget_update_length_bounds(void) {
    uint8_t buf[2 + MAX_WIDGET_DATA] = {};   // room to deliberately overshoot
    for (size_t i = 0; i < sizeof(buf); i++) buf[i] = uint8_t(i + 1);
    buf[0] = 0;   // fieldId must stay in range

    Frame f{};
    size_t n;

    // Zero bytes: not even a fieldId.
    n = buildFrame(CMD_WIDGET_UPDATE, 0, 0, buf, 0, encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, n);
    TEST_ASSERT_EQUAL(PARSE_ERR_LENGTH,
        parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));

    // fieldId plus a completely full data block: the largest legal payload.
    n = buildFrame(CMD_WIDGET_UPDATE, 0, 0, buf, 1 + MAX_WIDGET_DATA,
                   encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, n);
    TEST_ASSERT_EQUAL(PARSE_OK,
        parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));
    TEST_ASSERT_EQUAL_UINT32(1 + MAX_WIDGET_DATA, f.payloadLen);

    // One byte past the block: must be rejected.
    n = buildFrame(CMD_WIDGET_UPDATE, 0, 0, buf, 2 + MAX_WIDGET_DATA,
                   encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, n);
    TEST_ASSERT_EQUAL(PARSE_ERR_LENGTH,
        parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));
}

// ---------------------------------------------------------------------------
// Rejection paths: parseFrame must reject bad frames with the RIGHT reason.
// ---------------------------------------------------------------------------

// Flip one PAYLOAD byte after the checksum was computed -> the checksum is the
// only layer that can catch it.
//
// The frame is assembled by hand rather than built and then poked, because
// which encoded byte is a COBS code byte and which is literal data depends on
// the payload. Corrupting the encoded stream blindly tends to hit a code byte,
// which the COBS decoder rejects first and the checksum never sees.
void test_corrupted_payload_fails_checksum(void) {
    uint8_t body[FRAME_OVERHEAD + sizeof(ClockInitData)] = {};
    body[0] = PROTOCOL_VERSION;
    body[1] = CMD_CLOCK_INIT;
    body[2] = 0;    // targetNode
    body[3] = 0;    // sequence
    for (size_t i = 0; i < sizeof(ClockInitData); i++) {
        body[sizeof(ProtocolHeader) + i] = uint8_t(0x10 + i);
    }

    const size_t bodyLen = sizeof(ProtocolHeader) + sizeof(ClockInitData);
    body[bodyLen] = calculateChecksum(body, bodyLen);

    body[6] ^= 0xFF;   // corrupt a payload byte AFTER checksumming it

    size_t enc = cobsEncode(body, bodyLen + 1, encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, enc);

    Frame f{};
    TEST_ASSERT_EQUAL(PARSE_ERR_CHECKSUM,
        parseFrame(encoded, enc - 1, scratch, sizeof(scratch), &f));
}

// A command byte that is not a known CommandType must be rejected as such.
// buildFrame() only accepts a CommandType, so we forge the frame by hand: a
// 4-byte header with a bogus command, a valid checksum, then COBS-encode it.
void test_unknown_command_rejected(void) {
    uint8_t frame[5] = { PROTOCOL_VERSION, 0x7F, 0, 0, 0 };  // 0x7F is not a CommandType
    frame[4] = calculateChecksum(frame, 4);         // checksum over the 4 header bytes
    size_t enc = cobsEncode(frame, 5, encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, enc);

    Frame f{};
    ParseResult r = parseFrame(encoded, enc - 1, scratch, sizeof(scratch), &f);
    TEST_ASSERT_EQUAL(PARSE_ERR_COMMAND, r);
}

// A frame too short to hold even a header + checksum must be PARSE_ERR_SHORT.
void test_short_frame_rejected(void) {
    uint8_t frame[4] = { PROTOCOL_VERSION, CMD_CLOCK_INIT, 0, 0 };  // 3 bytes + checksum, header needs 4
    frame[3] = calculateChecksum(frame, 3);
    size_t enc = cobsEncode(frame, 4, encoded, sizeof(encoded));

    Frame f{};
    ParseResult r = parseFrame(encoded, enc - 1, scratch, sizeof(scratch), &f);
    TEST_ASSERT_EQUAL(PARSE_ERR_SHORT, r);
}

// Addresses above the last node are meaningless, and only 0xFF may exceed the
// node count. Without this check a bit-flip in the address byte could make a
// slave act on a frame that was never meant for it.
void test_invalid_target_rejected(void) {
    ClockInitData cfg{};
    size_t n = buildFrame(CMD_CLOCK_INIT, TOTAL_MATRIX_NODES, 0, &cfg, sizeof(cfg),
                          encoded, sizeof(encoded));   // node 72: one past the end
    TEST_ASSERT_NOT_EQUAL(0, n);

    Frame f{};
    TEST_ASSERT_EQUAL(PARSE_ERR_TARGET,
        parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));
}

// The other side of that check: both ends of the node range and the broadcast
// address must all get through.
void test_valid_targets_accepted(void) {
    const uint8_t targets[] = { 0, TOTAL_MATRIX_NODES - 1, GLOBAL_BROADCAST_ID };

    for (uint8_t t : targets) {
        ClockInitData cfg{};
        size_t n = buildFrame(CMD_CLOCK_INIT, t, 0, &cfg, sizeof(cfg),
                              encoded, sizeof(encoded));
        TEST_ASSERT_NOT_EQUAL(0, n);

        Frame f{};
        TEST_ASSERT_EQUAL(PARSE_OK,
            parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));
        TEST_ASSERT_EQUAL_UINT8(t, f.header.targetNode);
    }
}

// Every command except CMD_WIDGET_UPDATE has exactly one legal payload size.
// A frame carrying the right command but the wrong amount of data would other-
// wise be memcpy'd into a struct of a different size on the slave.
void test_payload_length_mismatch_rejected(void) {
    uint8_t junk[sizeof(ClockInitData) - 1] = {};
    size_t n = buildFrame(CMD_CLOCK_INIT, 0, 0, junk, sizeof(junk),
                          encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, n);

    Frame f{};
    TEST_ASSERT_EQUAL(PARSE_ERR_LENGTH,
        parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));
}

// A 0x00 can never appear inside a COBS frame -- the first byte is always a
// code byte, so a zero there means the receiver started reading mid-stream.
void test_zero_code_byte_rejected(void) {
    ClockInitData cfg{};
    size_t n = buildFrame(CMD_CLOCK_INIT, 0, 0, &cfg, sizeof(cfg),
                          encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, n);

    encoded[0] = 0x00;

    Frame f{};
    TEST_ASSERT_EQUAL(PARSE_ERR_COBS,
        parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));
}

// A code byte claiming more data than the frame actually contains, which is
// what a truncated or spliced frame looks like on the wire. The decoder must
// notice instead of reading past the end of the buffer.
void test_truncated_cobs_block_rejected(void) {
    uint8_t bad[] = { 0x05, 0x01, 0x02 };   // claims 4 data bytes, only 2 follow

    Frame f{};
    TEST_ASSERT_EQUAL(PARSE_ERR_COBS,
        parseFrame(bad, sizeof(bad), scratch, sizeof(scratch), &f));
}

// ---------------------------------------------------------------------------
// Caller-error paths: the guards that stop a bad call from corrupting memory.
// ---------------------------------------------------------------------------

// buildFrame must refuse a payload larger than the protocol's maximum.
void test_oversized_payload_refused(void) {
    static uint8_t big[MAX_PAYLOAD_SIZE + 1] = {0};
    size_t n = buildFrame(CMD_CLOCK_UPDATE, 0, 0, big, sizeof(big),
                          encoded, sizeof(encoded));
    TEST_ASSERT_EQUAL(0, n);   // 0 == build refused
}

// buildFrame must refuse a null payload when payloadLen > 0.
void test_null_payload_refused(void) {
    size_t n = buildFrame(CMD_CLOCK_INIT, 0, 0, nullptr, sizeof(ClockInitData),
                          encoded, sizeof(encoded));
    TEST_ASSERT_EQUAL(0, n);
}

// An undersized output buffer must return 0 rather than writing past the end.
void test_build_refuses_small_out_buffer(void) {
    ClockInitData cfg{};
    uint8_t tiny[5];
    size_t n = buildFrame(CMD_CLOCK_INIT, 0, 0, &cfg, sizeof(cfg),
                          tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL(0, n);
}

// Same guarantee on the receive side: a scratch buffer too small for the
// decoded frame must fail the parse, not overflow the slave's buffer.
void test_parse_refuses_small_scratch(void) {
    GlobalClockUpdate upd{};   // decodes to 293 bytes
    size_t n = buildFrame(CMD_CLOCK_UPDATE, GLOBAL_BROADCAST_ID, 0, &upd, sizeof(upd),
                          encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, n);

    uint8_t tinyScratch[10];
    Frame f{};
    TEST_ASSERT_EQUAL(PARSE_ERR_COBS,
        parseFrame(encoded, n - 1, tinyScratch, sizeof(tinyScratch), &f));
}

// ---------------------------------------------------------------------------
// Protocol version (byte 0 of every frame).
// ---------------------------------------------------------------------------

// A frame from another protocol version must be rejected, and the received
// version must still reach the caller for the "Protocol vX, expected vY" screen.
void test_version_mismatch_reported(void) {
    uint8_t body[FRAME_OVERHEAD + sizeof(DiagnosticData)] = {
        uint8_t(PROTOCOL_VERSION + 1), CMD_DIAGNOSTIC, 0, 0, DIAG_NORMAL, 0 };
    const size_t bodyLen = sizeof(body) - 1;
    body[bodyLen] = calculateChecksum(body, bodyLen);
    size_t enc = cobsEncode(body, sizeof(body), encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, enc);

    Frame f{};
    TEST_ASSERT_EQUAL(PARSE_ERR_VERSION,
        parseFrame(encoded, enc - 1, scratch, sizeof(scratch), &f));
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_VERSION + 1, f.header.version);
}

void test_diagnostic_round_trip(void) {
    DiagnosticData d{ DIAG_SHOW_ADDRESS };
    size_t n = buildFrame(CMD_DIAGNOSTIC, GLOBAL_BROADCAST_ID, 0, &d, sizeof(d),
                          encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, n);

    Frame f{};
    TEST_ASSERT_EQUAL(PARSE_OK,
        parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));
    TEST_ASSERT_EQUAL_UINT32(sizeof(DiagnosticData), f.payloadLen);
    TEST_ASSERT_EQUAL_UINT8(DIAG_SHOW_ADDRESS, f.payload[0]);
}

void test_display_round_trip(void) {
    DisplayData d{ 1, 200 };   // 90 degrees, duty 200
    size_t n = buildFrame(CMD_DISPLAY, GLOBAL_BROADCAST_ID, 0, &d, sizeof(d),
                          encoded, sizeof(encoded));
    TEST_ASSERT_NOT_EQUAL(0, n);

    Frame f{};
    TEST_ASSERT_EQUAL(PARSE_OK,
        parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));
    TEST_ASSERT_EQUAL_UINT32(sizeof(DisplayData), f.payloadLen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&d, f.payload, sizeof(d));
}

// ---------------------------------------------------------------------------
// ADDR chain.
// ---------------------------------------------------------------------------

static uint8_t addrEncoded[MAX_ADDR_ENCODED_FRAME];
static uint8_t addrScratch[MAX_ADDR_DECODED_FRAME];

// The 2x2 from the slave README: two rows of two. Row 1 starts at slot 2.
void test_addr_trunk_round_trip(void) {
    AddrTrunkData t{};
    t.row = 1;
    t.rowCount = 2;
    t.rowLengths[0] = 2;
    t.rowLengths[1] = 2;

    size_t n = buildAddrTrunk(t, addrEncoded, sizeof(addrEncoded));
    TEST_ASSERT_NOT_EQUAL(0, n);

    AddrFrame a{};
    TEST_ASSERT_EQUAL(PARSE_OK,
        parseAddrFrame(addrEncoded, n - 1, addrScratch, sizeof(addrScratch), &a));
    TEST_ASSERT_EQUAL_UINT8(ADDR_TRUNK, a.type);
    TEST_ASSERT_EQUAL_UINT8(1, a.row);
    TEST_ASSERT_EQUAL_UINT8(0, a.pos);
    TEST_ASSERT_EQUAL_UINT8(2, a.slot);
    TEST_ASSERT_EQUAL_UINT8(2, a.rowLength);
    TEST_ASSERT_EQUAL_UINT8(2, a.rowCount);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(t.rowLengths, a.rowLengths, 2);
}

void test_addr_row_round_trip(void) {
    AddrRowData r{ 1, 1, 3, 2 };   // row 1, pos 1, slot 3, row length 2

    size_t n = buildAddrRow(r, addrEncoded, sizeof(addrEncoded));
    TEST_ASSERT_NOT_EQUAL(0, n);

    AddrFrame a{};
    TEST_ASSERT_EQUAL(PARSE_OK,
        parseAddrFrame(addrEncoded, n - 1, addrScratch, sizeof(addrScratch), &a));
    TEST_ASSERT_EQUAL_UINT8(ADDR_ROW, a.type);
    TEST_ASSERT_EQUAL_UINT8(1, a.row);
    TEST_ASSERT_EQUAL_UINT8(1, a.pos);
    TEST_ASSERT_EQUAL_UINT8(3, a.slot);
    TEST_ASSERT_EQUAL_UINT8(2, a.rowLength);
    TEST_ASSERT_NULL(a.rowLengths);
}

// The largest ADDR frame: 72 rows of one node, no zero bytes.
// It must land exactly on MAX_ADDR_ENCODED_FRAME.
void test_addr_trunk_worst_case_size(void) {
    AddrTrunkData t{};
    t.row = 1;
    t.rowCount = MAX_ADDR_ROWS;
    for (uint8_t i = 0; i < MAX_ADDR_ROWS; i++) t.rowLengths[i] = 1;

    size_t n = buildAddrTrunk(t, addrEncoded, sizeof(addrEncoded));
    TEST_ASSERT_EQUAL_UINT32(MAX_ADDR_ENCODED_FRAME, n);

    AddrFrame a{};
    TEST_ASSERT_EQUAL(PARSE_OK,
        parseAddrFrame(addrEncoded, n - 1, addrScratch, sizeof(addrScratch), &a));
    TEST_ASSERT_EQUAL_UINT8(1, a.slot);
}

// Each "No address" case from the slave README must give PARSE_ERR_LAYOUT.
void test_addr_layout_errors(void) {
    AddrFrame a{};
    size_t n;

    // More rows wired than configured: row >= rowCount.
    AddrTrunkData t{};
    t.row = 2; t.rowCount = 2; t.rowLengths[0] = 2; t.rowLengths[1] = 2;
    n = buildAddrTrunk(t, addrEncoded, sizeof(addrEncoded));
    TEST_ASSERT_EQUAL(PARSE_ERR_LAYOUT,
        parseAddrFrame(addrEncoded, n - 1, addrScratch, sizeof(addrScratch), &a));

    // A row of length 0.
    t.row = 0; t.rowLengths[1] = 0;
    n = buildAddrTrunk(t, addrEncoded, sizeof(addrEncoded));
    TEST_ASSERT_EQUAL(PARSE_ERR_LAYOUT,
        parseAddrFrame(addrEncoded, n - 1, addrScratch, sizeof(addrScratch), &a));

    // More nodes in the layout than slots.
    t.rowLengths[0] = 40; t.rowLengths[1] = 40;
    n = buildAddrTrunk(t, addrEncoded, sizeof(addrEncoded));
    TEST_ASSERT_EQUAL(PARSE_ERR_LAYOUT,
        parseAddrFrame(addrEncoded, n - 1, addrScratch, sizeof(addrScratch), &a));

    // Row longer than configured: pos >= rowLength.
    AddrRowData r{ 0, 2, 2, 2 };
    n = buildAddrRow(r, addrEncoded, sizeof(addrEncoded));
    TEST_ASSERT_EQUAL(PARSE_ERR_LAYOUT,
        parseAddrFrame(addrEncoded, n - 1, addrScratch, sizeof(addrScratch), &a));

    // Slot past the last node.
    r = AddrRowData{ 0, 1, TOTAL_MATRIX_NODES, 2 };
    n = buildAddrRow(r, addrEncoded, sizeof(addrEncoded));
    TEST_ASSERT_EQUAL(PARSE_ERR_LAYOUT,
        parseAddrFrame(addrEncoded, n - 1, addrScratch, sizeof(addrScratch), &a));
}

void test_addr_none_round_trip(void) {
    size_t n = buildAddrNone(addrEncoded, sizeof(addrEncoded));
    TEST_ASSERT_NOT_EQUAL(0, n);

    AddrFrame a{};
    a.slot = 5;   // must be cleared by the parse
    TEST_ASSERT_EQUAL(PARSE_OK,
        parseAddrFrame(addrEncoded, n - 1, addrScratch, sizeof(addrScratch), &a));
    TEST_ASSERT_EQUAL_UINT8(ADDR_NONE, a.type);
    TEST_ASSERT_EQUAL_UINT8(0, a.slot);
    TEST_ASSERT_NULL(a.rowLengths);
}

// buildAddrTrunk must refuse a row count it cannot send.
void test_addr_trunk_bad_row_count_refused(void) {
    AddrTrunkData t{};
    t.rowCount = 0;
    TEST_ASSERT_EQUAL(0, buildAddrTrunk(t, addrEncoded, sizeof(addrEncoded)));
    t.rowCount = MAX_ADDR_ROWS + 1;
    TEST_ASSERT_EQUAL(0, buildAddrTrunk(t, addrEncoded, sizeof(addrEncoded)));
}

// fieldId and fieldCount above MAX_WIDGET_FIELDS must be rejected.
void test_widget_field_out_of_range_rejected(void) {
    Frame f{};

    WidgetUpdateData w{};
    w.fieldId = MAX_WIDGET_FIELDS;
    size_t n = buildFrame(CMD_WIDGET_UPDATE, 0, 0, &w, 2, encoded, sizeof(encoded));
    TEST_ASSERT_EQUAL(PARSE_ERR_FIELD,
        parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));

    WidgetInitData wi{};
    wi.fieldCount = MAX_WIDGET_FIELDS + 1;
    n = buildFrame(CMD_WIDGET_INIT, 0, 0, &wi, sizeof(wi), encoded, sizeof(encoded));
    TEST_ASSERT_EQUAL(PARSE_ERR_FIELD,
        parseFrame(encoded, n - 1, scratch, sizeof(scratch), &f));
}

int main(int, char**) {
    UNITY_BEGIN();

    // Round trips
    RUN_TEST(test_clock_init_round_trip);
    RUN_TEST(test_all_zero_payload_round_trip);
    RUN_TEST(test_cobs_worst_case_round_trip);
    RUN_TEST(test_widget_update_variable_length);
    RUN_TEST(test_widget_update_length_bounds);
    RUN_TEST(test_widget_field_out_of_range_rejected);

    // Rejection paths
    RUN_TEST(test_corrupted_payload_fails_checksum);
    RUN_TEST(test_unknown_command_rejected);
    RUN_TEST(test_short_frame_rejected);
    RUN_TEST(test_invalid_target_rejected);
    RUN_TEST(test_valid_targets_accepted);
    RUN_TEST(test_payload_length_mismatch_rejected);
    RUN_TEST(test_zero_code_byte_rejected);
    RUN_TEST(test_truncated_cobs_block_rejected);

    // Caller-error paths
    RUN_TEST(test_oversized_payload_refused);
    RUN_TEST(test_null_payload_refused);
    RUN_TEST(test_build_refuses_small_out_buffer);
    RUN_TEST(test_parse_refuses_small_scratch);

    // Protocol version
    RUN_TEST(test_version_mismatch_reported);
    RUN_TEST(test_diagnostic_round_trip);
    RUN_TEST(test_display_round_trip);

    // ADDR chain
    RUN_TEST(test_addr_trunk_round_trip);
    RUN_TEST(test_addr_row_round_trip);
    RUN_TEST(test_addr_trunk_worst_case_size);
    RUN_TEST(test_addr_layout_errors);
    RUN_TEST(test_addr_none_round_trip);
    RUN_TEST(test_addr_trunk_bad_row_count_refused);

    return UNITY_END();
}
