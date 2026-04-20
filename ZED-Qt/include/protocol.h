#pragma once

#include <QtGlobal>

// Protocol-defined constants — never scatter these as literals in parsing code.
constexpr quint32 PROTO_MAGIC        = 0x54524156u; // 'TRAV' in little-endian
constexpr int     PROTO_HEADER_SIZE  = 24;

// Byte offsets within the 24-byte header (all fields are uint32_t, little-endian).
constexpr int PROTO_OFFSET_MAGIC = 0;
constexpr int PROTO_OFFSET_SEQ   = 4;
constexpr int PROTO_OFFSET_NR    = 8;   // number of range bins
constexpr int PROTO_OFFSET_NT    = 12;  // number of theta bins
// Bytes 16–23 are reserved and not read by this version.

constexpr quint16 PROTO_PORT = 9090;
