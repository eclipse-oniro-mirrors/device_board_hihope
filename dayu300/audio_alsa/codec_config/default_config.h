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

#ifndef DEFAULT_CONFIG_H_
#define DEFAULT_CONFIG_H_

#include "config.h"

const struct ConfigControl DEFAULT_SPEAKER_NORMAL_CONTROLS[] = {
    {
        .ctlName = "Playback Path",
        .strVal = "SPK",
    },
};


const struct ConfigControl DEFAULT_MAIN_MIC_CAPTURE_CONTROLS[] = {
    {
        .ctlName = "Capture MIC Path",
        .strVal = "Main Mic",
    },
};

const struct ConfigControl DEFAULT_PLAYBACK_ON_CONTROLS[] = {
    {
        .ctlName = "Playback Path",
        .strVal = "on",
    },
};

const struct ConfigControl DEFAULT_PLAYBACK_OFF_CONTROLS[] = {
    {
        .ctlName = "Playback Path",
        .strVal = "off",
    },
};

const struct ConfigControl DEFAULT_CAPTURE_ON_CONTROLS[] = {
    {
        .ctlName = "Capture MIC Path",
        .strVal = "on",
    },
};

const struct ConfigControl DEFAULT_CAPTURE_OFF_CONTROLS[] = {
    {
        .ctlName = "Capture MIC Path",
        .strVal = "off",
    },
};

const struct ConfigRouteTable DEFAULT_CONFIG_TABLE = {
    //speaker init
    .speakerNormal = {
        .controls = DEFAULT_SPEAKER_NORMAL_CONTROLS,
        .controlsCount = sizeof(DEFAULT_SPEAKER_NORMAL_CONTROLS) / sizeof(struct ConfigControl),
    },

    //capture init
    .mainMicCapture = {
        .controls = DEFAULT_MAIN_MIC_CAPTURE_CONTROLS,
        .controlsCount = sizeof(DEFAULT_MAIN_MIC_CAPTURE_CONTROLS) / sizeof(struct ConfigControl),
    },

    //playback on
    .playbackOn = {
        .controls = DEFAULT_PLAYBACK_ON_CONTROLS,
        .controlsCount = sizeof(DEFAULT_PLAYBACK_ON_CONTROLS) / sizeof(struct ConfigControl),
    },

    //playback off
    .playbackOff = {
        .controls = DEFAULT_PLAYBACK_OFF_CONTROLS,
        .controlsCount = sizeof(DEFAULT_PLAYBACK_OFF_CONTROLS) / sizeof(struct ConfigControl),
    },

    //capture on
    .captureOn = {
        .controls = DEFAULT_CAPTURE_ON_CONTROLS,
        .controlsCount = sizeof(DEFAULT_CAPTURE_ON_CONTROLS) / sizeof(struct ConfigControl),
    },

    //capture off
    .captureOff = {
        .controls = DEFAULT_CAPTURE_OFF_CONTROLS,
        .controlsCount = sizeof(DEFAULT_CAPTURE_OFF_CONTROLS) / sizeof(struct ConfigControl),
    },
};


#endif //DEFAULT_CONFIG_H_