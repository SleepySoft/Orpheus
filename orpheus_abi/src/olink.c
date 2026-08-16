#include "orpheus_olink.h"

uint16_t olink_crc16(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFFu;
    for (uint16_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t olink_encode(const uint8_t* msg, uint16_t msg_len, uint8_t* out, uint16_t out_cap) {
    if (out == NULL || (msg == NULL && msg_len > 0)) return 0;
    uint8_t tail[2];
    uint16_t crc = olink_crc16(msg, msg_len);
    tail[0] = (uint8_t)(crc & 0xFFu);   /* CRC 小端附在消息尾 */
    tail[1] = (uint8_t)(crc >> 8);

    if (out_cap < 2) return 0;
    uint16_t di = 0;
    uint16_t code_pos = di++;           /* 首个 run 的码字节占位 */
    uint8_t code = 1;
    for (uint8_t seg = 0; seg < 2; ++seg) {
        const uint8_t* p = (seg == 0) ? msg : tail;
        uint16_t n = (seg == 0) ? msg_len : (uint16_t)OLINK_CRC_LEN;
        for (uint16_t i = 0; i < n; ++i) {
            uint8_t b = p[i];
            if (b == 0) {
                out[code_pos] = code;   /* 收尾当前 run，开新 run */
                code_pos = di++;
                code = 1;
                if (di > out_cap) return 0;
            } else {
                if (di >= out_cap) return 0;
                out[di++] = b;
                if (++code == 0xFFu) {  /* run 满 254：收尾且无隐含零 */
                    out[code_pos] = code;
                    code_pos = di++;
                    code = 1;
                }
            }
        }
    }
    out[code_pos] = code;
    if (di >= out_cap) return 0;
    out[di++] = 0x00;                   /* 帧界定界 */
    return di;
}

void olink_decoder_init(OLinkDecoder* d) {
    d->pos = 0;
    d->code_left = 0;
    d->prev_code = 0;
    d->overflow = 0;
}

static uint16_t finish_frame(OLinkDecoder* d, uint8_t* frame_buf) {
    /* 剥除尾部 CRC 并校验；任何异常都复位丢弃，返回 0（下一帧自动重同步） */
    uint16_t n = d->pos;
    olink_decoder_init(d);
    if (n < OLINK_CRC_LEN) return 0;   /* 空帧（连发定界） */
    uint16_t msg_len = (uint16_t)(n - OLINK_CRC_LEN);
    if (msg_len == 0) return 0;  /* 空帧丢弃：§18 消息最小 8 字节，0 与「无帧」无法区分 */
    uint16_t got = (uint16_t)frame_buf[msg_len] | ((uint16_t)frame_buf[msg_len + 1u] << 8);
    if (olink_crc16(frame_buf, msg_len) != got) return 0;
    return msg_len;
}

uint16_t olink_decode_byte(OLinkDecoder* d, uint8_t byte, uint8_t* frame_buf, uint16_t frame_cap) {
    if (byte == 0x00u) {                /* 帧界 */
        if (d->pos == 0) { olink_decoder_init(d); return 0; }  /* 连发定界/起始垃圾 */
        if (d->overflow) { olink_decoder_init(d); return 0; }
        return finish_frame(d, frame_buf);
    }
    if (d->code_left > 0) {
        d->code_left--;
        if (d->pos >= frame_cap) { d->overflow = 1; return 0; }
        frame_buf[d->pos++] = byte;
        return 0;
    }
    /* 码字节：上一 run 若非 0xFF 则补一个隐含零（帧起始除外） */
    if (d->prev_code != 0xFFu && d->prev_code != 0u) {
        if (d->pos >= frame_cap) { d->overflow = 1; return 0; }
        frame_buf[d->pos++] = 0x00u;
    }
    d->prev_code = byte;
    d->code_left = (uint8_t)(byte - 1u);
    return 0;
}
