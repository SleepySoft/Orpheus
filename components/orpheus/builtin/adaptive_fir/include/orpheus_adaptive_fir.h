#ifndef ORPHEUS_ADAPTIVE_FIR_H
#define ORPHEUS_ADAPTIVE_FIR_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AFIR_MAX_CH 32
#define AFIR_MAX_TAPS 1024

/* \u81ea\u9002\u5e94 FIR\uff08FxLMS \u6838\u5fc3\uff09\uff0c\u65e0\u73af\u5206\u89e3\uff1a
 *   x     : \u8f93\u51fa\u8bfb\u53d6\u7684\u53c2\u8003\u4fe1\u53f7\uff08\u539f\u59cb x\uff09
 *   deriv : \u66f4\u65b0\u7528\u7684 filtered-x \u4fe1\u53f7\uff08\u7ecf\u6b21\u7ea7\u8def\u5f84 S \u6ee4\u6ce2\uff09
 *   err   : \u88ab\u6d4b\u9ea6\u514b\u98ce d\uff08\u8bef\u5dee\u9ea6\uff09\uff0c\u5c31\u662f\u7b97\u6cd5\u7684\u76ee\u6807\u4fe1\u53f7
 *   out   : y = w^T x\uff08\u7528\u539f\u59cb\u53c2\u8003\u8bfb\u51fa\uff09
 *
 * \u5185\u90e8\u8ba1\u7b97\u8bef\u5dee\uff08\u628a\u201c\u8bef\u5dee\u8ba1\u7b97 + \u6743\u91cd\u66f4\u65b0\u201d\u5173\u5728\u4e00\u4e2a\u548c\u5b50\u5185\u4ee5\u907f\u514d\u6570\u636e\u6d41\u73af\uff09\uff1a
 *   e = d - sg*y       \uff08sg = secondary_gain\uff0c\u6b21\u7ea7\u8def\u5f84\u7ea6\u5b9a\u5e02\u5229\u76ca\uff09
 *   w <- leak*w + mu*e*deriv / (||deriv||^2 + eps)
 * \u4e0a\u5c42\u53ea\u9700\u628a filtered-x \u63a5\u5230 deriv\u3001\u8bef\u5dee\u9ea6\u63a5\u5230 err\u3001\u539f\u59cb x \u63a5\u5230 x\u3002
 */
typedef struct {
    uint32_t channels;
    uint32_t taps;
    float mu;
    float leak;
    float eps;
    float sg;      /* \u6b21\u7ea7\u8def\u5f84\u5e02\u76ca g */
    float* w;
    float* xbuf;
    float* dbuf;
    uint32_t* pos;
    uint64_t total_frames;
    float  acc_norm;
    float  conv_metric;
    char   json_detail[512];
} AdaptiveFirState;

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif
