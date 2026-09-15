/*
 * Copyright (c) 2024 HiSilicon (Shanghai) Technologies Co., Ltd.
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

/*
 * Stubs for libinterrupt.a unresolved symbols.
 *
 * libinterrupt.a (riscv_himideerv200_plic.c.obj) references two symbols
 * from 2.08.5's los_hwi.c that are not available in liteos_m:
 *
 *   U g_hwiOps    — pointer to HwiControllerOps struct
 *   U OsIntHandle — interrupt dispatch function
 *
 * In our liteos_m dispatch model, we never call OsIntEntry() (2.08.5's
 * interrupt entry), so g_hwiOps->handleIrq (which is HalIrqHandler inside
 * libinterrupt.a) is never invoked. These stubs satisfy the linker
 * without providing real functionality.
 *
 * HalIrqInit() inside libinterrupt.a sets g_hwiOps = &g_plicOps (internal
 * static const). This is harmless — we never dereference g_hwiOps.
 */

#include "los_compiler.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/*
 * g_hwiOps — writable global pointer.
 * HalIrqInit() in libinterrupt.a writes to this.
 * We never read it (no OsIntEntry call in our dispatch path).
 * Type is void* to avoid struct layout mismatch with 2.08.5's
 * HwiControllerOps (which has more fields than liteos_m's version).
 */
void *g_hwiOps = NULL;

/*
 * OsIntHandle — called by HalIrqHandler (internal in libinterrupt.a)
 * when g_hwiOps->handleIrq() dispatches an interrupt.
 * In our model, HalIrqHandler is never called (OsIntEntry is never
 * called), so this function is never reached. Empty stub is safe.
 */
__attribute__((weak)) void OsIntHandle(UINT32 hwiNum, void *hwiForm)
{
    (void)hwiNum;
    (void)hwiForm;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif
