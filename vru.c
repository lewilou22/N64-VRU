#include "vru.h"

#include <libdragon.h>
#include <stddef.h>
#include <string.h>

/* libdragon exports these helpers but older preview headers may omit declarations. */
extern uint16_t joybus_accessory_calculate_addr_checksum(uint16_t address);
extern uint8_t joybus_accessory_calculate_data_crc(const void *data, int size);

/* Command payload lives in a fixed 0xFFE0 VRU register window. */
#define VRU_ADDR_DEFAULT 0xFFE0u

static uint16_t be16_read(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint8_t vru_data_crc(const uint8_t *data, size_t size)
{
    return joybus_accessory_calculate_data_crc(data, (int)size);
}

static uint16_t vru_apply_addr_crc(uint16_t addr_without_crc)
{
    return joybus_accessory_calculate_addr_checksum(addr_without_crc);
}

static int vru_send_cmd(const vru_device_t *dev, size_t send_len, size_t recv_len, const void *send_data, void *recv_data)
{
    if (!dev || !send_data || send_len == 0)
        return -1;
    if (dev->port < 0 || dev->port > 3)
        return -1;
    if (recv_len > 0 && !recv_data)
        return -1;
    joybus_exec_cmd(dev->port, send_len, recv_len, send_data, recv_data);
    return 0;
}

static int vru_channel_reset(const vru_device_t *dev)
{
    uint8_t send[1] = {0xFD};
    return vru_send_cmd(dev, sizeof(send), 0, send, NULL);
}

static int vru_write_config_checked(const vru_device_t *dev, const uint8_t payload[4])
{
    uint8_t got_crc = 0;
    int rc = vru_write_config(dev, payload, &got_crc);
    if (rc != 0)
        return rc;
    return (got_crc == vru_data_crc(payload, 4)) ? VRU_OK : VRU_ERR_CRC;
}

static int vru_write_init_retry(const vru_device_t *dev, uint16_t addr_without_crc, int attempts)
{
    for (int i = 0; i < attempts; i++) {
        uint8_t flags = 0;
        int rc = vru_write_init(dev, addr_without_crc, &flags);
        if (rc != 0)
            continue;
        if ((flags & 0x01u) == 0)
            return VRU_OK;
    }
    return VRU_ERR_IO;
}

void vru_device_init(vru_device_t *dev, int port)
{
    if (!dev)
        return;
    dev->port = port;
    dev->address = VRU_ADDR_DEFAULT;
}

bool vru_identify(const vru_device_t *dev, vru_identify_t *out)
{
    if (!dev || !out)
        return false;

    uint8_t send[1] = {0x00};
    uint8_t recv[3] = {0};
    if (vru_send_cmd(dev, sizeof(send), sizeof(recv), send, recv) != 0)
        return false;

    out->identifier = be16_read(recv);
    out->status = recv[2];
    out->present = (out->identifier == JOYBUS_IDENTIFIER_N64_VOICE_RECOGNITION);
    out->ready = out->present && ((out->status & JOYBUS_IDENTIFY_STATUS_VOICE_RECOGNITON_READY) != 0);
    return true;
}

int vru_read_state(const vru_device_t *dev, uint16_t *state)
{
    if (!dev || !state)
        return -1;

    uint16_t addr = vru_apply_addr_crc(dev->address);
    uint8_t send[3] = {VRU_CMD_READ_STATUS, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFFu)};
    uint8_t recv[3] = {0};
    if (vru_send_cmd(dev, sizeof(send), sizeof(recv), send, recv) != 0)
        return -1;

    uint8_t got_crc = recv[2];
    uint8_t calc_crc = vru_data_crc(recv, 2);
    if (got_crc != calc_crc)
        return -2;

    *state = be16_read(recv);
    return 0;
}

int vru_write_init(const vru_device_t *dev, uint16_t addr_without_crc, uint8_t *out_error_flags)
{
    if (!dev)
        return -1;

    uint16_t reg = vru_apply_addr_crc(addr_without_crc);
    uint8_t send[3] = {VRU_CMD_WRITE_INIT, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFFu)};
    uint8_t recv[1] = {0};
    if (vru_send_cmd(dev, sizeof(send), sizeof(recv), send, recv) != 0)
        return -1;

    if (out_error_flags)
        *out_error_flags = recv[0];
    return 0;
}

int vru_write_config(const vru_device_t *dev, const uint8_t payload[4], uint8_t *out_crc)
{
    if (!dev || !payload)
        return -1;

    uint16_t addr = vru_apply_addr_crc(dev->address);
    uint8_t send[7] = {
        VRU_CMD_WRITE_CONFIG, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFFu), payload[0], payload[1], payload[2], payload[3]};
    uint8_t recv[1] = {0};
    if (vru_send_cmd(dev, sizeof(send), sizeof(recv), send, recv) != 0)
        return -1;

    if (out_crc)
        *out_crc = recv[0];
    return 0;
}

int vru_write_chunk(const vru_device_t *dev, const uint8_t payload[20], uint8_t *out_crc)
{
    if (!dev || !payload)
        return -1;

    uint16_t addr = vru_apply_addr_crc(dev->address);
    uint8_t send[23];
    memset(send, 0, sizeof(send));
    send[0] = VRU_CMD_WRITE_CHUNK;
    send[1] = (uint8_t)(addr >> 8);
    send[2] = (uint8_t)(addr & 0xFFu);
    memcpy(send + 3, payload, 20);

    uint8_t recv[1] = {0};
    if (vru_send_cmd(dev, sizeof(send), sizeof(recv), send, recv) != 0)
        return -1;

    if (out_crc)
        *out_crc = recv[0];
    return 0;
}

int vru_read_result(const vru_device_t *dev, vru_result_t *out)
{
    if (!dev || !out)
        return -1;

    uint16_t addr = vru_apply_addr_crc(dev->address);
    uint8_t send[3] = {VRU_CMD_READ_RESULT, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFFu)};
    uint8_t recv[37] = {0};
    if (vru_send_cmd(dev, sizeof(send), sizeof(recv), send, recv) != 0)
        return -1;

    uint8_t got_crc = recv[36];
    uint8_t calc_crc = vru_data_crc(recv, 36);
    if (got_crc != calc_crc)
        return -2;

    out->warning = be16_read(&recv[4]);
    out->answer_num = be16_read(&recv[6]);
    out->voice_level = be16_read(&recv[8]);
    out->voice_sn = be16_read(&recv[10]);
    out->voice_time = be16_read(&recv[12]);
    for (int i = 0; i < 5; i++) {
        out->answer[i] = be16_read(&recv[14 + i * 4]);
        out->distance[i] = be16_read(&recv[16 + i * 4]);
    }
    out->mode_status = be16_read(&recv[34]);
    return 0;
}

int vru_bootstrap(const vru_device_t *dev, bool us_timing)
{
    if (!dev)
        return VRU_ERR_ARG;

    vru_identify_t ident = {0};
    if (!vru_identify(dev, &ident))
        return VRU_ERR_IO;
    if (!ident.present)
        return VRU_ERR_IDENT;
    if (!ident.ready)
        return VRU_ERR_IDENT;

    if (vru_channel_reset(dev) != 0)
        return VRU_ERR_IO;

    if (us_timing)
        wait_ms(50);
    else
        wait_ms(1);

    static const uint16_t init_addrs[] = {0x00F0u, 0x0370u, 0x0040u, 0x02B0u, 0x0018u};
    for (size_t i = 0; i < sizeof(init_addrs) / sizeof(init_addrs[0]); i++) {
        int rc = vru_write_init_retry(dev, init_addrs[i], 4);
        if (rc != 0)
            return rc;
    }

    const uint8_t payload_boot[4] = {0x00, 0x00, 0x01, 0x00};
    int rc = vru_write_config_checked(dev, payload_boot);
    if (rc != 0)
        return rc;

    uint16_t state = 0;
    rc = vru_read_state(dev, &state);
    if (rc != 0)
        return rc;
    if ((state & 0xFF00u) != 0)
        return VRU_ERR_IO;

    return VRU_OK;
}

int vru_start_listen(const vru_device_t *dev)
{
    if (!dev)
        return VRU_ERR_ARG;
    const uint8_t payload[4] = {0x00, 0x00, 0x06, 0x00};
    return vru_write_config_checked(dev, payload);
}

int vru_stop_listen(const vru_device_t *dev)
{
    if (!dev)
        return VRU_ERR_ARG;
    const uint8_t payload[4] = {0x00, 0x00, 0x03, 0x00};
    return vru_write_config_checked(dev, payload);
}

int vru_request_results(const vru_device_t *dev)
{
    if (!dev)
        return VRU_ERR_ARG;
    const uint8_t payload[4] = {0x05, 0x00, 0x00, 0x00};
    return vru_write_config_checked(dev, payload);
}

int vru_clear_dictionary(const vru_device_t *dev, uint8_t word_count)
{
    if (!dev)
        return VRU_ERR_ARG;
    if (word_count == 0)
        return VRU_ERR_ARG;
    /*
     * Common clear-dictionary setup seen in captures:
     *   payload = { 0x00, 0x00, <word_count>, 0x02 }
     * where byte3 selects dictionary-init mode and byte2 is expected words.
     */
    const uint8_t payload[4] = {0x00, 0x00, word_count, 0x02};
    return vru_write_config_checked(dev, payload);
}

int vru_wait_ready(const vru_device_t *dev, int timeout_ms, int poll_ms)
{
    if (!dev)
        return VRU_ERR_ARG;
    if (poll_ms <= 0)
        poll_ms = 10;

    int elapsed = 0;
    do {
        vru_identify_t ident = {0};
        if (!vru_identify(dev, &ident))
            return VRU_ERR_IO;
        if (ident.present && ident.ready)
            return VRU_OK;
        if (timeout_ms <= 0)
            break;
        wait_ms(poll_ms);
        elapsed += poll_ms;
    } while (elapsed < timeout_ms);

    return VRU_ERR_IDENT;
}

int vru_wait_for_result(const vru_device_t *dev, vru_result_t *out, int timeout_ms, int poll_ms)
{
    if (!dev || !out)
        return VRU_ERR_ARG;
    if (poll_ms <= 0)
        poll_ms = 10;

    int elapsed = 0;
    do {
        uint16_t state = 0;
        int rc = vru_read_state(dev, &state);
        if (rc != 0)
            return rc;

        const uint16_t s = (uint16_t)(state & 0x00FFu);
        if (s != VRU_STATE_START && s != VRU_STATE_BUSY) {
            rc = vru_request_results(dev);
            if (rc != 0)
                return rc;
            return vru_read_result(dev, out);
        }

        if (timeout_ms <= 0)
            break;
        wait_ms(poll_ms);
        elapsed += poll_ms;
    } while (elapsed < timeout_ms);

    return VRU_ERR_IO;
}
