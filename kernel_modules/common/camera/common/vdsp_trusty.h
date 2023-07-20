/*
 * Copyright (C) 2021-2022 UNISOC Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _VDSP_TRUSTY_H_
#define _VDSP_TRUSTY_H_


#define TA_RESP_BIT		1 <<  31


/*Todo for faceid sec funcation*/
#define CAM_FACEID_SEC
#define FACEID_VERSION 			1
#define GET_TXBUF_TIMEOUT	10
#define CA_READ_TIMEOUT		1000
#define CA_CONN_TIMEOUT 		1000

enum vdsp_ip_type {
	TA_CADENCE_VQ6 = 0,
	TA_IP_MAX,
};


enum vdsp_command {
	TA_FACEID_ENTER_SEC_MODE = 1,
	TA_FACEID_EXIT_SEC_MODE,

	TA_GET_CAM_STATUS,
};

struct vdsp_msg {
	uint32_t vdsp_type;
	uint32_t msg_cmd;
};

enum vdsp_cmdack {
	TA_CMD_DONE = 0,
	TA_CMD_ERR ,
};

enum vdsp_ta_staus {
	VDSP_NORMAL	= 0,
	VDSP_SECURITY,
};

struct ack_message {
	uint32_t cmd;
	enum vdsp_cmdack ack;
};

struct status_message {
	uint32_t cmd;
	enum vdsp_ta_staus status;
};
struct vdsp_ca_ctrl {
	int   chanel_state;
	bool con_init;
	bool cam_temode;
	struct mutex wlock;
	struct mutex rlock;
	struct tipc_chan *chan;
	wait_queue_head_t readq;
	struct list_head rx_msg_queue;
};
/*firewall*/
bool vdsp_ca_connect(void);
void vdsp_ca_disconnect(void);
bool vdsp_set_sec_mode(struct vdsp_msg *vdsp_msg);

/*sign data*/
#define KERNELBOOTCP_PORT "com.android.trusty.kernelbootcp"
#define KERNELBOOTCP_MAX_BUFFER_LENGTH 4096

/* Size of the footer.                 */
/* original definition in avb_footer.h */
#define AVB_FOOTER_SIZE    64

/* Size of  partition name .          */
#define PART_NAME_SIZE     32

typedef struct {
    uint64_t img_addr;  // the base address of image to verify
    uint32_t img_len;   // length of image
    uint32_t map_len;   // mapping length
#ifdef CONFIG_VBOOT_V2
    uint8_t  footer[AVB_FOOTER_SIZE];
    uint8_t  partition[PART_NAME_SIZE];
#endif
} KBC_IMAGE_S;

typedef struct {
  KBC_IMAGE_S faceid_fw;
  uint16_t    flag;      // not use
  uint16_t    is_packed; // is packed image
#ifdef CONFIG_VBOOT_V2
  uint32_t    packed_offset; // packed offset(for avb2.0 cp verify)
#endif
} KBC_LOAD_TABLE_V;

enum secureboot_command {
    KERNELBOOTCP_BIT                = 1,
    KERNELBOOTCP_REQ_SHIFT          = 1,

    KERNEL_BOOTCP_VERIFY_ALL        = (0 << KERNELBOOTCP_REQ_SHIFT),
    KERNEL_BOOTCP_UNLOCK_DDR        = (1 << KERNELBOOTCP_REQ_SHIFT),
    KERNEL_BOOTCP_VERIFY_VDSP       = (2 << KERNELBOOTCP_REQ_SHIFT),
    KERNEL_BOOTCP_UNLOCK_DDR_VDSP   = (3 << KERNELBOOTCP_REQ_SHIFT),
};

struct kernelbootcp_message {
    uint32_t cmd;
    uint8_t  payload[0];
};
struct bootcp_ca_ctrl {
	int   chanel_state;
	bool con_init;
	struct mutex wlock;
	struct mutex rlock;
	struct tipc_chan *chan;
	wait_queue_head_t readq;
	struct list_head rx_msg_queue;
};

bool trusty_kernelbootcp_connect(void);
void trusty_kernelbootcp_disconnect(void);
bool kernel_bootcp_unlock_ddr(KBC_LOAD_TABLE_V  *table);
bool kernel_bootcp_verify_vdsp(KBC_LOAD_TABLE_V  *table);



#endif
