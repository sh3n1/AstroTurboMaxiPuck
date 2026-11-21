# libpeer (our copy)

A small WebRTC library in C, by sepfy: https://github.com/sepfy/libpeer. MIT licence, see
`LICENSE`, which must stay with the code.

Copied from upstream commit `5b849de378545c31d34759a145413846953e1366` (2026-08-23), the
`src/` folder only. We keep our own copy because the puck's one-QR-code pairing needs changes
upstream doesn't have.

## Changes from upstream

Every change is marked with an `AstroTurboMaxiPuck:` comment.

- Removed `peer_signaling.c` and `peer_signaling.h` (signalling over MQTT and HTTP, with
  coreMQTT and coreHTTP), and `ssl_transport.c` and `ssl_transport.h`, which only they used:
  the puck does its own signalling. `peer.h` no longer includes it.
- `sctp.c`, `sctp.h` (libpeer's own small SCTP, used instead of usrsctp): upstream read only
  the first DATA chunk of each incoming packet, acknowledged that chunk's TSN, and called
  `onmessage` while the SACK was being built in `sctp->buf`, so a reply sent from `onmessage`
  overwrote it. Browsers bundle several messages in a packet, so most were dropped and resent
  about a second later: about one message a second got through. Now every DATA chunk is
  read, the last TSN received in order is tracked (from the INIT or INIT ACK), duplicates are
  skipped, one SACK goes out per packet, and the messages are passed on after it.
- `sctp.c`, `sctp.h`, **partial reliability** (RFC 3758): upstream didn't announce
  Forward-TSN support, so browsers treated channels that never resend as reliable and resent
  lost messages, holding back everything after them. Now INIT and INIT ACK carry the
  Forward-TSN-Supported parameter, FORWARD-TSN moves the cumulative TSN on, and an
  unordered message arriving after a gap is taken at once (a 32-TSN window, reported in the
  SACK's gap blocks). An ordered one after a gap still waits for the resend, keeping its
  order.
- `sctp.c`, `sctp.h`, **messages in pieces**: upstream couldn't take a message bigger than
  one packet. Now the pieces (B and E flags, consecutive TSNs) are put back together in one
  buffer of `SCTP_MAX_MESSAGE_SIZE` (16 KB, allocated with the association); a message with
  a missing piece is dropped.
- `sctp.c`: a DATA_CHANNEL_OPEN's label is kept with its stream again (our rewrite of the
  packet reading had only acknowledged it), so `peer_connection_lookup_sid` finds channels
  by label.
- `sctp.c` test hook `SCTP_TEST_DROP_EVERY` (set in `CMakeLists.txt`, 0 is off): acts as if
  every Nth incoming DATA chunk was lost, to test resends and FORWARD-TSN.
- **Lite pairing**, for the puck's one-QR-code pairing: `peer_connection_start_lite()` in
  `peer_connection.c` and `.h` makes our ICE credentials and certificate and returns them
  with our address, for the other side to build our answer from. `Agent.lite` in `agent.c`
  and `.h`: the other side's first valid binding request gives its address and ICE username,
  and becomes the selected pair; we never send checks. `dtls_srtp.c`: with no remote
  fingerprint, the certificate isn't checked (the application checks a secret instead).
- `dtls_srtp.c`: the mbedTLS entropy and CTR-DRBG set-up and free calls only on mbedTLS 3
  and older. mbedTLS 4 (ESP-IDF 6.x) has no entropy API, and libpeer's mbedTLS 4 path
  doesn't use them.
