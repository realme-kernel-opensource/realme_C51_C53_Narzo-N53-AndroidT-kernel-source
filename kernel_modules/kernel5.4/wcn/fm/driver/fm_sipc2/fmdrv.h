/*
 *  FM Driver for Connectivity chip of Spreadtrum
 *
 *  Common header for all FM driver sub-modules.
 *
 *  Copyright (C) 2015 spreadtrum
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */

#ifndef _FM_DRV_H
#define _FM_DRV_H
#include <linux/completion.h>
#include <linux/ioctl.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include "fmdrv_ops.h"

#define FM_DEV_NAME	"fm"
#define FM_NAME			"fm"
#define FM_DEVICE_NAME		"/dev/fm"
#define FM_WRITE_SIZE		(64)
#define FM_READ_SIZE		(128)
#define FM_HEADER_ERR		"FM_ERR: "
#define FM_HEADER		"FM_DRV: "
/* 1: enable RDS, 0:disable RDS */
#define FM_RDS_ENABLE 0x01

#define FM_SIPC_TX_CHANNEL    12
#define FM_SIPC_RX_CHANNEL    13
#define FM_TX_INOUT    1
#define FM_RX_INOUT     0
#define FM_TX_POOL_SIZE   5
#define FM_RX_POOL_SIZE   5
#define FM_SIPC_HEAD_LEN   0

#define reset_open_state 2

enum fm_bool {
	fm_false = 0,
	fm_true  = 1
};

/* scan sort algorithm */
enum {
	FM_SCAN_SORT_NON = 0,
	FM_SCAN_SORT_UP,
	FM_SCAN_SORT_DOWN,
	FM_SCAN_SORT_MAX
};

/* scan methods */
enum {
	/* select hardware scan, advantage: fast */
	FM_SCAN_SEL_HW = 0,
	/* select software scan, advantage: more accurate */
	FM_SCAN_SEL_SW,
	FM_SCAN_SEL_MAX
};

/* FM config for customer */
/* FM radio long antenna RSSI threshold(11.375dBuV) */
#define FMR_RSSI_TH_LONG    0x0301
/* FM radio short antenna RSSI threshold(-1dBuV) */
#define FMR_RSSI_TH_SHORT   0x02E0
/* FM radio Channel quality indicator threshold(0x0000~0x00FF) */
#define FMR_CQI_TH          0x00E9
/* FM radio seek space,1:100KHZ; 2:200KHZ */
#define FMR_SEEK_SPACE      1
/* FM radio scan max channel size */
#define FMR_SCAN_CH_SIZE    40
/* FM radio band, 1:87.5MHz~108.0MHz;*/
/* 2:76.0MHz~90.0MHz;*/
/* 3:76.0MHz~108.0MHz; 4:special */
#define FMR_BAND            1
/* FM radio special band low freq(Default 87.5MHz) */
#define FMR_BAND_FREQ_L     875
/* FM radio special band high freq(Default 108.0MHz) */
#define FMR_BAND_FREQ_H     1080
#define FM_SCAN_SORT_SELECT FM_SCAN_SORT_NON
#define FM_SCAN_SELECT      FM_SCAN_SEL_HW
/* soft-mute threshold when software scan, rang: 0~3, */
/* 0 means better audio quality but less channel */
#define FM_SCAN_SOFT_MUTE_GAIN_TH  3
/* rang: -102 ~ -72 */
#define FM_CHIP_DESE_RSSI_TH (-102)

/* FM config for engineer */
/* FM radio MR threshold */
#define FMR_MR_TH			0x01BD
/* scan thrshold register */
#define ADDR_SCAN_TH			0xE0
/* scan CQI register */
#define ADDR_CQI_TH			0xE1
/* 4 sec */
#define FM_DRV_TX_TIMEOUT		(1*HZ)
/* 20 sec */
#define FM_DRV_RX_SEEK_TIMEOUT		(20*HZ)

/* errno */
#define FM_SUCCESS      0
#define FM_FAILED       1
#define FM_EPARM        2
#define FM_BADSTATUS    3
#define FM_TUNE_FAILED  4
#define FM_SEEK_FAILED  5
#define FM_BUSY         6
#define FM_SCAN_FAILED  7


/* band */
#define FM_BAND_UNKNOWN 0
/* US/Europe band 87.5MHz ~ 108MHz (DEFAULT) */
#define FM_BAND_UE      1
/* Japan band 76MHz ~ 90MHz */
#define FM_BAND_JAPAN   2
/* Japan wideband 76MHZ ~ 108MHz */
#define FM_BAND_JAPANW  3
/* special band between 76MHZ and 108MHz */
#define FM_BAND_SPECIAL 4
#define FM_BAND_DEFAULT FM_BAND_UE

#define FM_UE_FREQ_MIN  875
#define FM_UE_FREQ_MAX  1080
#define FM_JP_FREQ_MIN  760
#define FM_JP_FREQ_MAX  1080
#define FM_FREQ_MIN  FMR_BAND_FREQ_L
#define FM_FREQ_MAX  FMR_BAND_FREQ_H
#define FM_RAIDO_BAND FM_BAND_UE

/* space */
#define FM_SPACE_UNKNOWN    0
#define FM_SPACE_100K       1
#define FM_SPACE_200K       2
#define FM_SPACE_50K        5
#define FM_SPACE_DEFAULT    FM_SPACE_100K

#define FM_SEEK_SPACE FMR_SEEK_SPACE

/* max scan channel num */
#define FM_MAX_CHL_SIZE FMR_SCAN_CH_SIZE
/* auto HiLo */
#define FM_AUTO_HILO_OFF    0
#define FM_AUTO_HILO_ON     1

/* seek direction */
#define FM_SEEK_UP          0
#define FM_SEEK_DOWN        1

#define FM_VERSION	"v0.0"

/* seek threshold */
#define FM_SEEKTH_LEVEL_DEFAULT 4

struct fm_tune_parm {
	uint8_t err;
	uint8_t band;
	uint8_t space;
	uint8_t hilo;
	uint16_t freq;
};

struct fm_seek_parm {
	uint8_t err;
	uint8_t band;
	uint8_t space;
	uint8_t hilo;
	uint8_t seekdir;
	uint8_t seekth;
	uint16_t freq;
};

/* Frequency offset, PDP_TH,PHP_TH, SNR_TH,RSS_THI */
/* Frequency_Offset_Th        [0x0000 0xFFFF]   EXPERIENCE VALUES:0x5dc  */
/* Pilot_Power_Th RANGES:   [0x0000 0x1FFF]   EXPERIENCE VALUES:0x190  */
/* Noise_Power_Th RANGES:  [0x0000 0x1FFF]   EXPERIENCE VALUES:0xB0   */
struct fm_seek_criteria_parm {
	unsigned char rssi_th;
	unsigned char snr_th;
	unsigned short freq_offset_th;
	unsigned short pilot_power_th;
	unsigned short noise_power_th;
} __packed;

struct fm_audio_threshold_parm {
	unsigned short hbound;
	unsigned short lbound;
	unsigned short power_th;
	unsigned char phyt;
	unsigned char snr_th;
} __packed;
/*__attribute__ ((packed));*/

struct fm_reg_ctl_parm {
	unsigned char err;
	unsigned int addr;
	unsigned int val;
	/*0:write, 1:read*/
	unsigned char rw_flag;
} __packed;

struct fm_scan_parm {
	uint8_t  err;
	uint8_t  band;
	uint8_t  space;
	uint8_t  hilo;
	uint16_t freq;
	uint16_t scantbl[16];
	uint16_t scantblsize;
};

struct fm_scan_all_parm {
	unsigned char band;/*87.5~108,76~*/
	unsigned char space;/*50 or 100KHz */
	unsigned char chanel_num;
	unsigned short freq[36]; /* OUT parameter*/
};

struct fm_ch_rssi {
	uint16_t freq;
	int rssi;
};

enum fm_scan_cmd_t {
	FM_SCAN_CMD_INIT = 0,
	FM_SCAN_CMD_START,
	FM_SCAN_CMD_GET_NUM,
	FM_SCAN_CMD_GET_CH,
	FM_SCAN_CMD_GET_RSSI,
	FM_SCAN_CMD_GET_CH_RSSI,
	FM_SCAN_CMD_MAX
};

struct fm_rssi_req {
	uint16_t num;
	uint16_t read_cnt;
	struct fm_ch_rssi cr[16*16];
};

struct fm_hw_info {
	int chip_id;
	int eco_ver;
	int rom_ver;
	int patch_ver;
	int reserve;
};

#define NEED_DEF_RDS 1

#if NEED_DEF_RDS
/* For RDS feature */
struct rdslag {
	uint8_t TP;
	uint8_t TA;
	uint8_t music;
	uint8_t stereo;
	uint8_t artificial_head;
	uint8_t compressed;
	uint8_t dynamic_pty;
	uint8_t text_ab;
	uint32_t flag_status;
};

struct ct_info {
	uint16_t month;
	uint16_t day;
	uint16_t year;
	uint16_t hour;
	uint16_t minute;
	uint8_t local_time_offset_signbit;
	uint8_t local_time_offset_half_hour;
};

struct  af_info {
	int16_t AF_NUM;
	int16_t AF[2][25];
	uint8_t addr_cnt;
	uint8_t ismethod_a;
	uint8_t isafnum_get;
};

struct  ps_info {
	uint8_t PS[4][8];
	uint8_t addr_cnt;
};

struct  rt_info {
	uint8_t textdata[4][64];
	uint8_t getlength;
	uint8_t isrtdisplay;
	uint8_t textlength;
	uint8_t istypea;
	uint8_t bufcnt;
	uint16_t addr_cnt;
};

struct rds_raw_data {
	/* indicate if the data changed or not */
	int dirty;
	/* the data len form chip */
	int len;
	uint8_t data[146];
};

struct rds_group_cnt {
	unsigned int total;
	/* RDS groupA counter */
	unsigned int groupA[16];
	/* RDS groupB counter */
	unsigned int groupB[16];
};

enum rds_group_cnt_opcode {
	RDS_GROUP_CNT_READ = 0,
	RDS_GROUP_CNT_WRITE,
	RDS_GROUP_CNT_RESET,
	RDS_GROUP_CNT_MAX
};

struct rds_group_cnt_req {
	int err;
	enum rds_group_cnt_opcode op;
	struct rds_group_cnt gc;
};

struct fm_rds_data {
	struct ct_info CT;
	struct rdslag RDSFLAG;
	uint16_t PI;
	uint8_t switch_tp;
	uint8_t PTY;
	struct  af_info af_data;
	struct  af_info afon_data;
	uint8_t radio_page_code;
	uint16_t program_item_number_code;
	uint8_t extend_country_code;
	uint16_t language_code;
	struct  ps_info ps_data;
	uint8_t ps_on[8];
	struct  rt_info rt_data;
	uint16_t event_status;
	struct rds_group_cnt gc;
};

/* valid Rds Flag for notify */
enum {
	/* Program is a traffic program */
	RDS_FLAG_IS_TP              = 0x0001,
	/* Program currently broadcasts a traffic ann. */
	RDS_FLAG_IS_TA              = 0x0002,
	/* Program currently broadcasts music */
	RDS_FLAG_IS_MUSIC           = 0x0004,
	/* Program is transmitted in stereo */
	RDS_FLAG_IS_STEREO          = 0x0008,
	/* Program is an artificial head recording */
	RDS_FLAG_IS_ARTIFICIAL_HEAD = 0x0010,
	/* Program content is compressed */
	RDS_FLAG_IS_COMPRESSED      = 0x0020,
	/* Program type can change */
	RDS_FLAG_IS_DYNAMIC_PTY     = 0x0040,
	/* If this flag changes state, a new radio text string begins */
	RDS_FLAG_TEXT_AB            = 0x0080
};

enum {
	/* One of the RDS flags has changed state */
	RDS_EVENT_FLAGS          = 0x0001,
	/* The program identification code has changed */
	RDS_EVENT_PI_CODE        = 0x0002,
	/* The program type code has changed */
	RDS_EVENT_PTY_CODE       = 0x0004,
	/* The program name has changed */
	RDS_EVENT_PROGRAMNAME    = 0x0008,
	/* A new UTC date/time is available */
	RDS_EVENT_UTCDATETIME    = 0x0010,
	/* A new local date/time is available */
	RDS_EVENT_LOCDATETIME    = 0x0020,
	/* A radio text string was completed */
	RDS_EVENT_LAST_RADIOTEXT = 0x0040,
	/* Current Channel RF signal strength too weak, need do AF switch */
	RDS_EVENT_AF             = 0x0080,
	/* An alternative frequency list is ready */
	RDS_EVENT_AF_LIST        = 0x0100,
	/* An alternative frequency list is ready */
	RDS_EVENT_AFON_LIST      = 0x0200,
	/* Other Network traffic announcement start */
	RDS_EVENT_TAON           = 0x0400,
	/* Other Network traffic announcement finished. */
	RDS_EVENT_TAON_OFF       = 0x0800,
	/* RDS Interrupt had arrived durint timer period */
	RDS_EVENT_RDS            = 0x2000,
	/* RDS Interrupt not arrived durint timer period */
	RDS_EVENT_NO_RDS         = 0x4000,
	/* Timer for RDS Bler Check. ---- BLER  block error rate */
	RDS_EVENT_RDS_TIMER      = 0x8000
};
#endif

enum {
	FM_I2S_ON = 0,
	FM_I2S_OFF,
	FM_I2S_STATE_ERR
};

enum {
	FM_I2S_MASTER = 0,
	FM_I2S_SLAVE,
	FM_I2S_MODE_ERR
};

enum {
	FM_I2S_32K = 0,
	FM_I2S_44K,
	FM_I2S_48K,
	FM_I2S_SR_ERR
};

struct fm_i2s_setting {
	int onoff;
	int mode;
	int sample;
};

enum {
	FM_RX = 0,
	FM_TX = 1
};

struct fm_i2s_info_t {
	/* 0:FM_I2S_ON, 1:FM_I2S_OFF,2:error */
	int status;
	/* 0:FM_I2S_MASTER, 1:FM_I2S_SLAVE,2:error */
	int mode;
	/* 0:FM_I2S_32K:32000, 1:FM_I2S_44K:44100,2:FM_I2S_48K:48000,3:error */
	int rate;
};

enum fm_audio_path_e {
	FM_AUD_ANALOG = 0,
	FM_AUD_I2S = 1,
	FM_AUD_MRGIF = 2,
	FM_AUD_ERR
};

enum fm_i2s_pad_sel_e {
	FM_I2S_PAD_CONN = 0,
	FM_I2S_PAD_IO = 1,
	FM_I2S_PAD_ERR
};

struct fm_audio_info_t {
	enum fm_audio_path_e aud_path;
	struct fm_i2s_info_t i2s_info;
	enum fm_i2s_pad_sel_e i2s_pad;
};

struct fm_cqi {
	int ch;
	int rssi;
	int reserve;
};

struct fm_cqi_req {
	uint16_t ch_num;
	int buf_size;
	char *cqi_buf;
};

struct  fm_desense_check_t {
	int freq;
	int rssi;
};

struct  fm_full_cqi_log_t {
	/* lower band, Eg, 7600 -> 76.0Mhz */
	uint16_t lower;
	/* upper band, Eg, 10800 -> 108.0Mhz */
	uint16_t upper;
	/* 0x1: 50KHz, 0x2: 100Khz, 0x4: 200Khz */
	int space;
	/* repeat times */
	int cycle;
};

struct fm_rx_data {
	unsigned int channel;
	struct mbuf_t *head;
	struct mbuf_t *tail;
	unsigned int num;
	struct list_head	entry;
};

struct fm_rds_handle {
	/* is RDS on or off */
	unsigned char rds_flag;
	wait_queue_head_t rx_queue;
	unsigned short new_data_flag;
	unsigned long rds_parse_start_time;
	unsigned long rds_parse_stop_time;
	/* get RT count */
	unsigned short get_rt_cnt;
};

struct fmdrv_ops {
	struct completion	completed;
	unsigned int		rcv_len;
	void			*read_buf;
	struct platform_device *pdev;
	struct mbuf_t   *tx_head;
	struct mbuf_t   *tx_tail;
	void			*tx_buf_p;
	void				*com_response;
	void				*seek_response;
	unsigned int		tx_len;
	unsigned char		write_buf[FM_WRITE_SIZE];
	unsigned char		com_respbuf[12];
	unsigned char		seek_respbuf[12];
	struct tasklet_struct rx_task;
	struct tasklet_struct tx_task;
	struct fm_rds_data rds_data;
	spinlock_t		rw_lock;
	struct mutex		mutex;
	struct list_head	rx_head;
	struct completion commontask_completion;
	struct completion seektask_completion;
	struct completion *response_completion;
	struct fm_rds_handle rds_han;
    struct fm_init_data	*pdata;
	/* fm_state: open: 1, close: 0 */
	bool	fm_state;
	/* headset_state: plugin: 0, plugout: 1 */
	bool	headset_state;
	int8_t  fm_invalid;
};

/* ********** ***********FM IOCTL define start ****************/
#define FM_IOC_MAGIC		0xf5
#define FM_IOCTL_POWERUP       _IOWR(FM_IOC_MAGIC, 0, struct fm_tune_parm*)
#define FM_IOCTL_POWERDOWN     _IOWR(FM_IOC_MAGIC, 1, int32_t*)
#define FM_IOCTL_TUNE          _IOWR(FM_IOC_MAGIC, 2, struct fm_tune_parm*)
#define FM_IOCTL_SEEK          _IOWR(FM_IOC_MAGIC, 3, struct fm_seek_parm*)
#define FM_IOCTL_SETVOL        _IOWR(FM_IOC_MAGIC, 4, uint32_t*)
#define FM_IOCTL_GETVOL        _IOWR(FM_IOC_MAGIC, 5, uint32_t*)
#define FM_IOCTL_MUTE          _IOWR(FM_IOC_MAGIC, 6, uint32_t*)
#define FM_IOCTL_GETRSSI       _IOWR(FM_IOC_MAGIC, 7, int32_t*)
#define FM_IOCTL_SCAN          _IOWR(FM_IOC_MAGIC, 8, struct fm_scan_parm*)
#define FM_IOCTL_STOP_SCAN     _IO(FM_IOC_MAGIC,   9)

/* IOCTL and struct for test */
#define FM_IOCTL_GETCHIPID     _IOWR(FM_IOC_MAGIC, 10, uint16_t*)
#define FM_IOCTL_EM_TEST       _IOWR(FM_IOC_MAGIC, 11, struct fm_em_parm*)

#define FM_IOCTL_GETMONOSTERO  _IOWR(FM_IOC_MAGIC, 13, uint16_t*)
#define FM_IOCTL_GETCURPAMD    _IOWR(FM_IOC_MAGIC, 14, uint16_t*)
#define FM_IOCTL_GETGOODBCNT   _IOWR(FM_IOC_MAGIC, 15, uint16_t*)
#define FM_IOCTL_GETBADBNT     _IOWR(FM_IOC_MAGIC, 16, uint16_t*)
#define FM_IOCTL_GETBLERRATIO  _IOWR(FM_IOC_MAGIC, 17, uint16_t*)

/* IOCTL for RDS */
#define FM_IOCTL_RDS_ONOFF     _IOWR(FM_IOC_MAGIC, 18, uint16_t*)
#define FM_IOCTL_RDS_SUPPORT   _IOWR(FM_IOC_MAGIC, 19, int32_t*)

#define FM_IOCTL_RDS_SIM_DATA  _IOWR(FM_IOC_MAGIC, 23, uint32_t*)
#define FM_IOCTL_IS_FM_POWERED_UP  _IOWR(FM_IOC_MAGIC, 24, uint32_t*)

/* IOCTL for FM over BT */
#define FM_IOCTL_OVER_BT_ENABLE  _IOWR(FM_IOC_MAGIC, 29, int32_t*)

/* IOCTL for FM ANTENNA SWITCH */
#define FM_IOCTL_ANA_SWITCH     _IOWR(FM_IOC_MAGIC, 30, int32_t*)
#define FM_IOCTL_GETCAPARRAY	_IOWR(FM_IOC_MAGIC, 31, int32_t*)
#define FM_IOCTL_ANA_SWITCH_INNER     _IOWR(FM_IOC_MAGIC, 32, int32_t*)


/* IOCTL for FM I2S Setting  */
#define FM_IOCTL_I2S_SETTING  _IOWR(FM_IOC_MAGIC, 33, struct fm_i2s_setting*)

#define FM_IOCTL_RDS_GROUPCNT   _IOWR(FM_IOC_MAGIC, 34, \
				struct rds_group_cnt_req*)
#define FM_IOCTL_RDS_GET_LOG    _IOWR(FM_IOC_MAGIC, 35, struct rds_raw_data*)

#define FM_IOCTL_SCAN_GETRSSI   _IOWR(FM_IOC_MAGIC, 36, struct fm_rssi_req*)
#define FM_IOCTL_SETMONOSTERO   _IOWR(FM_IOC_MAGIC, 37, int32_t)
#define FM_IOCTL_RDS_BC_RST     _IOWR(FM_IOC_MAGIC, 38, int32_t*)
#define FM_IOCTL_CQI_GET	_IOWR(FM_IOC_MAGIC, 39, struct fm_cqi_req*)
#define FM_IOCTL_GET_HW_INFO    _IOWR(FM_IOC_MAGIC, 40, struct fm_hw_info*)
#define FM_IOCTL_GET_I2S_INFO   _IOWR(FM_IOC_MAGIC, 41, struct fm_i2s_info_t*)
#define FM_IOCTL_IS_DESE_CHAN   _IOWR(FM_IOC_MAGIC, 42, int32_t*)
#define FM_IOCTL_TOP_RDWR	_IOWR(FM_IOC_MAGIC, 43, struct fm_top_rw_parm*)
#define FM_IOCTL_HOST_RDWR	_IOWR(FM_IOC_MAGIC, 44, struct fm_host_rw_parm*)

#define FM_IOCTL_PRE_SEARCH	_IOWR(FM_IOC_MAGIC, 45, int32_t)
#define FM_IOCTL_RESTORE_SEARCH _IOWR(FM_IOC_MAGIC, 46, int32_t)

#define FM_IOCTL_SET_SEARCH_THRESHOLD   _IOWR(FM_IOC_MAGIC, 47, \
		fm_search_threshold_t*)

#define FM_IOCTL_GET_AUDIO_INFO _IOWR(FM_IOC_MAGIC, 48, struct fm_audio_info_t*)

#define FM_IOCTL_SCAN_NEW       _IOWR(FM_IOC_MAGIC, 60, struct fm_scan_t*)
#define FM_IOCTL_SEEK_NEW       _IOWR(FM_IOC_MAGIC, 61, struct fm_seek_t*)
#define FM_IOCTL_TUNE_NEW       _IOWR(FM_IOC_MAGIC, 62, struct fm_tune_t*)

#define FM_IOCTL_SOFT_MUTE_TUNE _IOWR(FM_IOC_MAGIC, 63, \
	struct fm_softmute_tune_t*)
#define FM_IOCTL_DESENSE_CHECK   _IOWR(FM_IOC_MAGIC, 64, \
	struct fm_desense_check_t*)


/*IOCTL for SPRD SPECIAL */
/*audio mode:0:mono, 1:stereo; 2:blending*/
#define FM_IOCTL_SET_AUDIO_MODE       _IOWR(FM_IOC_MAGIC, 0x47, int32_t*)
#define FM_IOCTL_SET_REGION       _IOWR(FM_IOC_MAGIC, 0x48, int32_t*)
#define FM_IOCTL_SET_SCAN_STEP       _IOWR(FM_IOC_MAGIC, 0x49, int32_t*)
#define FM_IOCTL_CONFIG_DEEMPHASIS       _IOWR(FM_IOC_MAGIC, 0x4A, int32_t*)
#define FM_IOCTL_GET_AUDIO_MODE       _IOWR(FM_IOC_MAGIC, 0x4B, int32_t*)
#define FM_IOCTL_GET_CUR_BLER       _IOWR(FM_IOC_MAGIC, 0x4C, int32_t*)
#define FM_IOCTL_GET_SNR       _IOWR(FM_IOC_MAGIC, 0x4D, int32_t*)
#define FM_IOCTL_SOFTMUTE_ONOFF       _IOWR(FM_IOC_MAGIC, 0x4E, int32_t*)
/*Frequency offset, PDP_TH,PHP_TH, SNR_TH,RSS_THI*/
#define FM_IOCTL_SET_SEEK_CRITERIA       _IOWR(FM_IOC_MAGIC, 0x4F, \
			struct fm_seek_criteria_parm*)
/*softmute ,blending ,snr_th*/
#define FM_IOCTL_SET_AUDIO_THRESHOLD _IOWR(FM_IOC_MAGIC, 0x50, \
			struct fm_audio_threshold_parm*)
/*Frequency offset, PDP_TH,PHP_TH, SNR_TH,RSS_THI*/
#define FM_IOCTL_GET_SEEK_CRITERIA       _IOWR(FM_IOC_MAGIC, 0x51, \
			struct fm_seek_criteria_parm*)
/*softmute ,blending ,snr_th*/
#define FM_IOCTL_GET_AUDIO_THRESHOLD _IOWR(FM_IOC_MAGIC, 0x52, \
			struct fm_audio_threshold_parm*)
#define FM_IOCTL_RW_REG        _IOWR(FM_IOC_MAGIC, 0xC, struct fm_reg_ctl_parm*)
#define FM_IOCTL_AF_ONOFF     _IOWR(FM_IOC_MAGIC, 0x53, uint16_t*)

/* IOCTL for EM */
#define FM_IOCTL_FULL_CQI_LOG _IOWR(FM_IOC_MAGIC, 70, \
	struct fm_full_cqi_log_t *)

#define FM_IOCTL_DUMP_REG   _IO(FM_IOC_MAGIC, 0xFF)

/*********** ***********FM IOCTL define end ********************************/

#ifdef CONFIG_FM_SEEK_STEP_50KHZ
#define MAX_FM_FREQ		10800
#define MIN_FM_FREQ		8750
#else
#define MAX_FM_FREQ	        1080
#define MIN_FM_FREQ	        875
#endif

#define FM_CTL_STI_MODE_NORMAL	0x0
#define	FM_CTL_STI_MODE_SEEK    0x1
#define	FM_CTL_STI_MODE_TUNE    0x2

#endif
