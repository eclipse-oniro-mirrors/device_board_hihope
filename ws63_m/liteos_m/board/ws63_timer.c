/*
 * Copyright (c) 2024 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: WS63 timer (TIMER0) adapted for liteos_m ArchTickTimer interface.
 *
 * Original: 2.08.5 timer_porting.c (HalClockInit/Start/GetCycles/DelayUs)
 * Adapted: register with PLIC via LOS_HwiCreate, provide ArchTickTimer callbacks.
 */

#include "los_timer.h"
#include "los_config.h"
#include "los_tick.h"
#include "los_reg.h"
#include "los_arch_interrupt.h"
#include "los_arch_timer.h"
#include "riscv_hal.h"
#include "soc.h"
#include "los_debug.h"

#define TIMER0_LOAD_COUNT0      (TIMER0_BASE + 0x00)
#define TIMER0_LOAD_COUNT1      (TIMER0_BASE + 0x04)
#define TIMER0_CURRENT_COUNT     (TIMER0_BASE + 0x08)
#define TIMER0_CONTROL           (TIMER0_BASE + 0x10)
#define TIMER0_EOI              (TIMER0_BASE + 0x14)

#define TICK_IRQN               NUM_HAL_INTERRUPT_TIMER
#define TICK_PRIO               2
#define TICK_PER_SECOND         LOSCFG_BASE_CORE_TICK_PER_SECOND
#define TICK_RELOAD             (TIMER_CLOCK / TICK_PER_SECOND)

STATIC HWI_PROC_FUNC g_sysTickHandler = (HWI_PROC_FUNC)NULL;

STATIC VOID HalTickIrqHandler(VOID *arg)
{
    (VOID)arg;
    volatile UINT32 *eoi = (volatile UINT32 *)TIMER0_EOI;
    *eoi = 1;

    if (g_sysTickHandler != NULL) {
        g_sysTickHandler(NULL);
    }
}

STATIC UINT32 HalClockStart(HWI_PROC_FUNC handler)
{
    HwiIrqParam irqParam;
    irqParam.pDevId = 0;

    g_sysTickHandler = handler;

    UINT32 ret = LOS_HwiCreate(TICK_IRQN, TICK_PRIO, 0, HalTickIrqHandler, &irqParam);
    if (ret != LOS_OK) {
        PRINT_ERR("WS63 timer HwiCreate failed: 0x%x\n", ret);
        return ret;
    }

    HalIrqEnable(TICK_IRQN);

    volatile UINT32 *load0 = (volatile UINT32 *)TIMER0_LOAD_COUNT0;
    volatile UINT32 *load1 = (volatile UINT32 *)TIMER0_LOAD_COUNT1;
    volatile UINT32 *ctrl  = (volatile UINT32 *)TIMER0_CONTROL;

    *load0 = TICK_RELOAD;
    *load1 = 0;
    *ctrl  = 0;
    *ctrl  = 0x3;

    return LOS_OK;
}

STATIC UINT64 HalClockReload(UINT64 nextResponseTime)
{
    (VOID)nextResponseTime;
    return nextResponseTime;
}

STATIC UINT64 HalClockGetCycles(UINT32 *period)
{
    volatile UINT32 *load = (volatile UINT32 *)TIMER0_LOAD_COUNT0;
    volatile UINT32 *cur  = (volatile UINT32 *)TIMER0_CURRENT_COUNT;
    if (period != NULL) {
        *period = *load;
    }
    return (UINT64)(*load - *cur);
}

STATIC VOID HalClockLock(VOID)
{
    HalIrqDisable(TICK_IRQN);
}

STATIC VOID HalClockUnlock(VOID)
{
    HalIrqEnable(TICK_IRQN);
}

STATIC ArchTickTimer g_archTickTimer = {
    .freq = TIMER_CLOCK,
    .irqNum = TICK_IRQN,
    .periodMax = LOSCFG_BASE_CORE_TICK_RESPONSE_MAX,
    .init = HalClockStart,
    .getCycle = HalClockGetCycles,
    .reload = HalClockReload,
    .lock = HalClockLock,
    .unlock = HalClockUnlock,
    .tickHandler = NULL,
};

ArchTickTimer *ArchSysTickTimerGet(VOID)
{
    return &g_archTickTimer;
}

UINT32 ArchEnterSleep(VOID)
{
    wfi();
    return LOS_OK;
}
