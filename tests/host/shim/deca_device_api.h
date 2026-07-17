/* Host shim for <deca_device_api.h> — only the STS key/IV surface ccc_sts.c
 * needs. The register writes are no-ops here, but the values are captured into
 * woz_host_last_sts_* so a unit test can assert the derivation packed them
 * correctly (field layout matches the DW3000 driver's cp_key/cp_iv structs). */
#ifndef WOZ_HOST_SHIM_DECA_DEVICE_API_H
#define WOZ_HOST_SHIM_DECA_DEVICE_API_H

#include <stdint.h>

typedef struct {
	uint32_t key0;
	uint32_t key1;
	uint32_t key2;
	uint32_t key3;
} dwt_sts_cp_key_t;

typedef struct {
	uint32_t iv0;
	uint32_t iv1;
	uint32_t iv2;
	uint32_t iv3;
} dwt_sts_cp_iv_t;

/* Last values handed to the (no-op) register writes — for test assertions. */
extern dwt_sts_cp_key_t woz_host_last_sts_key;
extern dwt_sts_cp_iv_t woz_host_last_sts_iv;
extern unsigned int woz_host_sts_loadiv_calls;

void dwt_configurestskey(dwt_sts_cp_key_t *key);
void dwt_configurestsiv(dwt_sts_cp_iv_t *iv);
void dwt_configurestsloadiv(void);

/* ── RX/radio surface for ccc_shim_rx.c (the Pre-POLL listener) ──────────────
 * Constants mirror the real deca_device_api.h values (the listener tests bit
 * positions in status words); the functions are recording doubles defined in
 * dw_rx_stub.c. */

enum { DWT_SUCCESS = 0, DWT_ERROR = -1 };

#define DWT_START_RX_IMMEDIATE 0x00
#define DWT_START_RX_DELAYED   0x01
#define DWT_IDLE_ON_DLY_ERR    0x02
#define DWT_START_TX_DELAYED   0x01

#define DWT_STS_MODE_OFF 0x0
#define DWT_STS_MODE_ND  0x3

#define DWT_PLEN_64     (64U)
#define DWT_PAC8        0
#define DWT_SFD_IEEE_4A 0
#define DWT_BR_6M8      1
#define DWT_PHRMODE_STD 0x0
#define DWT_PHRRATE_STD 0x0
#define DWT_STS_LEN_64  7
#define DWT_PDOA_M0     0x0
#define DWT_ENABLE_INT  1

#define DWT_INT_ARFE_BIT_MASK    0x20000000UL
#define DWT_INT_HPDWARN_BIT_MASK 0x8000000UL
#define DWT_INT_RXSTO_BIT_MASK   0x4000000UL
#define DWT_INT_RXPTO_BIT_MASK   0x200000UL
#define DWT_INT_RXFTO_BIT_MASK   0x20000UL
#define DWT_INT_RXFSL_BIT_MASK   0x10000UL
#define DWT_INT_RXFCE_BIT_MASK   0x8000U
#define DWT_INT_RXFCG_BIT_MASK   0x4000U
#define DWT_INT_RXPHE_BIT_MASK   0x1000U
#define DWT_INT_RXPHD_BIT_MASK   0x800U
#define DWT_INT_CIADONE_BIT_MASK 0x400U
#define DWT_INT_TXFRS_BIT_MASK   0x80U

typedef struct {
	uint8_t chan;
	uint16_t txPreambLength;
	uint8_t rxPAC;
	uint8_t txCode;
	uint8_t rxCode;
	uint8_t sfdType;
	uint8_t dataRate;
	uint8_t phrMode;
	uint8_t phrRate;
	uint16_t sfdTO;
	uint8_t stsMode;
	uint8_t stsLength;
	uint8_t pdoaMode;
} dwt_config_t;

typedef struct {
	uint32_t status;
	uint16_t status_hi;
	uint16_t datalength;
	uint8_t rx_flags;
} dwt_cb_data_t;

typedef void (*dwt_cb_t)(const dwt_cb_data_t *cb_data);

typedef struct {
	dwt_cb_t cbTxDone;
	dwt_cb_t cbRxOk;
	dwt_cb_t cbRxTo;
	dwt_cb_t cbRxErr;
	dwt_cb_t cbSPIErr;
	dwt_cb_t cbSPIRDErr;
	dwt_cb_t cbSPIRdy;
	dwt_cb_t cbDualSPIEv;
} dwt_callbacks_s;

int32_t dwt_configure(dwt_config_t *config);
void dwt_configurestsmode(uint8_t stsMode);
void dwt_setcallbacks(dwt_callbacks_s *callbacks);
void dwt_setinterrupt(uint32_t bitmask_lo, uint32_t bitmask_hi, int options);
void dwt_setrxtimeout(uint32_t time);
void dwt_setdelayedtrxtime(uint32_t starttime);
int32_t dwt_rxenable(int32_t mode);
int32_t dwt_starttx(int32_t mode);
void dwt_forcetrxoff(void);
int32_t dwt_writetxdata(uint16_t txDataLength, uint8_t *txDataBytes, uint16_t txBufferOffset);
void dwt_writetxfctrl(uint16_t txFrameLength, uint16_t txBufferOffset, uint8_t ranging);
uint32_t dwt_read_reg(uint32_t addr);
uint32_t dwt_readctrdbg(void);
uint32_t dwt_readsystimestamphi32(void);
uint32_t dwt_readsysstatuslo(void);
void dwt_readtxtimestamp(uint8_t *timestamp);
void dwt_readrxtimestamp_ipatov(uint8_t *timestamp);
void dwt_readrxdata(uint8_t *buffer, uint16_t length, uint16_t rxBufferOffset);
uint16_t dwt_getframelength(uint8_t *rng);
int dwt_readstsquality(int16_t *rxStsQualityIndex, int stsSegment);

/* Recording state for the doubles above — reset with woz_host_rx_reset(). */
struct woz_host_rx_rec {
	unsigned rxenable_calls;      /* __real_dwt_rxenable invocations */
	int32_t last_rxenable_mode;
	unsigned forcetrxoff_calls;
	unsigned starttx_calls;
	unsigned seq;                 /* global call sequencer */
	unsigned last_rxenable_seq;   /* seq at the last __real_dwt_rxenable */
	unsigned last_forcetrxoff_seq;
	dwt_callbacks_s cbs;          /* captured by dwt_setcallbacks */
	int32_t rxenable_ret;         /* returned by __real_dwt_rxenable (default DWT_SUCCESS) */
	int32_t starttx_ret;
	/* Injectable RX/TX state, so tests can feed the listener real frames. */
	uint8_t rxdata[128];          /* served by dwt_readrxdata */
	uint16_t rxdata_len;          /* served by dwt_getframelength */
	uint64_t rx_ts40;             /* 40-bit Ipatov RX timestamp (LE 5 bytes) */
	uint64_t tx_ts40;             /* 40-bit TX timestamp */
	uint32_t systime;             /* dwt_readsystimestamphi32 */
	int stsq_ret;                 /* dwt_readstsquality return */
	int16_t stsq_val;             /* ...and its quality index out-param */
};
extern struct woz_host_rx_rec woz_host_rx;
void woz_host_rx_reset(void);

#endif /* WOZ_HOST_SHIM_DECA_DEVICE_API_H */
