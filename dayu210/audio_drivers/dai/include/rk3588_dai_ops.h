/*
 * Copyright (C) 2022 HiHope Open Source Organization .
 *
 * HDF is dual licensed: you can use it either under the terms of
 * the GPL, or the BSD license, at your option.
 * See the LICENSE file in the root of this repository for complete details.
 */

#ifndef RK3588_DAI_OPS_H
#define RK3588_DAI_OPS_H

#include "audio_core.h"
#include <sound/dmaengine_pcm.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

int32_t Rk3588DeviceReadReg(const struct DaiDevice *dai, uint32_t reg, uint32_t *val);
int32_t Rk3588DeviceWriteReg(const struct DaiDevice *dai, uint32_t reg, uint32_t value);

int32_t Rk3588NormalTrigger(const struct AudioCard *card,
    int cmd, const struct DaiDevice *dai);
int32_t Rk3588DaiHwParams(const struct AudioCard *card,
    const struct AudioPcmHwParams *param);
int32_t Rk3588DaiStartup(const struct AudioCard *card,
    const struct DaiDevice *dai);
int32_t Rk3588DaiDeviceInit(struct AudioCard *card,
    const struct DaiDevice *dai);

#ifdef __cplusplus
#if __cplusplus
        }
#endif
#endif /* __cplusplus */
        
#endif

