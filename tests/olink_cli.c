/* olink_cli：OLINK 帧级命令行工具，供 Python 侧做跨语言互测。
 * 行协议（stdin/stdout 文本）：
 *   crc <hex>   -> 打印 4 位十六进制 CRC16
 *   enc <hex>   -> 打印线上帧 hex（含尾部 00 定界）
 *   dec <hex>   -> 逐字节喂入线上字节流；每个完整且 CRC 正确的帧打印一行 "F <hex>"；
 *                  输入含帧界但没解出任何帧时打印 "BAD"
 *   quit        -> 退出
 */
#include "orpheus_olink.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t unhex(const char* s, uint8_t* out, size_t cap) {
    size_t n = 0;
    while (s[0] && s[1] && s[0] != '\n' && s[1] != '\n' && n < cap) {
        int hi = hexval(s[0]), lo = hexval(s[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (uint8_t)((hi << 4) | lo);
        s += 2;
    }
    return n;
}

static void print_hex(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) printf("%02x", data[i]);
    putchar('\n');
}

int main(void) {
    static char line[16384];
    static uint8_t in[OLINK_FRAME_MAX + 16];   /* dec 输入是线上帧（含 COBS 膨胀），比消息大 */
    static uint8_t frame[OLINK_FRAME_MAX + 16];
    static uint8_t msg[OLINK_MSG_MAX + 8];
    OLinkDecoder dec;
    olink_decoder_init(&dec);

    while (fgets(line, sizeof(line), stdin)) {
        char* sp = strchr(line, ' ');
        if (strncmp(line, "quit", 4) == 0) break;
        if (!sp) continue;
        *sp = 0;
        const char* hex = sp + 1;
        if (strcmp(line, "crc") == 0) {
            size_t n = unhex(hex, in, sizeof(in));
            printf("%04x\n", olink_crc16(in, (uint16_t)n));
        } else if (strcmp(line, "enc") == 0) {
            size_t n = unhex(hex, in, sizeof(in));
            uint16_t m = olink_encode(in, (uint16_t)n, frame, sizeof(frame));
            if (m == 0) printf("ERR\n");
            else print_hex(frame, m);
        } else if (strcmp(line, "dec") == 0) {
            size_t n = unhex(hex, in, sizeof(in));
            int got = 0;
            for (size_t i = 0; i < n; ++i) {
                uint16_t m = olink_decode_byte(&dec, in[i], msg, sizeof(msg));
                if (m > 0) { printf("F "); print_hex(msg, m); got = 1; }
            }
            /* 输入含定界却一帧未出 -> 明确报告（CRC 错/超长/垃圾） */
            if (!got && memchr(in, 0x00, n) != NULL) printf("BAD\n");
        }
        fflush(stdout);
    }
    return 0;
}
