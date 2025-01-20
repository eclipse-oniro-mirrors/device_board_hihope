/*
 * hdf_bdh_mac80211.c
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

#include <net/cfg80211.h>
#include <net/regulatory.h>
#include <typedefs.h>
#include <ethernet.h>
#include <securec.h>
#include <linux/version.h>

#include "wifi_module.h"
#include "wifi_mac80211_ops.h"
#include "hdf_wlan_utils.h"
#include "hdf_wifi_event.h"
#include "net_bdh_adpater.h"
#include "hdf_wl_interface.h"
#include "hdf_public_ap6275s.h"
#include "bcmutils.h"
#include "wl_cfgp2p.h"
#include "eapol.h"

#define HDF_LOG_TAG BDH6Driver

struct NetDevice *get_real_netdev(NetDevice *netDev);
int32_t WalStopAp(NetDevice *hnetDev);
int32_t wl_cfg80211_abort_scan(struct wiphy *wiphy, struct wireless_dev *wdev);

struct wiphy *get_linux_wiphy_ndev(struct net_device *ndev)
{
    if (ndev == NULL || ndev->ieee80211_ptr == NULL) {
        return NULL;
    }

    return ndev->ieee80211_ptr->wiphy;
}

struct wiphy *get_linux_wiphy_hdfdev(NetDevice *netDev)
{
    struct net_device *ndev = GetLinuxInfByNetDevice(netDev);
    return get_linux_wiphy_ndev(ndev);
}

int get_scan_ifidx(const char *ifname)
{
    int i = 0;
    struct NetDevice *p2p_hnetdev = NULL;
    for (; i < HDF_INF_MAX; i ++) {
        p2p_hnetdev = g_hdf_infmap[i].hnetdev;
        if (p2p_hnetdev == NULL) {
            continue;
        }
        if (strcmp(p2p_hnetdev->name, ifname) == 0) {
            HDF_LOGE("get scan ifidx = %d, %s", i, ifname);
            return i;
        }
    }
    HDF_LOGI("get scan ifidx error %d, %s", i, ifname);
    return 0;
}

static int32_t BDH6WalSetMode(NetDevice *hnetDev, enum WlanWorkMode iftype)
{
    int32_t retVal = 0;
    struct net_device *netdev = NULL;
    NetDevice *netDev = NULL;
    struct wiphy *wiphy = NULL;
    enum nl80211_iftype old_iftype = 0;
    netDev = get_real_netdev(hnetDev);
    
    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }
    
    wiphy = get_linux_wiphy_ndev(netdev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }
    old_iftype = netdev->ieee80211_ptr->iftype;

    HDF_LOGI("%s: start... iftype=%d, oldiftype=%d", __func__, iftype, old_iftype);
    if (old_iftype == NL80211_IFTYPE_AP && (int32_t)iftype != (int32_t)old_iftype) {
        WalStopAp(netDev);
    }

    if ((int32_t)iftype == (int32_t)NL80211_IFTYPE_P2P_GO && old_iftype == NL80211_IFTYPE_P2P_GO) {
        HDF_LOGE("%s: p2p go don't change mode", __func__);
        return retVal;
    }
    
    rtnl_lock();
    retVal = (int32_t)wl_cfg80211_ops.change_virtual_intf(wiphy, netdev,
        (enum nl80211_iftype)iftype, NULL);
    rtnl_unlock();
    if (retVal < 0) {
        HDF_LOGE("%s: set mode failed!", __func__);
    }

    return retVal;
}

int32_t BDH6WalAddKey(struct NetDevice *hnetDev, uint8_t keyIndex, bool pairwise, const uint8_t *macAddr,
    struct KeyParams *params)
{
    int32_t retVal = 0;
    struct NetDevice *netDev = NULL;
    struct net_device *netdev = NULL;
    struct wiphy *wiphy = NULL;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 10, 0))
    struct key_params keypm;
#endif
    netDev = get_real_netdev(hnetDev);

    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }
    
    wiphy = get_linux_wiphy_ndev(netdev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }
    
    HDF_LOGI("%s: start..., mac = %p, keyIndex = %u,pairwise = %d, cipher = 0x%x, seqlen = %d, keylen = %d",
        __func__, macAddr, keyIndex, pairwise, params->cipher, params->seqLen, params->keyLen);
    rtnl_lock();
    bdh6_hdf_vxx_lock(netdev->ieee80211_ptr, 0);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 10, 0))
    memset_s(&keypm, sizeof(struct key_params), 0, sizeof(struct key_params));
    keypm.key = params->key;
    keypm.seq = params->seq;
    keypm.key_len = params->keyLen;
    keypm.seq_len = params->seqLen;
    keypm.cipher = params->cipher;
    keypm.vlan_id = 0;
    retVal = (int32_t)wl_cfg80211_ops.add_key(wiphy, netdev, keyIndex, pairwise, macAddr, &keypm);
#else
    retVal = (int32_t)wl_cfg80211_ops.add_key(wiphy, netdev, keyIndex, pairwise, macAddr, (struct key_params *)params);
#endif
    bdh6_hdf_vxx_unlock(netdev->ieee80211_ptr, 0);
    rtnl_unlock();
    if (retVal < 0) {
        HDF_LOGE("%s: add key failed!", __func__);
    }

    return retVal;
}

int32_t BDH6WalDelKey(struct NetDevice *hnetDev, uint8_t keyIndex, bool pairwise, const uint8_t *macAddr)
{
    int32_t retVal = 0;
    struct NetDevice *netDev = NULL;
    struct net_device *netdev = NULL;
    struct wiphy *wiphy = NULL;
    netDev = get_real_netdev(hnetDev);

    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }
    
    wiphy = get_linux_wiphy_ndev(netdev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }

    HDF_LOGI("%s: start..., mac=%p, keyIndex=%u,pairwise=%d", __func__, macAddr, keyIndex, pairwise);

    rtnl_lock();
    bdh6_hdf_vxx_lock(netdev->ieee80211_ptr, 0);
    retVal = (int32_t)wl_cfg80211_ops.del_key(wiphy, netdev, keyIndex, pairwise, macAddr);
    bdh6_hdf_vxx_unlock(netdev->ieee80211_ptr, 0);
    rtnl_unlock();
    if (retVal < 0) {
        HDF_LOGE("%s: delete key failed!", __func__);
    }

    return retVal;
}

int32_t BDH6WalSetDefaultKey(struct NetDevice *hnetDev, uint8_t keyIndex, bool unicast, bool multicas)
{
    int32_t retVal = 0;
    struct NetDevice *netDev = NULL;
    struct net_device *netdev = NULL;
    struct wiphy *wiphy = NULL;
    netDev = get_real_netdev(hnetDev);

    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }
    
    wiphy = get_linux_wiphy_ndev(netdev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }
    HDF_LOGI("%s: start..., keyIndex=%u,unicast=%d, multicas=%d", __func__, keyIndex, unicast, multicas);
    rtnl_lock();
    bdh6_hdf_vxx_lock(netdev->ieee80211_ptr, 0);
    retVal = (int32_t)wl_cfg80211_ops.set_default_key(wiphy, netdev, keyIndex, unicast, multicas);
    bdh6_hdf_vxx_unlock(netdev->ieee80211_ptr, 0);
    rtnl_unlock();
    if (retVal < 0) {
        HDF_LOGE("%s: set default key failed!", __func__);
    }

    return retVal;
}

int32_t BDH6WalGetDeviceMacAddr(NetDevice *hnetDev, int32_t type, uint8_t *mac, uint8_t len)
{
    struct NetDevice *netDev = NULL;
    struct net_device *netdev = NULL;
    netDev = get_real_netdev(hnetDev);
    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }

    (void)len;
    (void)type;
    (void)netDev;
    HDF_LOGI("%s: start...", __func__);
    
    memcpy_s(mac, len, netdev->dev_addr, netdev->addr_len);

    return HDF_SUCCESS;
}

int32_t BDH6WalSetMacAddr(NetDevice *hnetDev, uint8_t *mac, uint8_t len)
{
    int32_t retVal = 0;
    struct NetDevice *netDev = NULL;
    struct net_device *netdev = NULL;
    struct sockaddr sa;
    netDev = get_real_netdev(hnetDev);
    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }

    HDF_LOGI("%s: start...", __func__);
    if (mac == NULL || len != ETH_ALEN) {
        HDF_LOGE("%s: mac is error, len=%u", __func__, len);
        return -1;
    }
    if (!is_valid_ether_addr(mac)) {
        HDF_LOGE("%s: mac is invalid %02x:%02x:%02x:%02x:%02x:%02x", __func__,
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return -1;
    }
    
    memcpy_s(sa.sa_data, ETH_ALEN, mac, len);

    retVal = (int32_t)dhd_ops_pri.ndo_set_mac_address(netdev, (void *)&sa);
    if (retVal < 0) {
        HDF_LOGE("%s: set mac address failed!", __func__);
    }
    memcpy_s(hnetDev->macAddr, MAC_ADDR_SIZE, mac, len);
    return retVal;
}

int32_t BDH6WalSetTxPower(NetDevice *hnetDev, int32_t power)
{
    int retVal = 0;
    struct wiphy *wiphy = NULL;
    struct NetDevice *netDev = NULL;
    struct wireless_dev *wdev = NULL;
    netDev = get_real_netdev(hnetDev);

    // sync from net_device->ieee80211_ptr
    wdev = GET_NET_DEV_CFG80211_WIRELESS(netDev);

    wiphy = get_linux_wiphy_hdfdev(netDev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }

    HDF_LOGI("%s: start...", __func__);
    rtnl_lock();
    retVal = (int32_t)wl_cfg80211_ops.set_tx_power(wiphy, wdev, NL80211_TX_POWER_FIXED, power);
    rtnl_unlock();
    if (retVal < 0) {
        HDF_LOGE("%s: set_tx_power failed!", __func__);
    }

    return HDF_SUCCESS;
}

void BDH6WalReleaseHwCapability(struct WlanHwCapability *self)
{
    uint8_t i;
    if (self == NULL) {
        return;
    }
    for (i = 0; i < IEEE80211_NUM_BANDS; i++) {
        if (self->bands[i] != NULL) {
            OsalMemFree(self->bands[i]);
            self->bands[i] = NULL;
        }
    }
    if (self->supportedRates != NULL) {
        OsalMemFree(self->supportedRates);
        self->supportedRates = NULL;
    }
    OsalMemFree(self);
}

static void GetBdh24GFreq(const struct ieee80211_regdomain *regdom, int32_t *freqs, uint32_t *num)
{
    uint32_t channelNumber;
    uint32_t freqIndex = 0;
    uint32_t freqTmp;
    uint32_t minFreq;
    uint32_t maxFreq;

    minFreq = regdom->reg_rules[0].freq_range.start_freq_khz / MHZ_TO_KHZ(1);
    maxFreq = regdom->reg_rules[0].freq_range.end_freq_khz / MHZ_TO_KHZ(1);
    for (channelNumber = 1; channelNumber <= WIFI_24G_CHANNEL_NUMS; channelNumber++) {
        if (channelNumber < WAL_MAX_CHANNEL_2G) {
            freqTmp = WAL_MIN_FREQ_2G + (channelNumber - 1) * WAL_FREQ_2G_INTERVAL;
        } else if (channelNumber == WAL_MAX_CHANNEL_2G) {
            freqTmp = WAL_MAX_FREQ_2G;
        }
        if (freqTmp < minFreq || freqTmp > maxFreq) {
            continue;
        }

        HDF_LOGI("bdh6 2G %u: freq=%u\n", freqIndex, freqTmp);
        freqs[freqIndex] = freqTmp;
        freqIndex++;
    }
    *num = freqIndex;
}

int32_t Bdh6Fband(NetDevice *hnetDev, int32_t band, int32_t *freqs, uint32_t *num)
{
    uint32_t freqIndex = 0;
    struct wiphy* wiphy = NULL;
    struct NetDevice *netDev = NULL;
    struct ieee80211_supported_band *band5g = NULL;
    int32_t max5GChNum = 0;
    const struct ieee80211_regdomain *regdom = bdh6_get_regdomain();
    if (regdom == NULL) {
        HDF_LOGE("%s: wal_get_cfg_regdb failed!", __func__);
        return HDF_FAILURE;
    }
    netDev = get_real_netdev(hnetDev);
    wiphy = get_linux_wiphy_hdfdev(netDev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }
    HDF_LOGI("%s: start..., band=%d", __func__, band);
    switch (band) {
        case WLAN_BAND_2G:
            GetBdh24GFreq(regdom, freqs, num);
            break;

        case WLAN_BAND_5G:
            band5g = wiphy->bands[IEEE80211_BAND_5GHZ];
            if (band5g == NULL) {
                return HDF_ERR_NOT_SUPPORT;
            }
            max5GChNum = min(band5g->n_channels, WIFI_24G_CHANNEL_NUMS);
            for (freqIndex = 0; freqIndex < max5GChNum; freqIndex++) {
                freqs[freqIndex] = band5g->channels[freqIndex].center_freq;
                HDF_LOGI("bdh6 5G %u: freq=%u\n", freqIndex, freqs[freqIndex]);
            }
            *num = freqIndex;
            break;

        default:
            HDF_LOGE("%s: no support band!", __func__);
            return HDF_ERR_NOT_SUPPORT;
    }
    return HDF_SUCCESS;
}

static int32_t FillBdhBandInfo(const struct ieee80211_supported_band *band, enum Ieee80211Band bnum,
    struct WlanHwCapability *hwCapability)
{
    uint8_t loop = 0;
    if (hwCapability->bands[bnum] == NULL) {
        hwCapability->bands[bnum] =
            OsalMemCalloc(sizeof(struct WlanBand) + (sizeof(struct WlanChannel) * band->n_channels));
        if (hwCapability->bands[bnum] == NULL) {
            BDH6WalReleaseHwCapability(hwCapability);
            return HDF_FAILURE;
        }
    }
    
    hwCapability->htCapability = band->ht_cap.cap;
    
    hwCapability->bands[bnum]->channelCount = band->n_channels;
    for (loop = 0; loop < band->n_channels; loop++) {
        hwCapability->bands[bnum]->channels[loop].centerFreq = band->channels[loop].center_freq;
        hwCapability->bands[bnum]->channels[loop].flags = band->channels[loop].flags;
        hwCapability->bands[bnum]->channels[loop].channelId = band->channels[loop].hw_value;
        HDF_LOGI("bdh6 %d band %u: centerFreq=%u, channelId=%u, flags=0x%08x\n", bnum, loop,
            hwCapability->bands[bnum]->channels[loop].centerFreq,
            hwCapability->bands[bnum]->channels[loop].channelId,
            hwCapability->bands[bnum]->channels[loop].flags);
    }

    return HDF_SUCCESS;
}

static int32_t FillBdhSupportedRates(const struct ieee80211_supported_band *band2g,
    const struct ieee80211_supported_band *band5g, struct WlanHwCapability *hwCapability)
{
    uint8_t loop = 0;
    hwCapability->supportedRates = OsalMemCalloc(sizeof(uint16_t) * hwCapability->supportedRateCount);
    if (hwCapability->supportedRates == NULL) {
        HDF_LOGE("%s: oom!\n", __func__);
        BDH6WalReleaseHwCapability(hwCapability);
        return HDF_FAILURE;
    }
    
    for (loop = 0; loop < band2g->n_bitrates; loop++) {
        hwCapability->supportedRates[loop] = band2g->bitrates[loop].bitrate;
        HDF_LOGI("bdh6 2G supportedRates %u: %u\n", loop, hwCapability->supportedRates[loop]);
    }

    if (band5g) {
        for (loop = band2g->n_bitrates; loop < hwCapability->supportedRateCount; loop++) {
            hwCapability->supportedRates[loop] = band5g->bitrates[loop].bitrate;
            HDF_LOGI("bdh6 5G supportedRates %u: %u\n", loop, hwCapability->supportedRates[loop]);
        }
    }

    if (hwCapability->supportedRateCount > MAX_SUPPORTED_RATE) {
        hwCapability->supportedRateCount = MAX_SUPPORTED_RATE;
    }
    return HDF_SUCCESS;
}

int32_t Bdh6Ghcap(struct NetDevice *hnetDev, struct WlanHwCapability **capability)
{
    int32_t ret = 0;
    struct wiphy *wiphy = NULL;
    struct NetDevice *netDev = NULL;
    struct ieee80211_supported_band *band2g = NULL;
    struct ieee80211_supported_band *band5g = NULL;
    struct WlanHwCapability *hwCapability = NULL;
    uint16_t supportedRateCount = 0;
    netDev = get_real_netdev(hnetDev);

    wiphy = get_linux_wiphy_hdfdev(netDev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }

    HDF_LOGI("%s: start...", __func__);
    band2g = wiphy->bands[IEEE80211_BAND_2GHZ];
    hwCapability = (struct WlanHwCapability *)OsalMemCalloc(sizeof(struct WlanHwCapability));
    if (hwCapability == NULL) {
        HDF_LOGE("%s: oom!\n", __func__);
        return HDF_FAILURE;
    }
    hwCapability->Release = BDH6WalReleaseHwCapability;
    supportedRateCount = band2g->n_bitrates;
    ret = FillBdhBandInfo(band2g, IEEE80211_BAND_2GHZ, hwCapability);
    if (ret != HDF_SUCCESS) {
        HDF_LOGE("%s: FillBdh2GBandInfo failed", __func__);
        return HDF_FAILURE;
    }

    if (wiphy->bands[IEEE80211_BAND_5GHZ]) { // Fill 5Ghz band
        band5g = wiphy->bands[IEEE80211_BAND_5GHZ];
        ret = FillBdhBandInfo(band5g, IEEE80211_BAND_5GHZ, hwCapability);
        if (ret != HDF_SUCCESS) {
            HDF_LOGE("%s: FillBdh5GBandInfo failed", __func__);
            return HDF_FAILURE;
        }

        supportedRateCount += band5g->n_bitrates;
    }

    hwCapability->htCapability = band2g->ht_cap.cap;
    HDF_LOGI("bdh6 htCapability= %u,%u; supportedRateCount= %u,%u,%u\n", hwCapability->htCapability,
        band5g->ht_cap.cap, supportedRateCount, band2g->n_bitrates, band5g->n_bitrates);
    
    hwCapability->supportedRateCount = supportedRateCount;
    ret = FillBdhSupportedRates(band2g, band5g, hwCapability);
    
    *capability = hwCapability;
    return ret;
}

static int32_t FillActionParamFreq(struct wiphy *wiphy, struct cfg80211_mgmt_tx_params *params)
{
    u32 center_freq = 0;
    memset_s(params, sizeof(*params), 0, sizeof(*params));
    params->wait = 0;
    center_freq = p2p_remain_freq;
    params->chan = ieee80211_get_channel_khz(wiphy, MHZ_TO_KHZ(center_freq));
    if (params->chan == NULL) {
        HDF_LOGE("%s: get center_freq %u faild", __func__, center_freq);
        return -1;
    }

    return 0;
}

static int32_t Bdh6SActionEntry(struct NetDevice *hhnetDev, WifiActionData *actionData)
{
    int retVal = 0;
    struct net_device *netdev = NULL;
    struct NetDevice *netDev = NULL;
    struct wiphy *wiphy = NULL;
    struct wireless_dev *wdev = NULL;
    static u64 action_cookie = 0;
    struct cfg80211_mgmt_tx_params params;
    u8 *action_buf = NULL, *srcMac = NULL;
    struct ieee80211_mgmt *mgmt = NULL;

    g_mgmt_tx_event_ifidx = get_scan_ifidx(hhnetDev->name);
    HDF_LOGI("%s: start %s... ifidx=%d", __func__, hhnetDev->name, g_mgmt_tx_event_ifidx);
    
    netDev = get_real_netdev(hhnetDev);
    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }
    wiphy = get_linux_wiphy_ndev(netdev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }

    if (strcmp(hhnetDev->name, "p2p0") == 0) {
        wdev = g_hdf_infmap[HDF_INF_P2P0].wdev;
        if (g_hdf_infmap[HDF_INF_P2P1].netdev)
            srcMac = wdev->address;
        else
            srcMac = actionData->src;
    } else {
        wdev = netdev->ieee80211_ptr;
        srcMac = actionData->src;
    }
    if (FillActionParamFreq(wiphy, &params) != 0) {
        return -1;
    }

    // build 802.11 action header
    action_buf = (u8 *)OsalMemCalloc(MAC_80211_FRAME_LEN+actionData->dataLen);
    mgmt = (struct ieee80211_mgmt *)action_buf;
    mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION);
    memcpy_s(mgmt->da, ETH_ALEN, actionData->dst, ETH_ALEN);
    memcpy_s(mgmt->sa, ETH_ALEN, srcMac, ETH_ALEN);
    memcpy_s(mgmt->bssid, ETH_ALEN, actionData->bssid, ETH_ALEN);

    /* 填充payload信息 */
    if (actionData->dataLen > 0) {
        memcpy_s(action_buf+MAC_80211_FRAME_LEN, actionData->dataLen, actionData->data, actionData->dataLen);
    }
    params.buf = action_buf;
    params.len = (MAC_80211_FRAME_LEN + actionData->dataLen);
    retVal = (int32_t)wl_cfg80211_ops.mgmt_tx(wiphy, wdev, &params, &action_cookie);
    OsalMemFree(action_buf);
    return retVal;
}

int32_t Bdh6SAction(struct NetDevice *hhnetDev, WifiActionData *actionData)
{
    int retVal = 0;
    rtnl_lock();
    retVal = Bdh6SActionEntry(hhnetDev, actionData);
    rtnl_unlock();
    return retVal;
}

int32_t BDH6WalGetIftype(struct NetDevice *hnetDev, uint8_t *iftype)
{
    struct NetDevice *netDev = NULL;
    netDev = get_real_netdev(hnetDev);
    iftype = (uint8_t *)(&(GET_NET_DEV_CFG80211_WIRELESS(netDev)->iftype));
    HDF_LOGI("%s: start...", __func__);
    return HDF_SUCCESS;
}

static struct HdfMac80211BaseOps g_bdh6_baseOps = {
    .SetMode = BDH6WalSetMode,
    .AddKey = BDH6WalAddKey,
    .DelKey = BDH6WalDelKey,
    .SetDefaultKey = BDH6WalSetDefaultKey,
    
    .GetDeviceMacAddr = BDH6WalGetDeviceMacAddr,
    .SetMacAddr = BDH6WalSetMacAddr,
    .SetTxPower = BDH6WalSetTxPower,
    .GetValidFreqsWithBand = Bdh6Fband,
    
    .GetHwCapability = Bdh6Ghcap,
    .SendAction = Bdh6SAction,
    .GetIftype = BDH6WalGetIftype,
    
};

#define WIFI_SCAN_EXTRA_IE_LEN_MAX      (512)
#define PTR_IEEE80211_CHANNEL_SIZE 8

struct ieee80211_channel *GetChannelByFreq(const struct wiphy *wiphy, uint16_t center_freq)
{
    enum Ieee80211Band band;
    struct ieee80211_supported_band *currentBand = NULL;
    int32_t loop;
    for (band = (enum Ieee80211Band)0; band < IEEE80211_NUM_BANDS; band++) {
        currentBand = wiphy->bands[band];
        if (currentBand == NULL) {
            continue;
        }
        for (loop = 0; loop < currentBand->n_channels; loop++) {
            if (currentBand->channels[loop].center_freq == center_freq) {
                return &currentBand->channels[loop];
            }
        }
    }
    return NULL;
}


static int32_t WifiScanSetSsid(const struct WlanScanRequest *params, struct cfg80211_scan_request *request)
{
    int32_t count = 0;
    int32_t loop;

    if (params->ssidCount > WPAS_MAX_SCAN_SSIDS) {
        HDF_LOGE("%s:unexpected numSsids!numSsids=%u", __func__, params->ssidCount);
        return HDF_FAILURE;
    }

    if (params->ssidCount == 0) {
        HDF_LOGE("%s:ssid number is 0!", __func__);
        return HDF_SUCCESS;
    }

    request->ssids = (struct cfg80211_ssid *)OsalMemCalloc(params->ssidCount * sizeof(struct cfg80211_ssid));
    if (request->ssids == NULL) {
        HDF_LOGE("%s: calloc request->ssids null", __func__);
        return HDF_FAILURE;
    }

    for (loop = 0; loop < params->ssidCount; loop++) {
        if (count >= DRIVER_MAX_SCAN_SSIDS) {
            break;
        }

        if (params->ssids[loop].ssidLen > IEEE80211_MAX_SSID_LEN) {
            continue;
        }

        request->ssids[count].ssid_len = params->ssids[loop].ssidLen;
        if (memcpy_s(request->ssids[count].ssid, OAL_IEEE80211_MAX_SSID_LEN, params->ssids[loop].ssid,
            params->ssids[loop].ssidLen) != EOK) {
            continue;
        }
        count++;
    }
    request->n_ssids = count;

    return HDF_SUCCESS;
}

int32_t WifiScanSetUserIe(const struct WlanScanRequest *params, struct cfg80211_scan_request *request)
{
    if (params->extraIEsLen > WIFI_SCAN_EXTRA_IE_LEN_MAX) {
        HDF_LOGE("%s:unexpected extra len!extraIesLen=%d", __func__, params->extraIEsLen);
        return HDF_FAILURE;
    }
    if ((params->extraIEs != NULL) && (params->extraIEsLen != 0)) {
        request->ie = (uint8_t *)OsalMemCalloc(params->extraIEsLen);
        if (request->ie == NULL) {
            HDF_LOGE("%s: calloc request->ie null", __func__);
            return HDF_FAILURE;
        }
        (void)memcpy_s((void *)request->ie, params->extraIEsLen, params->extraIEs, params->extraIEsLen);
        request->ie_len = params->extraIEsLen;
    }

    return HDF_SUCCESS;
}

static int32_t FillAllValidChannels(const struct wiphy *wiphy, int32_t channelTotal,
    struct cfg80211_scan_request *request)
{
    int32_t loop;
    int32_t count = 0;
    enum Ieee80211Band band = IEEE80211_BAND_2GHZ;
    struct ieee80211_channel *chan = NULL;

    for (band = IEEE80211_BAND_2GHZ; band <= IEEE80211_BAND_5GHZ; band++) {
        if (wiphy->bands[band] == NULL) {
            HDF_LOGE("%s: wiphy->bands[band] = NULL!\n", __func__);
            continue;
        }

        for (loop = 0; loop < (int32_t)wiphy->bands[band]->n_channels; loop++) {
            if (count >= channelTotal) {
                break;
            }

            chan = &wiphy->bands[band]->channels[loop];
            if ((chan->flags & WIFI_CHAN_DISABLED) != 0) {
                continue;
            }
            request->channels[count++] = chan;
        }
    }
    return count;
}

int32_t WifiScanSetChannel(const struct wiphy *wiphy, const struct WlanScanRequest *params,
    struct cfg80211_scan_request *request)
{
    int32_t loop;
    int32_t count = 0;
    struct ieee80211_channel *chan = NULL;

    int32_t channelTotal = ieee80211_get_num_supported_channels((struct wiphy *)wiphy);

    if ((params->freqs == NULL) || (params->freqsCount == 0)) {
        count = FillAllValidChannels(wiphy, channelTotal, request);
    } else {
        for (loop = 0; loop < params->freqsCount; loop++) {
            chan = GetChannelByFreq(wiphy, (uint16_t)(params->freqs[loop]));
            if (chan == NULL) {
                HDF_LOGE("%s: freq not found!freq=%d!\n", __func__, params->freqs[loop]);
                continue;
            }

            if (count >= channelTotal) {
                break;
            }
            
            request->channels[count++] = chan;
        }
    }

    if (count == 0) {
        HDF_LOGE("%s: invalid freq info!\n", __func__);
        return HDF_FAILURE;
    }
    request->n_channels = count;

    return HDF_SUCCESS;
}

int32_t WifiScanSetRequest(struct NetDevice *netdev, const struct WlanScanRequest *params,
    struct cfg80211_scan_request *request)
{
    if (netdev == NULL || netdev->ieee80211Ptr == NULL) {
        return HDF_FAILURE;
    }
    request->wiphy = GET_NET_DEV_CFG80211_WIRELESS(netdev)->wiphy;
    request->wdev = GET_NET_DEV_CFG80211_WIRELESS(netdev);
    request->n_ssids = params->ssidCount;
    if (WifiScanSetChannel(GET_NET_DEV_CFG80211_WIRELESS(netdev)->wiphy, params, request)) {
        HDF_LOGE("%s: set channel failed!", __func__);
        return HDF_FAILURE;
    }
    if (WifiScanSetSsid(params, request)) {
        HDF_LOGE("%s: set ssid failed!", __func__);
        return HDF_FAILURE;
    }
    if (WifiScanSetUserIe(params, request)) {
        HDF_LOGE("%s: set user ie failed!", __func__);
        return HDF_FAILURE;
    }
    memset_s(request->bssid, sizeof(request->bssid), 0xff, sizeof(request->bssid));
    return HDF_SUCCESS;
}

void WifiScanFree(struct cfg80211_scan_request **request)
{
    HDF_LOGI("%s: enter... !", __func__);

    if (*request != NULL) {
        if ((*request)->ie != NULL) {
            OsalMemFree((void *)(*request)->ie);
            (*request)->ie = NULL;
        }
        if ((*request)->ssids != NULL) {
            OsalMemFree((void *)(*request)->ssids);
            (*request)->ssids = NULL;
        }
        OsalMemFree((void *)*request);
        *request = NULL;
    }
}

static int32_t HdfStartScanEntry(NetDevice *hhnetDev, struct WlanScanRequest *scanParam)
{
    int32_t ret = 0;
    struct net_device *ndev = NULL;
    struct wiphy *wiphy = NULL;
    NetDevice *hnetdev = hhnetDev;
    int32_t channelTotal;
    struct NetDevice *netDev = NULL;
    struct cfg80211_scan_request *request = NULL;

    netDev = get_real_netdev(hhnetDev);
    ndev = GetLinuxInfByNetDevice(netDev);
    wiphy = get_linux_wiphy_ndev(ndev);
    channelTotal = ieee80211_get_num_supported_channels(wiphy);
    g_scan_event_ifidx = get_scan_ifidx(hnetdev->name);

    request =
        (struct cfg80211_scan_request *)OsalMemCalloc(
            sizeof(struct cfg80211_scan_request) + (PTR_IEEE80211_CHANNEL_SIZE) * channelTotal);

    HDF_LOGI("%s: enter hdfStartScan %s, channelTotal: %d, for %u", __func__, ndev->name,
        channelTotal, (PTR_IEEE80211_CHANNEL_SIZE));

    if (request == NULL) {
        return HDF_FAILURE;
    }
    if (WifiScanSetRequest(netDev, scanParam, request) != HDF_SUCCESS) {
        WifiScanFree(&request);
        return HDF_FAILURE;
    }
    if (g_scan_event_ifidx == HDF_INF_P2P0 && g_hdf_infmap[HDF_INF_P2P0].wdev) {
        request->wdev = g_hdf_infmap[HDF_INF_P2P0].wdev;
    }
    
    HDF_LOGI("%s: enter cfg80211_scan, n_ssids=%d !", __func__, request->n_ssids);
    ret = wl_cfg80211_ops.scan(wiphy, request);
    HDF_LOGI("%s: left cfg80211_scan %d!", __func__, ret);

    if (ret != HDF_SUCCESS) {
        WifiScanFree(&request);
    }
    
    return ret;
}

int32_t HdfStartScan(NetDevice *hhnetDev, struct WlanScanRequest *scanParam)
{
    int32_t ret = 0;
    mutex_lock(&bdh6_reset_driver_lock);
    rtnl_lock();
    ret = HdfStartScanEntry(hhnetDev, scanParam);
    rtnl_unlock();
    mutex_unlock(&bdh6_reset_driver_lock);
    return ret;
}


int32_t HdfAbortScan(NetDevice *hnetDev)
{
    struct net_device *ndev = NULL;
    struct wireless_dev *wdev = NULL;
    struct wiphy *wiphy = NULL;
    struct NetDevice *netDev = NULL;
    g_scan_event_ifidx = get_scan_ifidx(hnetDev->name);
    netDev = get_real_netdev(hnetDev);
    if (netDev == NULL) {
        HDF_LOGE("%s:NULL ptr!", __func__);
        return HDF_FAILURE;
    }

    HDF_LOGI("%s: enter", __func__);
    ndev = GetLinuxInfByNetDevice(netDev);
    wiphy = get_linux_wiphy_ndev(ndev);
    if (ndev == NULL) {
        HDF_LOGE("%s:NULL ptr!", __func__);
        return HDF_FAILURE;
    }
    wdev = ndev->ieee80211_ptr;
    if (!wdev || !wdev->wiphy) {
        return HDF_FAILURE;
    }
    rtnl_lock();
    wl_cfg80211_abort_scan(wiphy, wdev);
    rtnl_unlock();
    return HDF_SUCCESS;
}

struct ieee80211_channel *WalGetChannel(struct wiphy *wiphy, int32_t freq)
{
    int32_t loop;

    enum Ieee80211Band band = IEEE80211_BAND_2GHZ;
    struct ieee80211_supported_band *currentBand = NULL;

    if (wiphy == NULL) {
        HDF_LOGE("%s: capality is NULL!", __func__);
        return NULL;
    }

    for (band = (enum Ieee80211Band)0; band < IEEE80211_NUM_BANDS; band++) {
        currentBand = wiphy->bands[band];
        if (currentBand == NULL) {
            continue;
        }

        for (loop = 0; loop < currentBand->n_channels; loop++) {
            if (currentBand->channels[loop].center_freq == freq) {
                return &currentBand->channels[loop];
            }
        }
    }

    return NULL;
}

#define BD0 0
#define BD1 1
#define BD2 2
#define BD3 3
#define BD4 4
#define BD5 5

static void FillCfg80211ConnectParams(const WlanConnectParams *param, struct cfg80211_connect_params *cfg80211_params)
{
    cfg80211_params->bssid = param->bssid;
    cfg80211_params->ssid = param->ssid;
    cfg80211_params->ie = param->ie;
    cfg80211_params->ssid_len = param->ssidLen;
    cfg80211_params->ie_len = param->ieLen;

    cfg80211_params->crypto.wpa_versions = param->crypto.wpaVersions;
    cfg80211_params->crypto.cipher_group = param->crypto.cipherGroup;
    cfg80211_params->crypto.n_ciphers_pairwise = param->crypto.n_ciphersPairwise;
                                                      
    memcpy_s(cfg80211_params->crypto.ciphers_pairwise,
        NL80211_MAX_NR_CIPHER_SUITES*sizeof(cfg80211_params->crypto.ciphers_pairwise[0]),
        param->crypto.ciphersPairwise, NL80211_MAX_NR_CIPHER_SUITES*sizeof(param->crypto.ciphersPairwise[0]));

    memcpy_s(cfg80211_params->crypto.akm_suites,
        NL80211_MAX_NR_AKM_SUITES*sizeof(cfg80211_params->crypto.akm_suites[0]), param->crypto.akmSuites,
        NL80211_MAX_NR_AKM_SUITES*sizeof(param->crypto.akmSuites[0]));

    cfg80211_params->crypto.n_akm_suites = param->crypto.n_akmSuites;

    if (param->crypto.controlPort) {
        cfg80211_params->crypto.control_port = true;
    } else {
        cfg80211_params->crypto.control_port = false;
    }

    cfg80211_params->crypto.control_port_ethertype = param->crypto.controlPortEthertype;
    cfg80211_params->crypto.control_port_no_encrypt = param->crypto.controlPortNoEncrypt;
    
    cfg80211_params->key = param->key;
    cfg80211_params->auth_type = (unsigned char)param->authType;
    cfg80211_params->privacy = param->privacy;
    cfg80211_params->key_len = param->keyLen;
    cfg80211_params->key_idx = param->keyIdx;
    cfg80211_params->mfp = (unsigned char)param->mfp;
}

static int32_t HdfConnectEntry(NetDevice *hnetDev, WlanConnectParams *param)
{
    int32_t ret = 0;
    struct net_device *ndev = NULL;
    struct wiphy *wiphy = NULL;
    struct NetDevice *netDev = NULL;
    struct cfg80211_connect_params cfg80211_params = { 0 };
    g_conn_event_ifidx = get_scan_ifidx(hnetDev->name);
    netDev = get_real_netdev(hnetDev);
    if (netDev == NULL || param == NULL) {
        HDF_LOGE("%s:NULL ptr!", __func__);
        return HDF_FAILURE;
    }
    ndev = GetLinuxInfByNetDevice(netDev);
    if (ndev == NULL) {
        HDF_LOGE("%s:NULL ptr!", __func__);
        return HDF_FAILURE;
    }

    wiphy = get_linux_wiphy_ndev(ndev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }

    if (param->centerFreq != WLAN_FREQ_NOT_SPECFIED) {
        cfg80211_params.channel = WalGetChannel(wiphy, param->centerFreq);
        if ((cfg80211_params.channel == NULL) || (cfg80211_params.channel->flags & WIFI_CHAN_DISABLED)) {
            HDF_LOGE("%s:illegal channel.flags=%u", __func__,
                (cfg80211_params.channel == NULL) ? 0 : cfg80211_params.channel->flags);
            return HDF_FAILURE;
        }
    }
    FillCfg80211ConnectParams(param, &cfg80211_params);

    HDF_LOGI("%s: %s connect ssid: %s", __func__, netDev->name, cfg80211_params.ssid);
    HDF_LOGI("%s: cfg80211_params auth_type:%d--channelId:%d--centerFreq:%d--Mac:%02x:%02x:%02x:%02x:%02x:%02x",
        __func__, cfg80211_params.auth_type, cfg80211_params.channel->band, param->centerFreq,
        cfg80211_params.bssid[BD0], cfg80211_params.bssid[BD1], cfg80211_params.bssid[BD2],
        cfg80211_params.bssid[BD3], cfg80211_params.bssid[BD4], cfg80211_params.bssid[BD5]);

    bdh6_hdf_vxx_lock(ndev->ieee80211_ptr, 0);
    ret = wl_cfg80211_ops.connect(wiphy, ndev, &cfg80211_params);
    bdh6_hdf_vxx_unlock(ndev->ieee80211_ptr, 0);
    if (ret < 0) {
        HDF_LOGE("%s: connect failed!\n", __func__);
    }
    return ret;
}

int32_t HdfConnect(NetDevice *hnetDev, WlanConnectParams *param)
{
    int32_t ret = 0;
    mutex_lock(&bdh6_reset_driver_lock);
    rtnl_lock();
    ret = HdfConnectEntry(hnetDev, param);
    rtnl_unlock();
    mutex_unlock(&bdh6_reset_driver_lock);
    return ret;
}

int32_t HdfDisconnect(NetDevice *hnetDev, uint16_t reasonCode)
{
    int32_t ret = 0;
    struct net_device *ndev = NULL;
    struct wiphy *wiphy = NULL;
    struct NetDevice *netDev = NULL;
    g_conn_event_ifidx = get_scan_ifidx(hnetDev->name);
    netDev = get_real_netdev(hnetDev);

    HDF_LOGI("%s: start...", __func__);
    if (netDev == NULL) {
        HDF_LOGE("%s:NULL ptr!", __func__);
        return HDF_FAILURE;
    }
    ndev = GetLinuxInfByNetDevice(netDev);
    if (ndev == NULL) {
        HDF_LOGE("%s:NULL ptr!", __func__);
        return HDF_FAILURE;
    }

    wiphy = get_linux_wiphy_ndev(ndev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }

    rtnl_lock();
    bdh6_hdf_vxx_lock(ndev->ieee80211_ptr, 0);
    ret = wl_cfg80211_ops.disconnect(wiphy, ndev, reasonCode);
    bdh6_hdf_vxx_unlock(ndev->ieee80211_ptr, 0);
    rtnl_unlock();
    return ret;
}

int32_t HdfSetScanningMacAddress(NetDevice *hnetDev, unsigned char *mac, uint32_t len)
{
    struct NetDevice *netDev = NULL;
    netDev = get_real_netdev(hnetDev);
    (void)mac;
    (void)len;
    return HDF_ERR_NOT_SUPPORT;
}

static struct HdfMac80211STAOps g_bdh6_staOps = {
    .Connect = HdfConnect,
    .Disconnect = HdfDisconnect,
    .StartScan = HdfStartScan,
    .AbortScan = HdfAbortScan,
    .SetScanningMacAddress = HdfSetScanningMacAddress,
};

/*--------------------------------------------------------------------------------------------------*/
/* public */
/*--------------------------------------------------------------------------------------------------*/
#define MAX_NUM_OF_ASSOCIATED_DEV		64
#define WLC_GET_ASSOCLIST		159
#define ETH_MAC_LEN 6

static struct cfg80211_ap_settings g_ap_setting_info;

int ChangDelSta(struct net_device *dev, const uint8_t *macAddr, uint8_t addrLen)
{
    int ret;
    struct NetDevice *netDev = GetHdfNetDeviceByLinuxInf(dev);
    ret = HdfWifiEventDelSta(netDev, macAddr, ETH_MAC_LEN);
    return ret;
}

int ChangNewSta(struct net_device *dev, const uint8_t *macAddr, uint8_t addrLen,
    const struct station_info *info)
{
    int ret;
    struct NetDevice *netDev = NULL;
    struct StationInfo Info = {0};
 
    Info.assocReqIes = info->assoc_req_ies;
    Info.assocReqIesLen = info->assoc_req_ies_len;
    netDev = GetHdfNetDeviceByLinuxInf(dev);
    ret = HdfWifiEventNewSta(netDev, macAddr, addrLen, &Info);
    return ret;
}

int32_t wl_get_all_sta(struct net_device *ndev, uint32_t *num)
{
    int ret = 0;
    char mac_buf[MAX_NUM_OF_ASSOCIATED_DEV *
    sizeof(struct ether_addr) + sizeof(uint)] = {0};
    struct maclist *assoc_maclist = (struct maclist *)mac_buf;
    assoc_maclist->count = MAX_NUM_OF_ASSOCIATED_DEV;
    ret = wldev_ioctl_get(ndev, WLC_GET_ASSOCLIST,
        (void *)assoc_maclist, sizeof(mac_buf));
    *num = assoc_maclist->count;
    return 0;
}
#define MAX_NUM_OF_ASSOCLIST    64

#define RATE_OFFSET  2
#define CAP_OFFSET  3

static void bdh6_nl80211_check_ap_rate_selectors(struct cfg80211_ap_settings *params,
    const u8 *rates)
{
    int i;
    if (!rates)
        return;

    for (i = 0; i < rates[1]; i++) {
        if (rates[RATE_OFFSET + i] == BSS_MEMBERSHIP_SELECTOR_HT_PHY)
            params->ht_required = true;
        if (rates[RATE_OFFSET + i] == BSS_MEMBERSHIP_SELECTOR_VHT_PHY)
            params->vht_required = true;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 10, 0))
        if (rates[RATE_OFFSET + i] == BSS_MEMBERSHIP_SELECTOR_HE_PHY)
            params->he_required = true;
#endif
	}
}

void bdh6_nl80211_calculate_ap_params(struct cfg80211_ap_settings *params)
{
    const struct cfg80211_beacon_data *bcn = &params->beacon;
    size_t ies_len = bcn->tail_len;
    const u8 *ies = bcn->tail;
    const u8 *rates;
    const u8 *cap;

    rates = cfg80211_find_ie(WLAN_EID_SUPP_RATES, ies, ies_len);
    HDF_LOGI("find beacon tail WLAN_EID_SUPP_RATES=%p", rates);
    bdh6_nl80211_check_ap_rate_selectors(params, rates);

    rates = cfg80211_find_ie(WLAN_EID_EXT_SUPP_RATES, ies, ies_len);
    HDF_LOGI("find beacon tail WLAN_EID_EXT_SUPP_RATES=%p", rates);
    bdh6_nl80211_check_ap_rate_selectors(params, rates);

    cap = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, ies, ies_len);
    HDF_LOGI("find beacon tail WLAN_EID_HT_CAPABILITY=%p", cap);
    if (cap && cap[1] >= sizeof(*params->ht_cap))
        params->ht_cap = (void *)(cap + RATE_OFFSET);
    cap = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, ies, ies_len);
    HDF_LOGI("find beacon tail WLAN_EID_VHT_CAPABILITY=%p", cap);
    if (cap && cap[1] >= sizeof(*params->vht_cap))
        params->vht_cap = (void *)(cap + RATE_OFFSET);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 10, 0))
    cap = cfg80211_find_ext_ie(WLAN_EID_EXT_HE_CAPABILITY, ies, ies_len);
    HDF_LOGI("find beacon tail WLAN_EID_EXT_HE_CAPABILITY=%p", cap);
    if (cap && cap[1] >= sizeof(*params->he_cap) + 1) {
        params->he_cap = (void *)(cap + CAP_OFFSET);
    }
    cap = cfg80211_find_ext_ie(WLAN_EID_EXT_HE_OPERATION, ies, ies_len);
    HDF_LOGI("find beacon tail WLAN_EID_EXT_HE_OPERATION=%p", cap);
    if (cap && cap[1] >= sizeof(*params->he_oper) + 1) {
        params->he_oper = (void *)(cap + CAP_OFFSET);
    }
#endif
}


static struct ieee80211_channel g_ap_ieee80211_channel;
static u8 g_ap_ssid[IEEE80211_MAX_SSID_LEN];
static int start_ap_flag = 0;

static int32_t SetupWireLessDev(struct net_device *netDev, struct WlanAPConf *apSettings)
{
    struct wireless_dev *wdev = NULL;
    struct ieee80211_channel *chan = NULL;
    
    wdev = netDev->ieee80211_ptr;
    if (wdev == NULL) {
        HDF_LOGE("%s: wdev is null", __func__);
        return -1;
    }
    HDF_LOGI("%s:%p, chan=%p, channel=%u, centfreq1=%u, centfreq2=%u, band=%u, width=%u", __func__,
        wdev, wdev->preset_chandef.chan,
        apSettings->channel, apSettings->centerFreq1, apSettings->centerFreq2, apSettings->band, apSettings->width);

    wdev->iftype = NL80211_IFTYPE_AP;
    wdev->preset_chandef.width = (enum nl80211_channel_type)apSettings->width;
    wdev->preset_chandef.center_freq1 = apSettings->centerFreq1;
    wdev->preset_chandef.center_freq2 = apSettings->centerFreq2;
    
    chan = ieee80211_get_channel(wdev->wiphy, apSettings->centerFreq1);
    if (chan) {
        wdev->preset_chandef.chan = chan;
        HDF_LOGI("%s: use valid channel %u", __func__, chan->center_freq);
    } else if (wdev->preset_chandef.chan == NULL) {
        wdev->preset_chandef.chan = &g_ap_ieee80211_channel;
        wdev->preset_chandef.chan->hw_value = apSettings->channel;
        wdev->preset_chandef.chan->band = apSettings->band; // IEEE80211_BAND_2GHZ;
        wdev->preset_chandef.chan->center_freq = apSettings->centerFreq1;
        HDF_LOGI("%s: fill new channel %u", __func__, wdev->preset_chandef.chan->center_freq);
    }

    g_ap_setting_info.chandef = wdev->preset_chandef;
    
    return HDF_SUCCESS;
}

/*--------------------------------------------------------------------------------------------------*/
/* HdfMac80211APOps */
/*--------------------------------------------------------------------------------------------------*/
static int32_t WalConfigApEntry(NetDevice *hnetDev, struct WlanAPConf *apConf)
{
    int32_t ret = 0;
    struct net_device *netdev = NULL;
    struct NetDevice *netDev = NULL;
    struct wiphy *wiphy = NULL;
    netDev = get_real_netdev(hnetDev);
    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }
    
    wiphy = get_linux_wiphy_ndev(netdev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }

    memset_s(&g_ap_setting_info, sizeof(g_ap_setting_info), 0, sizeof(g_ap_setting_info));
    ret = SetupWireLessDev(netdev, apConf);
    if (ret != 0) {
        HDF_LOGE("%s: set up wireless device failed!", __func__);
        return HDF_FAILURE;
    }

    HDF_LOGI("%s: before iftype = %d!", __func__, netdev->ieee80211_ptr->iftype);
    netdev->ieee80211_ptr->iftype = NL80211_IFTYPE_AP;
    HDF_LOGI("%s: after  iftype = %d!", __func__, netdev->ieee80211_ptr->iftype);

    g_ap_setting_info.ssid_len = apConf->ssidConf.ssidLen;
    memcpy_s(&g_ap_ssid[0], IEEE80211_MAX_SSID_LEN, &apConf->ssidConf.ssid[0], apConf->ssidConf.ssidLen);
    g_ap_setting_info.ssid = &g_ap_ssid[0];
    ret = (int32_t)wl_cfg80211_ops.change_virtual_intf(wiphy, netdev,
        (enum nl80211_iftype)netdev->ieee80211_ptr->iftype, NULL);
    if (ret < 0) {
        HDF_LOGE("%s: set mode failed!", __func__);
    }

    return HDF_SUCCESS;
}

int32_t WalConfigAp(NetDevice *hnetDev, struct WlanAPConf *apConf)
{
    int32_t ret = 0;
    rtnl_lock();
    ret = WalConfigApEntry(hnetDev, apConf);
    rtnl_unlock();
    return ret;
}

static void InitCfg80211BeaconDataInfo(struct cfg80211_beacon_data *pInfo, const struct WlanBeaconConf *param)
{
    memset_s(pInfo, sizeof(struct cfg80211_beacon_data), 0x00, sizeof(struct cfg80211_beacon_data));
    pInfo->head = param->headIEs;
    pInfo->head_len = (size_t)param->headIEsLength;
    pInfo->tail = param->tailIEs;
    pInfo->tail_len = (size_t)param->tailIEsLength;

    pInfo->beacon_ies = NULL;
    pInfo->proberesp_ies = NULL;
    pInfo->assocresp_ies = NULL;
    pInfo->probe_resp = NULL;
    pInfo->beacon_ies_len = 0X00;
    pInfo->proberesp_ies_len = 0X00;
    pInfo->assocresp_ies_len = 0X00;
    pInfo->probe_resp_len = 0X00;
}

static void InitCfg80211ApSettingInfo(const struct WlanBeaconConf *param)
{
    if (g_ap_setting_info.beacon.head != NULL) {
        OsalMemFree((uint8_t *)g_ap_setting_info.beacon.head);
        g_ap_setting_info.beacon.head = NULL;
    }
    if (g_ap_setting_info.beacon.tail != NULL) {
        OsalMemFree((uint8_t *)g_ap_setting_info.beacon.tail);
        g_ap_setting_info.beacon.tail = NULL;
    }

    if (param->headIEs && param->headIEsLength > 0) {
        g_ap_setting_info.beacon.head = OsalMemCalloc(param->headIEsLength);
        memcpy_s((uint8_t *)g_ap_setting_info.beacon.head, param->headIEsLength, param->headIEs, param->headIEsLength);
        g_ap_setting_info.beacon.head_len = param->headIEsLength;
    }

    if (param->tailIEs && param->tailIEsLength > 0) {
        g_ap_setting_info.beacon.tail = OsalMemCalloc(param->tailIEsLength);
        memcpy_s((uint8_t *)g_ap_setting_info.beacon.tail, param->tailIEsLength, param->tailIEs, param->tailIEsLength);
        g_ap_setting_info.beacon.tail_len = param->tailIEsLength;
    }

    /* add beacon data for start ap */
    g_ap_setting_info.dtim_period = param->DTIMPeriod;
    g_ap_setting_info.hidden_ssid = param->hiddenSSID;
    g_ap_setting_info.beacon_interval = param->interval;
    HDF_LOGI("%s: dtim_period:%d---hidden_ssid:%d---beacon_interval:%d!",
        __func__, g_ap_setting_info.dtim_period, g_ap_setting_info.hidden_ssid, g_ap_setting_info.beacon_interval);

    g_ap_setting_info.beacon.beacon_ies = NULL;
    g_ap_setting_info.beacon.proberesp_ies = NULL;
    g_ap_setting_info.beacon.assocresp_ies = NULL;
    g_ap_setting_info.beacon.probe_resp = NULL;
    g_ap_setting_info.beacon.beacon_ies_len = 0X00;
    g_ap_setting_info.beacon.proberesp_ies_len = 0X00;
    g_ap_setting_info.beacon.assocresp_ies_len = 0X00;
    g_ap_setting_info.beacon.probe_resp_len = 0X00;

    bdh6_nl80211_calculate_ap_params(&g_ap_setting_info);
}

int32_t WalStartAp(NetDevice *hnetDev)
{
    int32_t ret = 0;
    struct net_device *netdev = NULL;
    struct wiphy *wiphy = NULL;
    struct wireless_dev *wdev = NULL;
    struct NetDevice *netDev = NULL;
    netDev = get_real_netdev(hnetDev);

    if (start_ap_flag) {
        WalStopAp(netDev);
        HDF_LOGE("do not start up, because start_ap_flag=%d", start_ap_flag);
    }
    
    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }
    
    wiphy = get_linux_wiphy_ndev(netdev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }
    rtnl_lock();
    bdh6_hdf_vxx_lock(netdev->ieee80211_ptr, 0);
    wdev = netdev->ieee80211_ptr;
    HDF_LOGI("%s: start...", __func__);
    ret = (int32_t)wl_cfg80211_ops.start_ap(wiphy, netdev, &g_ap_setting_info);
    if (ret < 0) {
        HDF_LOGE("%s: start_ap failed!", __func__);
    } else {
        wdev->preset_chandef = g_ap_setting_info.chandef;
        wdev->beacon_interval = g_ap_setting_info.beacon_interval;
        wdev->chandef = g_ap_setting_info.chandef;
        wdev->ssid_len = g_ap_setting_info.ssid_len;
        memcpy_s(wdev->ssid, wdev->ssid_len, g_ap_setting_info.ssid, wdev->ssid_len);
        start_ap_flag = 1;
    }
    bdh6_hdf_vxx_unlock(netdev->ieee80211_ptr, 0);
    rtnl_unlock();
    return ret;
}

int32_t WalChangeBeacon(NetDevice *hnetDev, struct WlanBeaconConf *param)
{
    int32_t ret = 0;
    struct cfg80211_beacon_data info;
    struct net_device *netdev = NULL;
    struct wiphy *wiphy = NULL;
    NetDevice *netDev = NULL;
    netDev = get_real_netdev(hnetDev);
    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }
    
    wiphy = get_linux_wiphy_ndev(netdev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }

    HDF_LOGI("%s: start...", __func__);
    if ((int)param->interval <= 0) {
        HDF_LOGE("%s: invalid beacon interval=%d, %d,%d", __func__,
            (int)param->interval, param->DTIMPeriod, (int)param->hiddenSSID);
        return 0;
    }

    InitCfg80211BeaconDataInfo(&info, param);
    InitCfg80211ApSettingInfo(param);

    HDF_LOGI("%s: headIEsLen:%d---tailIEsLen:%d!", __func__, param->headIEsLength, param->tailIEsLength);
    ret = WalStartAp(netDev);
    HDF_LOGI("call start_ap ret=%d", ret);

    rtnl_lock();
    bdh6_hdf_vxx_lock(netdev->ieee80211_ptr, 0);
    ret = (int32_t)wl_cfg80211_ops.change_beacon(wiphy, netdev, &info);
    bdh6_hdf_vxx_unlock(netdev->ieee80211_ptr, 0);
    rtnl_unlock();
    if (ret < 0) {
        HDF_LOGE("%s: change_beacon failed!", __func__);
    }

    return HDF_SUCCESS;
}

int32_t WalSetCountryCode(NetDevice *hnetDev, const char *code, uint32_t len)
{
    int32_t ret = 0;
    struct net_device *netdev = NULL;
    struct NetDevice *netDev = NULL;
    netDev = get_real_netdev(hnetDev);
    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }

    rtnl_lock();
    ret = (int32_t)wl_cfg80211_set_country_code(netdev, (char*)code, false, true, len);
    rtnl_unlock();
    if (ret < 0) {
        HDF_LOGE("%s: set_country_code failed!", __func__);
    }
    return ret;
}

int32_t WalStopAp(NetDevice *hnetDev)
{
    int32_t ret = 0;
    struct net_device *netdev = NULL;
    struct NetDevice *netDev = NULL;
    struct wiphy *wiphy = NULL;
    netDev = get_real_netdev(hnetDev);
    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }
    
    wiphy = get_linux_wiphy_ndev(netdev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }

    HDF_LOGI("%s: start...", __func__);
    rtnl_lock();
    bdh6_hdf_vxx_lock(netdev->ieee80211_ptr, 0);
    ret = (int32_t)wl_cfg80211_ops.stop_ap(wiphy, netdev);
    bdh6_hdf_vxx_unlock(netdev->ieee80211_ptr, 0);
    rtnl_unlock();
    return ret;
}

int32_t WalDelStation(NetDevice *hnetDev, const uint8_t *macAddr)
{
    int32_t ret = 0;
    struct net_device *netdev = NULL;
    struct wiphy *wiphy = NULL;
    struct NetDevice *netDev = NULL;
    struct station_del_parameters del_param = {macAddr, 10, 0};
    netDev = get_real_netdev(hnetDev);
    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }
    
    wiphy = get_linux_wiphy_ndev(netdev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }

    (void)macAddr;
    HDF_LOGI("%s: start...", __func__);

    ret = (int32_t)wl_cfg80211_ops.del_station(wiphy, netdev, &del_param);
    if (ret < 0) {
        HDF_LOGE("%s: del_station failed!", __func__);
    }
    return ret;
}

int32_t WalGetAssociatedStasCount(NetDevice *hnetDev, uint32_t *num)
{
    int32_t ret = 0;
    struct net_device *netdev = NULL;
    struct NetDevice *netDev = NULL;
    netDev = get_real_netdev(hnetDev);
    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }

    rtnl_lock();
    ret = (int32_t)wl_get_all_sta(netdev, num);
    rtnl_unlock();
    if (ret < 0) {
        HDF_LOGE("%s: wl_get_all_sta failed!", __func__);
    }
    return ret;
}

int32_t WalGetAssociatedStasInfo(NetDevice *hnetDev, WifiStaInfo *staInfo, uint32_t num)
{
    struct net_device *netdev = NULL;
    struct NetDevice *netDev = NULL;
    int  error = 0, i = 0;
    char mac_buf[MAX_NUM_OF_ASSOCLIST *sizeof(struct ether_addr) + sizeof(uint)] = {0};
    struct maclist *assoc_maclist = (struct maclist *)mac_buf;
    netDev = get_real_netdev(hnetDev);
    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }
    HDF_LOGI("%s: start..., num=%u", __func__, num);
    if (staInfo == NULL || num == 0) {
        HDF_LOGE("%s: invalid parameter staInfo=%p, num=%u", __func__);
        return -1;
    }

    assoc_maclist->count = (MAX_NUM_OF_ASSOCLIST);
    rtnl_lock();
    error = wldev_ioctl_get(netdev, WLC_GET_ASSOCLIST, (void *)assoc_maclist, sizeof(mac_buf));
    rtnl_unlock();
    if (error) {
        HDF_LOGE("%s: WLC_GET_ASSOCLIST ret=%d", __func__, error);
        return -1;
    }
    if (num > assoc_maclist->count) {
        HDF_LOGE("%s: num=%u is larger than assoc_num=%u", __func__, num, assoc_maclist->count);
        num = assoc_maclist->count;
    }
    for (i = 0; i < num; i++) {
        memcpy_s(staInfo[i].mac, ETHER_ADDR_LEN, assoc_maclist->ea[i].octet, ETHER_ADDR_LEN);
    }

    return HDF_SUCCESS;
}

static struct HdfMac80211APOps g_bdh6_apOps = {
    .ConfigAp = WalConfigAp,
    .StartAp = WalStartAp,
    .StopAp = WalStopAp,
    .ConfigBeacon = WalChangeBeacon,
    .DelStation = WalDelStation,
    .SetCountryCode = WalSetCountryCode,
    .GetAssociatedStasCount = WalGetAssociatedStasCount,
    .GetAssociatedStasInfo = WalGetAssociatedStasInfo
};

enum wl_management_type {
    WL_BEACON = 0x1,
    WL_PROBE_RESP = 0x2,
    WL_ASSOC_RESP = 0x4
};


#define HISI_DRIVER_FLAGS_AP                         0x00000040
#define HISI_DRIVER_FLAGS_P2P_DEDICATED_INTERFACE    0x00000400
#define HISI_DRIVER_FLAGS_P2P_CONCURRENT             0x00000200
#define HISI_DRIVER_FLAGS_P2P_CAPABLE                0x00000800

#if defined(WL_CFG80211_P2P_DEV_IF)
static inline void *ndev_to_cfg(struct net_device *ndev)
{
    return ndev->ieee80211_ptr;
}
#else
static inline void *ndev_to_cfg(struct net_device *ndev)
{
    return ndev;
}
#endif

s32 wl_cfg80211_set_wps_p2p_ie(
    struct net_device *net, char *buf, int len, enum wl_management_type type);


static u64 p2p_cookie = 0;
u32 p2p_remain_freq = 0;
int start_p2p_completed = 0;

int32_t WalRemainOnChannel(struct NetDevice *hnetDev, WifiOnChannel *onChannel)
{
    struct net_device *netdev = NULL;
    struct wiphy *wiphy = NULL;
    bcm_struct_cfgdev *cfgdev = NULL;
    struct ieee80211_channel *channel = NULL;
    unsigned int duration;
    struct NetDevice *netDev = NULL;
    int ret = 0;

    netDev = get_real_netdev(hnetDev);
    netdev = GetLinuxInfByNetDevice(netDev);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }

    wiphy = get_linux_wiphy_ndev(netdev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }
    HDF_LOGI("%s: ifname=%s, freq=%u, duration=%u", __func__, hnetDev->name, onChannel->freq, onChannel->duration);

    channel = OsalMemCalloc(sizeof(struct ieee80211_channel));
    cfgdev = ndev_to_cfg(netdev);
    channel->center_freq = onChannel->freq;
    duration = (unsigned int)onChannel->duration;
    p2p_remain_freq = channel->center_freq;

    rtnl_lock();
    ret = wl_cfg80211_ops.remain_on_channel(wiphy, cfgdev, channel, duration, &p2p_cookie);
    rtnl_unlock();
    OsalMemFree(channel);
    return ret;
}

int32_t WalCancelRemainOnChannel(struct NetDevice *hnetDev)
{
    struct net_device *netdev = NULL;
    bcm_struct_cfgdev *cfgdev = NULL;
    struct wiphy *wiphy = NULL;
    struct NetDevice *netDev = NULL;
    int ret = 0;
    
    netDev = get_real_netdev(hnetDev);
    netdev = GetLinuxInfByNetDevice(netDev);
    wiphy = get_linux_wiphy_ndev(netdev);
    if (!wiphy) {
        HDF_LOGE("%s: wiphy is NULL", __func__);
        return -1;
    }

    HDF_LOGI("%s: ifname = %s", __func__, hnetDev->name);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }

    cfgdev =  ndev_to_cfg(netdev);
    rtnl_lock();
    ret = wl_cfg80211_ops.cancel_remain_on_channel(wiphy, cfgdev, p2p_cookie);
    rtnl_unlock();
    return ret;
}

int32_t WalProbeReqReport(struct NetDevice *netDev, int32_t report)
{
    (void)report;
    HDF_LOGI("%s: ifname = %s, report=%d", __func__, netDev->name, report);
    return HDF_SUCCESS;
}

#define M0 0
#define M1 1
#define M2 2
#define M3 3
#define M4 4
#define M5 5

int32_t WalAddIf(struct NetDevice *hnetDev, WifiIfAdd *ifAdd)
{
    struct wiphy *wiphy = NULL;
    struct wireless_dev *wdev = NULL;
    int ret = 0;
    struct net_device *p2p_netdev = NULL;
    struct NetDevice *p2p_hnetdev = NULL;
    struct NetDevice *netDev = NULL;
    
    if (hnetDev == NULL || ifAdd == NULL) {
        HDF_LOGE("%s:NULL ptr!", __func__);
        return -1;
    }
    
    HDF_LOGI("%s: ifname = %s, type=%u", __func__, hnetDev->name, ifAdd->type);
    netDev = get_real_netdev(hnetDev);
    if (g_hdf_infmap[HDF_INF_P2P1].hnetdev != NULL) {
        HDF_LOGE("%s: ifidx=%d was used, failed add if", __func__, HDF_INF_P2P1);
        return -1;
    }

    ret = P2pInitNetdev(netDev, ifAdd, get_dhd_priv_data_size(), HDF_INF_P2P1);
    if (ret != 0) {
        HDF_LOGE("%s:P2pInitNetdev %s failed", __func__, ifAdd->ifName);
        return HDF_FAILURE;
    }

    wiphy = get_linux_wiphy_hdfdev(netDev);
    if (wiphy == NULL) {
        HDF_LOGE("%s:get wlan0 wiphy failed", __func__);
        return HDF_FAILURE;
    }

    p2p_hnetdev = get_hdf_netdev(g_hdf_ifidx);
    p2p_netdev = get_krn_netdev(g_hdf_ifidx);
    
    wdev = wl_cfg80211_ops.add_virtual_intf(wiphy, p2p_hnetdev->name, NET_NAME_USER, ifAdd->type, NULL);
    if (wdev == NULL || wdev == ERR_PTR(-ENODEV)) {
        HDF_LOGE("%s:create wdev for %s %d failed", __func__, p2p_hnetdev->name, ifAdd->type);
        return HDF_FAILURE;
    }
    HDF_LOGI("%s:%s wdev->netdev=%p, %p", __func__, p2p_hnetdev->name, wdev->netdev, p2p_netdev);
    p2p_hnetdev->ieee80211Ptr = p2p_netdev->ieee80211_ptr;
    
    // update mac addr to NetDevice object
    memcpy_s(p2p_hnetdev->macAddr, MAC_ADDR_SIZE, p2p_netdev->dev_addr, p2p_netdev->addr_len);
    HDF_LOGI("%s: %s mac: %02x:%02x:%02x:%02x:%02x:%02x", __func__, p2p_hnetdev->name,
        p2p_hnetdev->macAddr[M0],
        p2p_hnetdev->macAddr[M1],
        p2p_hnetdev->macAddr[M2],
        p2p_hnetdev->macAddr[M3],
        p2p_hnetdev->macAddr[M4],
        p2p_hnetdev->macAddr[M5]);
    return HDF_SUCCESS;
}

int32_t WalRemoveIf(struct NetDevice *hnetDev, WifiIfRemove *ifRemove)
{
    int i = HDF_INF_WLAN0;
    struct wiphy *wiphy = NULL;
    struct wireless_dev *wdev = NULL;
    struct NetDevice *p2p_hnetdev = NULL;
    int ret = 0;
    struct NetDevice *netDev = NULL;
    netDev = get_real_netdev(hnetDev);

    wiphy = get_linux_wiphy_hdfdev(netDev);
    if (wiphy == NULL) {
        HDF_LOGE("%s:get wlan0 wiphy failed", __func__);
        return HDF_FAILURE;
    }
    
    HDF_LOGI("%s: ifname=%s, primary netdev %s, remove ifname=%s", __func__,
        hnetDev->name, netDev->name, ifRemove->ifName);
    for (; i < HDF_INF_MAX; i ++) {
        p2p_hnetdev = g_hdf_infmap[i].hnetdev;
        if (p2p_hnetdev == NULL) {
            continue;
        }
        
        if (strcmp(p2p_hnetdev->name, ifRemove->ifName) == 0) {
            // check safely
            if (i == HDF_INF_WLAN0) {
                HDF_LOGE("%s: don't remove master interface %s", __func__, ifRemove->ifName);
                continue;
            }
            if (i != HDF_INF_P2P1) {
                HDF_LOGE("%s: remove %s is not p2p interface (%d %d)", __func__, ifRemove->ifName, i, HDF_INF_P2P1);
            }

            wdev = (struct wireless_dev *)p2p_hnetdev->ieee80211Ptr;
            ret = (int32_t)wl_cfg80211_ops.change_virtual_intf(wiphy, g_hdf_infmap[i].netdev,
                NL80211_IFTYPE_STATION, NULL);
            HDF_LOGI("%s: change %s mode %d --> %d, ret=%d", __func__, g_hdf_infmap[i].netdev->name,
                wdev->iftype, NL80211_IFTYPE_STATION, ret);
            
            rtnl_lock();
            // clear private object
            DestroyEapolData(p2p_hnetdev);
            p2p_hnetdev->ieee80211Ptr = NULL;
            // This func free wdev object and call unregister_netdevice() and NetDeviceDeInit()
            ret = wl_cfg80211_ops.del_virtual_intf(wiphy, wdev);

            g_hdf_infmap[i].hnetdev = NULL;
            g_hdf_infmap[i].netdev = NULL;
            g_hdf_infmap[i].wdev = NULL;
            g_hdf_ifidx = HDF_INF_WLAN0;
            g_event_ifidx = HDF_INF_P2P0;
            rtnl_unlock();
            break;
        }
    }
    
    return ret;
}

int32_t WalSetApWpsP2pIe(struct NetDevice *hnetDev, WifiAppIe *appIe)
{
    struct net_device *netdev = NULL;
    enum wl_management_type type;
    struct NetDevice *netDev = NULL;
    int ret = 0;
    
    netDev = get_real_netdev(hnetDev);
    netdev = GetLinuxInfByNetDevice(netDev);
    type = appIe->appIeType;

    HDF_LOGI("%s: primary netdev %s, type=%d", __func__, netDev->name, type);
    if (!netdev) {
        HDF_LOGE("%s: net_device is NULL", __func__);
        return -1;
    }

    if (appIe->ieLen > WLAN_WPS_IE_MAX_SIZE) {
        return -1;
    }

    rtnl_lock();
    ret = wl_cfg80211_set_wps_p2p_ie(netdev, appIe->ie, appIe->ieLen, type);
    rtnl_unlock();
    return ret;
}

void cfg80211_init_wdev(struct wireless_dev *wdev);

int hdf_start_p2p_device(void)
{
    int ret = HDF_SUCCESS;
    struct wiphy *wiphy = NULL;
    struct wireless_dev *wdev = NULL;
    struct net_device *netdev = get_krn_netdev(HDF_INF_WLAN0);

    if (start_p2p_completed == 1) {
        HDF_LOGE("%s:start p2p completed already", __func__);
        return 0;
    }

    // create wdev object for p2p-dev-wlan0 device, refer nl80211_new_interface()
    wiphy = get_linux_wiphy_ndev(netdev);
    if (wiphy == NULL) {
        HDF_LOGE("%s:get wlan0 wiphy failed", __func__);
        return HDF_FAILURE;
    }

    wdev = wl_cfg80211_ops.add_virtual_intf(wiphy, "p2p-dev-wlan0", NET_NAME_USER, NL80211_IFTYPE_P2P_DEVICE, NULL);
    if (wdev == NULL) {
        HDF_LOGE("%s:create wdev for p2p-dev-wlan0 %d failed", __func__, NL80211_IFTYPE_P2P_DEVICE);
        return HDF_FAILURE;
    }
    cfg80211_init_wdev(wdev);
    HDF_LOGI("%s:p2p-dev-wlan0 wdev->netdev=%p", __func__, wdev->netdev);

    g_hdf_infmap[HDF_INF_P2P0].wdev = wdev;  // free it for module released !!

    ret = wl_cfg80211_ops.start_p2p_device(wiphy, NULL);
    HDF_LOGI("call start_p2p_device ret = %d", ret);
    g_event_ifidx = HDF_INF_P2P0;
    start_p2p_completed = 1;
    
    return ret;
}

int32_t WalGetDriverFlag(struct NetDevice *netDev, WifiGetDrvFlags **params)
{
    struct wireless_dev *wdev = NULL;
    WifiGetDrvFlags *getDrvFlag = NULL;
    int iftype = 0;
    int ifidx = 0;

    HDF_LOGI("%s: primary netdev %s", __func__, netDev->name);
    if (netDev == NULL || params == NULL) {
        HDF_LOGE("%s:NULL ptr!", __func__);
        return -1;
    }
    wdev = (struct wireless_dev*)((netDev)->ieee80211Ptr);
    getDrvFlag = (WifiGetDrvFlags *)OsalMemCalloc(sizeof(WifiGetDrvFlags));
    if (wdev) {
        iftype = wdev->iftype;
    } else {
        ifidx = get_scan_ifidx(netDev->name);
        if (ifidx == HDF_INF_P2P0)
            iftype = NL80211_IFTYPE_P2P_DEVICE;
    }
        
    switch (iftype) {
        case NL80211_IFTYPE_P2P_CLIENT:
             /* fall-through */
        case NL80211_IFTYPE_P2P_GO:
            getDrvFlag->drvFlags = (unsigned int)(HISI_DRIVER_FLAGS_AP);
            g_event_ifidx = HDF_INF_P2P1;
            break;
        case NL80211_IFTYPE_P2P_DEVICE:
            getDrvFlag->drvFlags = (unsigned int)(HISI_DRIVER_FLAGS_P2P_DEDICATED_INTERFACE |
                                            HISI_DRIVER_FLAGS_P2P_CONCURRENT |
                                            HISI_DRIVER_FLAGS_P2P_CAPABLE);
            rtnl_lock();
            hdf_start_p2p_device();
            rtnl_unlock();
            break;
        default:
            getDrvFlag->drvFlags = 0;
    }

    *params = getDrvFlag;

    HDF_LOGI("%s: %s iftype=%d, drvflag=%lu", __func__, netDev->name, iftype, getDrvFlag->drvFlags);
    return HDF_SUCCESS;
}

static struct HdfMac80211P2POps g_bdh6_p2pOps = {
    .RemainOnChannel = WalRemainOnChannel,
    .CancelRemainOnChannel = WalCancelRemainOnChannel,
    .ProbeReqReport = WalProbeReqReport,
    .AddIf = WalAddIf,
    .RemoveIf = WalRemoveIf,
    .SetApWpsP2pIe = WalSetApWpsP2pIe,
    .GetDriverFlag = WalGetDriverFlag,
};

void BDH6Mac80211Init(struct HdfChipDriver *chipDriver)
{
    HDF_LOGI("%s: start...", __func__);

    if (chipDriver == NULL) {
        HDF_LOGE("%s: input is NULL", __func__);
        return;
    }
    
    chipDriver->ops = &g_bdh6_baseOps;
    chipDriver->staOps = &g_bdh6_staOps;
    chipDriver->apOps = &g_bdh6_apOps;
    chipDriver->p2pOps = &g_bdh6_p2pOps;
}