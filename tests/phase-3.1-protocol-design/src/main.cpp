#include <stdio.h>
#include <string.h>
#include "protocol.h"

/*
 * ---------------------------------------------------------------------------
 * FRAME INSPECTOR
 * ---------------------------------------------------------------------------
 * A development tool, NOT a test. It never asserts and always runs to
 * completion -- test/test_protocol.cpp stays the thing that passes or fails.
 *
 * The unit tests answer "is the protocol correct?". This answers a different
 * question: given a command, a target node and a payload, what exactly goes on
 * the wire, what does each layer add, and what does the slave get back out?
 *
 * buildFrame() deliberately fuses raw assembly and COBS encoding into one call,
 * so those intermediate stages are invisible through the public API. This walks
 * the same pipeline one stage at a time, using the same protocol.h helpers the
 * real master and slave use.
 *
 * Run it with:
 *   pio run -d ./tests/phase-3.1-protocol-design -e native -t exec
 * ---------------------------------------------------------------------------
 */

/* ===========================================================================
 * CONFIG -- this is the part meant to be edited
 * ===========================================================================
 */

// Which frame to build. The payload is filled by the matching fill*() function
// further down; edit the field values there.
constexpr CommandType CMD         = CMD_WIDGET_UPDATE;

// 0..71 for one specific slave, or GLOBAL_BROADCAST_ID (0xFF) for all of them.
constexpr uint8_t     TARGET_NODE = 0x01;

// The master's rolling packet counter. Set FRAME_COUNT above 1 to send several
// frames in a row and watch the counter roll over at 255.
constexpr uint8_t     START_SEQ   = 42;
constexpr uint16_t    FRAME_COUNT = 1;

// Optional corruption, applied to the finished wire bytes so you can watch
// which layer catches it. Which error fires depends on WHAT you hit: a COBS
// code byte dies at the decoder, a literal data byte survives to the checksum.
// Stage 3 marks which bytes are which.
enum FaultMode { FAULT_NONE, FAULT_FLIP_BYTE, FAULT_TRUNCATE };
constexpr FaultMode   FAULT        = FAULT_NONE;
constexpr size_t      FAULT_OFFSET = 4;     // index into the wire bytes
constexpr uint8_t     FAULT_MASK   = 0xFF;  // XORed into that byte
constexpr size_t      TRUNCATE_TO  = 8;     // wire bytes kept when truncating

// A CLOCK_UPDATE carries all 72 nodes, which is far too much to read as hex.
// Only this many nodes are printed in full.
constexpr uint8_t     SHOW_NODES   = 1;

// Bus parameters, matching phase-3.2's pin_definitions.h and the 8N1 UART
// framing measured in phase 2.1.
constexpr long        BAUD_RATE       = 1000000;
constexpr int         BITS_PER_BYTE   = 10;      // 1 start + 8 data + 1 stop
constexpr double      BUS_CEILING_BPS = 100000;  // ~100 kB/s, from phase 2.1
constexpr double      UPDATE_RATE_HZ  = 60;

// --- Payload builders -------------------------------------------------------
// These mirror how the real master builds its payloads
// (see tests/phase-3.2-protocol-test-master/src/main.cpp).

static size_t fillClockInit(uint8_t* dst) {
    ClockInitData d{};
    d.hand1Color     = 0xF800;   // RGB565 red
    d.hand2Color     = 0xFFFF;   // RGB565 white
    d.bgColor        = 0x0000;   // black
    d.hand1LengthPct = 60;
    d.hand2LengthPct = 90;
    d.hand1Thickness = 8;
    d.hand2Thickness = 4;
    memcpy(dst, &d, sizeof(d));
    return sizeof(d);
}

static size_t fillWidgetInit(uint8_t* dst) {
    WidgetInitData d{};
    d.widgetType = 1;
    d.bgColor    = 0x001F;       // RGB565 blue
    d.fieldCount = 3;
    memcpy(dst, &d, sizeof(d));
    return sizeof(d);
}

static size_t fillClockUpdate(uint8_t* dst) {
    GlobalClockUpdate d{};
    // Same idea as the master: give every node its own slice of a revolution,
    // so it is obvious each slave reads ITS OWN slot rather than slot 0.
    const uint16_t phase = 3600 / TOTAL_MATRIX_NODES;
    for (uint8_t n = 0; n < TOTAL_MATRIX_NODES; n++) {
        d.nodes[n].angle1DeciDeg = uint16_t((n * phase) % 3600);
        d.nodes[n].angle2DeciDeg = uint16_t((1800 + n * phase) % 3600);
    }
    memcpy(dst, &d, sizeof(d));
    return sizeof(d);
}

static size_t fillWidgetUpdate(uint8_t* dst) {
    WidgetUpdateData d{};
    d.fieldId = 1;

    const char* text = "hello";
    const size_t textLen = strlen(text);
    memcpy(d.data, text, textLen);

    memcpy(dst, &d, sizeof(d));
    return 1 + textLen;          // only the used prefix goes on the wire
}

/* ===========================================================================
 * Everything below is the inspector itself
 * ===========================================================================
 */

static const int LINE_WIDTH = 74;

static uint8_t payloadBuf[MAX_PAYLOAD_SIZE];
static uint8_t rawFrame[MAX_DECODED_FRAME];
static uint8_t wire[MAX_ENCODED_FRAME];
static uint8_t rxBuf[MAX_ENCODED_FRAME];
static uint8_t scratch[MAX_DECODED_FRAME];
static bool    codeByte[MAX_ENCODED_FRAME];

// Wire length of the most recent frame, for the multi-frame summary table.
static size_t  lastWireLen = 0;

// ---------------------------------------------------------------------------
// Output helpers
// ---------------------------------------------------------------------------

static void banner(const char* title) {
    printf("\n");
    int n = printf("== %s ", title);
    for (; n < LINE_WIDTH; n++) putchar('=');
    printf("\n");
}

static void divider() {
    for (int i = 0; i < LINE_WIDTH; i++) putchar('-');
    printf("\n");
}

// Hex dump with offsets and an ASCII column. If `mark` is given, a caret line
// is printed under any row containing marked bytes. Output is elided after
// `maxBytes` so a 288-byte payload does not bury the report.
static void hexDump(const uint8_t* data, size_t len, const bool* mark, size_t maxBytes) {
    const size_t shown = (len < maxBytes) ? len : maxBytes;

    for (size_t off = 0; off < shown; off += 16) {
        const size_t n = (shown - off < 16) ? shown - off : 16;

        printf("  %04zX  ", off);
        for (size_t i = 0; i < 16; i++) {
            if (i == 8) putchar(' ');
            if (i < n) printf("%02X ", data[off + i]);
            else       printf("   ");
        }
        printf(" |");
        for (size_t i = 0; i < n; i++) {
            const uint8_t c = data[off + i];
            putchar((c >= 0x20 && c < 0x7F) ? c : '.');
        }
        printf("|\n");

        if (!mark) continue;

        bool any = false;
        for (size_t i = 0; i < n; i++) if (mark[off + i]) any = true;
        if (!any) continue;

        printf("        ");
        for (size_t i = 0; i < n; i++) {
            if (i == 8) putchar(' ');
            printf("%s", mark[off + i] ? "^^ " : "   ");
        }
        printf("\n");
    }

    if (shown < len) {
        printf("  ....  (%zu more bytes not shown)\n", len - shown);
    }
}

static const char* commandName(uint8_t cmd) {
    switch (cmd) {
        case CMD_CLOCK_INIT:    return "CMD_CLOCK_INIT";
        case CMD_WIDGET_INIT:   return "CMD_WIDGET_INIT";
        case CMD_CLOCK_UPDATE:  return "CMD_CLOCK_UPDATE";
        case CMD_WIDGET_UPDATE: return "CMD_WIDGET_UPDATE";
        default:                return "<unknown>";
    }
}

static const char* rgb565Name(uint16_t c) {
    switch (c) {
        case 0x0000: return "black";
        case 0xFFFF: return "white";
        case 0xF800: return "red";
        case 0x07E0: return "green";
        case 0x001F: return "blue";
        case 0xFFE0: return "yellow";
        case 0x07FF: return "cyan";
        case 0xF81F: return "magenta";
        default:     return "";
    }
}

// RGB565 packs 5/6/5 bits, so the components are scaled back up to 0..255 to
// be readable as an ordinary colour.
static void printColor(const char* field, uint16_t c) {
    const int r = ((c >> 11) & 0x1F) * 255 / 31;
    const int g = ((c >>  5) & 0x3F) * 255 / 63;
    const int b = ( c        & 0x1F) * 255 / 31;
    printf("  %-16s 0x%04X   r=%-3d g=%-3d b=%-3d  %s\n", field, c, r, g, b, rgb565Name(c));
}

static void printTargetNode(uint8_t node) {
    if (node == GLOBAL_BROADCAST_ID) {
        printf("  %-16s 0x%02X     broadcast to all %u nodes\n",
               "targetNode", node, TOTAL_MATRIX_NODES);
    } else {
        printf("  %-16s 0x%02X     node %u\n", "targetNode", node, node);
    }
}

// ---------------------------------------------------------------------------
// Payload decoding -- one per command, used for both the outgoing payload
// (stage 1) and whatever came back out of parseFrame() (stage 6).
// ---------------------------------------------------------------------------

static void describePayload(uint8_t cmd, const uint8_t* p, size_t len) {
    switch (cmd) {
        case CMD_CLOCK_INIT: {
            ClockInitData d;
            memcpy(&d, p, sizeof(d));
            printColor("hand1Color", d.hand1Color);
            printColor("hand2Color", d.hand2Color);
            printColor("bgColor",    d.bgColor);
            printf("  %-16s %u %%\n",  "hand1LengthPct", d.hand1LengthPct);
            printf("  %-16s %u %%\n",  "hand2LengthPct", d.hand2LengthPct);
            printf("  %-16s %u px\n",  "hand1Thickness", d.hand1Thickness);
            printf("  %-16s %u px\n",  "hand2Thickness", d.hand2Thickness);
            break;
        }
        case CMD_WIDGET_INIT: {
            WidgetInitData d;
            memcpy(&d, p, sizeof(d));
            printf("  %-16s %u\n", "widgetType", d.widgetType);
            printColor("bgColor", d.bgColor);
            printf("  %-16s %u\n", "fieldCount", d.fieldCount);
            printf("  %-16s %u %u   (room to grow without a version bump)\n",
                   "reserved", d.reserved[0], d.reserved[1]);
            break;
        }
        case CMD_CLOCK_UPDATE: {
            GlobalClockUpdate d;
            memcpy(&d, p, sizeof(d));
            const uint8_t show = (SHOW_NODES < TOTAL_MATRIX_NODES) ? SHOW_NODES : TOTAL_MATRIX_NODES;
            for (uint8_t n = 0; n < show; n++) {
                printf("  node %-3u         angle1 %6.1f deg   angle2 %6.1f deg\n",
                       n, d.nodes[n].angle1DeciDeg / 10.0, d.nodes[n].angle2DeciDeg / 10.0);
            }
            if (show < TOTAL_MATRIX_NODES) {
                printf("  ...              (%u nodes not shown)\n", TOTAL_MATRIX_NODES - show - 1);
                const uint8_t last = TOTAL_MATRIX_NODES - 1;
                printf("  node %-3u         angle1 %6.1f deg   angle2 %6.1f deg\n",
                       last, d.nodes[last].angle1DeciDeg / 10.0, d.nodes[last].angle2DeciDeg / 10.0);
            }
            // Every node must sit in 0..3599; anything else means the master
            // filled the array wrong or the frame decoded into the wrong slot.
            uint16_t lo = 3599, hi = 0;
            bool inRange = true;
            for (uint8_t n = 0; n < TOTAL_MATRIX_NODES; n++) {
                const uint16_t a[2] = { d.nodes[n].angle1DeciDeg, d.nodes[n].angle2DeciDeg };
                for (int k = 0; k < 2; k++) {
                    if (a[k] < lo) lo = a[k];
                    if (a[k] > hi) hi = a[k];
                    if (a[k] > 3599) inRange = false;
                }
            }
            printf("  %-16s %.1f .. %.1f deg   %s\n", "range", lo / 10.0, hi / 10.0,
                   inRange ? "(all within 0..359.9)" : "(OUT OF RANGE)");
            break;
        }
        case CMD_WIDGET_UPDATE: {
            const size_t dataLen = (len > 0) ? len - 1 : 0;
            printf("  %-16s %u\n", "fieldId", p[0]);
            printf("  %-16s %zu of %zu bytes used\n", "data", dataLen, (size_t)MAX_WIDGET_DATA);
            printf("  %-16s ", "as text");
            putchar('"');
            for (size_t i = 0; i < dataLen; i++) {
                const uint8_t c = p[1 + i];
                putchar((c >= 0x20 && c < 0x7F) ? c : '.');
            }
            printf("\"\n");
            break;
        }
        default:
            printf("  (no decoder for this command)\n");
            break;
    }
}

// ---------------------------------------------------------------------------
// Marks which bytes of an encoded frame are COBS code bytes rather than data.
// A block starts with a code byte and covers `code` bytes in total, so the
// next code byte sits exactly `code` bytes further along.
// ---------------------------------------------------------------------------
static void markCodeBytes(const uint8_t* enc, size_t len, bool* mark) {
    memset(mark, 0, len);
    for (size_t i = 0; i < len; ) {
        mark[i] = true;
        const uint8_t code = enc[i];
        if (code == 0) break;      // the delimiter, or a broken frame
        i += code;
    }
}

// ---------------------------------------------------------------------------
// One full walk through the pipeline. Returns the parse result.
// ---------------------------------------------------------------------------
static ParseResult inspect(uint8_t sequence, bool verbose) {
    // ---- Stage 1: the payload, as the master built it ----------------------
    size_t payloadLen = 0;
    switch (CMD) {
        case CMD_CLOCK_INIT:    payloadLen = fillClockInit(payloadBuf);    break;
        case CMD_WIDGET_INIT:   payloadLen = fillWidgetInit(payloadBuf);   break;
        case CMD_CLOCK_UPDATE:  payloadLen = fillClockUpdate(payloadBuf);  break;
        case CMD_WIDGET_UPDATE: payloadLen = fillWidgetUpdate(payloadBuf); break;
    }

    if (verbose) {
        banner("STAGE 1/6: PAYLOAD");
        printf("  %-16s %s\n", "command", commandName(CMD));
        printf("  %-16s %zu bytes\n", "payload size", payloadLen);
        printf("\n");
        describePayload(CMD, payloadBuf, payloadLen);
        printf("\n");
        hexDump(payloadBuf, payloadLen, nullptr, 64);
    }

    // ---- Stage 2: raw frame, header + payload + checksum -------------------
    // This is the one place the inspector reproduces logic from buildFrame(),
    // because buildFrame() fuses assembly with encoding. The cross-check in
    // stage 3 exists to catch this copy drifting from the real thing.
    rawFrame[0] = uint8_t(CMD);
    rawFrame[1] = TARGET_NODE;
    rawFrame[2] = sequence;
    memcpy(rawFrame + sizeof(ProtocolHeader), payloadBuf, payloadLen);

    const size_t bodyLen = sizeof(ProtocolHeader) + payloadLen;
    rawFrame[bodyLen] = calculateChecksum(rawFrame, bodyLen);
    const size_t rawLen = bodyLen + 1;

    if (verbose) {
        banner("STAGE 2/6: RAW FRAME (pre-COBS)");
        printf("  off      bytes                    meaning\n");
        printf("  %-8s %02X                       cmd  = %s\n", "0", rawFrame[0],
               commandName(rawFrame[0]));
        printf("  %-8s %02X                       node = ", "1", rawFrame[1]);
        if (rawFrame[1] == GLOBAL_BROADCAST_ID) printf("broadcast, all %u nodes\n", TOTAL_MATRIX_NODES);
        else                                    printf("node %u\n", rawFrame[1]);
        printf("  %-8s %02X                       seq  = %u\n", "2", rawFrame[2], rawFrame[2]);

        // First few payload bytes inline, so the span is not just an ellipsis.
        char span[16], head[32] = {0};
        snprintf(span, sizeof(span), "3-%zu", bodyLen - 1);
        const size_t peek = (payloadLen < 6) ? payloadLen : 6;
        for (size_t i = 0; i < peek; i++) {
            snprintf(head + i * 3, 4, "%02X ", payloadBuf[i]);
        }
        printf("  %-8s %-21s%s payload (%zu B)\n", span, head,
               peek < payloadLen ? ".." : "  ", payloadLen);
        printf("  %-8zu %02X                       xor  = 0x%02X  (covers bytes 0..%zu)\n",
               bodyLen, rawFrame[bodyLen], rawFrame[bodyLen], bodyLen - 1);
        printf("\n");
        hexDump(rawFrame, rawLen, nullptr, 64);
        printf("\n  %zu B payload + %zu B header + 1 B checksum = %zu B raw\n",
               payloadLen, sizeof(ProtocolHeader), rawLen);
    }

    // ---- Stage 3: COBS encode ---------------------------------------------
    const size_t wireLen = cobsEncode(rawFrame, rawLen, wire, sizeof(wire));
    if (wireLen == 0) {
        printf("\n  cobsEncode() FAILED -- output buffer too small\n");
        return PARSE_ERR_COBS;
    }
    markCodeBytes(wire, wireLen, codeByte);
    lastWireLen = wireLen;

    if (verbose) {
        banner("STAGE 3/6: COBS ENCODED");
        printf("  Code bytes are marked ^^ below. Each one says how far it is to\n");
        printf("  the next zero, which is what keeps 0x00 out of the frame body.\n\n");
        hexDump(wire, wireLen, codeByte, 320);

        size_t codeCount = 0;
        for (size_t i = 0; i < wireLen; i++) if (codeByte[i]) codeCount++;
        printf("\n  %zu B raw -> %zu B encoded (+%zu: %zu code bytes + 1 delimiter)\n",
               rawLen, wireLen, wireLen - rawLen, codeCount - 1);
        printf("  ceiling for any frame: %zu B (MAX_ENCODED_FRAME)", (size_t)MAX_ENCODED_FRAME);
        if (wireLen < MAX_ENCODED_FRAME) {
            // Zeros are the CHEAP case for COBS: one code byte stands in for the
            // zero itself. A payload with no zeros at all is what costs the most,
            // because the encoder must insert a code byte every 254 bytes on top
            // of the data. So this frame sits below the ceiling.
            printf(", %zu B below\n", (size_t)MAX_ENCODED_FRAME - wireLen);
            printf("  (the ceiling needs a payload with no zero bytes anywhere)\n");
        } else {
            printf(" -- this frame IS the worst case\n");
        }

        // Cross-check: does the real API produce exactly these bytes?
        uint8_t reference[MAX_ENCODED_FRAME];
        const size_t refLen = buildFrame(CMD, TARGET_NODE, sequence,
                                         payloadBuf, payloadLen, reference, sizeof(reference));
        const bool same = (refLen == wireLen) && (memcmp(reference, wire, wireLen) == 0);
        printf("  cross-check vs buildFrame(): %s\n",
               same ? "identical" : "*** MISMATCH -- the walkthrough above is lying ***");
    }

    // ---- Stage 4: the wire -------------------------------------------------
    size_t txLen = wireLen;

    if (verbose) {
        banner("STAGE 4/6: ON THE WIRE");
    }

    if (FAULT == FAULT_FLIP_BYTE) {
        if (FAULT_OFFSET < wireLen) {
            const uint8_t before = wire[FAULT_OFFSET];
            wire[FAULT_OFFSET] ^= FAULT_MASK;
            if (verbose) {
                printf("  FAULT: flipped byte %zu: 0x%02X -> 0x%02X\n",
                       (size_t)FAULT_OFFSET, before, wire[FAULT_OFFSET]);
                printf("         that byte is a %s\n", codeByte[FAULT_OFFSET]
                       ? "COBS CODE byte -- expect the decoder to reject it (stage 5)"
                       : "literal DATA byte -- COBS will not notice, expect the checksum to (stage 6)");
            }
        } else if (verbose) {
            printf("  FAULT: offset %zu is past the end of the frame, nothing done\n",
                   (size_t)FAULT_OFFSET);
        }
    } else if (FAULT == FAULT_TRUNCATE) {
        txLen = (TRUNCATE_TO < wireLen) ? TRUNCATE_TO : wireLen;
        if (verbose) {
            printf("  FAULT: truncated %zu B -> %zu B (the delimiter is gone, so the\n",
                   wireLen, txLen);
            printf("         receiver never sees a frame end and keeps accumulating)\n");
        }
    }

    if (verbose) {
        if (FAULT != FAULT_NONE) printf("\n");
        // Re-dumping a 294 B frame that stage 3 already showed unchanged is
        // just noise, so only dump when there is something new to see.
        if (FAULT == FAULT_NONE && txLen > 64) {
            printf("  %zu bytes, exactly what Serial1.write() would push\n", txLen);
            printf("  (byte for byte what stage 3 produced -- no fault injected)\n");
        } else {
            printf("  %zu bytes, exactly what Serial1.write() would push:\n\n", txLen);
            hexDump(wire, txLen, nullptr, 320);
        }

        const double ms  = txLen * BITS_PER_BYTE * 1000.0 / BAUD_RATE;
        const double bps = txLen * UPDATE_RATE_HZ;
        printf("\n  %zu B -> %.2f ms @ %ld baud (8N1, %d bits/byte)\n",
               txLen, ms, BAUD_RATE, BITS_PER_BYTE);
        printf("  at %.0f Hz: %.1f kB/s -> %.1f %% of the ~%.0f kB/s ceiling\n",
               UPDATE_RATE_HZ, bps / 1000.0, bps / BUS_CEILING_BPS * 100.0,
               BUS_CEILING_BPS / 1000.0);
    }

    // ---- Stage 5: receive and COBS decode ----------------------------------
    // Byte-at-a-time reassembly, the same shape as the slave's real RX loop in
    // tests/phase-3.2-protocol-test-slave/src/main.cpp.
    size_t rxLen = 0;
    bool   gotDelimiter = false;
    for (size_t i = 0; i < txLen; i++) {
        if (wire[i] == FRAME_DELIMITER) { gotDelimiter = true; break; }
        if (rxLen < sizeof(rxBuf)) rxBuf[rxLen++] = wire[i];
    }

    if (verbose) {
        banner("STAGE 5/6: RECEIVED AND DECODED");
        printf("  RX loop collected %zu B before %s\n", rxLen,
               gotDelimiter ? "the 0x00 delimiter" : "the stream ran out (NO DELIMITER)");
        if (!gotDelimiter) {
            printf("\n  On real hardware the slave would NOT parse this yet -- it only\n");
            printf("  parses when a delimiter arrives. It would keep accumulating, and\n");
            printf("  the next frame's bytes would be appended to this half-frame, so\n");
            printf("  both would be lost as one corrupt frame. The stages below force\n");
            printf("  a parse anyway, to show what the partial buffer decodes to.\n");
        }
    }

    const size_t decodedLen = cobsDecode(rxBuf, rxLen, scratch, sizeof(scratch));

    if (verbose) {
        if (decodedLen == 0) {
            printf("  cobsDecode() rejected the frame -- nothing to compare\n");
        } else {
            printf("  decoded back to %zu B\n\n", decodedLen);
            hexDump(scratch, decodedLen, nullptr, 64);
            const bool same = (decodedLen == rawLen) && (memcmp(scratch, rawFrame, rawLen) == 0);
            printf("\n  vs stage 2: %s\n", same ? "identical, nothing was lost"
                                                : "*** DIFFERENT -- the frame changed in transit ***");
        }
    }

    // ---- Stage 6: parseFrame ------------------------------------------------
    Frame f{};
    const ParseResult r = parseFrame(rxBuf, rxLen, scratch, sizeof(scratch), &f);

    if (verbose) {
        banner("STAGE 6/6: PARSED");
        printf("  %-16s %s\n", "result", parseResultName(r));

        if (r != PARSE_OK) {
            printf("\n  The frame was rejected, so the slave would drop it and wait for\n");
            printf("  the next delimiter. No payload reaches the application.\n");
        } else {
            printf("  %-16s %s\n", "command", commandName(f.header.command));
            printTargetNode(f.header.targetNode);
            printf("  %-16s %u\n", "sequence", f.header.sequence);
            printf("  %-16s %zu bytes\n", "payloadLen", f.payloadLen);
            printf("\n");
            describePayload(f.header.command, f.payload, f.payloadLen);

            const bool same = (f.payloadLen == payloadLen) &&
                              (memcmp(f.payload, payloadBuf, payloadLen) == 0);
            printf("\n  round trip: %s\n", same
                   ? "payload matches the source byte for byte"
                   : "*** payload does NOT match what was sent ***");
        }
    }

    return r;
}

int main() {
    banner("FRAME INSPECTOR");
    printf("  protocol v%u  |  %s  |  target %u  |  seq from %u  |  %u frame(s)\n",
           PROTOCOL_VERSION, commandName(CMD), TARGET_NODE, START_SEQ, FRAME_COUNT);
    if (FAULT != FAULT_NONE) {
        printf("  fault injection ACTIVE: %s\n",
               FAULT == FAULT_FLIP_BYTE ? "flip one wire byte" : "truncate the frame");
    }

    // The first frame gets the full walkthrough; any further ones are summarised
    // one per line, which is enough to watch the sequence counter roll over.
    const ParseResult first = inspect(START_SEQ, true);

    if (FRAME_COUNT > 1) {
        banner("REMAINING FRAMES");
        printf("  %-8s %-6s %-10s %s\n", "frame", "seq", "wire", "parse");
        divider();
        printf("  %-8u %-6u %-10zu %s\n", 1u, START_SEQ, lastWireLen, parseResultName(first));

        for (uint16_t i = 1; i < FRAME_COUNT; i++) {
            const uint8_t seq = uint8_t(START_SEQ + i);   // rolls over at 255 by design
            const ParseResult r = inspect(seq, false);
            printf("  %-8u %-6u %-10zu %s%s\n", unsigned(i + 1), seq, lastWireLen,
                   parseResultName(r), (seq == 0) ? "   <- counter wrapped 255 -> 0" : "");
        }
    }

    printf("\n");
    return 0;
}
