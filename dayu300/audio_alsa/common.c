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

#include "common.h"
#include "codec_config/config_list.h"
#define  MAX_SND_CARDS 2


const struct ConfigRouteTable *g_routeTable = NULL;

int32_t AlsaRouteInit(void)
{
    char soundCardID[20] = "";
    char str[32] = "";
    FILE* fp = NULL;
    unsigned i;
    unsigned configCount = sizeof(g_soundCardConfigList) / sizeof(struct AlsaSoundCardConfig);
    size_t read_size;
    int32_t card = 0;

    AUDIO_FUNC_LOGI("AUDIO route_init()");

    for (card = 0; card < MAX_SND_CARDS; card++) {
        (void)sprintf_s(str, sizeof(str), "/proc/asound/card%d/id", card);
        AUDIO_FUNC_LOGI("AUDIO route_init() str=%s", str);
        if (access(str, 0)) {
            continue;
        }
        fp = fopen(str, "r");
        if (!fp) {
            AUDIO_FUNC_LOGE("read %s failed", str);
            continue;
        }
        read_size = fread(soundCardID, sizeof(char), sizeof(soundCardID)/sizeof(char), fp);
        (void)fclose(fp);
        AUDIO_FUNC_LOGE("AUDIO route_init() read_size=%zu, %lu", read_size, sizeof(soundCardID)/sizeof(char));
        if (read_size == 0 || read_size > sizeof(soundCardID)/sizeof(char))
            continue;
        if (soundCardID[read_size - 1] == '\n') {
            read_size--;
            soundCardID[read_size] = '\0';
        }
        AUDIO_FUNC_LOGE("card[%d] Name is %s", card, soundCardID);

        for (i = 0; i < configCount; i++) {
            if (!(g_soundCardConfigList + i) || !g_soundCardConfigList[i].soundCardName ||
                !g_soundCardConfigList[i].routeTable)
                continue;

            if (strncmp(g_soundCardConfigList[i].soundCardName, soundCardID, read_size) == 0) {
                    g_routeTable = g_soundCardConfigList[i].routeTable;
                    AUDIO_FUNC_LOGI("Get route table for sound card %s", soundCardID);
                break;
                }
            }
        }
    if (!g_routeTable) {
        g_routeTable = &DEFAULT_CONFIG_TABLE;
        AUDIO_FUNC_LOGE("Can not get config table for sound card0 %s, so get default config table.", soundCardID);
        return HDF_FAILURE;
    }

    return HDF_SUCCESS;
}

const struct ConfigRoute *GetRouteConfig(enum AlsaAudioRoute route)
{
    AUDIO_FUNC_LOGD("GetRouteConfig() route %d", route);

    if (!g_routeTable) {
        AUDIO_FUNC_LOGE("GetRouteConfig() g_routeTable is NULL!");
        return NULL;
    }
    switch (route) {
        case SPEAKER_NORMAL_ROUTE:
            AUDIO_FUNC_LOGD("SPEAKER_NORMAL_ROUTE");
            return &(g_routeTable->speakerNormal);
        case MAIN_MIC_CAPTURE_ROUTE:
            return &(g_routeTable->mainMicCapture);
        case PLAYBACK_ON_ROUTE:
            return &(g_routeTable->playbackOn);
        case PLAYBACK_OFF_ROUTE:
            return &(g_routeTable->playbackOff);
        case CAPTURE_ON_ROUTE:
            return &(g_routeTable->captureOn);
        case CAPTURE_OFF_ROUTE:
            return &(g_routeTable->captureOff);
        default:
            AUDIO_FUNC_LOGE("GetRouteConfig() Error route %d", route);
            return NULL;
    }
}

int32_t AlsaElementsSet(struct AlsaSoundCard *cardIns, enum AlsaAudioRoute route)
{
    int32_t i;
    int32_t ret;
    unsigned int numid;
    struct AlsaMixerCtlElement elem;
    const struct ConfigRoute *routeInfo;

    CHECK_NULL_PTR_RETURN_DEFAULT(cardIns);

    routeInfo = GetRouteConfig(route);
    if (routeInfo->controls[0].ctlName==NULL || routeInfo->controls[0].strVal==NULL) {
        AUDIO_FUNC_LOGD("ctlName or strVal is NULL in config file，no need to set elem");
        return HDF_SUCCESS;
    }
    for (i = 0; i < routeInfo->controlsCount; i++) {
        SndElementItemInit(&elem);
        elem.name = (char*)routeInfo->controls[i].ctlName;
        elem.value = (char*)routeInfo->controls[i].strVal;
        ret = SndElementReadNumid(cardIns, &elem, &numid);
        AUDIO_FUNC_LOGD("AlsaElementsSet controls[%d] name=%s,value=%s numid=%d", i, elem.name, elem.value, numid);
        if (ret != HDF_SUCCESS) {
            AUDIO_FUNC_LOGE("AlsaElementsSet can't find elem name=%s !!!", elem.name);
            continue;
        }
        elem.numid=numid;
        ret = SndElementWrite(cardIns, &elem);
        if (ret != HDF_SUCCESS) {
            AUDIO_FUNC_LOGE("SndElementWrite fail!");
            return HDF_FAILURE;
        }
    }
    return HDF_SUCCESS;
}
