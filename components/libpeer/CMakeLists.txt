# libpeer, a small WebRTC library (MIT, Copyright (c) 2021 sepfy), our own copy: see README.md.
# Built with libpeer's own small SCTP, not usrsctp, as libpeer does on the ESP32.

file(GLOB LIBPEER_SOURCES "src/*.c")

idf_component_register(
    SRCS ${LIBPEER_SOURCES}
    INCLUDE_DIRS "src"
    REQUIRES mbedtls esp_netif lwip
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    CONFIG_USE_LWIP=1
    CONFIG_USE_USRSCTP=0
    CONFIG_AUDIO_BUFFER_SIZE=8096
    CONFIG_DATA_BUFFER_SIZE=102400
    __BYTE_ORDER=__LITTLE_ENDIAN
    # libpeer reassembles H.264 video frames in a fixed buffer of this size, 100 KB by default,
    # in internal memory. The puck has no video, so a tiny one saves that memory.
    CONFIG_MAX_NALU_SIZE=1024
    # An ECDSA certificate instead of RSA: much quicker to make, and every browser takes it.
    CONFIG_DTLS_USE_ECDSA=1
    # Test hook (our addition in sctp.c): act as if every Nth incoming DATA chunk was lost.
    # 0 is off; only for testing resends and FORWARD-TSN.
    SCTP_TEST_DROP_EVERY=0
)

# libpeer has warnings (switches without every case, and similar) that ESP-IDF would turn
# into errors. They are upstream's and harmless, so they stay warnings here.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
