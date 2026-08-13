#ifndef ORPHEUS_ANC_FXLMS_H
#define ORPHEUS_ANC_FXLMS_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ANC_FXLMS_MAX_CH  8
#define ANC_FXLMS_MAX_W   1024   /* \u81ea\u9002\u5e94\u6ee4\u6ce2\u5668 w \u6700\u5927\u9636\u6570 */

typedef struct {
    uint32_t channels;
    uint32_t w_len;        /* \u81ea\u9002\u5e94\u6ee4\u6ce2\u5668\u9636\u6570 */
    float    mu;           /* \u6b65\u957f */
    float    leak;         /* \u6cc4\u6f0f\u56e0\u5b50 */
    float    eps;          /* \u6b63\u5219\u5316 */

    float* w;              /* \u81ea\u9002\u5e94\u6ee4\u6ce2\u5668\u7cfb\u6570\uff0cch*w_len */
    float* xbuf;           /* \u53c2\u8003\u4fe1\u53f7\u5ef6\u8fdf\u7ebf\uff0cch*w_len */
    float* xf;             /* \u6ee4\u6ce2 x \u4fe1\u53f7\uff08\u901a\u8fc7\u4e8c\u6b21\u8def\u5f84\u6a21\u578b S \u6ee4\u6ce2\uff09 ch*w_len */
    float* xfd;            /* \u901a\u8fc7 S \u6ee4\u6ce2\u540e\u7684\u53c2\u8003\u5411\u91cf\uff08\u6bcf\u901a\u9053\uff09 */
    uint32_t* pos;         /* \u6bcf\u901a\u9053\u5ef6\u8fdf\u7ebf\u5199\u6307\u9488 */

    /* \u6b21\u8def\u5f84\u6a21\u578b S \uff08\u7ecf\u5178\u56fa\u5b9a\u5916\u6a21\uff0c\u53ef\u6301\u7eed\uff09 */
    float  s_gain;         /* \u6b21\u8def\u5f94\u76ca */
    float  s_delay;        /* \u6b21\u8def\u5ef6\u8fdf\uff08\u91c7\u6837\u70b9\uff09 */

    /* \u63a2\u9488 */
    double acc_px2;        /* \u53c2\u8003 x \u80fd\u91cf\uff08\u81ea\u9002\u5e94\u66f4\u65b0\u7528\uff09 */
    double acc_d2;         /* \u68c0\u6d4b\u4fe1\u53f7 d \u80fd\u91cf */
    double acc_e2;         /* \u6b8b\u5dee e \u80fd\u91cf */

    float  noise_reduction_db;   /* 10*log10(P_d / P_e) */
    uint64_t total_frames;
    float  power_d, power_e;
    char   json_detail[512];
} AncFxLmsState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
