#ifndef __SPRD_RTT_H__
#define __SPRD_RTT_H__

#include "vendor.h"

/* FTM/indoor location subcommands */
enum sprd_ftm_vendor_subcmds {
	SPRD_NL80211_VENDOR_SUBCMD_FTM_START_SESSION = 0x1100,
	SPRD_NL80211_VENDOR_SUBCMD_FTM_ABORT_SESSION = 0x1101,
	SPRD_NL80211_VENDOR_SUBCMD_LOC_GET_CAPA = 0x1102,
	SPRD_NL80211_VENDOR_SUBCMD_FTM_MEAS_RESULT = 131,
	SPRD_NL80211_VENDOR_SUBCMD_FTM_SESSION_DONE = 132,
	SPRD_NL80211_VENDOR_SUBCMD_FTM_CFG_RESPONDER = 133,
	SPRD_NL80211_VENDOR_SUBCMD_AOA_MEAS = 134,
	SPRD_NL80211_VENDOR_SUBCMD_AOA_ABORT_MEAS = 135,
	SPRD_NL80211_VENDOR_SUBCMD_AOA_MEAS_RESULT = 136,
};

/**
 * enum sprdwl_vendor_attr_loc - attributes for FTM and AOA commands
 *
 * @SPRDWL_VENDOR_ATTR_FTM_SESSION_COOKIE: Session cookie, specified in
 *  %SPRD_NL80211_VENDOR_SUBCMD_FTM_START_SESSION. It will be provided by driver
 *  events and can be used to identify events targeted for this session.
 * @SPRDWL_VENDOR_ATTR_LOC_CAPA: Nested attribute containing extra
 *  FTM/AOA capabilities, returned by %SPRD_NL80211_VENDOR_SUBCMD_LOC_GET_CAPA.
 *  see %enum sprdwl_vendor_attr_loc_capa.
 * @SPRDWL_VENDOR_ATTR_FTM_MEAS_PEERS: array of nested attributes
 *  containing information about each peer in measurement session
 *  request. See %enum sprdwl_vendor_attr_peer_info for supported
 *  attributes for each peer
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RESULTS: nested attribute containing
 *  measurement results for a peer. reported by the
 *  %SPRD_NL80211_VENDOR_SUBCMD_FTM_MEAS_RESULT event.
 *  See %enum sprdwl_vendor_attr_peer_result for list of supported
 *  attributes.
 * @SPRDWL_VENDOR_ATTR_FTM_RESPONDER_ENABLE: flag attribute for
 *  enabling or disabling responder functionality.
 * @SPRDWL_VENDOR_ATTR_FTM_LCI: used in the
 *  %SPRD_NL80211_VENDOR_SUBCMD_FTM_CFG_RESPONDER command in order to
 *  specify the LCI report that will be sent by the responder during
 *  a measurement exchange. The format is defined in IEEE P802.11-REVmc/D5.0,
 *  9.4.2.22.10
 * @SPRDWL_VENDOR_ATTR_FTM_LCR: provided with the
 *  %SPRD_NL80211_VENDOR_SUBCMD_FTM_CFG_RESPONDER command in order to
 *  specify the location civic report that will be sent by the responder during
 *  a measurement exchange. The format is defined in IEEE P802.11-REVmc/D5.0,
 *  9.4.2.22.13
 * @SPRDWL_VENDOR_ATTR_LOC_SESSION_STATUS: session/measurement completion
 *  status code, reported in %SPRD_NL80211_VENDOR_SUBCMD_FTM_SESSION_DONE
 *  and %SPRD_NL80211_VENDOR_SUBCMD_AOA_MEAS_RESULT
 * @SPRDWL_VENDOR_ATTR_FTM_INITIAL_TOKEN: initial dialog token used
 *  by responder (0 if not specified)
 * @SPRDWL_VENDOR_ATTR_AOA_TYPE: AOA measurement type. Requested in
 *  %SPRD_NL80211_VENDOR_SUBCMD_AOA_MEAS and optionally in
 *  %SPRD_NL80211_VENDOR_SUBCMD_FTM_START_SESSION if AOA measurements
 *  are needed as part of an FTM session.
 *  Reported by SPRD_NL80211_VENDOR_SUBCMD_AOA_MEAS_RESULT.
 *  See enum sprdwl_vendor_attr_aoa_type.
 * @SPRDWL_VENDOR_ATTR_LOC_ANTENNA_ARRAY_MASK: bit mask indicating
 *  which antenna arrays were used in location measurement.
 *  Reported in %SPRD_NL80211_VENDOR_SUBCMD_FTM_MEAS_RESULT and
 *  %SPRD_NL80211_VENDOR_SUBCMD_AOA_MEAS_RESULT
 * @SPRDWL_VENDOR_ATTR_AOA_MEAS_RESULT: AOA measurement data.
 *  Its contents depends on the AOA type and antenna array mask:
 *  %SPRDWL_VENDOR_ATTR_AOA_TYPE_TOP_CIR_PHASE: array of U16 values,
 *  phase of the strongest CIR path for each antenna in the measured
 *  array(s).
 *  %SPRDWL_VENDOR_ATTR_AOA_TYPE_TOP_CIR_PHASE_AMP: array of 2 U16
 *  values, phase and amplitude of the strongest CIR path for each
 *  antenna in the measured array(s)
 * @SPRDWL_VENDOR_ATTR_FREQ: Frequency where peer is listening, in MHz.
 *  Unsigned 32 bit value.
 */
enum sprdwl_vendor_attr_loc {
	/* we reuse these attributes */
	SPRDWL_VENDOR_ATTR_MAC_ADDR = 6,
	SPRDWL_VENDOR_ATTR_PAD = 13,
	SPRDWL_VENDOR_ATTR_FTM_SESSION_COOKIE = 14,
	SPRDWL_VENDOR_ATTR_LOC_CAPA = 15,
	SPRDWL_VENDOR_ATTR_FTM_MEAS_PEERS = 16,
	SPRDWL_VENDOR_ATTR_FTM_MEAS_PEER_RESULTS = 17,
	SPRDWL_VENDOR_ATTR_FTM_RESPONDER_ENABLE = 18,
	SPRDWL_VENDOR_ATTR_FTM_LCI = 19,
	SPRDWL_VENDOR_ATTR_FTM_LCR = 20,
	SPRDWL_VENDOR_ATTR_LOC_SESSION_STATUS = 21,
	SPRDWL_VENDOR_ATTR_FTM_INITIAL_TOKEN = 22,
	SPRDWL_VENDOR_ATTR_AOA_TYPE = 23,
	SPRDWL_VENDOR_ATTR_LOC_ANTENNA_ARRAY_MASK = 24,
	SPRDWL_VENDOR_ATTR_AOA_MEAS_RESULT = 25,
	SPRDWL_VENDOR_ATTR_FREQ = 28,
	/* keep last */
	SPRDWL_VENDOR_ATTR_LOC_AFTER_LAST,
	SPRDWL_VENDOR_ATTR_LOC_MAX = SPRDWL_VENDOR_ATTR_LOC_AFTER_LAST - 1,
};

/**
 * enum sprdwl_vendor_attr_loc_capa - indoor location capabilities
 *
 * @SPRDWL_VENDOR_ATTR_LOC_CAPA_FLAGS: various flags. See
 *  %enum sprdwl_vendor_attr_loc_capa_flags
 * @SPRDWL_VENDOR_ATTR_FTM_CAPA_MAX_NUM_SESSIONS: Maximum number
 *  of measurement sessions that can run concurrently.
 *  Default is one session (no session concurrency)
 * @SPRDWL_VENDOR_ATTR_FTM_CAPA_MAX_NUM_PEERS: The total number of unique
 *  peers that are supported in running sessions. For example,
 *  if the value is 8 and maximum number of sessions is 2, you can
 *  have one session with 8 unique peers, or 2 sessions with 4 unique
 *  peers each, and so on.
 * @SPRDWL_VENDOR_ATTR_FTM_CAPA_MAX_NUM_BURSTS_EXP: Maximum number
 *  of bursts per peer, as an exponent (2^value). Default is 0,
 *  meaning no multi-burst support.
 * @SPRDWL_VENDOR_ATTR_FTM_CAPA_MAX_MEAS_PER_BURST: Maximum number
 *  of measurement exchanges allowed in a single burst
 * @SPRDWL_VENDOR_ATTR_AOA_CAPA_SUPPORTED_TYPES: Supported AOA measurement
 *  types. A bit mask (unsigned 32 bit value), each bit corresponds
 *  to an AOA type as defined by %enum qca_vendor_attr_aoa_type.
 */
enum sprdwl_vendor_attr_loc_capa {
	SPRDWL_VENDOR_ATTR_LOC_CAPA_INVALID,
	SPRDWL_VENDOR_ATTR_LOC_CAPA_FLAGS,
	SPRDWL_VENDOR_ATTR_FTM_CAPA_MAX_NUM_SESSIONS,
	SPRDWL_VENDOR_ATTR_FTM_CAPA_MAX_NUM_PEERS,
	SPRDWL_VENDOR_ATTR_FTM_CAPA_MAX_NUM_BURSTS_EXP,
	SPRDWL_VENDOR_ATTR_FTM_CAPA_MAX_MEAS_PER_BURST,
	SPRDWL_VENDOR_ATTR_AOA_CAPA_SUPPORTED_TYPES,
	/* keep last */
	SPRDWL_VENDOR_ATTR_LOC_CAPA_AFTER_LAST,
	SPRDWL_VENDOR_ATTR_LOC_CAPA_MAX =
		SPRDWL_VENDOR_ATTR_LOC_CAPA_AFTER_LAST - 1,
};

/**
 * enum sprdwl_vendor_attr_loc_capa_flags: Indoor location capability flags
 *
 * @SPRDWL_VENDOR_ATTR_LOC_CAPA_FLAG_FTM_RESPONDER: Set if driver
 *  can be configured as an FTM responder (for example, an AP that
 *  services FTM requests). %SPRD_NL80211_VENDOR_SUBCMD_FTM_CFG_RESPONDER
 *  will be supported if set.
 * @SPRDWL_VENDOR_ATTR_LOC_CAPA_FLAG_FTM_INITIATOR: Set if driver
 *  can run FTM sessions. %SPRD_NL80211_VENDOR_SUBCMD_FTM_START_SESSION
 *  will be supported if set.
 * @SPRDWL_VENDOR_ATTR_LOC_CAPA_FLAG_ASAP: Set if FTM responder
 *  supports immediate (ASAP) response.
 * @SPRDWL_VENDOR_ATTR_LOC_CAPA_FLAG_AOA: Set if driver supports standalone
 *  AOA measurement using %SPRD_NL80211_VENDOR_SUBCMD_AOA_MEAS
 * @SPRDWL_VENDOR_ATTR_LOC_CAPA_FLAG_AOA_IN_FTM: Set if driver supports
 *  requesting AOA measurements as part of an FTM session.
 */
enum sprdwl_vendor_attr_loc_capa_flags {
	SPRDWL_VENDOR_ATTR_LOC_CAPA_FLAG_FTM_RESPONDER = 1 << 0,
	SPRDWL_VENDOR_ATTR_LOC_CAPA_FLAG_FTM_INITIATOR = 1 << 1,
	SPRDWL_VENDOR_ATTR_LOC_CAPA_FLAG_ASAP = 1 << 2,
	SPRDWL_VENDOR_ATTR_LOC_CAPA_FLAG_AOA = 1 << 3,
	SPRDWL_VENDOR_ATTR_LOC_CAPA_FLAG_AOA_IN_FTM = 1 << 4,
};

/**
 * enum sprdwl_vendor_attr_peer_info: information about
 *  a single peer in a measurement session.
 *
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_MAC_ADDR: The MAC address of the peer.
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_MEAS_FLAGS: Various flags related
 *  to measurement. See %enum sprdwl_vendor_attr_ftm_peer_meas_flags.
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_MEAS_PARAMS: Nested attribute of
 *  FTM measurement parameters, as specified by IEEE P802.11-REVmc/D7.0,
 *  9.4.2.167. See %enum sprdwl_vendor_attr_ftm_meas_param for
 *  list of supported attributes.
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_SECURE_TOKEN_ID: Initial token ID for
 *  secure measurement
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_AOA_BURST_PERIOD: Request AOA
 *  measurement every _value_ bursts. If 0 or not specified,
 *  AOA measurements will be disabled for this peer.
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_FREQ: Frequency in MHz where
 *  peer is listening. Optional; if not specified, use the
 *  entry from the kernel scan results cache.
 */
enum sprdwl_vendor_attr_ftm_peer_info {
	SPRDWL_VENDOR_ATTR_FTM_PEER_INVALID,
	SPRDWL_VENDOR_ATTR_FTM_PEER_MAC_ADDR,
	SPRDWL_VENDOR_ATTR_FTM_PEER_MEAS_FLAGS,
	SPRDWL_VENDOR_ATTR_FTM_PEER_MEAS_PARAMS,
	SPRDWL_VENDOR_ATTR_FTM_PEER_SECURE_TOKEN_ID,
	SPRDWL_VENDOR_ATTR_FTM_PEER_AOA_BURST_PERIOD,
	SPRDWL_VENDOR_ATTR_FTM_PEER_FREQ,
	/* keep last */
	SPRDWL_VENDOR_ATTR_FTM_PEER_AFTER_LAST,
	SPRDWL_VENDOR_ATTR_FTM_PEER_MAX =
		SPRDWL_VENDOR_ATTR_FTM_PEER_AFTER_LAST - 1,
};

/**
 * enum sprdwl_vendor_attr_ftm_peer_meas_flags: Measurement request flags,
 *  per-peer
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_ASAP: If set, request
 *  immediate (ASAP) response from peer
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_LCI: If set, request
 *  LCI report from peer. The LCI report includes the absolute
 *  location of the peer in "official" coordinates (similar to GPS).
 *  See IEEE P802.11-REVmc/D7.0, 11.24.6.7 for more information.
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_LCR: If set, request
 *  Location civic report from peer. The LCR includes the location
 *  of the peer in free-form format. See IEEE P802.11-REVmc/D7.0,
 *  11.24.6.7 for more information.
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_SECURE: If set,
 *  request a secure measurement.
 *  %SPRDWL_VENDOR_ATTR_FTM_PEER_SECURE_TOKEN_ID must also be provided.
 */
enum sprdwl_vendor_attr_ftm_peer_meas_flags {
	SPRDWL_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_ASAP	= 1 << 0,
	SPRDWL_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_LCI	= 1 << 1,
	SPRDWL_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_LCR	= 1 << 2,
	SPRDWL_VENDOR_ATTR_FTM_PEER_MEAS_FLAG_SECURE  = 1 << 3,
};

/**
 * enum sprdwl_vendor_attr_ftm_meas_param: Measurement parameters
 *
 * @SPRDWL_VENDOR_ATTR_FTM_PARAM_MEAS_PER_BURST: Number of measurements
 *  to perform in a single burst.
 * @SPRDWL_VENDOR_ATTR_FTM_PARAM_NUM_BURSTS_EXP: Number of bursts to
 *  perform, specified as an exponent (2^value)
 * @SPRDWL_VENDOR_ATTR_FTM_PARAM_BURST_DURATION: Duration of burst
 *  instance, as specified in IEEE P802.11-REVmc/D7.0, 9.4.2.167
 * @SPRDWL_VENDOR_ATTR_FTM_PARAM_BURST_PERIOD: Time between bursts,
 *  as specified in IEEE P802.11-REVmc/D7.0, 9.4.2.167. Must
 *  be larger than %SPRDWL_VENDOR_ATTR_FTM_PARAM_BURST_DURATION
 */
enum sprdwl_vendor_attr_ftm_meas_param {
	SPRDWL_VENDOR_ATTR_FTM_PARAM_INVALID,
	SPRDWL_VENDOR_ATTR_FTM_PARAM_MEAS_PER_BURST,
	SPRDWL_VENDOR_ATTR_FTM_PARAM_NUM_BURSTS_EXP,
	SPRDWL_VENDOR_ATTR_FTM_PARAM_BURST_DURATION,
	SPRDWL_VENDOR_ATTR_FTM_PARAM_BURST_PERIOD,
	/* keep last */
	SPRDWL_VENDOR_ATTR_FTM_PARAM_AFTER_LAST,
	SPRDWL_VENDOR_ATTR_FTM_PARAM_MAX =
		SPRDWL_VENDOR_ATTR_FTM_PARAM_AFTER_LAST - 1,
};

/**
 * enum sprdwl_vendor_attr_ftm_peer_result: Per-peer results
 *
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_MAC_ADDR: MAC address of the reported
 *  peer
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_STATUS: Status of measurement
 *  request for this peer.
 *  See %enum sprdwl_vendor_attr_ftm_peer_result_status
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_FLAGS: Various flags related
 *  to measurement results for this peer.
 *  See %enum sprdwl_vendor_attr_ftm_peer_result_flags
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_VALUE_SECONDS: Specified when
 *  request failed and peer requested not to send an additional request
 *  for this number of seconds.
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_LCI: LCI report when received
 *  from peer. In the format specified by IEEE P802.11-REVmc/D7.0,
 *  9.4.2.22.10
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_LCR: Location civic report when
 *  received from peer.In the format specified by IEEE P802.11-REVmc/D7.0,
 *  9.4.2.22.13
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_MEAS_PARAMS: Reported when peer
 *  overridden some measurement request parameters. See
 *  enum sprdwl_vendor_attr_ftm_meas_param.
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_AOA_MEAS: AOA measurement
 *  for this peer. Same contents as %SPRDWL_VENDOR_ATTR_AOA_MEAS_RESULT
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_MEAS: Array of measurement
 *  results. Each entry is a nested attribute defined
 *  by enum sprdwl_vendor_attr_ftm_meas.
 */
enum sprdwl_vendor_attr_ftm_peer_result {
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_INVALID,
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_MAC_ADDR,
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_STATUS,
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_FLAGS,
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_VALUE_SECONDS,
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_LCI,
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_LCR,
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_MEAS_PARAMS,
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_AOA_MEAS,
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_MEAS,
	/* keep last */
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_AFTER_LAST,
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_MAX =
		SPRDWL_VENDOR_ATTR_FTM_PEER_RES_AFTER_LAST - 1,
};

/**
 * enum sprdwl_vendor_attr_ftm_peer_result_status
 *
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_STATUS_OK: Request sent ok and results
 *  will be provided. Peer may have overridden some measurement parameters,
 *  in which case overridden parameters will be report by
 *  %SPRDWL_VENDOR_ATTR_FTM_PEER_RES_MEAS_PARAMS attribute
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_STATUS_INCAPABLE: Peer is incapable
 *  of performing the measurement request. No more results will be sent
 *  for this peer in this session.
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_STATUS_FAILED: Peer reported request
 *  failed, and requested not to send an additional request for number
 *  of seconds specified by %SPRDWL_VENDOR_ATTR_FTM_PEER_RES_VALUE_SECONDS
 *  attribute.
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_STATUS_INVALID: Request validation
 *  failed. Request was not sent over the air.
 */
enum sprdwl_vendor_attr_ftm_peer_result_status {
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_STATUS_OK,
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_STATUS_INCAPABLE,
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_STATUS_FAILED,
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_STATUS_INVALID,
};

/**
 * enum sprdwl_vendor_attr_ftm_peer_result_flags : Various flags
 *  for measurement result, per-peer
 *
 * @SPRDWL_VENDOR_ATTR_FTM_PEER_RES_FLAG_DONE: If set,
 *  measurement completed for this peer. No more results will be reported
 *  for this peer in this session.
 */
enum sprdwl_vendor_attr_ftm_peer_result_flags {
	SPRDWL_VENDOR_ATTR_FTM_PEER_RES_FLAG_DONE = 1 << 0,
};

/**
 * enum qca_vendor_attr_loc_session_status: Session completion status code
 *
 * @SPRDWL_VENDOR_ATTR_LOC_SESSION_STATUS_OK: Session completed
 *  successfully.
 * @SPRDWL_VENDOR_ATTR_LOC_SESSION_STATUS_ABORTED: Session aborted
 *  by request
 * @SPRDWL_VENDOR_ATTR_LOC_SESSION_STATUS_INVALID: Session request
 *  was invalid and was not started
 * @SPRDWL_VENDOR_ATTR_LOC_SESSION_STATUS_FAILED: Session had an error
 *  and did not complete normally (for example out of resources)
 *
 */
enum sprdwl_vendor_attr_loc_session_status {
	SPRDWL_VENDOR_ATTR_LOC_SESSION_STATUS_OK,
	SPRDWL_VENDOR_ATTR_LOC_SESSION_STATUS_ABORTED,
	SPRDWL_VENDOR_ATTR_LOC_SESSION_STATUS_INVALID,
	SPRDWL_VENDOR_ATTR_LOC_SESSION_STATUS_FAILED,
};

/**
 * enum sprdwl_vendor_attr_ftm_meas: Single measurement data
 *
 * @SPRDWL_VENDOR_ATTR_FTM_MEAS_T1: Time of departure(TOD) of FTM packet as
 *  recorded by responder, in picoseconds.
 *  See IEEE P802.11-REVmc/D7.0, 11.24.6.4 for more information.
 * @SPRDWL_VENDOR_ATTR_FTM_MEAS_T2: Time of arrival(TOA) of FTM packet at
 *  initiator, in picoseconds.
 *  See IEEE P802.11-REVmc/D7.0, 11.24.6.4 for more information.
 * @SPRDWL_VENDOR_ATTR_FTM_MEAS_T3: TOD of ACK packet as recorded by
 *  initiator, in picoseconds.
 *  See IEEE P802.11-REVmc/D7.0, 11.24.6.4 for more information.
 * @SPRDWL_VENDOR_ATTR_FTM_MEAS_T4: TOA of ACK packet at
 *  responder, in picoseconds.
 *  See IEEE P802.11-REVmc/D7.0, 11.24.6.4 for more information.
 * @SPRDWL_VENDOR_ATTR_FTM_MEAS_RSSI: RSSI (signal level) as recorded
 *  during this measurement exchange. Optional and will be provided if
 *  the hardware can measure it.
 * @SPRDWL_VENDOR_ATTR_FTM_MEAS_TOD_ERR: TOD error reported by
 *  responder. Not always provided.
 *  See IEEE P802.11-REVmc/D7.0, 9.6.8.33 for more information.
 * @SPRDWL_VENDOR_ATTR_FTM_MEAS_TOA_ERR: TOA error reported by
 *  responder. Not always provided.
 *  See IEEE P802.11-REVmc/D7.0, 9.6.8.33 for more information.
 * @SPRDWL_VENDOR_ATTR_FTM_MEAS_INITIATOR_TOD_ERR: TOD error measured by
 *  initiator. Not always provided.
 *  See IEEE P802.11-REVmc/D7.0, 9.6.8.33 for more information.
 * @SPRDWL_VENDOR_ATTR_FTM_MEAS_INITIATOR_TOA_ERR: TOA error measured by
 *  initiator. Not always provided.
 *  See IEEE P802.11-REVmc/D7.0, 9.6.8.33 for more information.
 * @SPRDWL_VENDOR_ATTR_FTM_MEAS_PAD: Dummy attribute for padding.
 */
enum sprdwl_vendor_attr_ftm_meas {
	SPRDWL_VENDOR_ATTR_FTM_MEAS_INVALID,
	SPRDWL_VENDOR_ATTR_FTM_MEAS_T1,
	SPRDWL_VENDOR_ATTR_FTM_MEAS_T2,
	SPRDWL_VENDOR_ATTR_FTM_MEAS_T3,
	SPRDWL_VENDOR_ATTR_FTM_MEAS_T4,
	SPRDWL_VENDOR_ATTR_FTM_MEAS_RSSI,
	SPRDWL_VENDOR_ATTR_FTM_MEAS_TOD_ERR,
	SPRDWL_VENDOR_ATTR_FTM_MEAS_TOA_ERR,
	SPRDWL_VENDOR_ATTR_FTM_MEAS_INITIATOR_TOD_ERR,
	SPRDWL_VENDOR_ATTR_FTM_MEAS_INITIATOR_TOA_ERR,
	SPRDWL_VENDOR_ATTR_FTM_MEAS_PAD,
	/* keep last */
	SPRDWL_VENDOR_ATTR_FTM_MEAS_AFTER_LAST,
	SPRDWL_VENDOR_ATTR_FTM_MEAS_MAX =
		SPRDWL_VENDOR_ATTR_FTM_MEAS_AFTER_LAST - 1,
};

enum sprdwl_vendor_attr_aoa_type {
	SPRDWL_VENDOR_ATTR_AOA_TYPE_TOP_CIR_PHASE,
	SPRDWL_VENDOR_ATTR_AOA_TYPE_TOP_CIR_PHASE_AMP,
	SPRDWL_VENDOR_ATTR_AOA_TYPE_MAX,
};

/* vendor rtt cap */
enum sprdwl_vendor_ftm_cap_index {
	SPRD_VENDOR_RTT_ONE_SIDED_SUPPORTED,
	SPRD_VENDOR_RTT_FTM_SUPPORTED,
	SPRD_VENDOR_LCI_SUPPORT,
	SPRD_VENDOR_LCR_SUPPORT,
	SPRD_VENDOR_PREAMBLE_SUPPORT,
	SPRD_VENDOR_BW_SUPPORT,
	SPRD_VENDOR_RESPONDER_SUPPORTED,
	SPRD_VENDOR_MC_version,
};

enum sprdwl_vendor_rtt_attribute {
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_CNT = 0,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_INFO,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_MAC,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_TYPE,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_PEER,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_CHAN,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_PERIOD,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_NUM_BURST,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_NUM_FTM_BURST,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_NUM_RETRY_FTM,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_NUM_RETRY_FTMR,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_LCI,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_LCR,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_BURST_DURATION,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_PREAMBLE,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_BW,
	SPRD_VENDOR_RTT_ATTRIBUTE_TARGET_RESPONDER_INFO,
	SPRD_VENDOR_RTT_ATTRIBUTE_MAX
};

enum sprdwl_vendor_rtt_result_attribute{
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_CNT = 0,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_INFO,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_MAC,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_TYPE,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_PEER,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_CHAN,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_PERIOD,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_NUM_BURST,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_NUM_FTM_BURST,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_NUM_RETRY_FTM,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_NUM_RETRY_FTMR,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_LCI,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_LCR,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_BURST_DURATION,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_PREAMBLE,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_BW,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_RESPONDER_INFO,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULTS_COMPLETE = 30,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULTS_PER_TARGET,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT_CNT_CNT,
    SPRD_VENDOR_RTT_ATTRIBUTE_RESULT
} ;

/* measurement parameters. Specified for each peer as part
 * of measurement request, or provided with measurement
 * results for peer in case peer overridden parameters
 */
struct sprdwl_ftm_meas_params {
	u8 meas_per_burst;
	u8 num_of_bursts_exp;
	u8 burst_duration;
	u16 burst_period;
} __packed;

/* measurement request for a single peer */
struct sprdwl_ftm_meas_peer_info {
	u8 mac_addr[ETH_ALEN];
	u8 wifi_rtt_type;
	u32 rtt_peer_type;
	struct wifi_channel_info channel;
	u32 burst_period;					//range 0-31
	u32 num_burst;					//range 0-15
	u32 num_frames_per_burst;		//range 0-31
	u32 num_retries_per_rtt_frame;	//0-3
	u32 num_retries_per_ftmr;
	u8 LCI_request;
	u8 LCR_request;
	u32 burst_duration;
	u8 preamble;		//wifi_rtt_preamble
	u8 bw;					//wifi_rtt_bw
} __packed;

struct sprdwl_ftm_meas_cancel_peer_info {
	u8 n_peers;
	u8 mac_addr[0][ETH_ALEN];
} __packed;

/* session request, passed to wil_ftm_cfg80211_start_session */
struct sprdwl_ftm_session_request {
	u8 n_peers;
	/* keep last, variable size according to n_peers */
	struct sprdwl_ftm_meas_peer_info peers[0];
} __packed;

/* single measurement for a peer */
struct sprdwl_ftm_peer_meas {
	s64 t1, t2, t3, t4;
} __packed;

/* measurement results for a single peer */
struct sprdwl_ftm_peer_meas_res {
	u8 mac_addr[ETH_ALEN];
	u32 flags; /* enum sprdwl_vendor_attr_ftm_peer_result_flags */
	u8 status; /* enum sprdwl_vendor_attr_ftm_peer_result_status */
	u8 value_seconds;
	bool has_params; /* true if params is valid */
	struct sprdwl_ftm_meas_params params; /* peer overridden params */
	u8 *lci;
	u8 lci_length;
	u8 *lcr;
	u8 lcr_length;
	u32 n_meas;
	/* keep last, variable size according to n_meas */
	struct sprdwl_ftm_peer_meas meas[0];
};

/* private data related to FTM. Part of the priv structure */
struct sprdwl_ftm_priv {
	struct mutex lock; /* protects the FTM data */
	u8 session_started;
	u64 session_cookie;
	struct sprdwl_ftm_peer_meas_res *ftm_res;
	u8 has_ftm_res;
	u32 max_ftm_meas;

	/* standalone AOA measurement */
	u8 aoa_started;
	u8 aoa_peer_mac_addr[ETH_ALEN];
	u32 aoa_type;
	struct timer_list aoa_timer;
	struct work_struct aoa_timeout_work;
};

/**
 * RTT Capabilities
 * @rtt_one_sided_supported: if 1-sided rtt data collection is supported
 * @rtt_ftm_supported: if ftm rtt data collection is supported
 * @lci_support: if initiator supports LCI request. Applies to 2-sided RTT
 * @lcr_support: if initiator supports LCR request. Applies to 2-sided RTT
 * @preamble_support: bit mask indicates what preamble is supported by initiator
 * @bw_support: bit mask indicates what BW is supported by initiator
 * @responder_supported: if 11mc responder mode is supported
 * @mc_version: draft 11mc spec version supported by chip.
 *   For instance, version 4.0 should be 40 and version 4.3 should be 43 etc.
 *
 */
struct sprdwl_rtt_capabilities {
	u8 rtt_one_sided_supported;
	u8 rtt_ftm_supported;
	u8 lci_support;
	u8 lcr_support;
	u8 preamble_support;
	u8 bw_support;
	u8 responder_supported;
	u8 mc_version;
};

enum wifi_rtt_preamble {
	WIFI_RTT_PREAMBLE_LEGACY = 0x1,
	WIFI_RTT_PREAMBLE_HT     = 0x2,
	WIFI_RTT_PREAMBLE_VHT    = 0x4
};

enum wifi_rtt_bw {
	WIFI_RTT_BW_5   = 0x01,
	WIFI_RTT_BW_10  = 0x02,
	WIFI_RTT_BW_20  = 0x04,
	WIFI_RTT_BW_40  = 0x08,
	WIFI_RTT_BW_80  = 0x10,
	WIFI_RTT_BW_160 = 0x20
};

struct sprdwl_rtt_responder {
	struct wifi_channel_info channel;
	enum wifi_rtt_preamble preamble;
};

#define RTT_MAX_CONFIG 5

/* Ranging status */
enum wifi_rtt_status{
	RTT_STATUS_SUCCESS       = 0,
	RTT_STATUS_FAILURE       = 1,           // general failure status
	RTT_STATUS_FAIL_NO_RSP   = 2,           // target STA does not respond to request
	RTT_STATUS_FAIL_REJECTED = 3,           // request rejected. Applies to 2-sided RTT only
	RTT_STATUS_FAIL_NOT_SCHEDULED_YET  = 4,
	RTT_STATUS_FAIL_TM_TIMEOUT         = 5, // timing measurement times out
	RTT_STATUS_FAIL_AP_ON_DIFF_CHANNEL = 6, // Target on different channel, cannot range
	RTT_STATUS_FAIL_NO_CAPABILITY  = 7,     // ranging not supported
	RTT_STATUS_ABORTED             = 8,     // request aborted for unknown reason
	RTT_STATUS_FAIL_INVALID_TS     = 9,     // Invalid T1-T4 timestamp
	RTT_STATUS_FAIL_PROTOCOL       = 10,    // 11mc protocol failed
	RTT_STATUS_FAIL_SCHEDULE       = 11,    // request could not be scheduled
	RTT_STATUS_FAIL_BUSY_TRY_LATER = 12,    // responder cannot collaborate at time of request
	RTT_STATUS_INVALID_REQ         = 13,    // bad request args
	RTT_STATUS_NO_WIFI             = 14,    // WiFi not enabled
	RTT_STATUS_FAIL_FTM_PARAM_OVERRIDE = 15 // Responder overrides param info, cannot range with new params
};

/* RTT Type */
enum wifi_rtt_type{
	RTT_TYPE_1_SIDED = 0x1,
	RTT_TYPE_2_SIDED = 0x2,
};

struct wifi_information_element{
	u8 id;                            // element identifier
	u8 len;                           // number of bytes to follow
	u8 *data;
};

/* RTT results */
struct wifi_hal_rtt_result{
	u8 mac_addr[ETH_ALEN];                // device mac address
	unsigned burst_num;           // burst number in a multi-burst request
	unsigned measurement_number;  // Total RTT measurement frames attempted
	unsigned success_number;      // Total successful RTT measurement frames
	u8  number_per_burst_peer;  // Maximum number of "FTM frames per burst" supported by
								// the responder STA. Applies to 2-sided RTT only.
								// If reponder overrides with larger value:
								// - for single-burst request initiator will truncate the
								// larger value and send a TMR_STOP after receiving as
								// many frames as originally requested.
								// - for multi-burst request, initiator will return
								// failure right away.
	enum wifi_rtt_status status;       // ranging status
	u8 retry_after_duration;    // When status == RTT_STATUS_FAIL_BUSY_TRY_LATER,
								// this will be the time provided by the responder as to
								// when the request can be tried again. Applies to 2-sided
								// RTT only. In sec, 1-31sec.
	enum wifi_rtt_type type;           // RTT type
	int rssi;               // average rssi in 0.5 dB steps e.g. 143 implies -71.5 dB
	int rssi_spread;        // rssi spread in 0.5 dB steps e.g. 5 implies 2.5 dB spread (optional)
	struct wifi_rate tx_rate;            // 1-sided RTT: TX rate of RTT frame.
								// 2-sided RTT: TX rate of initiator's Ack in response to FTM frame.
	struct wifi_rate rx_rate;            // 1-sided RTT: TX rate of Ack from other side.
								// 2-sided RTT: TX rate of FTM frame coming from responder.
	u64 rtt;            // round trip time in picoseconds
	u64 rtt_sd;         // rtt standard deviation in picoseconds
	u64 rtt_spread;     // difference between max and min rtt times recorded in picoseconds
	int distance_mm;              // distance in mm (optional)
	int distance_sd_mm;           // standard deviation in mm (optional)
	int distance_spread_mm;       // difference between max and min distance recorded in mm (optional)
	u64 ts;            // time of the measurement (in microseconds since boot)
	int burst_duration;           // in ms, actual time taken by the FW to finish one burst
								// measurement. Applies to 1-sided and 2-sided RTT.
	int negotiated_burst_num;     // Number of bursts allowed by the responder. Applies
								// to 2-sided RTT only.
	struct wifi_information_element *LCI; // for 11mc only
	struct wifi_information_element *LCR; // for 11mc only
};

struct sprdwl_dot11_rm_ie {
    u8 id;
    u8 len;
    u8 token;
    u8 mode;
    u8 type;
};

#define LCI_MAX_LEN 200
#define LCR_MAX_LEN 200
#define RTT_MAX_RESULT_SUPPORT 100
struct wifi_rtt_result {
	u8 mac_addr[ETH_ALEN];                // device mac address
	enum wifi_rtt_status status;       // ranging status
	struct sprdwl_ftm_meas_params params;
	u8 measurement_number;
	u8 success_number;
	u8 retry_after_duration;
	enum wifi_rtt_type type;           // RTT type
	int rssi;               // average rssi in 0.5 dB steps e.g. 143 implies -71.5 dB
	int rssi_spread;        // rssi spread in 0.5 dB steps e.g. 5 implies 2.5 dB spread (optional)
	struct wifi_rate tx_rate;            // 1-sided RTT: TX rate of RTT frame.
								// 2-sided RTT: TX rate of initiator's Ack in response to FTM frame.
	struct wifi_rate rx_rate;            // 1-sided RTT: TX rate of Ack from other side.
								// 2-sided RTT: TX rate of FTM frame coming from responder.
	u32 ts;            // time of the measurement (in microseconds since boot)
	u8 lci_length;
	u8 lci_content[LCI_MAX_LEN];
	u8 lcr_length;
	u8 lcr_content[LCR_MAX_LEN];
	u32 n_meas;
	struct sprdwl_ftm_peer_meas meas[RTT_MAX_RESULT_SUPPORT];
} __packed;

int sprdwl_ftm_get_capabilities(struct wiphy *wiphy,
				struct wireless_dev *wdev,
				const void *data, int data_len);
int sprdwl_ftm_start_session(struct wiphy *wiphy,
			     struct wireless_dev *wdev,
			     const void *data, int data_len);
int sprdwl_ftm_abort_session(struct wiphy *wiphy,
			     struct wireless_dev *wdev,
			     const void *data, int data_len);
int sprdwl_ftm_get_responder_info(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  const void *data, int data_len);
int sprdwl_ftm_configure_responder(struct wiphy *wiphy,
				   struct wireless_dev *wdev,
				   const void *data, int data_len);
int sprdwl_event_ftm(struct sprdwl_vif *vif, u8 *data, u16 len);
void sprdwl_ftm_init(struct sprdwl_priv *priv);
void sprdwl_ftm_deinit(struct sprdwl_priv *priv);
void sprdwl_ftm_stop_operations(struct sprdwl_priv *priv);

#endif /* __SPRD_RTT_H__ */

