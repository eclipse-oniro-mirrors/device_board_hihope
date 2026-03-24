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
#ifndef ES8336_CONFIG_H_
#define ES8336_CONFIG_H_

#include "config.h"

const struct ConfigControl ES8336_SPEAKER_NORMAL_CONTROLS[] = {
    {
        .ctlName = "DAC Playback Volume",
        .strVal = "188,188",               /* min:0; max:192 */
    },
};

const struct ConfigControl ES8336_MAINMICCAPTURE_CONTROLS[] = {
    {
        .ctlName = "Differential Mux",
        .strVal = "1",
        /* Item #0 'lin1-rin1' */
        /* Item #1 'lin2-rin2' */
        /* Item #2 'lin1-rin1 with 15db Boost' */
        /* Item #3 'lin2-rin2 with 15db Boost' */
    },
};

const struct ConfigControl ES8336_PLAYBACK_ON_CONTROLS[] = {
};

const struct ConfigControl ES8336_PLAYBACK_OFF_CONTROLS[] = {
};

const struct ConfigControl ES8336_CAPTURE_ON_CONTROLS[] = {
};

const struct ConfigControl ES8336_CAPTUREOFF_CONTROLS[] = {
};

const struct ConfigRouteTable ES8336_CONFIG_TABLE = {
    //speaker init
    .speakerNormal = {
        .controls = ES8336_SPEAKER_NORMAL_CONTROLS,
        .controlsCount = sizeof(ES8336_SPEAKER_NORMAL_CONTROLS) / sizeof(struct ConfigControl),
    },
    //capture init
    .mainMicCapture = {
        .controls = ES8336_MAINMICCAPTURE_CONTROLS,
        .controlsCount = sizeof(ES8336_MAINMICCAPTURE_CONTROLS) / sizeof(struct ConfigControl),
    },
    //playback on
    .playbackOn = {
        .controls = ES8336_PLAYBACK_ON_CONTROLS,
        .controlsCount = sizeof(ES8336_PLAYBACK_ON_CONTROLS) / sizeof(struct ConfigControl),
    },
    //playback off
    .playbackOff = {
        .controls = ES8336_PLAYBACK_OFF_CONTROLS,
        .controlsCount = sizeof(ES8336_PLAYBACK_OFF_CONTROLS) / sizeof(struct ConfigControl),
    },
    //capture on
    .captureOn = {
        .controls = ES8336_CAPTURE_ON_CONTROLS,
        .controlsCount = sizeof(ES8336_CAPTURE_ON_CONTROLS) / sizeof(struct ConfigControl),
    },
    //capture off
    .captureOff = {
        .controls = ES8336_CAPTUREOFF_CONTROLS,
        .controlsCount = sizeof(ES8336_CAPTUREOFF_CONTROLS) / sizeof(struct ConfigControl),
    },
};


#endif //ES8336_CONFIG_H_
