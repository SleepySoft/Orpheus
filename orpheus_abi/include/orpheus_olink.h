#ifndef ORPHEUS_OLINK_H
#define ORPHEUS_OLINK_H

/* OLINK：Orpheus 消息（§18 信封）上串行字节流的成帧层。
 * 线上帧 = COBS( 消息 || CRC16-CCITT 小端 ) || 0x00 定界。
 * 纯成帧/校验，不含消息语义；COBS 保证线上不含 0x00，故 0x00 恒为帧尾，
 * 任意错位最多丢一帧、下一帧自动重新同步。
 * 无动态内存、无平台依赖（C99），嵌入式直接可用。 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OLINK_MSG_MAX   (8u + 1023u * 4u)          /* §18 信封最大 4104 字节 */
#define OLINK_CRC_LEN   2u
#define OLINK_FRAME_MAX (OLINK_MSG_MAX + OLINK_CRC_LEN + (OLINK_MSG_MAX + 2u) / 254u + 2u)

/* CRC16-CCITT-FALSE（poly 0x1021，初值 0xFFFF，不反射）："123456789" -> 0x29B1 */
uint16_t olink_crc16(const uint8_t* data, uint16_t len);

/* 编码：msg -> 线上帧（含尾部 0x00）。返回帧长；out_cap 不足返回 0。
 * 实现内联拼接 CRC，不需要 msg 之外的临时缓冲。 */
uint16_t olink_encode(const uint8_t* msg, uint16_t msg_len, uint8_t* out, uint16_t out_cap);

/* 流式解码器：逐字节喂入（olink_decode_byte），凑齐一帧且 CRC 正确即返回帧长。
 * 帧缓冲由调用方提供（建议 >= OLINK_MSG_MAX 容量），内部不含大缓冲。 */
typedef struct {
    uint16_t pos;        /* 当前已解码字节数（含 CRC 尾部） */
    uint8_t  code_left;  /* 当前 COBS run 剩余数据字节 */
    uint8_t  prev_code;  /* 上一 run 的码字节（0=帧起始；0xFF 无隐含零） */
    uint8_t  overflow;   /* 本帧已溢出（丢弃至帧尾） */
} OLinkDecoder;

void olink_decoder_init(OLinkDecoder* d);

/* 喂入一个字节。
 * 返回 >0：收到完整帧且 CRC 正确，帧内容在 frame_buf[0..ret)（CRC 已剥除）；
 * 返回  0：未凑齐，或本帧 CRC 错误/超长被丢弃（解码器已自动复位重同步）。 */
uint16_t olink_decode_byte(OLinkDecoder* d, uint8_t byte, uint8_t* frame_buf, uint16_t frame_cap);

#ifdef __cplusplus
}
#endif

#endif /* ORPHEUS_OLINK_H */
