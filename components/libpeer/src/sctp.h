#ifndef SCTP_H_
#define SCTP_H_

#include "config.h"
#include "dtls_srtp.h"
#include "utils.h"

typedef enum DecpMsgType {

  DATA_CHANNEL_OPEN = 0x03,
  DATA_CHANNEL_ACK = 0x02,

} DecpMsgType;

typedef enum DataChannelPpid {

  DATA_CHANNEL_PPID_CONTROL = 50,
  DATA_CHANNEL_PPID_DOMSTRING = 51,
  DATA_CHANNEL_PPID_BINARY_PARTIAL = 52,
  DATA_CHANNEL_PPID_BINARY = 53,
  DATA_CHANNEL_PPID_DOMSTRING_PARTIAL = 54

} DataChannelPpid;

#if !CONFIG_USE_USRSCTP

typedef struct SctpChunkParam {
  uint16_t type;
  uint16_t length;
  uint8_t value[0];

} SctpChunkParam;

typedef enum SctpParamType {

  SCTP_PARAM_STATE_COOKIE = 7,
  // AstroTurboMaxiPuck: announces partial reliability (RFC 3758), so the other side really
  // drops messages on channels that never resend, and tells us with FORWARD-TSN.
  SCTP_PARAM_FORWARD_TSN_SUPPORTED = 0xC000,

} SctpParamType;

typedef enum SctpHeaderType {

  SCTP_DATA = 0,
  SCTP_INIT = 1,
  SCTP_INIT_ACK = 2,
  SCTP_SACK = 3,
  SCTP_HEARTBEAT = 4,
  SCTP_HEARTBEAT_ACK = 5,
  SCTP_ABORT = 6,
  SCTP_SHUTDOWN = 7,
  SCTP_SHUTDOWN_ACK = 8,
  SCTP_ERROR = 9,
  SCTP_COOKIE_ECHO = 10,
  SCTP_COOKIE_ACK = 11,
  SCTP_ECNE = 12,
  SCTP_CWR = 13,
  SCTP_SHUTDOWN_COMPLETE = 14,
  SCTP_AUTH = 15,
  SCTP_ASCONF_ACK = 128,
  SCTP_ASCONF = 130,
  SCTP_FORWARD_TSN = 192

} SctpHeaderType;

typedef struct SctpChunkCommon {
  uint8_t type;
  uint8_t flags;
  uint16_t length;

} SctpChunkCommon;

typedef struct SctpForwardTsnChunk {
  SctpChunkCommon common;
  uint32_t new_cumulative_tsn;
  uint16_t stream_number;
  uint16_t stream_sequence_number;

} SctpForwardTsnChunk;

typedef struct SctpHeader {
  uint16_t source_port;
  uint16_t destination_port;
  uint32_t verification_tag;
  uint32_t checksum;

} SctpHeader;

typedef struct SctpPacket {
  SctpHeader header;
  uint8_t chunks[0];

} SctpPacket;

typedef struct SctpSackChunk {
  SctpChunkCommon common;
  uint32_t cumulative_tsn_ack;
  uint32_t a_rwnd;
  uint16_t number_of_gap_ack_blocks;
  uint16_t number_of_dup_tsns;
  uint8_t blocks[0];

} SctpSackChunk;

typedef struct SctpDataChunk {
  uint8_t type;
  uint8_t iube;
  uint16_t length;
  uint32_t tsn;
  uint16_t sid;
  uint16_t sqn;
  uint32_t ppid;
  uint8_t data[0];

} SctpDataChunk;

typedef struct SctpInitChunk {
  SctpChunkCommon common;
  uint32_t initiate_tag;
  uint32_t a_rwnd;
  uint16_t number_of_outbound_streams;
  uint16_t number_of_inbound_streams;
  uint32_t initial_tsn;
  SctpChunkParam param[0];

} SctpInitChunk;

typedef struct SctpCookieEchoChunk {
  SctpChunkCommon common;
  uint8_t cookie[0];
} SctpCookieEchoChunk;

#endif

typedef enum SctpDataPpid {

  PPID_CONTROL = 50,
  PPID_STRING = 51,
  PPID_BINARY = 53,
  PPID_STRING_EMPTY = 56,
  PPID_BINARY_EMPTY = 57

} SctpDataPpid;

#define SCTP_MAX_STREAMS 5

typedef struct {
  char label[32];  // Stream label
  uint16_t sid;    // Stream ID
} SctpStreamEntry;

typedef struct {
  uint16_t sid;
  uint16_t next_sequence;
} SctpOutgoingStream;

// AstroTurboMaxiPuck: the DATA chunk's flags: U for a message on an unordered channel, and
// B and E for the first and last piece of a message split over several packets.
#define SCTP_DATA_FLAG_UNORDERED 0x04
#define SCTP_DATA_FLAG_BEGIN 0x02
#define SCTP_DATA_FLAG_END 0x01

// AstroTurboMaxiPuck: the biggest message put back together from pieces. The puck's messages
// stay under this (docs/puck.md in the app), and the app's answer says so (max-message-size).
#define SCTP_MAX_MESSAGE_SIZE 16384

// AstroTurboMaxiPuck: how far ahead of the last TSN received in order we keep what arrives
// early, and the biggest chunk kept. What arrives early on an ordered channel is kept (in
// PSRAM on the puck) and passed on in order once the gap is filled, so the other side only
// resends what was lost, at once, rather than everything after it, after a timeout.
#define SCTP_TSN_WINDOW 64
#define SCTP_EARLY_CHUNK_MAX 1280
// The receive window we advertise: what the chunks kept early can hold. Upstream advertised
// one packet's worth (1300 bytes), which let a browser send one packet at a time, and a lost
// one stopped everything (views too) for its resend timeout, up to seconds.
#define SCTP_RECEIVE_WINDOW (SCTP_TSN_WINDOW * 1000)

// AstroTurboMaxiPuck: counters, to see from the application's log whether the other side
// still acknowledges what we send (a link that dies with nothing failing on our side).
typedef struct SctpStats {
  uint32_t data_in;        // DATA chunks received
  uint32_t sacks_in;       // SACKs received: the other side acknowledging what we sent
  uint32_t last_sack_cum;  // the TSN the last SACK acknowledged up to
  uint32_t heartbeats_in;  // HEARTBEATs received (each answered)
  uint32_t write_errors;   // DTLS writes that failed
} SctpStats;

typedef struct Sctp {
  struct socket* sock;

  int local_port;
  int remote_port;
  int connected;
  uint32_t verification_tag;
  uint32_t tsn;
  // AstroTurboMaxiPuck: the last DATA TSN received in order from the other side, for SACKs,
  // and which of the next SCTP_TSN_WINDOW TSNs have arrived early (bit i: remote_cum_tsn + 1
  // + i). Early chunks of ordered channels wait in early_chunks, slot TSN % SCTP_TSN_WINDOW,
  // with their length in early_len (0: empty).
  uint32_t remote_cum_tsn;
  uint64_t remote_tsn_mask;
  int remote_tsn_known;
  uint8_t* early_chunks;     // SCTP_TSN_WINDOW × SCTP_EARLY_CHUNK_MAX bytes
  uint16_t early_len[SCTP_TSN_WINDOW];
  // AstroTurboMaxiPuck: a message arriving in pieces. The pieces of one message have
  // consecutive TSNs, so one buffer is enough: the next piece must have the next TSN.
  uint8_t* reasm_buf;        // SCTP_MAX_MESSAGE_SIZE bytes, made with the association
  size_t reasm_len;
  uint32_t reasm_next_tsn;
  uint16_t reasm_sid;
  uint32_t reasm_ppid;
  int reasm_active;          // a message's first piece came, and none is missing so far
  DtlsSrtp* dtls_srtp;
  int stream_count;
  SctpStreamEntry stream_table[SCTP_MAX_STREAMS];
  int outgoing_stream_count;
  SctpOutgoingStream outgoing_streams[SCTP_MAX_STREAMS];

  /* datachannel */
  void (*onmessage)(char* msg, size_t len, void* userdata, uint16_t sid);
  void (*onopen)(void* userdata);
  void (*onclose)(void* userdata);

  void* userdata;
  SctpStats stats;  // AstroTurboMaxiPuck
  uint8_t buf[CONFIG_MTU];
} Sctp;

int sctp_create_association(Sctp* sctp, DtlsSrtp* dtls_srtp);

void sctp_destroy_association(Sctp* sctp);

void sctp_usrsctp_init();

void sctp_usrsctp_deinit();

int sctp_is_connected(Sctp* sctp);

void sctp_incoming_data(Sctp* sctp, char* buf, size_t len);

int sctp_outgoing_data(Sctp* sctp, char* buf, size_t len, SctpDataPpid ppid, uint16_t sid);

void sctp_onmessage(Sctp* sctp, void (*onmessage)(char* msg, size_t len, void* userdata, uint16_t sid));

void sctp_onopen(Sctp* sctp, void (*onopen)(void* userdata));

void sctp_onclose(Sctp* sctp, void (*onclose)(void* userdata));

#endif  // SCTP_H_
