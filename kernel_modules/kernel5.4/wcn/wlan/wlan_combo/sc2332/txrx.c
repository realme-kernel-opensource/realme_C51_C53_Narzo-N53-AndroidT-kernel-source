/*
* SPDX-FileCopyrightText: 2021-2022 Unisoc (Shanghai) Technologies Co., Ltd
* SPDX-License-Identifier: GPL-2.0
*
* Copyright 2021-2022 Unisoc (Shanghai) Technologies Co., Ltd
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of version 2 of the GNU General Public License
* as published by the Free Software Foundation.
*/

#include <linux/skbuff.h>

#include "cmdevt.h"
#include "common/cfg80211.h"
#include "common/chip_ops.h"
#include "common/common.h"
#include "common/hif.h"
#include "common/iface.h"
#include "qos.h"
#include "txrx.h"
#include "wapi.h"

static void tx_list_flush(struct sprd_msg_list *list)
{
	struct sprd_msg *msg;

	while ((msg = sprd_peek_msg(list))) {
		if (msg->skb) {
			dev_kfree_skb(msg->skb);
			msg->skb = NULL;
		}
		sprd_dequeue_msg(msg, list);
		continue;
	}
}

static int rx_normal_data_process(struct sprd_vif *vif,
				  unsigned char *pdata, unsigned short len)
{
	struct sk_buff *skb;
	struct net_device *ndev;

	skb = dev_alloc_skb(len + NET_IP_ALIGN);
	if (!skb)
		return -ENOMEM;
	sc2332_tcp_ack_filter_rx(pdata);
	ndev = vif->ndev;
	skb_reserve(skb, NET_IP_ALIGN);
	/* for copy align */
	skb->data[0] = pdata[0];
	skb->data[1] = pdata[1];
	memcpy(skb->data + NET_IP_ALIGN,
	       pdata + NET_IP_ALIGN, len - NET_IP_ALIGN);
	skb_put(skb, len);
	sprd_netif_rx(ndev, skb);

	return 0;
}

static int rx_route_data_process(struct sprd_vif *vif,
				 unsigned char *pdata, unsigned short len)
{
	struct sk_buff *skb;
	struct sprd_msg *msg;

	msg = sprd_chip_get_msg(&vif->priv->chip, SPRD_TYPE_DATA, vif->mode);
	if (!msg) {
		printk_ratelimited("%s no msg\n", __func__);
		return len;
	}

	skb = dev_alloc_skb(len + vif->ndev->needed_headroom);
	if (!skb) {
		sprd_chip_free_msg(&vif->priv->chip, msg);
		printk_ratelimited("%s allock skb erro\n", __func__);
		return len;
	}
	skb_reserve(skb, vif->ndev->needed_headroom);
	/* for copy align */
	skb->data[0] = pdata[0];
	skb->data[1] = pdata[1];
	memcpy(skb->data + NET_IP_ALIGN,
	       pdata + NET_IP_ALIGN, len - NET_IP_ALIGN);
	skb_put(skb, len);

	sprd_send_data(vif->priv, vif, msg, skb,
		       SPRD_DATA_TYPE_ROUTE, SPRD_SEND_DATA_OFFSET, false);

	return len;
}

static int rx_wapi_data_process(struct sprd_vif *vif,
				unsigned char *pdata, unsigned short len)
{
	int decryp_data_len = 0;
	struct ieee80211_hdr_3addr *addr;
	struct sk_buff *skb;
	struct net_device *ndev;
	u8 snap_header[6] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};

	ndev = vif->ndev;
	addr = (struct ieee80211_hdr_3addr *)pdata;
	skb = dev_alloc_skb(len + NET_IP_ALIGN);
	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, NET_IP_ALIGN);
	if (len <= 24) {
		pr_err("%s data len is invalid\n", __func__);
		return -EINVAL;
	}

	decryp_data_len = sc2332_wapi_dec(vif, (unsigned char *)addr,
					  24, (len - 24), (skb->data + 12));
	if (!decryp_data_len) {
	        dev_kfree_skb(skb);
	        return -EINVAL;
	}

	if (!memcmp((skb->data + 12), snap_header, sizeof(snap_header))) {
	        skb_reserve(skb, 6);
	        memcpy(skb->data, addr->addr1, ETH_ALEN);
	        memcpy(skb->data + ETH_ALEN, addr->addr2, ETH_ALEN);
	        skb_put(skb, (decryp_data_len + 6));
	} else {
	        /* copy eth header */
	        memcpy(skb->data, addr->addr3, ETH_ALEN);
	        memcpy(skb->data + ETH_ALEN, addr->addr2, ETH_ALEN);
	        skb_put(skb, (decryp_data_len + 12));
	}

	sprd_netif_rx(ndev, skb);
	return 0;
}

static void sc2332_tx_qos_flush(struct sprd_qos_t *qos)
{
	unsigned long flags = 0;
	int icnt;
	struct sprd_msg *msg = NULL;
	struct qos_list *list;

	for (icnt = 0; icnt < SPRD_QOS_NUM; icnt++) {
		list = &qos->list[icnt];
		while (1) {
			spin_lock_irqsave(&qos->txlist->busylock, flags);
			msg = list->head;
			if (msg) {
				list->head = msg->next;
				msg->next = NULL;
				spin_unlock_irqrestore(&qos->txlist->busylock, flags);

				if (msg->skb) {
					dev_kfree_skb(msg->skb);
					msg->skb = NULL;
				}
				sc2332_qos_update(qos, msg, &msg->list);
				sprd_dequeue_msg(msg, qos->txlist);
			} else {
				spin_unlock_irqrestore(&qos->txlist->busylock, flags);
				break;
			}
		}
		pr_info("%s(%d) %d/%d.\n", __func__, icnt, qos->num[icnt], qos->txnum);
	}
}

struct sprd_msg *sc2332_tx_get_msg(struct sprd_chip *chip,
				   enum sprd_head_type type,
				   enum sprd_mode mode)
{
	struct sprd_msg *msg;
	struct sprd_msg_list *list;
	struct sprd_priv *priv = chip->priv;
	struct sprd_hif *hif = &priv->hif;

	if (unlikely(hif->exit))
		return NULL;
	if (type == SPRD_TYPE_DATA) {
		if (mode <= SPRD_MODE_AP)
			list = &hif->tx_list1;
		else
			list = &hif->tx_list2;
	} else {
		list = &hif->tx_list0;
	}
	msg = sprd_alloc_msg(list);
	if (msg) {
		msg->type = sc2332_convert_msg_type(type);
		msg->msglist = list;
		msg->mode = mode;
		return msg;
	}

	if (type == SPRD_TYPE_DATA) {
		hif->net_stop_cnt++;
		sprd_net_flowcontrl(hif->priv, mode, false);
		atomic_set(&list->flow, 1);
	}
	printk_ratelimited("%s no more msg for %s\n",
			   __func__, type == SPRD_TYPE_DATA ? "data" : "cmd");

	return NULL;
}

void sc2332_tx_free_msg(struct sprd_chip *chip, struct sprd_msg *msg)
{
	sprd_free_msg(msg, msg->msglist);
}

int sc2332_tx(struct sprd_chip *chip, struct sprd_msg *msg)
{
	u16 len;
	unsigned char *info;
	struct sprd_priv *priv = chip->priv;
	struct sprd_hif *hif = &priv->hif;

	if (msg->msglist == &hif->tx_list0) {
		info = "cmd";
		msg->timeout = jiffies + hif->cmd_timeout;
	} else {
		info = "data";
		msg->timeout = jiffies + hif->data_timeout;
	}

	len = sc2332_max_tx_len(hif, msg);
	if (msg->len > len) {
		dev_err(&hif->pdev->dev,
			"%s err:%s too long:%d > %d,drop it\n",
			__func__, info, msg->len, len);
		WARN_ON(1);
		if (msg->skb) {
			dev_kfree_skb(msg->skb);
			msg->skb = NULL;
		}
		sprd_free_msg(msg, msg->msglist);
		return 0;
	}

	sprd_queue_msg(msg, msg->msglist);
	sc2332_tx_wakeup(hif);
	queue_work(hif->tx_queue, &hif->tx_work);

	return 0;
}

int sc2332_tx_force_exit(struct sprd_chip *chip)
{
	struct sprd_priv *priv = chip->priv;
	struct sprd_hif *hif = &priv->hif;

	hif->exit = 1;
	wake_up_all(&hif->waitq);
	return 0;
}

int sc2332_tx_is_exit(struct sprd_chip *chip)
{
	struct sprd_priv *priv = chip->priv;
	struct sprd_hif *hif = &priv->hif;

	return hif->exit;
}

void sc2332_tx_drop_tcp_msg(struct sprd_chip *chip, struct sprd_msg *msg)
{
	enum sprd_mode mode;
	struct sprd_msg_list *list;
	struct sprd_priv *priv = chip->priv;
	struct sprd_hif *hif = &priv->hif;

	if (msg->skb) {
		dev_kfree_skb(msg->skb);
		msg->skb = NULL;
	}
	mode = msg->mode;
	list = msg->msglist;
	sprd_free_msg(msg, list);
	sc2332_wake_queue(hif, list, mode);
}

void sc2332_tx_set_qos(struct sprd_chip *chip, enum sprd_mode mode, int enable)
{
	struct sprd_qos_t *qos;
	struct sprd_priv *priv = chip->priv;
	struct sprd_hif *hif = &priv->hif;

	if (mode <= SPRD_MODE_AP)
		qos = &hif->qos1;
	else
		qos = &hif->qos2;
	dev_info(&hif->pdev->dev, "%s set qos:%d\n", __func__, enable);
	qos->enable = enable ? 1 : 0;
	qos->change = 1;
}

void sc2332_flush_all_txlist(struct sprd_hif *hif)
{
	tx_list_flush(&hif->tx_list0);
	sc2332_tx_qos_flush(&hif->qos1);
	tx_list_flush(&hif->tx_list1);
	sc2332_tx_qos_flush(&hif->qos2);
	tx_list_flush(&hif->tx_list2);
}

void sc2332_keep_wakeup(struct sprd_hif *hif)
{
	if (time_after(jiffies, hif->wake_last_time)) {
		hif->wake_last_time = jiffies + hif->wake_pre_timeout;
		__pm_wakeup_event(hif->keep_wake, hif->wake_timeout);
	}
}

unsigned short sc2332_rx_data_process(struct sprd_priv *priv,
				      unsigned char *msg, unsigned int msg_len)
{
	unsigned char mode, data_type;
	unsigned short len, plen;
	unsigned char *data;
	struct sprd_data_hdr *hdr;
	struct sprd_vif *vif;

	hdr = (struct sprd_data_hdr *)msg;
	mode = hdr->common.mode;
	data_type = SPRD_GET_DATA_TYPE(hdr->info1);
	if (mode == SPRD_MODE_NONE || mode > SPRD_MODE_MAX ||
	    data_type > SPRD_DATA_TYPE_MAX) {
		pr_err("%s [mode %d]RX err[type %d]\n", __func__, mode,
		       data_type);
		return 0;
	}

	data = (unsigned char *)msg;
	data += sizeof(*hdr) + (hdr->info1 & SPRD_DATA_OFFSET_MASK);
	plen = SPRD_GET_LE16(hdr->plen);
	if (plen > msg_len ||
	    plen < (sizeof(*hdr) + (hdr->info1 & SPRD_DATA_OFFSET_MASK))) {
		pr_err("%s plen is invalid!\n", __func__);
		return plen;
	}

	if (!priv) {
		pr_err("%s sdio->priv not init.\n", __func__);
		return plen;
	}

	vif = sprd_mode_to_vif(priv, mode);
	if (!vif) {
		pr_err("%s cant't get vif %d\n", __func__, mode);
		return plen;
	}

	len = plen - sizeof(*hdr) - (hdr->info1 & SPRD_DATA_OFFSET_MASK);
	switch (data_type) {
	case SPRD_DATA_TYPE_NORMAL:
		rx_normal_data_process(vif, data, len);
		break;
	case SPRD_DATA_TYPE_MGMT:
		sc2332_report_frame_evt(vif, data, len, true);
		break;
	case SPRD_DATA_TYPE_ROUTE:
		rx_route_data_process(vif, data, len);
		break;
	case SPRD_DATA_TYPE_WAPI:
		pr_debug("%s SPRD_DATA_TYPE_WAPI\n", __func__);
		rx_wapi_data_process(vif, data, len);
		break;
	default:
		break;
	}
	sprd_put_vif(vif);

	return plen;
}

/* if err, the caller judge the skb if need free,
 * here just free the msg buf to the freelist
 * if wapi free skb here
 */
int sc2332_send_data(struct sprd_vif *vif, struct sprd_msg *msg,
		     struct sk_buff *skb, u8 type, u8 offset, bool flag)
{
	int ret;
	unsigned char *buf;
	struct sprd_data_hdr *hdr;

	if (is_wapi(vif, skb->data)) {
		if (sc2332_rebuild_wapi_skb(vif, &skb))
			return -ENOMEM;
		type = SPRD_DATA_TYPE_WAPI;
		flag = false;
		pr_debug("%s TX data : SPRD_DATA_TYPE_WAPI\n", __func__);
	}

	buf = skb->data;
	skb_push(skb, sizeof(*hdr) + offset +
		 sprd_hif_reserve_len(&vif->priv->hif));
	hdr = (struct sprd_data_hdr *)skb->data;
	memset(hdr, 0, sizeof(*hdr));
	hdr->common.type = SPRD_TYPE_DATA;
	hdr->common.mode = vif->mode;
	hdr->info1 = type | (offset & SPRD_DATA_OFFSET_MASK);
	hdr->plen = cpu_to_le16(skb->len);

	sprd_fill_msg(msg, skb, skb->data, skb->len);

	if (flag) {
		msg->index = sc2332_qos_map(buf);

		if (sc2332_tcp_ack_filter_send(msg, buf))
			return 0;
	} else {
		msg->index = QOS_OTHER;
	}

	ret = sprd_chip_tx(&vif->priv->chip, msg);
	if (ret)
		pr_err("%s TX data Err: %d\n", __func__, ret);

	return ret;
}

int sc2332_send_data_offset(void)
{
	return SPRD_SEND_DATA_OFFSET;
}
