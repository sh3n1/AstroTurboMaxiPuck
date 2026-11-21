#include <stdlib.h>
#include <string.h>

#include "dtls_srtp.h"
#include "sctp.h"
#include "utils.h"
#if CONFIG_USE_USRSCTP
#include <usrsctp.h>
#endif

static const uint32_t crc32c_table[256] = {
    0x00000000L, 0xF26B8303L, 0xE13B70F7L, 0x1350F3F4L,
    0xC79A971FL, 0x35F1141CL, 0x26A1E7E8L, 0xD4CA64EBL,
    0x8AD958CFL, 0x78B2DBCCL, 0x6BE22838L, 0x9989AB3BL,
    0x4D43CFD0L, 0xBF284CD3L, 0xAC78BF27L, 0x5E133C24L,
    0x105EC76FL, 0xE235446CL, 0xF165B798L, 0x030E349BL,
    0xD7C45070L, 0x25AFD373L, 0x36FF2087L, 0xC494A384L,
    0x9A879FA0L, 0x68EC1CA3L, 0x7BBCEF57L, 0x89D76C54L,
    0x5D1D08BFL, 0xAF768BBCL, 0xBC267848L, 0x4E4DFB4BL,
    0x20BD8EDEL, 0xD2D60DDDL, 0xC186FE29L, 0x33ED7D2AL,
    0xE72719C1L, 0x154C9AC2L, 0x061C6936L, 0xF477EA35L,
    0xAA64D611L, 0x580F5512L, 0x4B5FA6E6L, 0xB93425E5L,
    0x6DFE410EL, 0x9F95C20DL, 0x8CC531F9L, 0x7EAEB2FAL,
    0x30E349B1L, 0xC288CAB2L, 0xD1D83946L, 0x23B3BA45L,
    0xF779DEAEL, 0x05125DADL, 0x1642AE59L, 0xE4292D5AL,
    0xBA3A117EL, 0x4851927DL, 0x5B016189L, 0xA96AE28AL,
    0x7DA08661L, 0x8FCB0562L, 0x9C9BF696L, 0x6EF07595L,
    0x417B1DBCL, 0xB3109EBFL, 0xA0406D4BL, 0x522BEE48L,
    0x86E18AA3L, 0x748A09A0L, 0x67DAFA54L, 0x95B17957L,
    0xCBA24573L, 0x39C9C670L, 0x2A993584L, 0xD8F2B687L,
    0x0C38D26CL, 0xFE53516FL, 0xED03A29BL, 0x1F682198L,
    0x5125DAD3L, 0xA34E59D0L, 0xB01EAA24L, 0x42752927L,
    0x96BF4DCCL, 0x64D4CECFL, 0x77843D3BL, 0x85EFBE38L,
    0xDBFC821CL, 0x2997011FL, 0x3AC7F2EBL, 0xC8AC71E8L,
    0x1C661503L, 0xEE0D9600L, 0xFD5D65F4L, 0x0F36E6F7L,
    0x61C69362L, 0x93AD1061L, 0x80FDE395L, 0x72966096L,
    0xA65C047DL, 0x5437877EL, 0x4767748AL, 0xB50CF789L,
    0xEB1FCBADL, 0x197448AEL, 0x0A24BB5AL, 0xF84F3859L,
    0x2C855CB2L, 0xDEEEDFB1L, 0xCDBE2C45L, 0x3FD5AF46L,
    0x7198540DL, 0x83F3D70EL, 0x90A324FAL, 0x62C8A7F9L,
    0xB602C312L, 0x44694011L, 0x5739B3E5L, 0xA55230E6L,
    0xFB410CC2L, 0x092A8FC1L, 0x1A7A7C35L, 0xE811FF36L,
    0x3CDB9BDDL, 0xCEB018DEL, 0xDDE0EB2AL, 0x2F8B6829L,
    0x82F63B78L, 0x709DB87BL, 0x63CD4B8FL, 0x91A6C88CL,
    0x456CAC67L, 0xB7072F64L, 0xA457DC90L, 0x563C5F93L,
    0x082F63B7L, 0xFA44E0B4L, 0xE9141340L, 0x1B7F9043L,
    0xCFB5F4A8L, 0x3DDE77ABL, 0x2E8E845FL, 0xDCE5075CL,
    0x92A8FC17L, 0x60C37F14L, 0x73938CE0L, 0x81F80FE3L,
    0x55326B08L, 0xA759E80BL, 0xB4091BFFL, 0x466298FCL,
    0x1871A4D8L, 0xEA1A27DBL, 0xF94AD42FL, 0x0B21572CL,
    0xDFEB33C7L, 0x2D80B0C4L, 0x3ED04330L, 0xCCBBC033L,
    0xA24BB5A6L, 0x502036A5L, 0x4370C551L, 0xB11B4652L,
    0x65D122B9L, 0x97BAA1BAL, 0x84EA524EL, 0x7681D14DL,
    0x2892ED69L, 0xDAF96E6AL, 0xC9A99D9EL, 0x3BC21E9DL,
    0xEF087A76L, 0x1D63F975L, 0x0E330A81L, 0xFC588982L,
    0xB21572C9L, 0x407EF1CAL, 0x532E023EL, 0xA145813DL,
    0x758FE5D6L, 0x87E466D5L, 0x94B49521L, 0x66DF1622L,
    0x38CC2A06L, 0xCAA7A905L, 0xD9F75AF1L, 0x2B9CD9F2L,
    0xFF56BD19L, 0x0D3D3E1AL, 0x1E6DCDEEL, 0xEC064EEDL,
    0xC38D26C4L, 0x31E6A5C7L, 0x22B65633L, 0xD0DDD530L,
    0x0417B1DBL, 0xF67C32D8L, 0xE52CC12CL, 0x1747422FL,
    0x49547E0BL, 0xBB3FFD08L, 0xA86F0EFCL, 0x5A048DFFL,
    0x8ECEE914L, 0x7CA56A17L, 0x6FF599E3L, 0x9D9E1AE0L,
    0xD3D3E1ABL, 0x21B862A8L, 0x32E8915CL, 0xC083125FL,
    0x144976B4L, 0xE622F5B7L, 0xF5720643L, 0x07198540L,
    0x590AB964L, 0xAB613A67L, 0xB831C993L, 0x4A5A4A90L,
    0x9E902E7BL, 0x6CFBAD78L, 0x7FAB5E8CL, 0x8DC0DD8FL,
    0xE330A81AL, 0x115B2B19L, 0x020BD8EDL, 0xF0605BEEL,
    0x24AA3F05L, 0xD6C1BC06L, 0xC5914FF2L, 0x37FACCF1L,
    0x69E9F0D5L, 0x9B8273D6L, 0x88D28022L, 0x7AB90321L,
    0xAE7367CAL, 0x5C18E4C9L, 0x4F48173DL, 0xBD23943EL,
    0xF36E6F75L, 0x0105EC76L, 0x12551F82L, 0xE03E9C81L,
    0x34F4F86AL, 0xC69F7B69L, 0xD5CF889DL, 0x27A40B9EL,
    0x79B737BAL, 0x8BDCB4B9L, 0x988C474DL, 0x6AE7C44EL,
    0xBE2DA0A5L, 0x4C4623A6L, 0x5F16D052L, 0xAD7D5351L};

uint32_t crc32c(uint32_t crc, const uint8_t* data, unsigned int length) {
  while (length--) {
    crc = crc32c_table[(crc ^ *data++) & 0xFFL] ^ (crc >> 8);
  }
  return crc ^ 0xffffffff;
}

static uint32_t sctp_get_checksum(Sctp* sctp, const uint8_t* buf, size_t len) {
  uint32_t crc = crc32c(0xffffffff, buf, len);
  return crc;
}

static int sctp_outgoing_data_cb(void* userdata, void* buf, size_t len, uint8_t tos, uint8_t set_df) {
  Sctp* sctp = (Sctp*)userdata;

  dtls_srtp_write(sctp->dtls_srtp, buf, len);
  return 0;
}

#if !CONFIG_USE_USRSCTP
static int sctp_next_stream_sequence(Sctp* sctp, uint16_t sid, uint16_t* sequence) {
  int i;

  for (i = 0; i < sctp->outgoing_stream_count; i++) {
    if (sctp->outgoing_streams[i].sid == sid) {
      *sequence = sctp->outgoing_streams[i].next_sequence++;
      return 0;
    }
  }

  if (sctp->outgoing_stream_count >= SCTP_MAX_STREAMS) {
    return -1;
  }

  sctp->outgoing_streams[sctp->outgoing_stream_count].sid = sid;
  sctp->outgoing_streams[sctp->outgoing_stream_count].next_sequence = 1;
  sctp->outgoing_stream_count++;
  *sequence = 0;
  return 0;
}
#endif

int sctp_outgoing_data(Sctp* sctp, char* buf, size_t len, SctpDataPpid ppid, uint16_t sid) {
#if CONFIG_USE_USRSCTP
  int res;
  struct sctp_sendv_spa spa = {0};

  spa.sendv_flags = SCTP_SEND_SNDINFO_VALID;

  spa.sendv_sndinfo.snd_sid = sid;
  spa.sendv_sndinfo.snd_flags = SCTP_EOR;
  spa.sendv_sndinfo.snd_ppid = htonl(ppid);

  res = usrsctp_sendv(sctp->sock, buf, len, NULL, 0, &spa, sizeof(spa), SCTP_SENDV_SPA, 0);
  if (res < 0) {
    LOGE("sctp sendv error %d: %s", errno, strerror(errno));
  }
  return res;
#else
  size_t padding_len = 0;
  size_t payload_max = SCTP_MTU - sizeof(SctpPacket) - sizeof(SctpDataChunk);
  size_t pos = 0;
  uint16_t stream_seq = 0;

  if (ppid == PPID_CONTROL && sctp_next_stream_sequence(sctp, sid, &stream_seq) != 0) {
    return -1;
  }

  SctpPacket* packet = (SctpPacket*)(sctp->buf);
  SctpDataChunk* chunk = (SctpDataChunk*)(packet->chunks);

  packet->header.source_port = htons(sctp->local_port);
  packet->header.destination_port = htons(sctp->remote_port);
  packet->header.verification_tag = sctp->verification_tag;

  chunk->type = SCTP_DATA;
  /* DCEP OPEN/ACK control messages must be sent reliably and in order. */
  chunk->iube = ppid == PPID_CONTROL ? 0x02 : 0x06;
  chunk->sid = htons(sid);
  chunk->sqn = htons(stream_seq);
  chunk->ppid = htonl(ppid);

  while (len > payload_max) {
    chunk->length = htons(payload_max + sizeof(SctpDataChunk));
    chunk->tsn = htonl(sctp->tsn++);
    memcpy(chunk->data, buf + pos, payload_max);
    packet->header.checksum = 0;

    packet->header.checksum = sctp_get_checksum(sctp, (const uint8_t*)sctp->buf, SCTP_MTU);

    sctp_outgoing_data_cb(sctp, sctp->buf, SCTP_MTU, 0, 0);
    chunk->iube = ppid == PPID_CONTROL ? 0x00 : 0x04;
    len -= payload_max;
    pos += payload_max;
  }

  if (len > 0) {
    chunk->length = htons(len + sizeof(SctpDataChunk));
    chunk->iube++;
    chunk->tsn = htonl(sctp->tsn++);
    memset(chunk->data, 0, payload_max);
    memcpy(chunk->data, buf + pos, len);
    packet->header.checksum = 0;

    padding_len = 4 * ((len + sizeof(SctpDataChunk) + sizeof(SctpPacket) + 3) / 4);

    packet->header.checksum = sctp_get_checksum(sctp, (const uint8_t*)sctp->buf, padding_len);

    sctp_outgoing_data_cb(sctp, sctp->buf, padding_len, 0, 0);
  }
#endif
  return len;
}

void sctp_add_stream_mapping(Sctp* sctp, const char* label, uint16_t sid) {
  if (sctp->stream_count < SCTP_MAX_STREAMS) {
    strncpy(sctp->stream_table[sctp->stream_count].label, label, sizeof(sctp->stream_table[sctp->stream_count].label));
    sctp->stream_table[sctp->stream_count].sid = sid;
    sctp->stream_count++;
  } else
    LOGE("Stream table full. Cannot add more streams.");
}

void sctp_parse_data_channel_open(Sctp* sctp, uint16_t sid, char* data, size_t length) {
  if (length < 12)
    return;  // Not enough data for a DATA_CHANNEL_OPEN message

  if (data[0] == DATA_CHANNEL_OPEN) {
    uint16_t label_length = ntohs(*(uint16_t*)(data + 8));
    uint16_t protocol_length = ntohs(*(uint16_t*)(data + 10));

    // Ensure we have enough data for the label and protocol
    if (length < 12 + label_length + protocol_length)
      return;

    char* label = (char*)(data + 12);

    // copy and null-terminate
    char label_str[label_length + 1];
    memcpy(label_str, label, label_length);
    label_str[label_length] = '\0';

    // Log or process the DATA_CHANNEL_OPEN message
    printf("DATA_CHANNEL_OPEN: Label=%s, sid=%d\n", label_str, sid);

    // Add stream mapping
    sctp_add_stream_mapping(sctp, label_str, sid);
    char ack = DATA_CHANNEL_ACK;
    sctp_outgoing_data(sctp, &ack, 1, DATA_CHANNEL_PPID_CONTROL, sid);
  }
}

void sctp_handle_sctp_packet(Sctp* sctp, char* buf, size_t len) {
  if (len <= 29)
    return;

  if (buf[12] != 0)  // if chunk_type is no zero, it's not data
    return;

  uint16_t sid = ntohs(*(uint16_t*)(buf + 20));
  uint32_t ppid = ntohl(*(uint32_t*)(buf + 24));

  if (ppid == DATA_CHANNEL_PPID_CONTROL)
    sctp_parse_data_channel_open(sctp, sid, buf + 28, len - 28);
}

#if !CONFIG_USE_USRSCTP
// AstroTurboMaxiPuck: moves the cumulative TSN over every TSN that has now arrived in a row.
static void sctp_absorb_received_tsns(Sctp* sctp) {
  while (sctp->remote_tsn_mask & 1) {
    sctp->remote_cum_tsn++;
    sctp->remote_tsn_mask >>= 1;
  }
}

// AstroTurboMaxiPuck: adds one piece of a message that came in pieces. A first piece (B)
// starts the message; any other must have the next TSN and the same stream, or a piece was
// lost and the message is dropped. Returns 1 when this piece was the last (E) and the message
// is complete in reasm_buf, with reasm_done set until it is passed on.
static int sctp_add_piece(Sctp* sctp, SctpDataChunk* chunk, uint16_t len, uint32_t tsn) {
  uint16_t sid = ntohs(chunk->sid);
  if (chunk->iube & SCTP_DATA_FLAG_BEGIN) {
    sctp->reasm_active = 1;
    sctp->reasm_len = 0;
    sctp->reasm_sid = sid;
    sctp->reasm_ppid = ntohl(chunk->ppid);
  } else if (!sctp->reasm_active || tsn != sctp->reasm_next_tsn || sid != sctp->reasm_sid) {
    sctp->reasm_active = 0;  // a piece is missing: this message is lost
    return 0;
  }
  if (!sctp->reasm_buf || sctp->reasm_len + len > SCTP_MAX_MESSAGE_SIZE) {
    LOGW("message in pieces too big, dropped");
    sctp->reasm_active = 0;
    return 0;
  }
  memcpy(sctp->reasm_buf + sctp->reasm_len, chunk->data, len);
  sctp->reasm_len += len;
  sctp->reasm_next_tsn = tsn + 1;
  if (!(chunk->iube & SCTP_DATA_FLAG_END)) {
    return 0;
  }
  sctp->reasm_active = 0;
  sctp->reasm_done = 1;
  return 1;
}

// AstroTurboMaxiPuck: writes the SACK's gap blocks, the runs of TSNs that arrived after a gap,
// as offsets from the cumulative TSN. Returns how many it wrote.
static uint16_t sctp_write_gap_blocks(Sctp* sctp, uint8_t* blocks, size_t room) {
  uint16_t count = 0;
  uint32_t mask = sctp->remote_tsn_mask;
  for (int i = 0; i < 32 && (count + 1) * 4 <= room;) {
    if (!((mask >> i) & 1)) {
      i++;
      continue;
    }
    int start = i;
    while (i < 32 && ((mask >> i) & 1)) i++;
    uint16_t* block = (uint16_t*)(blocks + count * 4);
    block[0] = htons(start + 1);  // bit i is remote_cum_tsn + 1 + i
    block[1] = htons(i);          // the run's last TSN: (i - 1) + 1
    count++;
  }
  return count;
}
#endif

void sctp_incoming_data(Sctp* sctp, char* buf, size_t len) {
  if (!sctp || !buf)
    return;

#if CONFIG_USE_USRSCTP
  sctp_handle_sctp_packet(sctp, buf, len);
  usrsctp_conninput(sctp, buf, len, 0);
#else
  if (len < sizeof(SctpHeader))
    return;

  size_t length = 0;
  size_t pos = sizeof(SctpHeader);
  uint16_t chunk_len;
  SctpChunkCommon* chunk_common;
  SctpPacket* in_packet = (SctpPacket*)buf;
  SctpPacket* out_packet = (SctpPacket*)sctp->buf;

  // Header
#if 0
  LOGD("source_port %d", ntohs(in_packet->header.source_port));
  LOGD("destination_port %d", ntohs(in_packet->header.destination_port));
  LOGD("verification_tag %ld", ntohl(in_packet->header.verification_tag));
  LOGD("checksum %d", ntohs(in_packet->header.checksum));
#endif
  uint32_t crc32c = in_packet->header.checksum;

  in_packet->header.checksum = 0;

  if (crc32c != sctp_get_checksum(sctp, (const uint8_t*)buf, len)) {
    LOGE("checksum error");
    return;
  }

  // AstroTurboMaxiPuck: upstream handled only the first DATA chunk of a packet, acknowledged
  // that chunk's TSN whatever came before, and called onmessage while the SACK was being
  // built in sctp->buf, so a reply sent from onmessage overwrote it. Browsers bundle several
  // messages in one packet, so most were lost and resent a second later. Now every DATA chunk
  // is read, in order, one SACK goes out for the packet, and only then are the messages
  // passed on. (Fragmented messages, bigger than one packet, are still not supported.)
  struct {
    char* data;
    uint16_t len;
    uint16_t sid;
  } messages[SCTP_MAX_MESSAGES_PER_PACKET];
  int message_count = 0;
  int need_sack = 0;
  int open_sid = -1;  // a DATA_CHANNEL_OPEN to answer with DATA_CHANNEL_ACK

  // prepare outgoing packet
  memset(sctp->buf, 0, sizeof(sctp->buf));
  while (pos + sizeof(SctpChunkCommon) <= len) {
    chunk_common = (SctpChunkCommon*)(buf + pos);
    chunk_len = ntohs(chunk_common->length);
    if (chunk_len < sizeof(SctpChunkCommon) || pos + chunk_len > len) {
      LOGW("Invalid SCTP chunk length=%u", chunk_len);
      return;
    }
    length = 0;  // AstroTurboMaxiPuck: nothing to send unless this chunk sets it

    switch (chunk_common->type) {
      case SCTP_DATA: {
        SctpDataChunk* data_chunk = (SctpDataChunk*)(buf + pos);
        if (chunk_len < sizeof(SctpDataChunk)) {
          return;
        }
        uint16_t payload_len = chunk_len - sizeof(SctpDataChunk);
        uint32_t ppid = ntohl(data_chunk->ppid);
        uint16_t sid = ntohs(data_chunk->sid);
        uint32_t tsn = ntohl(data_chunk->tsn);
#if SCTP_TEST_DROP_EVERY > 0
        // AstroTurboMaxiPuck: test hook, set in CMakeLists.txt. Acts as if every Nth DATA
        // chunk was lost on the way, to test resends and FORWARD-TSN.
        static unsigned test_chunks;
        if (++test_chunks % SCTP_TEST_DROP_EVERY == 0) {
          LOGI("test: dropped TSN %u (%s)", (unsigned)tsn,
               (data_chunk->iube & SCTP_DATA_FLAG_UNORDERED) ? "unordered" : "ordered");
          break;
        }
#endif
        need_sack = 1;

        if (!sctp->remote_tsn_known) {
          sctp->remote_cum_tsn = tsn - 1;
          sctp->remote_tsn_mask = 0;
          sctp->remote_tsn_known = 1;
        }
        // AstroTurboMaxiPuck: the next TSN in order is always taken. One after a gap is
        // taken only for an unordered message (U flag), such as the app's poses, so a lost
        // one doesn't hold the next back; it is reported in the SACK's gap blocks. An
        // ordered one after a gap is left for the other side to resend once the gap is
        // filled, which keeps its order. An older TSN, or one already taken, is a resend.
        int32_t ahead = (int32_t)(tsn - sctp->remote_cum_tsn);
        int unordered = (data_chunk->iube & SCTP_DATA_FLAG_UNORDERED) != 0;
        if (ahead < 1 || ahead > 32 || (sctp->remote_tsn_mask >> (ahead - 1)) & 1) {
          break;
        }
        if (ahead > 1 && !unordered) {
          break;
        }
        // AstroTurboMaxiPuck: a message in pieces (not both B and E). The pieces go into one
        // buffer, passed on after this packet's SACK; a new message's first piece in the same
        // packet as the end of another would overwrite it, so it isn't taken now: left
        // unacknowledged, the other side resends it.
        uint8_t piece_flags = data_chunk->iube & (SCTP_DATA_FLAG_BEGIN | SCTP_DATA_FLAG_END);
        int whole = piece_flags == (SCTP_DATA_FLAG_BEGIN | SCTP_DATA_FLAG_END);
        if (!whole && (piece_flags & SCTP_DATA_FLAG_BEGIN) && sctp->reasm_done) {
          break;
        }
        sctp->remote_tsn_mask |= 1u << (ahead - 1);
        sctp_absorb_received_tsns(sctp);

        if (!whole) {
          // Every piece of a message carries the message's PPID.
          int is_message = ppid == DATA_CHANNEL_PPID_DOMSTRING || ppid == DATA_CHANNEL_PPID_BINARY;
          if (sctp_add_piece(sctp, data_chunk, payload_len, tsn) && is_message &&
              message_count < SCTP_MAX_MESSAGES_PER_PACKET) {
            messages[message_count].data = (char*)sctp->reasm_buf;
            messages[message_count].len = sctp->reasm_len;
            messages[message_count].sid = sctp->reasm_sid;
            message_count++;
          }
          break;
        }

        if (ppid == DATA_CHANNEL_PPID_CONTROL && payload_len > 0 &&
            data_chunk->data[0] == DATA_CHANNEL_OPEN) {
          open_sid = sid;
        } else if ((ppid == DATA_CHANNEL_PPID_DOMSTRING || ppid == DATA_CHANNEL_PPID_BINARY) &&
                   message_count < SCTP_MAX_MESSAGES_PER_PACKET) {
          messages[message_count].data = (char*)data_chunk->data;
          messages[message_count].len = payload_len;
          messages[message_count].sid = sid;
          message_count++;
        }
      } break;
      case SCTP_INIT: {
        if (chunk_len < sizeof(SctpInitChunk)) {
          return;
        }
        LOGD("SCTP_INIT");

        SctpInitChunk* init_chunk;
        init_chunk = (SctpInitChunk*)in_packet->chunks;
        sctp->verification_tag = init_chunk->initiate_tag;
        // AstroTurboMaxiPuck: the other side's DATA starts at its initial TSN.
        sctp->remote_cum_tsn = ntohl(init_chunk->initial_tsn) - 1;
        sctp->remote_tsn_mask = 0;
        sctp->remote_tsn_known = 1;

        SctpInitChunk* init_ack = (SctpInitChunk*)out_packet->chunks;
        init_ack->common.type = SCTP_INIT_ACK;
        init_ack->common.flags = 0x00;
        // AstroTurboMaxiPuck: + 4 for the Forward-TSN-Supported parameter after the cookie.
        init_ack->common.length = htons(20 + 8 + 4);
        init_ack->initiate_tag = htonl(0x12345678);
        init_ack->a_rwnd = htonl(0x100000);
        init_ack->number_of_outbound_streams = 0xffff;
        init_ack->number_of_inbound_streams = 0xffff;
        init_ack->initial_tsn = htonl(sctp->tsn);

        SctpChunkParam* param = init_ack->param;

        param->type = htons(SCTP_PARAM_STATE_COOKIE);
        param->length = htons(8);
        *(uint32_t*)&param->value = htonl(0x02);
        // AstroTurboMaxiPuck: we take FORWARD-TSN (partial reliability).
        SctpChunkParam* forward_tsn = (SctpChunkParam*)((uint8_t*)param + 8);
        forward_tsn->type = htons(SCTP_PARAM_FORWARD_TSN_SUPPORTED);
        forward_tsn->length = htons(4);
        length = ntohs(init_ack->common.length) + sizeof(SctpHeader);
      } break;
      case SCTP_INIT_ACK: {
        if (chunk_len < sizeof(SctpInitChunk)) {
          return;
        }
        SctpInitChunk* init_ack = (SctpInitChunk*)in_packet->chunks;
        SctpCookieEchoChunk* cookie_echo = (SctpCookieEchoChunk*)out_packet->chunks;
        SctpChunkParam* param = NULL;
        sctp->verification_tag = init_ack->initiate_tag;
        // AstroTurboMaxiPuck: the other side's DATA starts at its initial TSN.
        sctp->remote_cum_tsn = ntohl(init_ack->initial_tsn) - 1;
        sctp->remote_tsn_mask = 0;
        sctp->remote_tsn_known = 1;
        int type;
        // find cookie
        uint8_t* cookie = NULL;
        cookie = (uint8_t*)&init_ack->param[0];
        for (int i = 0; i < init_ack->common.length - 20; i += 2) {
          type = ntohs(*(uint16_t*)&cookie[i]);
          // find cookie param
          if (type == 0x07) {
            param = (SctpChunkParam*)&cookie[i];
            break;
          }
        }

        cookie_echo->common.type = SCTP_COOKIE_ECHO;
        cookie_echo->common.flags = 0x00;
        // cookie echo: type + flag + length (4 bytes) + cookie
        cookie_echo->common.length = htons(ntohs(param->length));
        // param: type + length (4 bytes) + cookie
        memcpy(cookie_echo->cookie, param->value, ntohs(param->length) - 4);
        length = ntohs(cookie_echo->common.length) + sizeof(SctpHeader);
      } break;
      case SCTP_SACK:
        if (chunk_len < sizeof(SctpSackChunk)) {
          return;
        }
#if 0
        LOGD("SCTP_SACK");
        sack = (SctpSackChunk*)in_packet->chunks;
        LOGD("cumulative_tsn_ack %d", ntohl(sack->cumulative_tsn_ack));
        LOGD("a_rwnd %d", ntohl(sack->a_rwnd));
        LOGD("number_of_gap_ack_blocks %d", sack->number_of_gap_ack_blocks);
        LOGD("number_of_dup_tsns %d", sack->number_of_dup_tsns);
#endif
// XXX: unordered sequence
#if 0
        if (sack->number_of_gap_ack_blocks > 0) {

          int blocks = ntohs(sack->number_of_gap_ack_blocks);
          LOGW("cumulative_tsn_ack: %ld, number_of_gap_ack_blocks: %d",
           ntohl(sack->cumulative_tsn_ack), blocks);

          for (int i = 0; i < blocks; i++) {

            uint16_t *start = (uint16_t*)sack->blocks + i*2;
            uint16_t *end = (uint16_t*)sack->blocks + i*2 + 1;
            LOGW("start: %d, end: %d", ntohs(*start), ntohs(*end));
            sctp->tsn = ntohl(sack->cumulative_tsn_ack) + 1;// + (*start) - 1;
          }
        } else if (sack->number_of_dup_tsns > 0) {

          LOGW("cumulative_tsn_ack: %ld, number_of_dup_tsns: %d",
           ntohl(sack->cumulative_tsn_ack),
           ntohs(sack->number_of_dup_tsns));
        }
#endif
        break;
      case SCTP_FORWARD_TSN: {
        // AstroTurboMaxiPuck: the other side gave up on messages up to this TSN (a channel
        // that never resends), so we stop waiting for them. Our SCTP doesn't keep the
        // order of ordered messages within a stream by sequence number, so the
        // stream/sequence pairs that follow need nothing.
        if (chunk_len < 8 || !sctp->remote_tsn_known) {
          break;
        }
        SctpForwardTsnChunk* forward = (SctpForwardTsnChunk*)(buf + pos);
        uint32_t new_cum = ntohl(forward->new_cumulative_tsn);
        int32_t skip = (int32_t)(new_cum - sctp->remote_cum_tsn);
        // Only on a lost message, so it's rare and worth seeing.
        LOGI("FORWARD-TSN to %u, %d ahead", (unsigned)new_cum, (int)skip);
        if (skip > 0) {
          sctp->remote_tsn_mask = skip >= 32 ? 0 : sctp->remote_tsn_mask >> skip;
          sctp->remote_cum_tsn = new_cum;
          sctp_absorb_received_tsns(sctp);
        }
        need_sack = 1;
      } break;
      case SCTP_HEARTBEAT: {
        SctpChunkCommon* heartbeat_ack = (SctpChunkCommon*)out_packet->chunks;

        memcpy(heartbeat_ack, chunk_common, chunk_len);
        heartbeat_ack->type = SCTP_HEARTBEAT_ACK;
        length = chunk_len + sizeof(SctpHeader);
        pos = len;  // Echo the HEARTBEAT-INFO parameter unchanged.
      } break;
      case SCTP_HEARTBEAT_ACK:
        break;
      case SCTP_COOKIE_ECHO: {
        LOGD("SCTP_COOKIE_ECHO");
        SctpChunkCommon* common = (SctpChunkCommon*)out_packet->chunks;
        common->type = SCTP_COOKIE_ACK;
        common->length = htons(4);
        length = ntohs(common->length) + sizeof(SctpHeader);
        pos = len;  // Do not handle other msg
        if (!sctp->connected) {
          sctp->connected = 1;
          if (sctp->onopen) {
            sctp->onopen(sctp->userdata);
          }
        }
      } break;
      case SCTP_COOKIE_ACK: {
        if (!sctp->connected) {
          sctp->connected = 1;
          if (sctp->onopen) {
            sctp->onopen(sctp->userdata);
          }
        }
        break;
      }
      case SCTP_ABORT:
        sctp->connected = 0;
        if (sctp->onclose) {
          sctp->onclose(sctp->userdata);
        }
        break;
      default:
        LOGI("Unknown chunk type %d", chunk_common->type);
        length = 0;
        break;
    }

    out_packet->header.source_port = htons(sctp->local_port);
    out_packet->header.destination_port = htons(sctp->remote_port);
    out_packet->header.verification_tag = sctp->verification_tag;
    out_packet->header.checksum = 0x00;

    if (length > 0) {
      // padding 4
      length = (4 * ((length + 3) / 4));
      out_packet->header.checksum = sctp_get_checksum(sctp, sctp->buf, length);
      dtls_srtp_write(sctp->dtls_srtp, sctp->buf, length);
      // sctp_outgoing_data_cb(sctp, sctp->buf, SCTP_MTU, 0, 0);
    }
    pos += 4 * ((chunk_len + 3) / 4);
  }

  // AstroTurboMaxiPuck: one SACK for all the packet's DATA chunks, with the DATA_CHANNEL_ACK
  // bundled when a channel was opened, then the messages.
  if (need_sack) {
    memset(sctp->buf, 0, sizeof(sctp->buf));
    SctpSackChunk* sack_chunk = (SctpSackChunk*)out_packet->chunks;
    uint16_t gaps = sctp_write_gap_blocks(sctp, sack_chunk->blocks, 64);
    sack_chunk->common.type = SCTP_SACK;
    sack_chunk->common.flags = 0x00;
    sack_chunk->common.length = htons(16 + gaps * 4);
    sack_chunk->cumulative_tsn_ack = htonl(sctp->remote_cum_tsn);
    sack_chunk->a_rwnd = htonl((uint32_t)sizeof(sctp->buf));
    sack_chunk->number_of_gap_ack_blocks = htons(gaps);
    length = ntohs(sack_chunk->common.length) + sizeof(SctpHeader);

    uint16_t ack_sequence;
    if (open_sid >= 0 && sctp_next_stream_sequence(sctp, open_sid, &ack_sequence) == 0) {
      SctpDataChunk* ack_chunk = (SctpDataChunk*)(sack_chunk->blocks + gaps * 4);
      ack_chunk->type = SCTP_DATA;
      ack_chunk->iube = 0x03;
      ack_chunk->tsn = htonl(sctp->tsn++);
      ack_chunk->sid = htons(open_sid);
      ack_chunk->sqn = htons(ack_sequence);
      ack_chunk->ppid = htonl(DATA_CHANNEL_PPID_CONTROL);
      ack_chunk->length = htons(1 + sizeof(SctpDataChunk));
      ack_chunk->data[0] = DATA_CHANNEL_ACK;
      length += ntohs(ack_chunk->length);
    }

    out_packet->header.source_port = htons(sctp->local_port);
    out_packet->header.destination_port = htons(sctp->remote_port);
    out_packet->header.verification_tag = sctp->verification_tag;
    out_packet->header.checksum = 0x00;
    length = (4 * ((length + 3) / 4));
    out_packet->header.checksum = sctp_get_checksum(sctp, sctp->buf, length);
    dtls_srtp_write(sctp->dtls_srtp, sctp->buf, length);
  }

  for (int i = 0; i < message_count; i++) {
    if (sctp->onmessage) {
      sctp->onmessage(messages[i].data, messages[i].len, sctp->userdata, messages[i].sid);
    }
  }
  sctp->reasm_done = 0;  // AstroTurboMaxiPuck: the buffer is free for the next message
#endif
}

static int sctp_handle_incoming_data(Sctp* sctp, char* data, size_t len, uint32_t ppid, uint16_t sid, int flags) {
#if CONFIG_USE_USRSCTP
  switch (ppid) {
    case DATA_CHANNEL_PPID_CONTROL:
      break;

    case DATA_CHANNEL_PPID_DOMSTRING:
    case DATA_CHANNEL_PPID_BINARY:
    case DATA_CHANNEL_PPID_DOMSTRING_PARTIAL:
    case DATA_CHANNEL_PPID_BINARY_PARTIAL:

      LOGD("Got message (size = %ld)", len);
      if (sctp->onmessage) {
        sctp->onmessage(data, len, sctp->userdata, sid);
      }
      break;

    default:
      break;
  }
#endif
  return 0;
}

#if CONFIG_USE_USRSCTP

static void sctp_process_notification(Sctp* sctp, union sctp_notification* notification, size_t len) {
  if (notification->sn_header.sn_length != (uint32_t)len) {
    return;
  }

  switch (notification->sn_header.sn_type) {
    case SCTP_ASSOC_CHANGE:

      switch (notification->sn_assoc_change.sac_state) {
        case SCTP_COMM_UP:

          sctp->connected = 1;
          if (sctp->onopen) {
            sctp->onopen(sctp->userdata);
          }

          break;

        case SCTP_COMM_LOST:
        case SCTP_SHUTDOWN_COMP:
          sctp->connected = 0;
          if (sctp->onclose) {
            sctp->onclose(sctp->userdata);
          }
        default:
          break;
      }
      break;
    default:
      break;
  }
}

static int sctp_incoming_data_cb(struct socket* sock, union sctp_sockstore addr, void* data, size_t len, struct sctp_rcvinfo recv_info, int flags, void* userdata) {
  Sctp* sctp = (Sctp*)userdata;
  LOGD("Data of length %u received on stream %u with SSN %u, TSN %u, PPID %u",
       (uint32_t)len,
       recv_info.rcv_sid,
       recv_info.rcv_ssn,
       recv_info.rcv_tsn,
       ntohl(recv_info.rcv_ppid));
  if (flags & MSG_NOTIFICATION) {
    sctp_process_notification(sctp, (union sctp_notification*)data, len);
  } else {
    sctp_handle_incoming_data(sctp, data, len, ntohl(recv_info.rcv_ppid), recv_info.rcv_sid, flags);
  }
  free(data);  // we need to free the memory that usrsctp allocates
  return 0;
}
#endif

void sctp_usrsctp_init() {
#if CONFIG_USE_USRSCTP
  usrsctp_init(0, sctp_outgoing_data_cb, NULL);
#endif
}

void sctp_usrsctp_deinit() {
#if CONFIG_USE_USRSCTP
  usrsctp_finish();
#endif
}

int sctp_create_association(Sctp* sctp, DtlsSrtp* dtls_srtp) {
  sctp->dtls_srtp = dtls_srtp;
  sctp->local_port = 5000;
  sctp->remote_port = 5000;
  sctp->tsn = 1234;
  sctp->remote_tsn_known = 0;  // AstroTurboMaxiPuck: learnt from the INIT or INIT ACK
  sctp->remote_tsn_mask = 0;
#if !CONFIG_USE_USRSCTP
  // AstroTurboMaxiPuck: the buffer for messages in pieces (in PSRAM on the puck, since it
  // is big), freed in sctp_destroy_association.
  if (!sctp->reasm_buf) sctp->reasm_buf = malloc(SCTP_MAX_MESSAGE_SIZE);
  sctp->reasm_active = 0;
  sctp->reasm_done = 0;
#endif
  sctp->outgoing_stream_count = 0;
  memset(sctp->outgoing_streams, 0, sizeof(sctp->outgoing_streams));
#if CONFIG_USE_USRSCTP
  int ret = -1;
  usrsctp_sysctl_set_sctp_ecn_enable(0);
  usrsctp_register_address(sctp);

  struct socket* sock = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP,
                                       sctp_incoming_data_cb, NULL, 0, sctp);

  if (!sock) {
    LOGE("usrsctp_socket failed");
    return -1;
  }

  do {
    if (usrsctp_set_non_blocking(sock, 1) < 0) {
      LOGE("usrsctp_set_non_blocking failed");
      break;
    }

    struct linger lopt;
    lopt.l_onoff = 1;
    lopt.l_linger = 0;
    usrsctp_setsockopt(sock, SOL_SOCKET, SO_LINGER, &lopt, sizeof(lopt));

#if 0
    struct sctp_paddrparams peer_param;
    memset(&peer_param, 0, sizeof peer_param);
    peer_param.spp_flags = SPP_PMTUD_DISABLE;
    peer_param.spp_pathmtu = 1200;
    usrsctp_setsockopt(s, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS, &peer_param, sizeof peer_param);
#endif

    struct sctp_assoc_value av;
    av.assoc_id = SCTP_ALL_ASSOC;
    av.assoc_value = SCTP_ENABLE_RESET_STREAM_REQ | SCTP_ENABLE_CHANGE_ASSOC_REQ;
    usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET, &av, sizeof(av));

    uint32_t nodelay = 1;
    usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_NODELAY, &nodelay, sizeof(nodelay));

    static uint16_t event_types[] = {
        SCTP_ASSOC_CHANGE,
        SCTP_PEER_ADDR_CHANGE,
        SCTP_REMOTE_ERROR,
        SCTP_SHUTDOWN_EVENT,
        SCTP_ADAPTATION_INDICATION,
        SCTP_SEND_FAILED_EVENT,
        SCTP_SENDER_DRY_EVENT,
        SCTP_STREAM_RESET_EVENT,
        SCTP_STREAM_CHANGE_EVENT};

    struct sctp_event event;
    memset(&event, 0, sizeof(event));
    event.se_assoc_id = SCTP_ALL_ASSOC;
    event.se_on = 1;
    for (int i = 0; i < sizeof(event_types) / sizeof(uint16_t); i++) {
      event.se_type = event_types[i];
      usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof(event));
    }

    struct sctp_initmsg init_msg;
    memset(&init_msg, 0, sizeof init_msg);
    init_msg.sinit_num_ostreams = 300;
    init_msg.sinit_max_instreams = 300;
    usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_INITMSG, &init_msg, sizeof init_msg);

    struct sockaddr_conn sconn;
    memset(&sconn, 0, sizeof(sconn));
    sconn.sconn_family = AF_CONN;
    sconn.sconn_port = htons(sctp->local_port);
    sconn.sconn_addr = (void*)sctp;
    ret = usrsctp_bind(sock, (struct sockaddr*)&sconn, sizeof(sconn));

    struct sockaddr_conn rconn;

    memset(&rconn, 0, sizeof(struct sockaddr_conn));
    rconn.sconn_family = AF_CONN;
    rconn.sconn_port = htons(sctp->remote_port);
    rconn.sconn_addr = (void*)sctp;
    ret = usrsctp_connect(sock, (struct sockaddr*)&rconn, sizeof(struct sockaddr_conn));

    if (ret < 0 && errno != EINPROGRESS) {
      LOGE("connect error");
      break;
    }

    ret = 0;

  } while (0);

  if (ret < 0) {
    sctp_destroy_association(sctp);
    return -1;
  }

  sctp->sock = sock;
#else
  // send SCTP_INIT
  int length = 0;
  SctpInitChunk* init_chunk;
  SctpHeader* header;
  SctpPacket* out_packet = (SctpPacket*)sctp->buf;
  header = &out_packet->header;
  init_chunk = (SctpInitChunk*)out_packet->chunks;

  header->source_port = htons(sctp->local_port);
  header->destination_port = htons(sctp->remote_port);
  header->verification_tag = 0x0;
  init_chunk->common.type = SCTP_INIT;
  init_chunk->common.flags = 0x00;
  // AstroTurboMaxiPuck: + 4 for the Forward-TSN-Supported parameter.
  init_chunk->common.length = htons(20 + 4);
  init_chunk->initiate_tag = htonl(0x12345678);
  init_chunk->a_rwnd = htonl(0x100000);
  init_chunk->number_of_outbound_streams = 0xffff;
  init_chunk->number_of_inbound_streams = 0xffff;
  init_chunk->initial_tsn = htonl(sctp->tsn);
  // AstroTurboMaxiPuck: we take FORWARD-TSN (partial reliability).
  init_chunk->param[0].type = htons(SCTP_PARAM_FORWARD_TSN_SUPPORTED);
  init_chunk->param[0].length = htons(4);
  length = ntohs(init_chunk->common.length) + sizeof(SctpHeader);
  length = (4 * ((length + 3) / 4));
  header->checksum = sctp_get_checksum(sctp, sctp->buf, length);
  dtls_srtp_write(sctp->dtls_srtp, sctp->buf, length);
#endif

  return 0;
}

void sctp_destroy_association(Sctp* sctp) {
#if CONFIG_USE_USRSCTP
  if (sctp && sctp->sock) {
    usrsctp_shutdown(sctp->sock, SHUT_RDWR);
    usrsctp_close(sctp->sock);
    sctp->sock = NULL;
  }
#else
  // AstroTurboMaxiPuck: the buffer for messages in pieces.
  if (sctp) {
    free(sctp->reasm_buf);
    sctp->reasm_buf = NULL;
  }
#endif
}

int sctp_is_connected(Sctp* sctp) {
  return sctp->connected;
}

void sctp_onmessage(Sctp* sctp, void (*onmessage)(char* msg, size_t len, void* userdata, uint16_t sid)) {
  sctp->onmessage = onmessage;
}

void sctp_onopen(Sctp* sctp, void (*onopen)(void* userdata)) {
  sctp->onopen = onopen;
}

void sctp_onclose(Sctp* sctp, void (*onclose)(void* userdata)) {
  sctp->onclose = onclose;
}
