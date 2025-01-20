/*
 * Copyright (C) 2022 HiHope Open Source Organization .
 *
 * HDF is dual licensed: you can use it either under the terms of
 * the GPL, or the BSD license, at your option.
 * See the LICENSE file in the root of this repository for complete details.
 */

#ifndef RK3588_PLATFORM_OPS_H
#define RK3588_PLATFORM_OPS_H

#include <linux/dmaengine.h>
#include "audio_core.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

int32_t AudioDmaDeviceInit(const struct AudioCard *card, const struct PlatformDevice *platform);
int32_t Rk3588DmaBufAlloc(struct PlatformData *data, const enum AudioStreamType streamType);
int32_t Rk3588DmaBufFree(struct PlatformData *data, const enum AudioStreamType streamType);
int32_t Rk3588DmaRequestChannel(const struct PlatformData *data, const enum AudioStreamType streamType);
int32_t Rk3588DmaConfigChannel(const struct PlatformData *data, const enum AudioStreamType streamType);
int32_t Rk3588PcmPointer(struct PlatformData *data, const enum AudioStreamType streamType, uint32_t *pointer);
int32_t Rk3588DmaPrep(const struct PlatformData *data, const enum AudioStreamType streamType);
int32_t Rk3588DmaSubmit(const struct PlatformData *data, const enum AudioStreamType streamType);
int32_t Rk3588DmaPending(struct PlatformData *data, const enum AudioStreamType streamType);
int32_t Rk3588DmaPause(struct PlatformData *data, const enum AudioStreamType streamType);
int32_t Rk3588DmaResume(const struct PlatformData *data, const enum AudioStreamType streamType);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* RK3588_PLATFORM_OPS_H */
