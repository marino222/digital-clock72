#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>   // memcpy

/*
 * ---------------------------------------------------------------------------
 * PROTOCOL
 * ---------------------------------------------------------------------------
 * Two links, one framing.
 *
 *   RS485 bus   master -> all slaves, 1 Mbaud. Clock and widget state.
 *   ADDR chain  node -> next node, 9600 baud. Auto-addressing.
 *
 * A decoded frame on either link:
 *
 *   [version][ ...header... ][ ...payload... ][xor]
 *       0                                       N
 *
 * - Byte 0 is always PROTOCOL_VERSION. It must never move, so an old slave can
 *   still read the version of a newer master and report the mismatch.
 * - The last byte is the XOR of all bytes before it.
 * - The frame is COBS-encoded and ends with one 0x00 delimiter.
 * - There is no length field. COBS marks the end of the frame, so the payload
 *   length is (decoded length - header - checksum).
 *
 * Plain C++ without Arduino includes, so it builds and is tested on the PC.
 * ---------------------------------------------------------------------------
 */

// Packed structs are copied onto the wire with memcpy. Both ends must therefore
// use the same byte order.
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "protocol.h copies packed structs onto the wire; little-endian target required."
#endif

// Everything below is used as protocol::name, so it is clear where it comes from.
namespace protocol {

/*
 * ---------------------------------------------------------
 * CONSTANTS
 * ---------------------------------------------------------
 */

// Bump on every change to this file that changes bytes on the wire
// (see software/README.md).
constexpr uint8_t PROTOCOL_VERSION    = 5;

constexpr uint8_t FRAME_DELIMITER     = 0x00;
constexpr uint8_t GLOBAL_BROADCAST_ID = 0xFF; // targetNode value for all nodes
constexpr uint8_t TOTAL_MATRIX_NODES  = 72;   // highest node count, sizes the slot table

// Link timing. Master and slave must use the same values.
constexpr uint32_t RS485_BAUD     = 1'000'000;
constexpr uint32_t ADDR_BAUD      = 9600;
constexpr uint32_t ADDR_REPEAT_MS = 100;   // every ADDR sender repeats its frame this often

// Ceiling for one widget update. Only the used bytes are sent (see WidgetUpdateData).
constexpr size_t  MAX_WIDGET_DATA     = 32;

// Fields per widget. The slave keeps the latest value of every field.
constexpr uint8_t MAX_WIDGET_FIELDS   = 8;

// Ceiling for the row table in a TRUNK frame. One node per row is the worst case.
constexpr uint8_t MAX_ADDR_ROWS       = TOTAL_MATRIX_NODES;

/*
 * ---------------------------------------------------------
 * COMMANDS
 * ---------------------------------------------------------
 */

// RS485 bus. Init commands carry the configuration and are sent rarely.
// Update commands are small and sent every frame.
enum CommandType : uint8_t {
    CMD_CLOCK_INIT    = 0x01,
    CMD_WIDGET_INIT   = 0x02,
    CMD_CLOCK_UPDATE  = 0x03,
    CMD_WIDGET_UPDATE = 0x04,
    CMD_DIAGNOSTIC    = 0x05,
    CMD_DISPLAY       = 0x06   // display settings for every mode
};

// ADDR chain. A separate wire, so its own numbering.
enum AddrType : uint8_t {
    ADDR_TRUNK = 0x01,  // to the first node of a row
    ADDR_ROW   = 0x02,  // to every other node of a row
    ADDR_NONE  = 0x03   // no payload. The sender has no valid address, so the receiver has none either
};

// Payload value of CMD_DIAGNOSTIC.
enum DiagnosticMode : uint8_t {
    DIAG_NORMAL       = 0,
    DIAG_SHOW_ADDRESS = 1   // screen shows "Row r · Pos p · Slot s"
};

/*
 * ---------------------------------------------------------
 * PACKED STRUCTURES
 * ---------------------------------------------------------
 * pack(1) does two things:
 * 1. No padding, so the struct matches the bytes on the wire.
 * 2. Byte-wise access. The payload starts at an odd offset, and the RP2040
 *    (Cortex-M0+) faults on unaligned 16-bit loads.
 */
#pragma pack(push, 1)

// --- RS485 HEADER ---

// Starts every RS485 frame. 4 bytes.
struct ProtocolHeader {
    uint8_t version;       // PROTOCOL_VERSION
    uint8_t command;       // CommandType
    uint8_t targetNode;    // 0..71 for one node, GLOBAL_BROADCAST_ID for all
    uint8_t sequence;      // rolls over 0..255. A gap means a dropped frame
};
static_assert(sizeof(ProtocolHeader) == 4, "ProtocolHeader must be 4 bytes");

// --- CLOCK PAYLOADS ---

// Clock state of one node.
struct ClockNodeData {
    uint16_t angle1DeciDeg; // hand 1: 0..3599
    uint16_t angle2DeciDeg; // hand 2: 0..3599
};
static_assert(sizeof(ClockNodeData) == 4, "ClockNodeData must be 4 bytes");

// Payload of CMD_CLOCK_UPDATE. All nodes in one broadcast, so all displays step
// at the same moment. Each slave reads only nodes[its slot].
struct GlobalClockUpdate {
    ClockNodeData nodes[TOTAL_MATRIX_NODES];
};
static_assert(sizeof(GlobalClockUpdate) == 288, "GlobalClockUpdate must be 72 * 4 bytes");

// Payload of CMD_CLOCK_INIT. What a node needs before it can draw the clock.
struct ClockInitData {
    uint16_t hand1Color;     // RGB565
    uint16_t hand2Color;     // RGB565
    uint16_t bgColor;        // RGB565
    uint8_t  hand1LengthPct; // % of the face radius
    uint8_t  hand2LengthPct; // % of the face radius
    uint8_t  hand1Thickness; // px
    uint8_t  hand2Thickness; // px
};
static_assert(sizeof(ClockInitData) == 10, "ClockInitData must be 10 bytes");

// --- WIDGET PAYLOADS ---

// Payload of CMD_WIDGET_INIT.
struct WidgetInitData {
    uint8_t  widgetType;
    uint16_t bgColor;        // RGB565
    uint8_t  fieldCount;     // number of fields an update can address, 0..MAX_WIDGET_FIELDS
    uint8_t  reserved[2];    // must be 0
};
static_assert(sizeof(WidgetInitData) == 6, "WidgetInitData must be 6 bytes");

// Payload of CMD_WIDGET_UPDATE.
//
// A storage type, not a fixed wire size. Only the used prefix is sent:
// the sender passes payloadLen = 1 + usedBytes to buildFrame(), and the
// receiver gets usedBytes = frame.payloadLen - 1.
struct WidgetUpdateData {
    uint8_t fieldId;                // which field of the widget, 0..MAX_WIDGET_FIELDS-1
    uint8_t data[MAX_WIDGET_DATA];  // only the first (payloadLen - 1) bytes are sent
};
static_assert(sizeof(WidgetUpdateData) == 1 + MAX_WIDGET_DATA,
              "WidgetUpdateData must be fieldId + MAX_WIDGET_DATA bytes");

// --- DISPLAY PAYLOAD ---

// Payload of CMD_DISPLAY. How the display is mounted and driven, the same for
// all nodes and every mode. Not part of an init, because it does not change
// with the clock or widget design.
struct DisplayData {
    uint8_t rotation;        // 0..3 = 0/90/180/270 degrees, for LGFX::setRotation()
    uint8_t brightness;      // backlight PWM duty, 0..255. Gamma is applied by the master
};
static_assert(sizeof(DisplayData) == 2, "DisplayData must be 2 bytes");

// --- DIAGNOSTIC PAYLOAD ---

// Payload of CMD_DIAGNOSTIC.
struct DiagnosticData {
    uint8_t mode;            // DiagnosticMode. Unknown values count as DIAG_NORMAL
};
static_assert(sizeof(DiagnosticData) == 1, "DiagnosticData must be 1 byte");

// --- ADDR CHAIN ---

// Starts every ADDR frame. 2 bytes.
struct AddrHeader {
    uint8_t version;         // PROTOCOL_VERSION
    uint8_t type;            // AddrType
};
static_assert(sizeof(AddrHeader) == 2, "AddrHeader must be 2 bytes");

// Payload of ADDR_TRUNK. A storage type like WidgetUpdateData:
// only the first rowCount entries of rowLengths are sent.
struct AddrTrunkData {
    uint8_t row;                        // row of the receiving node
    uint8_t rowCount;                   // rows in the layout, 1..MAX_ADDR_ROWS
    uint8_t rowLengths[MAX_ADDR_ROWS];  // nodes per row
};
static_assert(sizeof(AddrTrunkData) == 2 + MAX_ADDR_ROWS,
              "AddrTrunkData must be row + rowCount + MAX_ADDR_ROWS bytes");

// Payload of ADDR_ROW.
struct AddrRowData {
    uint8_t row;             // row of the receiving node
    uint8_t pos;             // position in the row, 0 = first node
    uint8_t slot;            // index into GlobalClockUpdate::nodes
    uint8_t rowLength;       // nodes in this row
};
static_assert(sizeof(AddrRowData) == 4, "AddrRowData must be 4 bytes");

#pragma pack(pop)

/*
 * ---------------------------------------------------------
 * BUFFER SIZES
 * ---------------------------------------------------------
 * Size every buffer from these. Do not count bytes by hand.
 *
 * COBS worst case: one code byte per 254 data bytes, plus the leading code
 * byte, plus the delimiter.
 */
constexpr size_t cobsMaxEncoded(size_t decoded) { return decoded + decoded / 254 + 2; }

// RS485 bus
constexpr size_t FRAME_OVERHEAD    = sizeof(ProtocolHeader) + 1;         // header + checksum
constexpr size_t MAX_PAYLOAD_SIZE  = sizeof(GlobalClockUpdate);          // 288
constexpr size_t MAX_DECODED_FRAME = FRAME_OVERHEAD + MAX_PAYLOAD_SIZE;  // 293
constexpr size_t MAX_ENCODED_FRAME = cobsMaxEncoded(MAX_DECODED_FRAME);  // 296

// ADDR chain
constexpr size_t ADDR_OVERHEAD          = sizeof(AddrHeader) + 1;                     // header + checksum
constexpr size_t MAX_ADDR_PAYLOAD       = sizeof(AddrTrunkData);                      // 74
constexpr size_t MAX_ADDR_DECODED_FRAME = ADDR_OVERHEAD + MAX_ADDR_PAYLOAD;           // 77
constexpr size_t MAX_ADDR_ENCODED_FRAME = cobsMaxEncoded(MAX_ADDR_DECODED_FRAME);     // 79

/*
 * ---------------------------------------------------------
 * CHECKSUM
 * ---------------------------------------------------------
 * 1-byte XOR. CRC-8 would catch more errors. XOR is enough for a short, quiet
 * bus and costs almost nothing.
 */
inline uint8_t calculateChecksum(const uint8_t* data, size_t length) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

/*
 * ---------------------------------------------------------
 * COBS
 * ---------------------------------------------------------
 * COBS removes every 0x00 from the data. 0x00 then only marks the end of a
 * frame. A receiver that loses sync drops bytes until the next 0x00.
 *
 * Both functions return 0 instead of writing past dstCap.
 */

/**
 * Encodes `src` as COBS and appends FRAME_DELIMITER.
 *
 * @param src    Raw bytes. May contain 0x00.
 * @param length Number of bytes in `src`.
 * @param dst    Output buffer.
 * @param dstCap Capacity of `dst`. cobsMaxEncoded(length) always fits.
 *
 * @return Bytes written, including the delimiter. 0 if `dst` is too small.
 */
inline size_t cobsEncode(const uint8_t* src, size_t length, uint8_t* dst, size_t dstCap) {
    if (dstCap < 2) return 0;   // no room for a code byte and a delimiter

    size_t read_index  = 0;
    size_t write_index = 1;
    size_t code_index  = 0;
    uint8_t code = 1;

    while (read_index < length) {
        if (src[read_index] == 0) {
            dst[code_index] = code;
            code = 1;
            code_index = write_index++;          // reserve the next code byte
            if (write_index > dstCap) return 0;
            read_index++;
        } else {
            if (write_index >= dstCap) return 0;
            dst[write_index++] = src[read_index++];
            code++;
            if (code == 0xFF) {                  // block full, start a new one
                dst[code_index] = code;
                code = 1;
                code_index = write_index++;
                if (write_index > dstCap) return 0;
            }
        }
    }

    dst[code_index] = code;
    if (write_index >= dstCap) return 0;
    dst[write_index++] = FRAME_DELIMITER;
    return write_index;
}

/**
 * Decodes one COBS frame.
 *
 * @param src    Encoded bytes, without the 0x00 delimiter.
 * @param length Number of bytes in `src`.
 * @param dst    Output buffer.
 * @param dstCap Capacity of `dst`.
 *
 * @return Decoded length. 0 on a framing error:
 *         - a 0x00 inside `src`
 *         - a code byte that points past the end of `src`
 *         - `dst` too small
 */
inline size_t cobsDecode(const uint8_t* src, size_t length, uint8_t* dst, size_t dstCap) {
    size_t read_index  = 0;
    size_t write_index = 0;

    while (read_index < length) {
        const uint8_t code = src[read_index];

        // A 0x00 never appears inside valid COBS. The framing is broken.
        if (code == 0) return 0;

        // The block claims code-1 data bytes. They must fit inside `src`.
        if (read_index + code > length) return 0;

        read_index++;

        for (uint8_t i = 1; i < code; i++) {
            if (write_index >= dstCap) return 0;
            dst[write_index++] = src[read_index++];
        }

        // A block shorter than 0xFF stands for a zero byte. The last block's
        // zero is the delimiter, so it is not written.
        if (code != 0xFF && read_index != length) {
            if (write_index >= dstCap) return 0;
            dst[write_index++] = 0;
        }
    }

    return write_index;
}

/*
 * ---------------------------------------------------------
 * FRAMING API
 * ---------------------------------------------------------
 * Build and parse for both links. Everything above is what these use.
 */

enum ParseResult : uint8_t {
    PARSE_OK = 0,
    PARSE_ERR_COBS,      // bad code byte, truncated block, or scratch too small
    PARSE_ERR_SHORT,     // shorter than header + checksum
    PARSE_ERR_CHECKSUM,  // bytes corrupted
    PARSE_ERR_VERSION,   // version byte != PROTOCOL_VERSION
    PARSE_ERR_COMMAND,   // unknown CommandType or AddrType
    PARSE_ERR_LENGTH,    // payload size does not fit the command
    PARSE_ERR_TARGET,    // targetNode neither 0..71 nor GLOBAL_BROADCAST_ID
    PARSE_ERR_FIELD,     // fieldCount or fieldId above MAX_WIDGET_FIELDS
    PARSE_ERR_LAYOUT     // ADDR only: row, pos or slot outside the layout
};

// Internal. Decodes, then checks length and checksum.
// On success, *bodyLen is the decoded length without the checksum.
inline ParseResult decodeAndVerify(const uint8_t* encoded, size_t encodedLen,
                                   uint8_t* scratch, size_t scratchCap,
                                   size_t overhead, size_t* bodyLen)
{
    const size_t rawLen = cobsDecode(encoded, encodedLen, scratch, scratchCap);
    if (rawLen == 0)        return PARSE_ERR_COBS;
    if (rawLen < overhead)  return PARSE_ERR_SHORT;

    *bodyLen = rawLen - 1;
    if (calculateChecksum(scratch, *bodyLen) != scratch[*bodyLen]) return PARSE_ERR_CHECKSUM;
    return PARSE_OK;
}

// Internal. Appends the checksum to `raw` and encodes it.
inline size_t sealAndEncode(uint8_t* raw, size_t bodyLen, uint8_t* out, size_t outCap) {
    raw[bodyLen] = calculateChecksum(raw, bodyLen);
    return cobsEncode(raw, bodyLen + 1, out, outCap);
}

// --- RS485 BUS ---

struct Frame {
    ProtocolHeader header;
    const uint8_t* payload;     // points into the scratch buffer given to parseFrame
    size_t         payloadLen;  // CMD_WIDGET_UPDATE: 1 + used data bytes
};

/**
 * Builds one RS485 frame: header, payload, checksum, COBS, delimiter.
 * The version byte is always PROTOCOL_VERSION.
 *
 * @param cmd         Command. Tells the receiver how to read `payload`.
 * @param targetNode  0..71 or GLOBAL_BROADCAST_ID.
 * @param sequence    Rolling counter, 0..255.
 * @param payload     Payload bytes. May be nullptr if payloadLen is 0.
 * @param payloadLen  Bytes to send. For WidgetUpdateData only the used prefix.
 * @param out         Output buffer. MAX_ENCODED_FRAME always fits.
 * @param outCap      Capacity of `out`.
 *
 * @return Bytes written, ready to send. 0 on error.
 */
inline size_t buildFrame(CommandType cmd, uint8_t targetNode, uint8_t sequence,
                         const void* payload, size_t payloadLen,
                         uint8_t* out, size_t outCap)
{
    if (payloadLen > MAX_PAYLOAD_SIZE)        return 0;
    if (payloadLen > 0 && payload == nullptr) return 0;

    uint8_t raw[MAX_DECODED_FRAME];
    raw[0] = PROTOCOL_VERSION;
    raw[1] = uint8_t(cmd);
    raw[2] = targetNode;
    raw[3] = sequence;
    if (payloadLen > 0) {
        memcpy(raw + sizeof(ProtocolHeader), payload, payloadLen);
    }

    return sealAndEncode(raw, sizeof(ProtocolHeader) + payloadLen, out, outCap);
}

/**
 * Checks and unpacks one received RS485 frame.
 *
 * Order: COBS -> length -> checksum -> version -> command and payload size
 * -> target. The version is checked after the checksum, so a corrupt byte
 * never looks like a version mismatch.
 *
 * @param encoded     Received bytes, without the 0x00 delimiter.
 * @param encodedLen  Number of bytes in `encoded`.
 * @param scratch     Decode buffer. `out->payload` points into it, so keep it
 *                    unchanged while the frame is in use.
 * @param scratchCap  Capacity of `scratch`. MAX_DECODED_FRAME always fits.
 * @param out         Filled on PARSE_OK. On PARSE_ERR_VERSION only
 *                    `out->header.version` is set, for the error screen.
 *                    Untouched on every other error.
 *
 * @return PARSE_OK, or the first check that failed.
 */
inline ParseResult parseFrame(const uint8_t* encoded, size_t encodedLen,
                              uint8_t* scratch, size_t scratchCap,
                              Frame* out)
{
    size_t bodyLen;
    const ParseResult r = decodeAndVerify(encoded, encodedLen, scratch, scratchCap,
                                          FRAME_OVERHEAD, &bodyLen);
    if (r != PARSE_OK) return r;

    if (scratch[0] != PROTOCOL_VERSION) {
        out->header.version = scratch[0];
        return PARSE_ERR_VERSION;
    }

    const size_t  payloadLen = bodyLen - sizeof(ProtocolHeader);
    const uint8_t command    = scratch[1];

    switch (command) {
        case CMD_CLOCK_INIT:
            if (payloadLen != sizeof(ClockInitData))     return PARSE_ERR_LENGTH;
            break;
        case CMD_WIDGET_INIT:
            if (payloadLen != sizeof(WidgetInitData))    return PARSE_ERR_LENGTH;
            if (scratch[sizeof(ProtocolHeader) + offsetof(WidgetInitData, fieldCount)] > MAX_WIDGET_FIELDS) return PARSE_ERR_FIELD;
            break;
        case CMD_CLOCK_UPDATE:
            if (payloadLen != sizeof(GlobalClockUpdate)) return PARSE_ERR_LENGTH;
            break;
        case CMD_WIDGET_UPDATE:
            // At least a fieldId, at most a fieldId plus a full data block.
            if (payloadLen < 1 || payloadLen > 1 + MAX_WIDGET_DATA) return PARSE_ERR_LENGTH;
            if (scratch[sizeof(ProtocolHeader)] >= MAX_WIDGET_FIELDS) return PARSE_ERR_FIELD;    // fieldId
            break;
        case CMD_DIAGNOSTIC:
            if (payloadLen != sizeof(DiagnosticData))    return PARSE_ERR_LENGTH;
            break;
        case CMD_DISPLAY:
            if (payloadLen != sizeof(DisplayData))       return PARSE_ERR_LENGTH;
            break;
        default:
            return PARSE_ERR_COMMAND;
    }

    const uint8_t targetNode = scratch[2];
    if (targetNode >= TOTAL_MATRIX_NODES && targetNode != GLOBAL_BROADCAST_ID) {
        return PARSE_ERR_TARGET;
    }

    out->header.version    = scratch[0];
    out->header.command    = command;
    out->header.targetNode = targetNode;
    out->header.sequence   = scratch[3];
    out->payload           = scratch + sizeof(ProtocolHeader);
    out->payloadLen        = payloadLen;
    return PARSE_OK;
}

// --- ADDR CHAIN ---

// A checked ADDR frame. TRUNK and ROW are resolved to the receiving node's
// address. ADDR_NONE has only `type` set, everything else is 0.
struct AddrFrame {
    uint8_t        type;        // AddrType
    uint8_t        row;
    uint8_t        pos;         // 0 for ADDR_TRUNK
    uint8_t        slot;        // ADDR_TRUNK: sum of rowLengths[0..row-1]
    uint8_t        rowLength;
    uint8_t        rowCount;    // ADDR_TRUNK only, 0 for ADDR_ROW
    const uint8_t* rowLengths;  // ADDR_TRUNK only, points into scratch. nullptr for ADDR_ROW
};

/**
 * Builds an ADDR_TRUNK frame. Sends only the first rowCount entries of rowLengths.
 *
 * @param out     Output buffer. MAX_ADDR_ENCODED_FRAME always fits.
 * @return Bytes written, ready to send. 0 if rowCount is 0 or > MAX_ADDR_ROWS,
 *         or `out` is too small.
 */
inline size_t buildAddrTrunk(const AddrTrunkData& trunk, uint8_t* out, size_t outCap) {
    if (trunk.rowCount == 0 || trunk.rowCount > MAX_ADDR_ROWS) return 0;

    uint8_t raw[MAX_ADDR_DECODED_FRAME];
    raw[0] = PROTOCOL_VERSION;
    raw[1] = ADDR_TRUNK;
    const size_t payloadLen = 2 + trunk.rowCount;
    memcpy(raw + sizeof(AddrHeader), &trunk, payloadLen);

    return sealAndEncode(raw, sizeof(AddrHeader) + payloadLen, out, outCap);
}

/**
 * Builds an ADDR_ROW frame.
 *
 * @param out     Output buffer. MAX_ADDR_ENCODED_FRAME always fits.
 * @return Bytes written, ready to send. 0 if `out` is too small.
 */
inline size_t buildAddrRow(const AddrRowData& row, uint8_t* out, size_t outCap) {
    uint8_t raw[MAX_ADDR_DECODED_FRAME];
    raw[0] = PROTOCOL_VERSION;
    raw[1] = ADDR_ROW;
    memcpy(raw + sizeof(AddrHeader), &row, sizeof(row));

    return sealAndEncode(raw, sizeof(AddrHeader) + sizeof(row), out, outCap);
}

/**
 * Builds an ADDR_NONE frame. Sent instead of TRUNK or ROW by a node without a
 * valid address, so the nodes behind it do not keep an outdated one.
 *
 * @param out     Output buffer. MAX_ADDR_ENCODED_FRAME always fits.
 * @return Bytes written, ready to send. 0 if `out` is too small.
 */
inline size_t buildAddrNone(uint8_t* out, size_t outCap) {
    uint8_t raw[ADDR_OVERHEAD];
    raw[0] = PROTOCOL_VERSION;
    raw[1] = ADDR_NONE;
    return sealAndEncode(raw, sizeof(AddrHeader), out, outCap);
}

/**
 * Checks one received ADDR frame and resolves the receiving node's address.
 *
 * Order: COBS -> length -> checksum -> version -> type and payload size
 * -> layout. PARSE_ERR_LAYOUT when:
 *   ADDR_TRUNK  rowCount is 0, a row length is 0, the lengths add up to more
 *               than TOTAL_MATRIX_NODES, or row >= rowCount
 *   ADDR_ROW    pos >= rowLength, or slot >= TOTAL_MATRIX_NODES
 *
 * @param encoded     Received bytes, without the 0x00 delimiter.
 * @param encodedLen  Number of bytes in `encoded`.
 * @param scratch     Decode buffer. `out->rowLengths` points into it.
 * @param scratchCap  Capacity of `scratch`. MAX_ADDR_DECODED_FRAME always fits.
 * @param out         Filled on PARSE_OK. Untouched on any error.
 *
 * @return PARSE_OK, or the first check that failed.
 */
inline ParseResult parseAddrFrame(const uint8_t* encoded, size_t encodedLen,
                                  uint8_t* scratch, size_t scratchCap,
                                  AddrFrame* out)
{
    size_t bodyLen;
    const ParseResult r = decodeAndVerify(encoded, encodedLen, scratch, scratchCap,
                                          ADDR_OVERHEAD, &bodyLen);
    if (r != PARSE_OK) return r;

    if (scratch[0] != PROTOCOL_VERSION) return PARSE_ERR_VERSION;

    const size_t   payloadLen = bodyLen - sizeof(AddrHeader);
    const uint8_t* p          = scratch + sizeof(AddrHeader);

    switch (scratch[1]) {
        case ADDR_TRUNK: {
            if (payloadLen < 2) return PARSE_ERR_LENGTH;
            const uint8_t row      = p[0];
            const uint8_t rowCount = p[1];
            if (rowCount == 0 || rowCount > MAX_ADDR_ROWS) return PARSE_ERR_LAYOUT;
            if (payloadLen != size_t(2) + rowCount)        return PARSE_ERR_LENGTH;
            if (row >= rowCount)                           return PARSE_ERR_LAYOUT;

            const uint8_t* lengths = p + 2;
            unsigned total = 0;
            unsigned slot  = 0;
            for (uint8_t i = 0; i < rowCount; i++) {
                if (lengths[i] == 0) return PARSE_ERR_LAYOUT;
                if (i == row) slot = total;
                total += lengths[i];
            }
            if (total > TOTAL_MATRIX_NODES) return PARSE_ERR_LAYOUT;

            out->type       = ADDR_TRUNK;
            out->row        = row;
            out->pos        = 0;
            out->slot       = uint8_t(slot);
            out->rowLength  = lengths[row];
            out->rowCount   = rowCount;
            out->rowLengths = lengths;
            return PARSE_OK;
        }
        case ADDR_ROW: {
            if (payloadLen != sizeof(AddrRowData)) return PARSE_ERR_LENGTH;
            const uint8_t row = p[0], pos = p[1], slot = p[2], rowLength = p[3];
            if (pos >= rowLength)           return PARSE_ERR_LAYOUT;
            if (slot >= TOTAL_MATRIX_NODES) return PARSE_ERR_LAYOUT;

            out->type       = ADDR_ROW;
            out->row        = row;
            out->pos        = pos;
            out->slot       = slot;
            out->rowLength  = rowLength;
            out->rowCount   = 0;
            out->rowLengths = nullptr;
            return PARSE_OK;
        }
        case ADDR_NONE: {
            if (payloadLen != 0) return PARSE_ERR_LENGTH;
            *out = AddrFrame{};
            out->type = ADDR_NONE;
            return PARSE_OK;
        }
        default:
            return PARSE_ERR_COMMAND;
    }
}

/** Readable name of a ParseResult, for the USB report. */
inline const char* parseResultName(ParseResult r) {
    switch (r) {
        case PARSE_OK:           return "OK";
        case PARSE_ERR_COBS:     return "COBS framing error";
        case PARSE_ERR_SHORT:    return "frame too short";
        case PARSE_ERR_CHECKSUM: return "checksum mismatch";
        case PARSE_ERR_VERSION:  return "protocol version mismatch";
        case PARSE_ERR_COMMAND:  return "unknown command";
        case PARSE_ERR_LENGTH:   return "payload length mismatch";
        case PARSE_ERR_TARGET:   return "invalid target node";
        case PARSE_ERR_FIELD:    return "widget field out of range";
        case PARSE_ERR_LAYOUT:   return "address outside layout";
    }
    return "unknown";
}

}  // namespace protocol
