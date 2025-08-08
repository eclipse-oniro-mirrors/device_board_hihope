/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>

#include "init_cmdexecutor.h"
#include "init_module_engine.h"

#define REBOOT_MAGIC1       0xfee1dead
#define REBOOT_MAGIC2       672274793
#define REBOOT_CMD_RESTART2 0xA1B2C3D4
static int DoRebootLoader(int id, const char *name, int argc, const char **argv)
{
    // by job to stop service and unmount
    DoJobNow("reboot");
    syscall(__NR_reboot, REBOOT_MAGIC1, REBOOT_MAGIC2, REBOOT_CMD_RESTART2, "loader");
    return 0;
}

MODULE_CONSTRUCTOR(void)
{
    AddRebootCmdExecutor("loader", DoRebootLoader);
}