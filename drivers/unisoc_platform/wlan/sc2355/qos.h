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

#ifndef __QOS_H__
#define __QOS_H__

#include <linux/skbuff.h>

#include "common/cfg80211.h"
#include "common/msg.h"
#include "common/qos.h"

#define QOS_MAP_MAX_DSCP_EXCEPTION 21

#define NUM_AC 4
#define NUM_TID 16
#define WMMAC_EDCA_TIMEOUT_MS		1000

#define WLAN_EID_VENDOR_SPECIFIC 221
/* Microsoft (also used in Wi-Fi specs)* 00:50:F2 */
#define OUI_MICROSOFT 0x0050f2
#define WMM_OUI_TYPE 2
#define WMM_AC_ACM 0x10

#define INCR_RING_BUFF_INDX(indx, max_num) \
	((((indx) + 1) < (max_num)) ? ((indx) + 1) : (0))

#define ETHER_ADDR_LEN 6

/* 11u QoS map set */
#define DOT11_MNG_QOS_MAP_ID 110
/* DSCP ranges fixed with 8 entries */
#define QOS_MAP_FIXED_LENGTH	(8 * 2)
/* header length */
#define TLV_HDR_LEN 2

/* user priority */
#define VLAN_PRI_SHIFT	13
/* 3 bits of priority */
#define VLAN_PRI_MASK	7
/* VLAN ethertype/Tag Protocol ID */
#define VLAN_TPID	0x8100

/* IPV4 and IPV6 common */
#define ETHER_TYPE_IP	0x0800
/* IPv6 */
#define ETHER_TYPE_IPV6 0x86dd
/* offset to version field */
#define IP_VER_OFFSET	0x0
/* version mask */
#define IP_VER_MASK	0xf0
/* version shift */
#define IP_VER_SHIFT	4
/* version number for IPV4 */
#define IP_VER_4	4
/* version number for IPV6 */
#define IP_VER_6	6
 /* type of service offset */
#define IPV4_TOS_OFFSET            1
/* DiffServ codepoint shift */
#define IPV4_TOS_DSCP_SHIFT	2
#define IPV4_TOS(ipv4_body)\
	(((unsigned char *)(ipv4_body))[IPV4_TOS_OFFSET])
/* Historical precedence shift */
#define IPV4_TOS_PREC_SHIFT 5
/* 802.1Q */
#define ETHER_TYPE_8021Q 0x8100

/* IPV6 field decodes */
#define IPV6_TRAFFIC_CLASS(ipv6_body) \
	(((((unsigned char *)(ipv6_body))[0] & 0x0f) << 4) | \
	((((unsigned char *)(ipv6_body))[1] & 0xf0) >> 4))

#define IP_VER(ip_body) \
	((((unsigned char *)(ip_body))[IP_VER_OFFSET] & IP_VER_MASK) >> \
	IP_VER_SHIFT)

/* IPV4 TOS or IPV6 Traffic Classifier or 0 */
#define IP_TOS46(ip_body) \
	(IP_VER(ip_body) == IP_VER_4 ? IPV4_TOS(ip_body) : \
	IP_VER(ip_body) == IP_VER_6 ? IPV6_TRAFFIC_CLASS(ip_body) : 0)

#define PKT_SET_PRIO(skb, x) (((struct sk_buff *)(skb))->priority = (x))

#define VI_TOTAL_QUOTA 1500
#define BE_TOTAL_QUOTA 200
#define BK_TOTAL_QUOTA 200

enum qos_head_type_t {
	SPRD_AC_VO,
	SPRD_AC_VI,
	SPRD_AC_BE,
	SPRD_AC_BK,
	SPRD_AC_MAX,
};

struct qos_tx_t {
	int ac_index;
	unsigned char lut_id;
	atomic_t mode_list_num;
	struct qos_list q_list[SPRD_AC_MAX];
};

enum qos_ip_pkt_prio_t {
	prio_0 = 0,		/* Mapped to AC_BE_Q */
	prio_1 = 1,		/* Mapped to AC_BK_Q */
	prio_4 = 4,		/* Mapped to AC_VI_Q */
	prio_6 = 6,		/* Mapped to AC_VO_Q */
};

struct qos_capab_info {
	unsigned char id;
	unsigned char len;
	unsigned char qos_info[1];
};

struct qos_dscp_range {
	u8 low;
	u8 high;
};

struct qos_dscp_exception {
	u8 dscp;
	u8 up;
};

struct qos_map_range {
	u8 low;
	u8 high;
	u8 up;
};

struct qos_map_set {
	struct qos_dscp_exception qos_exceptions[QOS_MAP_MAX_DSCP_EXCEPTION];
	struct qos_map_range qos_ranges[8];
};

enum qos_edca_ac_t {
	AC_BK = 0,
	AC_BE = 1,
	AC_VI = 2,
	AC_VO = 3,
};

struct qos_wmm_ac_ts_t {
	bool exist;
	u8 ac;
	u8 up;
	u8 direction;
	u16 admitted_time;
};

struct qos_ether_header {
	unsigned char ether_dhost[ETHER_ADDR_LEN];
	unsigned char ether_shost[ETHER_ADDR_LEN];
	unsigned short ether_type;

} __packed;

struct qos_ethervlan_header {
	unsigned char ether_dhost[ETHER_ADDR_LEN];
	unsigned char ether_shost[ETHER_ADDR_LEN];
	/* 0x8100 */
	unsigned short vlan_type;
	/* priority, cfi and vid */
	unsigned short vlan_tag;
	unsigned short ether_type;
};

static inline u8 qos_index_2_tid(unsigned int qos_index)
{
	unsigned char tid = 0;

	switch (qos_index) {
	case SPRD_AC_VO:
		tid = 6;
		break;
	case SPRD_AC_VI:
		tid = 4;
		break;
	case SPRD_AC_BK:
		tid = 1;
		break;
	default:
		tid = 0;
		break;
	}
	return tid;
}

extern struct qos_map_set qos_map;
void sc2355_qos_init(struct qos_tx_t *qos);
int sc2355_qos_get_list_num(struct list_head *list);
unsigned int sc2355_qos_tid_map_to_index(unsigned char tid);
unsigned int sc2355_qos_get_tid_index(void *skb, int data_offset,
				      unsigned char *tid, unsigned char *tos);
unsigned int sc2355_qos_map_priority_to_edca_ac(int priority);
void sc2355_qos_update_wmmac_ts_info(u8 tsid, u8 up, u8 ac, bool status,
				     u16 admitted_time);
int sc2355_sync_wmm_param(struct sprd_priv *priv,
			  struct sprd_connect_info *conn_info);
void sc2355_qos_remove_wmmac_ts_info(u8 tsid);
void sc2355_qos_update_admitted_time(struct sprd_priv *priv, u8 tsid,
				     u16 medium_time, bool increase);
u16 sc2355_qos_get_wmmac_admitted_time(u8 tsid);
void qos_update_wmmac_edcaftime_timeout(struct timer_list *t);
void update_wmmac_vo_timeout(unsigned long data);
void update_wmmac_vi_timeout(unsigned long data);
unsigned int sc2355_qos_change_priority_if(struct sprd_priv *priv,
					   unsigned char *tid,
					   unsigned char *tos, u16 len);
void sc2355_qos_init_default_map(void);
void sc2355_qos_enable(int flag);
void sc2355_qos_wmm_ac_init(struct sprd_priv *priv);
void sc2355_qos_reset_wmmac_parameters(struct sprd_priv *priv);
void sc2355_qos_reset_wmmac_ts_info(void);
#endif
