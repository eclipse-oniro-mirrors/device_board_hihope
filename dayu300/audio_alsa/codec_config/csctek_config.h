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

#ifndef CSCTEK_CONFIG_H_
#define CSCTEK_CONFIG_H_

#include "config.h"

const struct ConfigControl CSCTEK_SPEAKER_NORMAL_CONTROLS[] = {
    {
        .ctlName = "PCM Playback Volume",
        .intVal = {768, 768},
    },
    {
        .ctlName = "PCM Playback Switch",
        .intVal = {1},
    },
};

const struct ConfigControl CSCTEK_MAIN_MIC_CAPTURE_CONTROLS[] = {
    {
        .ctlName = "Mic Capture Volume",
        .intVal = {384},
    },
    {
        .ctlName = "Mic Capture Switch",
        .intVal = {1},
    },
};


const struct ConfigControl CSCTEK_PLAYBACK_ON_CONTROLS[] = {
};

const struct ConfigControl CSCTEK_PLAYBACK_OFF_CONTROLS[] = {
    {
        .ctlName = "PCM Playback Volume",
        .intVal = {0, 0},
    },
    {
        .ctlName = "PCM Playback Switch",
        .intVal = {0},
    },
};

const struct ConfigControl CSCTEK_CAPTURE_ON_CONTROLS[] = {
};

const struct ConfigControl CSCTEK_CAPTURE_OFF_CONTROLS[] = {
};

const struct ConfigRouteTable CSCTEK_CONFIG_TABLE = {
    //speaker init
    .speakerNormal = {
        .controls = CSCTEK_SPEAKER_NORMAL_CONTROLS,
        .controlsCount = sizeof(CSCTEK_SPEAKER_NORMAL_CONTROLS) / sizeof(struct ConfigControl),
    },
    //capture init
    .mainMicCapture = {
        .controls = CSCTEK_MAIN_MIC_CAPTURE_CONTROLS,
        .controlsCount = sizeof(CSCTEK_MAIN_MIC_CAPTURE_CONTROLS) / sizeof(struct ConfigControl),
    },
    //playback on
    .playbackOn = {
        .controls = CSCTEK_PLAYBACK_ON_CONTROLS,
        .controlsCount = sizeof(CSCTEK_PLAYBACK_ON_CONTROLS) / sizeof(struct ConfigControl),
    },
    //playback off
    .playbackOff = {
        .controls = CSCTEK_PLAYBACK_OFF_CONTROLS,
        .controlsCount = sizeof(CSCTEK_PLAYBACK_OFF_CONTROLS) / sizeof(struct ConfigControl),
    },
    //capture on
    .captureOn = {
        .controls = CSCTEK_CAPTURE_ON_CONTROLS,
        .controlsCount = sizeof(CSCTEK_CAPTURE_ON_CONTROLS) / sizeof(struct ConfigControl),
    },
    //capture off
    .captureOff = {
        .controls = CSCTEK_CAPTURE_OFF_CONTROLS,
        .controlsCount = sizeof(CSCTEK_CAPTURE_OFF_CONTROLS) / sizeof(struct ConfigControl),
    },
};


#endif //CSCTEK_CONFIG_H_
