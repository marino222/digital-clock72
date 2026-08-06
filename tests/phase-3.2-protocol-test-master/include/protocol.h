#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>   // memcpy

/*
 * ---------------------------------------------------------------------------
 * RS485 STATE PROTOCOL
 * ---------------------------------------------------------------------------
 * One master broadcasts to 72 slave nodes over a single RS485 bus.
 *
 * A frame on the wire looks like this:
 *
 *   [command][targetNode][sequence][ ...payload... ][xorChecksum]
 *      0          1          2         3 .. N-1          N
 *      |______ header ______|                            |
 *                                                  covers bytes 0..N-1
 *
 * The frame is COBS-encoded and terminated with a single 0x00 delimiter.
 *
 * Design Notes:
 * - No explicit length field is transmitted. COBS framing implicitly defines 
 *   the payload length: (decoded length - 3 header bytes - 1 checksum byte).
 * - The checksum is placed at the end so it consistently represents the last 
 *   byte of the decoded payload, regardless of the payload's size.
 * ---------------------------------------------------------------------------
 */

/*
 * Packed structs are memcpy'd straight onto the wire, so both ends must agree
 * on byte order. Since the master and the slave code run on different hardware,
 * we enforce little-endian order at compile time for safety reasons.
 */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "protocol.h memcpy's packed structs onto the wire; little-endian target required."
#endif

/*
 * ---------------------------------------------------------
 * PROTOCOL CONSTANTS
 * ---------------------------------------------------------
 */

// Not transmitted. This file is duplicated into the master and slave projects.
constexpr uint8_t PROTOCOL_VERSION    = 1;

constexpr uint8_t FRAME_DELIMITER     = 0x00;
constexpr uint8_t GLOBAL_BROADCAST_ID = 0xFF; // targetNode value to address all nodes at once
constexpr uint8_t TOTAL_MATRIX_NODES  = 72;

// Largest widget payload we can carry today. Custom widgets are a future goal
// and their real sizes are still unknown, so this is a ceiling, not a promise:
// only the bytes actually used are transmitted (see WidgetUpdateData).
constexpr size_t  MAX_WIDGET_DATA     = 32;

/*
 * ---------------------------------------------------------
 * COMMAND ROUTING
 * ---------------------------------------------------------
 * Init commands carry more data and are only sent once. Update commands
 * are much smaller and sent repeatedly.
 */
enum CommandType : uint8_t {
    CMD_CLOCK_INIT    = 0x01,
    CMD_WIDGET_INIT   = 0x02,
    CMD_CLOCK_UPDATE  = 0x03,
    CMD_WIDGET_UPDATE = 0x04
};

/*
 * ---------------------------------------------------------
 * PACKED STRUCTURES
 * ---------------------------------------------------------
 * #pragma pack(push, 1) serves two critical purposes here:
 * 1. Wire Layout: Prevents the compiler from inserting hidden padding, 
 *    ensuring the in-memory struct perfectly matches the byte layout on the wire.
 * 2. Memory Alignment: Forces the compiler to emit safe, byte-wise accesses. 
 *    Since parseFrame() returns a payload pointer at an unaligned offset (byte 3), 
 *    this prevents hard alignment faults on architectures like the RP2040's 
 *    Cortex-M0+, which cannot handle unaligned halfword/word loads natively.
 */
#pragma pack(push, 1)

// Attached to EVERY packet. 3 bytes.
struct ProtocolHeader {
    uint8_t command;       // CommandType
    uint8_t targetNode;    // 0..71 for a specific node, 0xFF for all
    uint8_t sequence;      // rolling 0..255, so gaps reveal dropped packets
};
static_assert(sizeof(ProtocolHeader) == 3, "ProtocolHeader must be 3 bytes");

// --- CLOCK PAYLOADS ---

// One node's worth of clock state.
struct ClockNodeData {
    uint16_t angle1DeciDeg; // Hand 1: 0..3599
    uint16_t angle2DeciDeg; // Hand 2: 0..3599
};
static_assert(sizeof(ClockNodeData) == 4, "ClockNodeData must be 4 bytes");

// Payload for CMD_CLOCK_UPDATE. Every node's state in ONE broadcast, so all 72
// displays step at the same instant and the per-packet overhead is paid once
// rather than 72 times. Each slave reads only nodes[its own address].
struct GlobalClockUpdate {
    ClockNodeData nodes[TOTAL_MATRIX_NODES];
};
static_assert(sizeof(GlobalClockUpdate) == 288, "GlobalClockUpdate must be 72 * 4 bytes");

// Payload for CMD_CLOCK_INIT: the config a node needs before it can render.
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

// Payload for CMD_WIDGET_INIT.
struct WidgetInitData {
    uint8_t  widgetType;
    uint16_t bgColor;        // RGB565
    uint8_t  fieldCount;     // how many addressable fields this widget has
    uint8_t  reserved[2];    // room to grow without a version bump
};
static_assert(sizeof(WidgetInitData) == 6, "WidgetInitData must be 6 bytes");

// Payload for CMD_WIDGET_UPDATE.
//
// This is a STORAGE type, not a fixed wire type. Widget payload sizes are not
// known yet, so `data` is sized to the worst case but only the used prefix goes
// on the wire: the sender passes payloadLen = 1 + usedBytes to buildFrame(),
// and the receiver recovers usedBytes as (frame.payloadLen - 1). No length byte
// is needed -- COBS already told us where the frame ended.
struct WidgetUpdateData {
    uint8_t fieldId;                // which element of the widget to update
    uint8_t data[MAX_WIDGET_DATA];  // only the first (payloadLen - 1) bytes are sent
};
static_assert(sizeof(WidgetUpdateData) == 1 + MAX_WIDGET_DATA,
              "WidgetUpdateData must be fieldId + MAX_WIDGET_DATA bytes");

#pragma pack(pop)

/*
 * ---------------------------------------------------------
 * BUFFER SIZING
 * ---------------------------------------------------------
 * Callers need these to size their own buffers. Re-deriving them by hand at
 * each call site is how buffer overruns get written.
 */
constexpr size_t FRAME_OVERHEAD    = sizeof(ProtocolHeader) + 1;         // header + checksum
constexpr size_t MAX_PAYLOAD_SIZE  = sizeof(GlobalClockUpdate);          // 288
constexpr size_t MAX_DECODED_FRAME = FRAME_OVERHEAD + MAX_PAYLOAD_SIZE;  // 292

// COBS worst case: one extra code byte per 254 bytes of data, plus the leading
// code byte, plus the trailing delimiter.
constexpr size_t MAX_ENCODED_FRAME = MAX_DECODED_FRAME + MAX_DECODED_FRAME / 254 + 2;  // 295

/*
 * ---------------------------------------------------------
 * INTEGRITY VERIFICATION
 * ---------------------------------------------------------
 * Extremely fast 1-byte check. CRC-8 would catch more, but XOR is enough for a
 * short, electrically quiet bus and costs almost nothing per frame.
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
 * COBS ENCODING & DECODING
 * ---------------------------------------------------------
 * COBS rewrites the data so it contains no 0x00 bytes at all, which frees 0x00
 * to be an unambiguous end-of-frame marker. A single 0x00 therefore both ends
 * the current frame and starts the next one, and a receiver that gets lost can
 * always resynchronise by discarding bytes until it sees one.
 *
 * Both functions take the destination capacity and return 0 rather than
 * overrunning it. Neither writes to dst beyond the returned length.
 */
 
/**
 * Encodes data into a COBS frame and appends the FRAME_DELIMITER.
 *
 * COBS replaces every 0x00 byte in the source with a "code byte" indicating 
 * the distance to the next zero. This ensures 0x00 never appears in the payload, 
 * allowing it to act as an unambiguous end-of-frame marker.
 *
 * @param src    Raw bytes to encode. May contain 0x00. Read-only.
 * @param length Number of bytes from `src` to encode.
 * @param dst    Caller-owned destination buffer. Written to directly.
 * @param dstCap Capacity of `dst` in bytes. Used purely as a safety ceiling 
 *               for bounds checking. Sizing this to >= MAX_ENCODED_FRAME 
 *               guarantees success for any valid protocol frame.
 *
 * @return Bytes actually written to `dst` (including the 0x00 delimiter), 
 *         or 0 if dstCap is insufficient to hold the result.
 */
inline size_t cobsEncode(const uint8_t* src, size_t length, uint8_t* dst, size_t dstCap) {
    if (dstCap < 2) return 0;   // no room for even a code byte and a delimiter

    size_t read_index  = 0;
    size_t write_index = 1;
    size_t code_index  = 0;
    uint8_t code = 1;

    while (read_index < length) {
        if (src[read_index] == 0) {
            dst[code_index] = code;
            code = 1;
            code_index = write_index++;          // reserve the next code slot
            if (write_index > dstCap) return 0;
            read_index++;
        } else {
            if (write_index >= dstCap) return 0;
            dst[write_index++] = src[read_index++];
            code++;
            if (code == 0xFF) {                  // block is full, start a new one
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
 * Decodes a COBS frame back into raw bytes.
 *
 * Processes `src` one block at a time. Each block begins with a code byte 
 * indicating the number of literal data bytes that follow, before the next 
 * implied zero. This function extracts those data bytes and reinserts the 
 * missing zeros.
 *
 * @param src      The encoded payload. Must NOT include the 0x00 frame 
 *                 delimiters (the caller's receive logic must strip them prior 
 *                 to calling). Read-only.
 * @param length   Number of valid encoded bytes contained in `src`.
 * @param dst      Caller-owned output buffer for the decoded bytes.
 * @param dstCap   Capacity of `dst` in bytes. Used for strict bounds checking. 
 *                 Should be >= MAX_DECODED_FRAME to guarantee success.
 *
 * @return The decoded payload length (number of bytes written to `dst`). 
 *         Returns 0 on any framing error, specifically:
 *         - An unexpected 0x00 byte is encountered in the encoded data.
 *         - A code block claims more bytes than remain in `src`.
 *         - `dstCap` is insufficient to hold the decoded result.
 */
inline size_t cobsDecode(const uint8_t* src, size_t length, uint8_t* dst, size_t dstCap) {
    size_t read_index  = 0;
    size_t write_index = 0;

    while (read_index < length) {
        const uint8_t code = src[read_index];

        // 0x00 can never appear inside a valid COBS frame -- seeing one means
        // the framing is broken, not that a zero-length block arrived.
        if (code == 0) return 0;

        // A block of `code` claims code-1 data bytes after itself; reject it if
        // reading them would run past the end of the frame.
        if (read_index + code > length) return 0;

        read_index++;

        for (uint8_t i = 1; i < code; i++) {
            if (write_index >= dstCap) return 0;
            dst[write_index++] = src[read_index++];
        }

        // A block shorter than 0xFF stood in for a zero byte -- unless it was
        // the final block, where that zero is the frame delimiter itself.
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
 * The entire public surface: build one, parse one, and an enum saying why a
 * parse failed. Everything above is machinery these two functions drive.
 */

enum ParseResult : uint8_t {
    PARSE_OK = 0,
    PARSE_ERR_COBS,      // bad code byte, truncated block, or decode buffer too small
    PARSE_ERR_SHORT,     // decoded frame too small to hold even a header + checksum
    PARSE_ERR_CHECKSUM,  // survived framing but the bytes are corrupt
    PARSE_ERR_COMMAND,   // command byte is not a known CommandType
    PARSE_ERR_LENGTH,    // payload size does not match what the command requires
    PARSE_ERR_TARGET     // targetNode is neither 0..71 nor GLOBAL_BROADCAST_ID
};

struct Frame {
    ProtocolHeader header;
    const uint8_t* payload;     // points INTO the scratch buffer passed to parseFrame
    size_t         payloadLen;  // for CMD_WIDGET_UPDATE this is 1 + usedDataBytes
};

/**
 * Builds header + payload + checksum, COBS-encodes it, and appends the delimiter.
 *
 * @param cmd         Command identifier determining how the receiver interprets `payload`.
 * @param targetNode  Specific node ID (0..71) or GLOBAL_BROADCAST_ID (0xFF).
 * @param sequence    Rolling 0..255 sequence counter to detect dropped packets.
 * @param payload     Pointer to payload data (may be nullptr if payloadLen is 0).
 * @param payloadLen  Actual bytes of `payload` to transmit. For variable storage 
 *                    types like WidgetUpdateData, this should only be the used prefix.
 * @param out         Caller-owned destination buffer for the fully encoded frame.
 * @param outCap      Capacity of `out`. Used for bounds checking.
 *
 * @return Total bytes written to `out` (ready for transmission), or 0 on error.
 */
inline size_t buildFrame(CommandType cmd, uint8_t targetNode, uint8_t sequence,
                         const void* payload, size_t payloadLen,
                         uint8_t* out, size_t outCap)
{
    if (payloadLen > MAX_PAYLOAD_SIZE)        return 0;
    if (payloadLen > 0 && payload == nullptr) return 0;

    uint8_t raw[MAX_DECODED_FRAME];

    raw[0] = uint8_t(cmd);
    raw[1] = targetNode;
    raw[2] = sequence;
    if (payloadLen > 0) {
        memcpy(raw + sizeof(ProtocolHeader), payload, payloadLen);
    }

    const size_t bodyLen = sizeof(ProtocolHeader) + payloadLen;  // what the checksum covers
    raw[bodyLen] = calculateChecksum(raw, bodyLen);

    return cobsEncode(raw, bodyLen + 1, out, outCap);
}

/**
 * Validates and unpacks one received frame.
 *
 * Validation is strictly layered: COBS framing -> minimum length -> 
 * XOR checksum -> command/payload matching -> target node validation.
 *
 * @param encoded     COBS-encoded bytes received (excluding the 0x00 delimiter).
 * @param encodedLen  Number of valid bytes in `encoded`.
 * @param scratch     Decode workspace buffer. IMPORTANT: `out->payload` points 
 *                    directly into this buffer upon success, so `scratch` must 
 *                    remain alive and unmodified while the parsed frame is in use.
 * @param scratchCap  Capacity of `scratch`.
 * @param out         Populated with the parsed header and a payload pointer/length. 
 *                    Left untouched if parsing fails.
 *
 * @return PARSE_OK on success, or a specific PARSE_ERR_* identifying the failure point.
 */
inline ParseResult parseFrame(const uint8_t* encoded, size_t encodedLen,
                              uint8_t* scratch, size_t scratchCap,
                              Frame* out)
{
    const size_t rawLen = cobsDecode(encoded, encodedLen, scratch, scratchCap);
    if (rawLen == 0)             return PARSE_ERR_COBS;
    if (rawLen < FRAME_OVERHEAD) return PARSE_ERR_SHORT;

    const size_t bodyLen = rawLen - 1;  // everything except the trailing checksum
    if (calculateChecksum(scratch, bodyLen) != scratch[bodyLen]) {
        return PARSE_ERR_CHECKSUM;
    }

    const size_t  payloadLen = bodyLen - sizeof(ProtocolHeader);
    const uint8_t command    = scratch[0];

    switch (command) {
        case CMD_CLOCK_INIT:
            if (payloadLen != sizeof(ClockInitData))     return PARSE_ERR_LENGTH;
            break;
        case CMD_WIDGET_INIT:
            if (payloadLen != sizeof(WidgetInitData))    return PARSE_ERR_LENGTH;
            break;
        case CMD_CLOCK_UPDATE:
            if (payloadLen != sizeof(GlobalClockUpdate)) return PARSE_ERR_LENGTH;
            break;
        case CMD_WIDGET_UPDATE:
            // At least a fieldId, at most a fieldId plus a full data block.
            if (payloadLen < 1 || payloadLen > 1 + MAX_WIDGET_DATA) return PARSE_ERR_LENGTH;
            break;
        default:
            return PARSE_ERR_COMMAND;
    }

    const uint8_t targetNode = scratch[1];
    if (targetNode >= TOTAL_MATRIX_NODES && targetNode != GLOBAL_BROADCAST_ID) {
        return PARSE_ERR_TARGET;
    }

    out->header.command    = command;
    out->header.targetNode = targetNode;
    out->header.sequence   = scratch[2];
    out->payload           = scratch + sizeof(ProtocolHeader);
    out->payloadLen        = payloadLen;
    return PARSE_OK;
}

/** Human-readable name for a ParseResult, for serial logging. */
inline const char* parseResultName(ParseResult r) {
    switch (r) {
        case PARSE_OK:           return "OK";
        case PARSE_ERR_COBS:     return "COBS framing error";
        case PARSE_ERR_SHORT:    return "frame too short";
        case PARSE_ERR_CHECKSUM: return "checksum mismatch";
        case PARSE_ERR_COMMAND:  return "unknown command";
        case PARSE_ERR_LENGTH:   return "payload length mismatch";
        case PARSE_ERR_TARGET:   return "invalid target node";
    }
    return "unknown";
}
