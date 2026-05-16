/*
 * hdf_mac80211_sta_event.c
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

#include <net/netlink.h>
#include <net/cfg80211.h>
#include <securec.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/in6.h>
#include <linux/wireless.h>

#include "osal_mem.h"
#include "net_device.h"
#include "net_device_impl.h"
#include "net_device_adapter.h"
#include "wifi_mac80211_ops.h"
#include "hdf_wifi_cmd.h"
#include "hdf_wifi_event.h"
#include "hdf_wl_interface.h"
#include "hdf_public_ap6275s.h"
#include "hdf_bdh_event.h"

#define HDF_LOG_TAG BDH6Driver

void HdfInformBssFrameEventCallback(struct net_device *ndev, const struct InnerBssInfo *innerBssInfo)
{
    int32_t retVal = 0;
    NetDevice *netDev = GetHdfNetDeviceByLinuxInf(ndev);
    struct ScannedBssInfo bssInfo;
    struct WlanChannel hdfchannel;

    if (innerBssInfo->channel == NULL || netDev == NULL || innerBssInfo->mgmt == NULL) {
        HDF_LOGE("%s: inform_bss_frame channel = null or netDev = null!", __func__);
        return;
    }
    netDev = get_hdf_netdev(g_scan_event_ifidx);
    bssInfo.signal = innerBssInfo->signal;
    bssInfo.freq = innerBssInfo->freq;
    bssInfo.mgmtLen = innerBssInfo->mgmtLen;
    bssInfo.mgmt = (struct Ieee80211Mgmt *)innerBssInfo->mgmt;

    hdfchannel.flags = innerBssInfo->channel->flags;
    hdfchannel.channelId = innerBssInfo->channel->hw_value;
    hdfchannel.centerFreq = innerBssInfo->channel->center_freq;
    retVal = HdfWifiEventInformBssFrame(netDev, &hdfchannel, &bssInfo);
    if (retVal < 0) {
        HDF_LOGE("%s: hdf wifi event inform bss frame failed!", __func__);
    }
}

int32_t HdfScanEventCallback(struct net_device *ndev, HdfWifiScanStatus _status)
{
    int32_t ret = 0;

    NetDevice *netDev = GetHdfNetDeviceByLinuxInf(ndev);
    WifiScanStatus status = _status;
    netDev = get_hdf_netdev(g_scan_event_ifidx);
    HDF_LOGI("%s: %d, scandone!", __func__, _status);
    ret = HdfWifiEventScanDone(netDev, status);

    return ret;
}

int32_t HdfConnectResultEventCallback(struct net_device *ndev, const struct InnerConnetResult *innerConnResult)
{
    int32_t retVal = 0;
    NetDevice *netDev = GetHdfNetDeviceByLinuxInf(ndev);
    struct ConnetResult connResult;
    // for check p2p0 report
    netDev = get_hdf_netdev(g_conn_event_ifidx);

    HDF_LOGI("%s: enter", __func__);

    if (netDev == NULL || innerConnResult->bssid == NULL || innerConnResult->rspIe == NULL
        || innerConnResult->reqIe == NULL) {
        HDF_LOGE("%s: netDev / bssid / rspIe / reqIe  null!", __func__);
        return -1;
    }

    memcpy_s(&connResult.bssid[0], ETH_ADDR_LEN, innerConnResult->bssid, ETH_ADDR_LEN);

    connResult.rspIe = innerConnResult->rspIe;
    connResult.rspIeLen = innerConnResult->rspIeLen;
    connResult.reqIe = innerConnResult->reqIe;
    connResult.reqIeLen = innerConnResult->reqIeLen;
    connResult.connectStatus = innerConnResult->connectStatus;
    connResult.freq = innerConnResult->freq;
    connResult.statusCode = innerConnResult->statusCode;

    retVal = HdfWifiEventConnectResult(netDev, &connResult);
    if (retVal < 0) {
        HDF_LOGE("%s: hdf wifi event inform connect result failed!", __func__);
    }
    return retVal;
}

int32_t HdfDisconnectedEventCallback(struct net_device *ndev, uint16_t reason, uint8_t *ie, uint32_t len)
{
    int32_t ret = 0;

    NetDevice *netDev = GetHdfNetDeviceByLinuxInf(ndev);
    netDev = get_hdf_netdev(g_conn_event_ifidx);
    HDF_LOGI("%s: leave", __func__);

    ret = HdfWifiEventDisconnected(netDev, reason, ie, len);
    return ret;
}
