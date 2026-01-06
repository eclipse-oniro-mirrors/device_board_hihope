/*
 * Copyright (c) 2025 Phytium Technology Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "alsa_snd_render.h"
#include "common.h"
#include "codec_config/config.h"


#define HDF_LOG_TAG HDF_AUDIO_HAL_RENDER

typedef struct _RENDER_DATA_ {
    struct AlsaMixerCtlElement ctrlVolume;
    long tempVolume;
}RenderData;

static int32_t SndElementReadNumid(struct AlsaSoundCard *cardIns,
    struct AlsaMixerCtlElement *ctlElem,
    unsigned int *numid)
{
    return 0 ;
}
#include "common.c"

static int32_t RenderInitImpl(struct AlsaRender *renderIns)
{
    int32_t ret;
    unsigned int numid;
    const struct ConfigRoute *routeInfo;
    struct AlsaSoundCard *cardIns = (struct AlsaSoundCard *)renderIns;

    if (renderIns->priData != NULL) {
        return HDF_SUCCESS;
    }

    CHECK_NULL_PTR_RETURN_DEFAULT(renderIns);

    RenderData *priData = (RenderData *)OsalMemCalloc(sizeof(RenderData));
    if (priData == NULL) {
        AUDIO_FUNC_LOGE("Failed to allocate memory!");
        return HDF_FAILURE;
    }

    if (AlsaRouteInit() != HDF_SUCCESS) {
        AUDIO_FUNC_LOGE("Render RouteInit Failure!");
        return HDF_FAILURE;
    }

    routeInfo = GetRouteConfig(SPEAKER_NORMAL_ROUTE);
    if (routeInfo->controlsCount <= 0) {
        AUDIO_FUNC_LOGE("couldn't find controls!");
        return HDF_FAILURE;
    }

    AUDIO_FUNC_LOGI("routeInfo ctlName =%s", routeInfo->controls[0].ctlName);
    SndElementItemInit(&priData->ctrlVolume);
    priData->ctrlVolume.name = (char *)routeInfo->controls[0].ctlName;
    ret = SndElementReadNumid(cardIns, &priData->ctrlVolume, &numid);
    if (ret != HDF_SUCCESS) {
            AUDIO_FUNC_LOGE("read numid failed!");
            return HDF_FAILURE;
    }
    priData->ctrlVolume.numid = numid;
    AUDIO_FUNC_LOGI("priData->ctrlVolume.name =%s, priData->ctrlVolume.numid=%d",
        priData->ctrlVolume.name, priData->ctrlVolume.numid);
    RenderSetPriData(renderIns, (RenderPriData)priData);

    ret = AlsaElementsSet(&renderIns->soundCard, SPEAKER_NORMAL_ROUTE);
    if (ret != HDF_SUCCESS) {
        AUDIO_FUNC_LOGE("AlsaElementsSet SPEAKER_NORMAL_ROUTE fail!");
        return HDF_FAILURE;
    }
    AUDIO_FUNC_LOGI("RenderInitImpl success!");
    return HDF_SUCCESS;
}

static int32_t RenderSelectSceneImpl(struct AlsaRender *renderIns, const struct AudioHwRenderParam *handleData)
{
    AUDIO_FUNC_LOGI("Not support descPins");
    return HDF_SUCCESS;
}

static int32_t RenderGetVolThresholdImpl(struct AlsaRender *renderIns, long *volMin, long *volMax)
{
    int32_t ret;
    long _volMin = 0;
    long _volMax = 0;
    struct AlsaSoundCard *cardIns = (struct AlsaSoundCard *)renderIns;
    RenderData *priData = RenderGetPriData(renderIns);
    CHECK_NULL_PTR_RETURN_DEFAULT(cardIns);
    CHECK_NULL_PTR_RETURN_DEFAULT(priData);

    ret = SndElementReadRange(cardIns, &priData->ctrlVolume, &_volMin, &_volMax);
    if (ret != HDF_SUCCESS) {
        AUDIO_FUNC_LOGE("SndElementReadRange fail!");
        return HDF_FAILURE;
    }
    *volMin = _volMin;
    *volMax = _volMax;
    AUDIO_FUNC_LOGI("RenderGetVolumeImpl min=%ld  max=%ld !", _volMin, _volMax);
    return HDF_SUCCESS;
}

static int32_t RenderGetVolumeImpl(struct AlsaRender *renderIns, long *volume)
{
    int32_t ret;
    struct AlsaSoundCard *cardIns = (struct AlsaSoundCard *)renderIns;
    RenderData *priData = RenderGetPriData(renderIns);
    CHECK_NULL_PTR_RETURN_DEFAULT(cardIns);
    CHECK_NULL_PTR_RETURN_DEFAULT(priData);

    ret = SndElementReadInt(cardIns, &priData->ctrlVolume, volume);
    if (ret != HDF_SUCCESS) {
        AUDIO_FUNC_LOGE("Read Volume fail!");
        return HDF_FAILURE;
    }

    AUDIO_FUNC_LOGI("RenderGetVolumeImpl %ld !!!!!", *volume);
    return HDF_SUCCESS;
}

static int32_t RenderSetVolumeImpl(struct AlsaRender *renderIns, long volume)
{
    int32_t ret;
    struct AlsaSoundCard *cardIns = (struct AlsaSoundCard *)renderIns;
    RenderData *priData = RenderGetPriData(renderIns);
    CHECK_NULL_PTR_RETURN_DEFAULT(cardIns);
    CHECK_NULL_PTR_RETURN_DEFAULT(priData);

    ret = SndElementWriteInt(cardIns, &priData->ctrlVolume, volume);
    if (ret != HDF_SUCCESS) {
        AUDIO_FUNC_LOGE("Write volume fail!");
        return HDF_FAILURE;
    }
    AUDIO_FUNC_LOGI("RenderSetVolumeImpl %ld !!!!!", volume);
    return HDF_SUCCESS;
}

static bool RenderGetMuteImpl(struct AlsaRender *renderIns)
{
    return renderIns->muteState;
}

static int32_t RenderSetMuteImpl(struct AlsaRender *renderIns, bool muteFlag)
{
    int32_t ret;
    long vol, setVol;
    RenderData *priData = RenderGetPriData(renderIns);
    CHECK_NULL_PTR_RETURN_DEFAULT(renderIns);
    CHECK_NULL_PTR_RETURN_DEFAULT(priData);

    ret = renderIns->GetVolume(renderIns, &vol);
    if (ret != HDF_SUCCESS) {
        AUDIO_FUNC_LOGE("GetVolume failed!");
        return HDF_FAILURE;
    }

    if (muteFlag) {
        priData->tempVolume = vol;
        setVol = 0;
    } else {
        setVol = priData->tempVolume;
    }

    renderIns->SetVolume(renderIns, setVol);
    if (ret != HDF_SUCCESS) {
        AUDIO_FUNC_LOGE("SetVolume failed!");
        return HDF_FAILURE;
    }
    renderIns->muteState = muteFlag;
    return HDF_SUCCESS;
}

static int32_t RenderStartImpl(struct AlsaRender *renderIns, const struct AudioHwRenderParam *handleData)
{
    int32_t ret;

    CHECK_NULL_PTR_RETURN_DEFAULT(renderIns);
    CHECK_NULL_PTR_RETURN_DEFAULT(&renderIns->soundCard);

    ret = AlsaElementsSet(&renderIns->soundCard, PLAYBACK_ON_ROUTE);
    if (ret != HDF_SUCCESS) {
        AUDIO_FUNC_LOGE("RenderStartImpl failed!");
        return HDF_FAILURE;
    }

    AUDIO_FUNC_LOGI("RenderStopImpl success!");
    return HDF_SUCCESS;
}

static int32_t RenderStopImpl(struct AlsaRender *renderIns)
{
    int32_t ret;

    CHECK_NULL_PTR_RETURN_DEFAULT(renderIns);
    CHECK_NULL_PTR_RETURN_DEFAULT(&renderIns->soundCard);

    ret = AlsaElementsSet(&renderIns->soundCard, PLAYBACK_OFF_ROUTE);
    if (ret != HDF_SUCCESS) {
        AUDIO_FUNC_LOGE("RenderStoptImpl failed!");
        return HDF_FAILURE;
    }
    snd_pcm_drain(renderIns->soundCard.pcmHandle);
    AUDIO_FUNC_LOGI("RenderStopImpl success!");
    return HDF_SUCCESS;
}

static int32_t RenderGetGainThresholdImpl(struct AlsaRender *renderIns, float *gainMin, float *gainMax)
{
    AUDIO_FUNC_LOGI("Not support gain operation");
    return HDF_SUCCESS;
}

static int32_t RenderGetGainImpl(struct AlsaRender *renderIns, float *volume)
{
    AUDIO_FUNC_LOGI("Not support gain operation");
    return HDF_SUCCESS;
}

static int32_t RenderSetGainImpl(struct AlsaRender *renderIns, float volume)
{
    AUDIO_FUNC_LOGI("Not support gain operation");
    return HDF_SUCCESS;
}

static int32_t RenderGetChannelModeImpl(struct AlsaRender *renderIns, enum AudioChannelMode *mode)
{
    return HDF_SUCCESS;
}

static int32_t RenderSetChannelModeImpl(struct AlsaRender *renderIns, enum AudioChannelMode mode)
{
    return HDF_SUCCESS;
}

int32_t RenderOverrideFunc(struct AlsaRender *renderIns)
{
    struct AlsaSoundCard *cardIns = (struct AlsaSoundCard *)renderIns;

    if (cardIns->cardType == SND_CARD_PRIMARY) {
        renderIns->Init = RenderInitImpl;
        renderIns->SelectScene = RenderSelectSceneImpl;
        renderIns->Start = RenderStartImpl;
        renderIns->Stop = RenderStopImpl;
        renderIns->GetVolThreshold = RenderGetVolThresholdImpl;
        renderIns->GetVolume = RenderGetVolumeImpl;
        renderIns->SetVolume = RenderSetVolumeImpl;
        renderIns->GetGainThreshold = RenderGetGainThresholdImpl;
        renderIns->GetGain = RenderGetGainImpl;
        renderIns->SetGain = RenderSetGainImpl;
        renderIns->GetMute = RenderGetMuteImpl;
        renderIns->SetMute = RenderSetMuteImpl;
        renderIns->GetChannelMode = RenderGetChannelModeImpl;
        renderIns->SetChannelMode = RenderSetChannelModeImpl;
    }

    return HDF_SUCCESS;
}
