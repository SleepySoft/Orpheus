#ifndef ORPHEUS_GAIN_ARRAY_H
#define ORPHEUS_GAIN_ARRAY_H

#include "orpheus_abi.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>

/* 仅用于prepare：空串采用增益1/整数0；非空必须完整声明全部项。 */
static inline int orpheus_parse_gain_array(const char* cursor, void* values, unsigned count,
                                          bool integer, int32_t minimum, int32_t maximum) {
    if (!cursor) return ORPHEUS_ERR_INVALID_ARG;
    if (!*cursor) {
        for (unsigned i=0;i<count;++i) {
            if (integer) ((int32_t*)values)[i]=0;
            else ((float*)values)[i]=1;
        }
        return ORPHEUS_OK;
    }
    for (unsigned i=0;i<count;++i) {
        char* end;
        errno=0;
        if (integer) {
            long long value=strtoll(cursor,&end,10);
            if (errno==ERANGE || value<minimum || value>maximum) return ORPHEUS_ERR_INVALID_ARG;
            ((int32_t*)values)[i]=(int32_t)value;
        } else {
            float value=strtof(cursor,&end);
            if (!isfinite(value) || value<0) return ORPHEUS_ERR_INVALID_ARG;
            ((float*)values)[i]=value;
        }
        if (end==cursor) return ORPHEUS_ERR_INVALID_ARG;
        while (isspace((unsigned char)*end)) ++end;
        if (i+1<count ? *end!=',' : *end!='\0') return ORPHEUS_ERR_INVALID_ARG;
        cursor=i+1<count ? end+1 : end;
    }
    return ORPHEUS_OK;
}

#endif
