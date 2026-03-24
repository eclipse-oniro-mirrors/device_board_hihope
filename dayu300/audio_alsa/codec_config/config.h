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

#ifndef CONFIG_H_
#define CONFIG_H_

struct ConfigControl {
    const char *ctlName; //name of control.
    const char *strVal; //value of control, which type is stream.
    const int intVal[2]; //left and right value of control, which type are int.
};

struct ConfigRoute {
    const struct ConfigControl *controls;
    const unsigned controlsCount;
};

struct ConfigRouteTable {
    const struct ConfigRoute speakerNormal;
    const struct ConfigRoute mainMicCapture;
    const struct ConfigRoute playbackOn;
    const struct ConfigRoute playbackOff;
    const struct ConfigRoute captureOn;
    const struct ConfigRoute captureOff;
};

#define DEVICES_0 0

#endif //CONFIG_H_
