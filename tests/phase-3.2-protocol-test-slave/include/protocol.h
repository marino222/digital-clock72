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
 * ...and that whole thing is then COBS-encoded and followed by a single 0x00:
 *
 *   [ COBS( header + payload + checksum ) ][ 0x00 ]
 *
 * There is deliberately NO length field. COBS framing already tells the
 * receiver where the frame ends, so the payload length is simply
 * (decoded length - 3 header bytes - 1 checksum byte). Sending a length as
 * well would just be two wasted bytes on every packet.
 *
 * The checksum sits at the END rather than in the header so that its position
 * is always "the last byte of the decoded frame" -- derivable without first
 * knowing which command, and therefore which payload size, came in.
 *
 * Nothing here should be assembled by hand. buildFrame() and parseFrame() at
 * the bottom of this file own the layout above; they are the only two
 * functions a caller needs.
 * ---------------------------------------------------------------------------
 */

/*
 * Packed structs are memcpy'd straight onto the wire, so both ends must agree
 * on byte order. Every node is an RP2040, so this is a safety net rather than
 * a real portability concern -- but a silent assumption is worse than a loud one.
 */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "protocol.h memcpy's packed structs onto the wire; little-endian target required."
#endif

/*
 * ---------------------------------------------------------
 * PROTOCOL CONSTANTS
 * ---------------------------------------------------------
 */

// Not transmitted. This file is duplicated into the master and slave projects;
// bump this on any wire-format change so a stale copy is obvious in a diff.
constexpr uint8_t PROTOCOL_VERSION    = 1;

constexpr uint8_t FRAME_DELIMITER     = 0x00;
constexpr uint8_t GLOBAL_BROADCAST_ID = 0xFF;
constexpr uint8_t TOTAL_MATRIX_NODES  = 72;

// Largest widget payload we can carry today. Custom widgets are a future goal
// and their real sizes are still unknown, so this is a ceiling, not a promise:
// only the bytes actually used are transmitted (see WidgetUpdateData).
constexpr size_t  MAX_WIDGET_DATA     = 32;

/*
 * ---------------------------------------------------------
 * COMMAND ROUTING
 * ---------------------------------------------------------
 * Init commands are heavy and rare: the master halts all other bus traffic to
 * push them. Update commands are light and continuous (60 Hz).
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
 * #pragma pack(push, 1) stops the compiler inserting hidden padding bytes, so
 * the in-memory layout is exactly the wire layout.
 *
 * Packing to 1 also has a second, less obvious job: it tells the compiler these
 * structs may live at any address. parseFrame() hands back a payload pointer at
 * offset 3 of a byte buffer, which is not 2-byte aligned, and the RP2040's
 * Cortex-M0+ faults on unaligned halfword loads. Because the structs are packed,
 * the compiler emits byte-wise accesses instead, and the cast is safe.
 *
 * Every struct below is followed by a static_assert on its size. Master and
 * slave each compile their own copy of this file, and a layout mismatch between
 * them is the failure mode that is hardest to spot in a serial log -- these turn
 * it into a build error on both sides instead.
 */
#pragma pack(push, 1)

// Attached to EVERY packet. 3 bytes.
struct ProtocolHeader {
    uint8_t command;       // CommandType
    uint8_t targetNode;    // 0..71 for a specific node, 0xFF for all
    uint8_t sequence;      // rolling 0..255, so gaps reveal dropped packets
};
static_assert(sizeof(ProtocolHeader) == 3, "ProtocolHeader must be 3 bytes on the wire");

// --- CLOCK PAYLOADS ---

// One node's worth of clock state.
struct ClockNodeData {
    uint16_t angle1DeciDeg; // Hand 1: 0..3599
    uint16_t angle2DeciDeg; // Hand 2: 0..3599
};
static_assert(sizeof(ClockNodeData) == 4, "ClockNodeData must be 4 bytes on the wire");

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
static_assert(sizeof(ClockInitData) == 10, "ClockInitData must be 10 bytes on the wire");

// --- WIDGET PAYLOADS ---

// Payload for CMD_WIDGET_INIT.
struct WidgetInitData {
    uint8_t  widgetType;
    uint16_t bgColor;        // RGB565
    uint8_t  fieldCount;     // how many addressable fields this widget has
    uint8_t  reserved[2];    // room to grow without a version bump
};
static_assert(sizeof(WidgetInitData) == 6, "WidgetInitData must be 6 bytes on the wire");

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
 * Encodes data into a COBS frame and appends FRAME_DELIMITER.
 * @return bytes written to dst (delimiter included), or 0 if dst is too small.
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
 * @param src the bytes BETWEEN two delimiters (the 0x00 itself is not included).
 * @return the decoded length, or 0 on any framing error.
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
 * Builds header + payload + checksum, COBS-encodes it, appends the delimiter.
 *
 * @param payloadLen bytes of `payload` to actually transmit. For a fixed-size
 *        payload this is sizeof(that struct); for a widget update it is
 *        1 + however many data bytes are in use.
 * @param outCap should be at least MAX_ENCODED_FRAME.
 * @return bytes written to out (delimiter included), or 0 on error.
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
 * Checks run most-fundamental first: framing, then size, then integrity, then
 * meaning. The payload-length check matters more than it looks -- it is what
 * makes the caller's cast to a payload struct safe. Without it, a corrupted
 * command byte could reinterpret a 33-byte widget frame as a 288-byte
 * GlobalClockUpdate and read 255 bytes off the end of the buffer.
 *
 * @param encoded bytes received between two delimiters (delimiter NOT included).
 * @param scratch caller-owned decode buffer, at least MAX_DECODED_FRAME bytes.
 *        out->payload points into it, so it must outlive the returned Frame.
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
