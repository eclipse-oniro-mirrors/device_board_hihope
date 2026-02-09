/*
 * hdfinit_bdh.c
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

#include <uapi/linux/nl80211.h>
#include <securec.h>
#include <asm/byteorder.h>
#include <linux/kernel.h>

#include "hdf_wifi_product.h"
#include "wifi_mac80211_ops.h"
#include "hdf_wlan_utils.h"
#include "hdf_wl_interface.h"
#include "net_bdh_adpater.h"
#include "hdf_public_ap6275s.h"
#include "eapol.h"

#define HDF_LOG_TAG BDH6Driver
int hdf_cfgp2p_register_ndev(struct net_device *p2p_netdev, struct net_device *primary_netdev, struct wiphy *wiphy);
int dhd_module_init(void);
void dhd_module_exit(void);
struct bcm_cfg80211;
s32 wl_get_vif_macaddr(struct bcm_cfg80211 *cfg, u16 wl_iftype, u8 *mac_addr);

struct NetDeviceInterFace *wal_get_net_p2p_ops(void);
struct hdf_inf_map g_hdf_infmap[HDF_INF_MAX];

int g_hdf_ifidx = HDF_INF_WLAN0;
int g_event_ifidx = HDF_INF_WLAN0;
int g_scan_event_ifidx = HDF_INF_WLAN0;
int g_conn_event_ifidx = HDF_INF_WLAN0;
int g_mgmt_tx_event_ifidx = HDF_INF_P2P0;
int bdh6_reset_driver_flag = 0;

// BDH Wifi6 chip driver init
int32_t InitBDH6Chip(struct HdfWlanDevice *device)
{
    (void)device;
    HDF_LOGW("bdh6: call InitBDH6Chip");
    return HDF_SUCCESS;
}

int32_t DeinitBDH6Chip(struct HdfWlanDevice *device)
{
    int32_t ret = HDF_SUCCESS;
    (void)device;
    if (ret != 0) {
        HDF_LOGE("%s:Deinit failed!ret=%d", __func__, ret);
    }
    return ret;
}

static void HdfInfMapInit(void)
{
    int32_t i = 0;

    memset_s(g_hdf_infmap, sizeof(g_hdf_infmap), 0, sizeof(g_hdf_infmap));
    for (i = 0; i < HDF_INF_MAX; i ++) {
        INIT_WORK(&g_hdf_infmap[i].eapolEvent.eapol_report, eapol_report_handler);
        NetBufQueueInit(&g_hdf_infmap[i].eapolEvent.eapolQueue);
        g_hdf_infmap[i].eapolEvent.idx = i;
    }
    g_hdf_ifidx = HDF_INF_WLAN0; // master interface
}

#define WIFI_IMAGE_FILE_NAME "/vendor/etc/firmware/fw_bcm43752a2_ag.bin"
#define CK_WF_IMG_MAX_CNT 2
#define  CK_WF_IMG_DELAY_MS 500
int32_t CheckWiFiImageFile(const char *filename)
{
    struct file *fp = NULL;
    int32_t icnt = 0;

    while (icnt ++ <= CK_WF_IMG_MAX_CNT) {
        fp = filp_open(filename, O_RDONLY, 0);
        if (IS_ERR(fp)) {
            HDF_LOGE("%s:open %s failed, cnt=%d, error=%d", __func__, filename, icnt, PTR_ERR(fp));
            fp = NULL;
            OsalMSleep(CK_WF_IMG_DELAY_MS);
        } else {
            filp_close(fp, NULL);
            fp = NULL;
            HDF_LOGE("%s:open %s success, cnt=%d", __func__, filename, icnt);
            return 0;
        }
    }
    
    HDF_LOGE("%s:open %s failed, cnt=%d reach to max times", __func__, filename, icnt);
    return -1;
}

static int32_t InitPrimaryInterface(struct NetDevice *netDevice, struct net_device *netdev)
{
    int32_t ret = 0;
    int32_t private_data_size = 0;
    struct HdfWifiNetDeviceData *data = NULL;
    // Init primary interface wlan0

    data = GetPlatformData(netDevice);
    if (data == NULL) {
        HDF_LOGE("%s:netdevice data null!", __func__);
        return HDF_FAILURE;
    }

    hdf_bdh6_netdev_init(netDevice);
    netDevice->classDriverPriv = data;
    private_data_size = get_dhd_priv_data_size(); // create bdh6 private object
    netDevice->mlPriv = kzalloc(private_data_size, GFP_KERNEL);
    if (netDevice->mlPriv == NULL) {
        HDF_LOGE("%s:kzalloc mlPriv failed", __func__);
        return HDF_FAILURE;
    }

    set_krn_netdev(netDevice, netdev, g_hdf_ifidx);
    dhd_module_init();
    return HDF_SUCCESS;
}

static int32_t InitP2pInterface(struct NetDevice *netDevice, struct net_device *netdev)
{
    int32_t ret = 0;
    struct wiphy *wiphy = NULL;
    struct net_device *p2p_netdev = NULL;
    struct NetDevice *p2p_hnetdev = NULL;
    struct bcm_cfg80211 *cfg = NULL;
    WifiIfAdd ifAdd;

    memset_s(&ifAdd, sizeof(ifAdd), 0, sizeof(ifAdd));
    if (snprintf_s(ifAdd.ifName, IFNAMSIZ, IFNAMSIZ - 1, "p2p%d", 0) < 0) {
        HDF_LOGE("%s:format ifName failed!", __func__);
        return HDF_FAILURE;
    }

    ifAdd.type = NL80211_IFTYPE_P2P_DEVICE;
    // Init interface p2p0
    ret = P2pInitNetdev(netDevice, &ifAdd, sizeof(void *), HDF_INF_P2P0);
    if (ret != 0) {
        HDF_LOGE("%s:P2pInitNetdev %s failed", __func__, ifAdd.ifName);
        return HDF_FAILURE;
    }
    wiphy = get_linux_wiphy_ndev(netdev);
    if (wiphy == NULL) {
        HDF_LOGE("%s:get wlan0 wiphy failed", __func__);
        return HDF_FAILURE;
    }
    p2p_hnetdev = get_hdf_netdev(g_hdf_ifidx);
    p2p_netdev = get_krn_netdev(g_hdf_ifidx);
    p2p_netdev->ieee80211_ptr = NULL;
    p2p_hnetdev->ieee80211Ptr = p2p_netdev->ieee80211_ptr;
    cfg = wiphy_priv(wiphy);  // update mac from wdev address
    wl_get_vif_macaddr(cfg, 7, p2p_hnetdev->macAddr);  // WL_IF_TYPE_P2P_DISC = 7
    memcpy_s(p2p_netdev->dev_addr, p2p_netdev->addr_len, p2p_hnetdev->macAddr, MAC_ADDR_SIZE);
    p2p_hnetdev->netDeviceIf = wal_get_net_p2p_ops();  // reset netdev_ops
    hdf_cfgp2p_register_ndev(p2p_netdev, netdev, wiphy);
    ret = NetDeviceAdd(p2p_hnetdev);  // Call linux register_netdev()
    HDF_LOGI("NetDeviceAdd %s ret = %d", p2p_hnetdev->name, ret);
    return ret;
}

int32_t BDH6Init(struct HdfChipDriver *chipDriver, struct NetDevice *netDevice)
{
    int32_t ret = 0;
    struct net_device *netdev = NULL;
    struct net_device *p2p_netdev = NULL;
    struct NetDevice *p2p_hnetdev = NULL;
    
    (void)chipDriver;
    HDF_LOGI("bdh6: call BDH6Init");
    HdfInfMapInit();

    // Init primary interface wlan0
    if (netDevice == NULL) {
        HDF_LOGE("%s netdevice is null!", __func__);
        return HDF_FAILURE;
    }

    netdev = GetLinuxInfByNetDevice(netDevice);
    if (netdev == NULL) {
        HDF_LOGE("%s net_device is null!", __func__);
        return HDF_FAILURE;
    }

    ret = InitPrimaryInterface(netDevice, netdev);
    if (ret != HDF_SUCCESS) {
        return HDF_FAILURE;
    }

    ret = InitP2pInterface(netDevice, netdev);
    if (ret != HDF_SUCCESS) {
        return HDF_FAILURE;
    }
    p2p_hnetdev = get_hdf_netdev(g_hdf_ifidx);
    p2p_netdev = get_krn_netdev(g_hdf_ifidx);

    if (bdh6_reset_driver_flag) {
        p2p_hnetdev->netDeviceIf->open(p2p_hnetdev);
        rtnl_lock();
        dev_open(netdev, NULL);
        rtnl_unlock();
        rtnl_lock();
        dev_open(p2p_netdev, NULL);
        rtnl_unlock();
        rtnl_lock();
        if (start_p2p_completed) {
            start_p2p_completed = 0;
            hdf_start_p2p_device();
        }
        rtnl_unlock();
        bdh6_reset_driver_flag = 0;
        HDF_LOGI("%s: reset driver ok", __func__);
    }
    return HDF_SUCCESS;
}

int32_t BDH6Deinit(struct HdfChipDriver *chipDriver, struct NetDevice *netDevice)
{
    // free p2p0
    int32_t i = 0;
    struct NetDevice *p2p_hnetdev = get_hdf_netdev(HDF_INF_P2P0);
    (void)chipDriver;
    kfree(p2p_hnetdev->mlPriv);
    p2p_hnetdev->mlPriv = NULL;
    DestroyEapolData(p2p_hnetdev);
    if (NetDeviceDelete(p2p_hnetdev) != 0) {
        return HDF_FAILURE;
    }
    if (NetDeviceDeInit(p2p_hnetdev) != 0) {
        return HDF_FAILURE;
    }
	
    hdf_bdh6_netdev_stop(netDevice);
    dhd_module_exit();

    // free primary wlan0
    kfree(netDevice->mlPriv);
    netDevice->mlPriv = NULL;
    DestroyEapolData(netDevice);

    for (i = 0; i < HDF_INF_MAX; i ++) {
        cancel_work_sync(&g_hdf_infmap[i].eapolEvent.eapol_report);
        NetBufQueueClear(&g_hdf_infmap[i].eapolEvent.eapolQueue);
    }
    bdh6_reset_driver_flag = 1;
    HDF_LOGI("%s: ok", __func__);
    
    return HDF_SUCCESS;
}

