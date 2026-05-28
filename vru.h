#ifndef N64_VIDEO_ROM_VRU_H
#define N64_VIDEO_ROM_VRU_H

#include <stdbool.h>
#include <stdint.h>

/*
 * VRU is expected on controller port 4 (index 3), but all APIs take an
 * explicit port so we can probe/debug other ports.
 */
#define VRU_DEFAULT_PORT 3
#define VRU_DEFAULT_ADDRESS 0xFFE0

/* Joybus commands used by VRU/VRS. */
#define VRU_CMD_READ_RESULT 0x09
#define VRU_CMD_WRITE_CHUNK 0x0A
#define VRU_CMD_READ_STATUS 0x0B
#define VRU_CMD_WRITE_CONFIG 0x0C
#define VRU_CMD_WRITE_INIT 0x0D

/* Common VRU status/mode values seen in libultra docs and captures. */
#define VRU_STATE_READY 0x0000
#define VRU_STATE_START 0x0001
#define VRU_STATE_CANCEL 0x0003
#define VRU_STATE_BUSY 0x0005
#define VRU_STATE_END 0x0007
/* Bitfield warnings observed in captures (big-endian u16 values). */
#define VRU_WARN_TOO_LOW 0x0400
#define VRU_WARN_TOO_HIGH 0x0800
#define VRU_WARN_NO_MATCH 0x4000
#define VRU_WARN_TOO_NOISY 0x8000

typedef struct {
    uint16_t warning;
    uint16_t answer_num;
    uint16_t voice_level;
    uint16_t voice_sn;
    uint16_t voice_time;
    uint16_t answer[5];
    uint16_t distance[5];
    uint16_t mode_status;
} vru_result_t;

typedef struct {
    uint16_t identifier;
    uint8_t status;
    bool present;
    bool ready;
} vru_identify_t;

typedef struct {
    int port;
    uint16_t address;
} vru_device_t;

enum {
    VRU_OK = 0,
    VRU_ERR_ARG = -1,
    VRU_ERR_IDENT = -2,
    VRU_ERR_CRC = -3,
    VRU_ERR_IO = -4,
};

/* Initialize descriptor with sane defaults. */
void vru_device_init(vru_device_t *dev, int port);

/* Poll Joybus identify (0x00) for VRU presence/readiness. */
bool vru_identify(const vru_device_t *dev, vru_identify_t *out);

/* Read VRU state/mode (0x0B). Returns 0 on success, <0 on transport/CRC error. */
int vru_read_state(const vru_device_t *dev, uint16_t *state);

/* Low-level init step (0x0D). addr_without_crc is one of 0x00F0/0x0370/... */
int vru_write_init(const vru_device_t *dev, uint16_t addr_without_crc, uint8_t *out_error_flags);

/* Write 4-byte config payload (0x0C). */
int vru_write_config(const vru_device_t *dev, const uint8_t payload[4], uint8_t *out_crc);

/* Write one 20-byte dictionary chunk (0x0A). */
int vru_write_chunk(const vru_device_t *dev, const uint8_t payload[20], uint8_t *out_crc);

/* Read recognition result frame (0x09). */
int vru_read_result(const vru_device_t *dev, vru_result_t *out);

/* Bring VRU to a known initialized state using the observed 0x0D sequence. */
int vru_bootstrap(const vru_device_t *dev, bool us_timing);

/* Convenience wrappers around common 0x0C modes from captures/libultra flow. */
int vru_start_listen(const vru_device_t *dev);
int vru_stop_listen(const vru_device_t *dev);
int vru_request_results(const vru_device_t *dev);
int vru_clear_dictionary(const vru_device_t *dev, uint8_t word_count);

/*
 * Poll identify until VRU reports present+ready, or timeout expires.
 * timeout_ms <= 0 performs a single check.
 */
int vru_wait_ready(const vru_device_t *dev, int timeout_ms, int poll_ms);

/*
 * Poll status with low CPU usage until recognition leaves START/BUSY state,
 * then fetch one result frame (0x09). timeout_ms <= 0 performs one pass.
 */
int vru_wait_for_result(const vru_device_t *dev, vru_result_t *out, int timeout_ms, int poll_ms);

#endif
