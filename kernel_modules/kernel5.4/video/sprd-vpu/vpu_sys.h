/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Unisoc QOGIRN6PRO VPU driver
 *
 * Copyright (C) 2019 Unisoc, Inc.
 */

#define DPU_VPU_SOC_QOS_BASE 0x30100000
#define VPU_SOC_QOS_BASE 0x30130000

struct vpu_qos_reg {
	unsigned int      offset;
	unsigned int      mask;
	unsigned int      value;
};

struct vpu_qos_reg vpu_mtx_qos[] = {
	{ 0x0100, 0x00000001, 0x00000001},
	{ 0x0104, 0xffffffff, 0x01889090},
	{ 0x0108, 0x3f3f3f3f, 0x10101010},
	{ 0x010C, 0x3f3fffff, 0x20100808},
	{ 0x0144, 0xffffffff, 0x00000000},
	{ 0x0148, 0x073fffff, 0x00000000},
	{ 0x014C, 0xffffffff, 0x0190014e},
	{ 0x0150, 0x073fffff, 0x04040800},
	{ 0x0140, 0x00000003, 0x00000000},
	{ 0x0160, 0x80000003, 0x00000003},
	{ 0x0164, 0x3fff3fff, 0x18881A88},
	{ 0x0168, 0x00000701, 0x00000001},
	{ 0x0180, 0x00000001, 0x00000001},
	{ 0x0184, 0xffffffff, 0x08848484},
	{ 0x0188, 0x3f3f3f3f, 0x04201008},
	{ 0x018C, 0x3f3fffff, 0x20080808},
	{ 0x01C0, 0x00000003, 0x00000000},
	{ 0x01C4, 0xffffffff, 0x00000000},
	{ 0x01C8, 0x073fffff, 0x00000000},
	{ 0x01CC, 0xffffffff, 0x00000000},
	{ 0x01D0, 0x073fffff, 0x00000000},
	{ 0x01E0, 0x80000003, 0x00000003},
	{ 0x01E4, 0x3fff3fff, 0x18881A88},
	{ 0x01E8, 0x00000701, 0x00000001},
	{ 0x0200, 0x00000001, 0x00000001},
	{ 0x0204, 0xffffffff, 0x08848484},
	{ 0x0208, 0x3f3f3f3f, 0x04201008},
	{ 0x020C, 0x3f3fffff, 0x20080808},
	{ 0x0240, 0x00000003, 0x00000000},
	{ 0x0244, 0xffffffff, 0x00000000},
	{ 0x0248, 0x073fffff, 0x00000000},
	{ 0x024C, 0xffffffff, 0x00000000},
	{ 0x0250, 0x073fffff, 0x00000000},
	{ 0x0260, 0x80000003, 0x00000003},
	{ 0x0264, 0x3fff3fff, 0x18881A88},
	{ 0x0268, 0x00000701, 0x00000001},
};

struct vpu_qos_reg dpu_vpu_mtx_qos[] = {
	{ 0x0040, 0x00010000, 0x00000000},
	{ 0x0040, 0x00010000, 0x00010000},
	{ 0x0044, 0x00010000, 0x00000000},
	{ 0x0044, 0x00010000, 0x00010000},
	{ 0x0048, 0x00010000, 0x00000000},
	{ 0x0048, 0x00010000, 0x00010000},
	{ 0x004C, 0x00010000, 0x00000000},
	{ 0x004C, 0x00010000, 0x00010000},
	{ 0x0050, 0x00010000, 0x00000000},
	{ 0x0050, 0x00010000, 0x00010000},
	{ 0x0030, 0x00010000, 0x00000000},
	{ 0x0030, 0x00010000, 0x00010000},
	{ 0x0034, 0x00010000, 0x00000000},
	{ 0x0034, 0x00010000, 0x00010000},
};
