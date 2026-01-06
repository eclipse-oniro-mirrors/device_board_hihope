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
#ifndef FTHDA_CONFIG_H_
#define FTHDA_CONFIG_H_

#include "config.h"

const struct ConfigControl FTHDA_SPEAKER_NORMAL_CONTROLS[] = {
    {
        .ctlName = "Master Playback Volume",
        .strVal = "80",                /* min:0; max:87 */
    },
    {
        .ctlName = "Master Playback Switch",
        .strVal = "on",
    },
};

const struct ConfigControl FTHDA_MAIN_MIC_CAPTURE_CONTROLS[] = {
    {
        .ctlName = "Capture Volume",
        .strVal = "56,56",               /* min:0; max:63 */
    },
    {
        .ctlName = "Capture Switch",
        .strVal = "on,on",
    },
    {
        .ctlName = "Input Source",
        .strVal = "1",                   /* 0:Front Mic; 1:Rear Mic; 2:Line */
    },
    {
        .ctlName = "Rear Mic Boost Volume",
        .strVal = "2,2",                 /* min:0; max:3 */
    },
};

const struct ConfigControl FTHDA_PLAYBACK_ON_CONTROLS[] = {
    {
        .ctlName = "Master Playback Switch",
        .strVal = "on",
    },
};

const struct ConfigControl FTHDA_PLAYBACK_OFF_CONTROLS[] = {
    {
        .ctlName = "Master Playback Switch",
        .strVal = "off",
    },
};

const struct ConfigControl FTHDA_CAPTURE_ON_CONTROLS[] = {
    {
        .ctlName = "Capture Switch",
        .strVal = "on,on",
    },
};

const struct ConfigControl FTHDA_CAPTURE_OFF_CONTROLS[] = {
    {
        .ctlName = "Capture Switch",
        .strVal = "off,off",
    },
};

const struct ConfigRouteTable FTHDA_CONFIG_TABLE = {
    //speaker init
    .speakerNormal = {
        .controls = FTHDA_SPEAKER_NORMAL_CONTROLS,
        .controlsCount = sizeof(FTHDA_SPEAKER_NORMAL_CONTROLS) / sizeof(struct ConfigControl),
    },
    //capture init
    .mainMicCapture = {
        .controls = FTHDA_MAIN_MIC_CAPTURE_CONTROLS,
        .controlsCount = sizeof(FTHDA_MAIN_MIC_CAPTURE_CONTROLS) / sizeof(struct ConfigControl),
    },
    //playback on
    .playbackOn = {
        .controls = FTHDA_PLAYBACK_ON_CONTROLS,
        .controlsCount = sizeof(FTHDA_PLAYBACK_ON_CONTROLS) / sizeof(struct ConfigControl),
    },
    //playback off
    .playbackOff = {
        .controls = FTHDA_PLAYBACK_OFF_CONTROLS,
        .controlsCount = sizeof(FTHDA_PLAYBACK_OFF_CONTROLS) / sizeof(struct ConfigControl),
    },
    //capture on
    .captureOn = {
        .controls = FTHDA_CAPTURE_ON_CONTROLS,
        .controlsCount = sizeof(FTHDA_CAPTURE_ON_CONTROLS) / sizeof(struct ConfigControl),
    },
    //capture off
    .captureOff = {
        .controls = FTHDA_CAPTURE_OFF_CONTROLS,
        .controlsCount = sizeof(FTHDA_CAPTURE_OFF_CONTROLS) / sizeof(struct ConfigControl),
    },
};


#endif //_FTHDA_CONFIG_H_
