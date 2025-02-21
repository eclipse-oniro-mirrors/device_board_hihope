/*
 * hdf_wl_interface.h
 *
 * hdf driver
 *
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef HDF_WL_INTERFACE_H
#define HDF_WL_INTERFACE_H
#include <net/cfg80211.h>
#include <linux/netdevice.h>
#include "net_device.h"

enum hdf_inf_type {
    HDF_INF_WLAN0 = 0,
    HDF_INF_P2P0,
    HDF_INF_P2P1,
    HDF_INF_AP0,
    HDF_INF_MAX
};

struct hdf_eapol_event_s {
    struct work_struct eapol_report;
    NetBufQueue eapolQueue;
    int32_t idx;
};

struct hdf_inf_map {
    struct NetDevice  *hnetdev;
    struct net_device *netdev;
    struct wireless_dev *wdev;
    u8 macaddr[ETH_ALEN];
    struct hdf_eapol_event_s eapolEvent;
};

struct InnerConnetResult {
    uint8_t   *bssid;  /**< MAC address of the AP associated with the station */
    uint16_t  statusCode;           /**< 16-bit status code defined in the IEEE protocol */
    uint8_t  *rspIe;                /**< Association response IE */
    uint8_t  *reqIe;                /**< Association request IE */
    uint32_t  reqIeLen;             /**< Length of the association request IE */
    uint32_t  rspIeLen;             /**< Length of the association response IE */
    uint16_t  connectStatus;        /**< Connection status */
    uint16_t  freq;                 /**< Frequency of the AP */
};

struct InnerBssInfo {
    struct ieee80211_channel *channel;
    int32_t signal;
    int16_t freq;
    struct ieee80211_mgmt *mgmt;
    uint32_t mgmtLen;
};

static inline int bdh6_hdf_vxx_lock(struct wireless_dev *vxx, int pad)
    __acquires(vxx)
{
    pad = 0;
    mutex_lock(&vxx->mtx);
    __acquire(vxx->mtx);
    return pad;
}

static inline int bdh6_hdf_vxx_unlock(struct wireless_dev *vxx, int pad)
    __releases(vxx)
{
    pad = 1;
    __release(vxx->mtx);
    mutex_unlock(&vxx->mtx);
    return pad;
}

void eapol_report_handler(struct work_struct *work_data);
#endif
