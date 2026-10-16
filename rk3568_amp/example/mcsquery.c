/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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
/* mcsquery <cpu> - query PSCI affinity state of a cpu through /dev/mcs.
 * Prints "OFF" (exit 0) or "ON" (exit 1); exit 2 means /dev/mcs is missing
 * (mcs_km.ko not loaded). Used by deploy scripts to make sure the slave core
 * really powered off before loading a new image over it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>

#define MAGIC_NUMBER 'A'
#define IOC_AFFINITY_INFO _IOW(MAGIC_NUMBER, 2, int)

struct cpu_info {
    uint32_t cpu;
    uint64_t boot_addr;
};

int main(int argc, char **argv)
{
    struct cpu_info info;
    int fd, ret;

    memset(&info, 0, sizeof(info));
    info.cpu = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0) : 3;
    fd = open("/dev/mcs", O_RDWR | O_SYNC);
    if (fd < 0) {
        printf("mcsquery: open /dev/mcs: %s (mcs_km loaded?)\n", strerror(errno));
        return 2;
    }
    ret = ioctl(fd, IOC_AFFINITY_INFO, &info);
    close(fd);
    if (ret == 0) {
        printf("OFF\n");
        return 0;
    }
    printf("ON (%s)\n", strerror(errno));
    return 1;
}
