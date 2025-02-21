/*
 * Copyright (C) 2022 HiHope Open Source Organization .
 *
 * HDF is dual licensed: you can use it either under the terms of
 * the GPL, or the BSD license, at your option.
 * See the LICENSE file in the root of this repository for complete details.
 */

#include "es8323_codec_impl.h"
#include "audio_codec_if.h"
#include "audio_codec_base.h"
#include "audio_driver_log.h"

#define HDF_LOG_TAG "es8323_codec_adapter"

struct CodecData g_es8323Data = {
    .Init = Es8323DeviceInit,
    .Read = Es8323DeviceRegRead,
    .Write = Es8323DeviceRegWrite,
};

struct AudioDaiOps g_es8323DaiDeviceOps = {
    .Startup = Es8323DaiStartup,
    .HwParams = Es8323DaiHwParams,
    .Trigger = Es8323NormalTrigger,
};

struct DaiData g_es8323DaiData = {
    .drvDaiName = "codec_dai",
    .DaiInit = Es8323DaiDeviceInit,
    .ops = &g_es8323DaiDeviceOps,
};

/* HdfDriverEntry */
static int32_t GetServiceName(const struct HdfDeviceObject *device)
{
    const struct DeviceResourceNode *node = NULL;
    struct DeviceResourceIface *drsOps = NULL;
    int32_t ret;
    if (device == NULL) {
        AUDIO_DRIVER_LOG_ERR("input HdfDeviceObject object is nullptr.");
        return HDF_FAILURE;
    }
    node = device->property;
    if (node == NULL) {
        AUDIO_DRIVER_LOG_ERR("get drs node is nullptr.");
        return HDF_FAILURE;
    }
    drsOps = DeviceResourceGetIfaceInstance(HDF_CONFIG_SOURCE);
    if (drsOps == NULL || drsOps->GetString == NULL) {
        AUDIO_DRIVER_LOG_ERR("drsOps or drsOps getString is null!");
        return HDF_FAILURE;
    }
    ret = drsOps->GetString(node, "serviceName", &g_es8323Data.drvCodecName, 0);
    if (ret != HDF_SUCCESS) {
        AUDIO_DRIVER_LOG_ERR("read serviceName failed.");
        return ret;
    }
    AUDIO_DRIVER_LOG_ERR("g_es8323Data.drvCodecName = %s", g_es8323Data.drvCodecName);
    return HDF_SUCCESS;
}

/* HdfDriverEntry implementations */
static int32_t Es8323DriverBind(struct HdfDeviceObject *device)
{
    (void)device;
    AUDIO_DRIVER_LOG_DEBUG("success!");
    return HDF_SUCCESS;
}

static int32_t Es8323DriverInit(struct HdfDeviceObject *device)
{
    int32_t ret;
    if (device == NULL) {
        AUDIO_DRIVER_LOG_ERR("device is NULL.");
        return HDF_ERR_INVALID_OBJECT;
    }

    ret = Es8323GetConfigInfo(device, &g_es8323Data);
    if (ret !=  HDF_SUCCESS) {
        AUDIO_DRIVER_LOG_ERR("Es8323GetConfigInfo failed.");
        return ret;
    }
    if (CodecDaiGetPortConfigInfo(device, &g_es8323DaiData) != HDF_SUCCESS) {
        AUDIO_DRIVER_LOG_ERR("get port config info failed.");
        return HDF_FAILURE;
    }

    if (CodecSetConfigInfoOfControls(&g_es8323Data, &g_es8323DaiData) != HDF_SUCCESS) {
        AUDIO_DRIVER_LOG_ERR("set config info failed.");
        return HDF_FAILURE;
    }
    ret = GetServiceName(device);
    if (ret !=  HDF_SUCCESS) {
        AUDIO_DRIVER_LOG_ERR("GetServiceName failed.");
        return ret;
    }

    ret = AudioRegisterCodec(device, &g_es8323Data, &g_es8323DaiData);
    if (ret !=  HDF_SUCCESS) {
        AUDIO_DRIVER_LOG_ERR("AudioRegisterCodec failed.");
        return ret;
    }
    AUDIO_DRIVER_LOG_DEBUG("success!");
    return HDF_SUCCESS;
}

/* HdfDriverEntry definitions */
struct HdfDriverEntry g_es8323DriverEntry = {
    .moduleVersion = 1,
    .moduleName = "CODEC_ES8323",
    .Bind = Es8323DriverBind,
    .Init = Es8323DriverInit,
    .Release = NULL,
};
HDF_INIT(g_es8323DriverEntry);