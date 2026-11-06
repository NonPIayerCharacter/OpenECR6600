/*******************************************************************************
 * Copyright by eswin.
 *
 * File Name: amt_cli.c
 * File Mark:    
 * Description:  
 * Others:        
 * Version:       v1.0
 * Author:        zhangyunpeng
 * Date:         
 ********************************************************************************/

//#include "amt.h"
//#include "hal_rf_6600.h"
//#include "reg_macbypass.h"
//#include "hal_desc.h"
#include "cmd_wifi_amt_cli.h"
#include "cli.h"
#include "amtNV.h"
#include "easyflash.h"
#include "chip_pinmux.h"


#if defined(CONFIG_WIFI_AMT_VERSION) && defined(CONFIG_WIFI_LMAC_TEST)
/* AMT test command */

extern int AmtRfStart(int argc, char *argv[]);
extern void AmtRfStop(void);
extern int AmtGetVerStartFlag(void);
extern int AmtMacTxStart(int argc, char *argv[]);
extern int AmtMacbypTxStart(int argc, char *argv[]);
extern int AmtMacbypTxStop(void);
extern int AmtRfSetTxGain(int argc, char *argv[]);
extern int AmtRfGetTxGain(void);
extern int AmtRfSetApcIndex(int argc, char *argv[]);
extern int AmtRfSetTxGainFlag(int argc, char *argv[]);
extern int AmtRfGetTxGainFlag(void);
extern int AmtRfGetTxPowerCal(void);
extern int AmtMacbypRxStart(int argc, char *argv[]);
extern int AmtMacbypRxStop(int argc, char *argv[]);
extern int AmtdumpDC(int argc, char *argv[]);
extern int AmtTxSingletone1M(int argc, char *argv[]);
extern int AmtRfSetMac(int argc, char *argv[]);
extern int AmtRfGetMac(int argc, char *argv[]);
extern int AmtRfSet(int argc, char *argv[]);
extern int AmtRfGet(int argc, char *argv[]);
extern int AmtLOLeakage(int argc, char *argv[]);
extern int AmtSetRfFinish(int argc, char *argv[]);
extern int AmtGetRfFinish(int argc, char *argv[]);
extern int AmtRfSetFreqOffset(int argc, char *argv[]);
extern int AmtRfSetCfo(int argc, char *argv[]);
extern int AmtRfGetFreqOffset();
extern int AmtRfSetFreqOffsetFlag(int argc, char *argv[]);
extern int AmtRfGetFreqOffsetFlag(void);
extern int AmtRfSetPaBias(int argc, char *argv[]);
#ifdef NX_ESWIN_SDIO
extern int AmtRfSaveResult(int argc, char *argv[]);
extern int AmtRfGetResult(int argc, char *argv[]);
extern int AmtRfSetTxDeltaGain(int argc, char *argv[]);
#endif
extern int AmtTempEnable(int argc, char *argv[]);
extern int AmtFccEnable(int argc, char *argv[]);

static int AmtStart(int argc, char *argv[])
{
    int ret;

    ret = AmtRfStart(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_start, AmtStart, "amtStart", "amtStart", E_ALL);

#ifndef NX_ESWIN_SDIO
static int AmtStop(   int argc, char *argv[])
{
    AmtRfStop();

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_stop, AmtStop, "amtStop", "amtStop", E_AMT);
#endif

static int AmtReadFlagFromNV(   int argc, char *argv[])
{
#ifdef NX_ESWIN_SDIO
    AmtGetVerStartFlag();

    return CMD_RET_SUCCESS;
#else
    	#ifdef CONFIG_AI_VLLM
	chip_uart0_pinmux_cfg(UART0_RX_USED_GPIO5, UART0_TX_USED_GPIO6, 0);
	#endif

	extern int g_in_amt_flag;
	os_printf(LM_CMD, LL_INFO, "%d,", g_in_amt_flag);

	return CMD_RET_SUCCESS;
#endif
}
CLI_CMD_M(get_version_type, AmtReadFlagFromNV, "get_version_type", "get_version_type", E_ALL);
static int AmtTempStart(   int argc, char *argv[])
{
    int ret;

    ret = AmtTempEnable(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}

CLI_CMD_M(amt_temp_start, AmtTempStart, "amtTempStart", "amtTempStart", E_AMT);

static int AmtFccStart(   int argc, char *argv[])
{
    int ret;

    ret = AmtFccEnable(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_fcc_start, AmtFccStart, "amtFccStart", "amtFccStart", E_AMT);

static int AmtTxStart2(   int argc, char *argv[])
{
    int ret;

    ret = AmtMacTxStart(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_mac_tx_start, AmtTxStart2, "amtTxStart2", "amtTxStart2", E_AMT);

static int AmtTxStart( int argc, char *argv[])
{
    int ret;

    ret = AmtMacbypTxStart(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_tx_start, AmtTxStart, "amtTxStart", "amtTxStart", E_AMT);

static int AmtTxStop(   int argc, char *argv[])
{


    AmtMacbypTxStop();

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_tx_stop, AmtTxStop, "amtTxStop", "amtTxStop", E_AMT);

static int AmtSetTxGain(   int argc, char *argv[])
{
    int ret;

    ret = AmtRfSetTxGain(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }
    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_set_txgain, AmtSetTxGain, "amtSetTxGain", "amtSetTxGain", E_AMT);

static int AmtSetTxApcIndex(   int argc, char *argv[])
{
    int ret;

    ret = AmtRfSetApcIndex(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }
    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_set_apcindex, AmtSetTxApcIndex, "amt_set_apcindex", "amt_set_apcindex", E_AMT);

static int AmtGetTxGain(   int argc, char *argv[])
{
    AmtRfGetTxGain();

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_get_txgain, AmtGetTxGain, "amtGetTxGain", "amtGetTxGain", E_AMT);

static int AmtGetTxPower(   int argc, char *argv[])
{
    int ret;

    ret = AmtRfGetTxPowerCal();
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amtGetTxPower, AmtGetTxPower, "amtGetTxPower", "amtGetTxPower", E_AMT);

#ifndef NX_ESWIN_SDIO
static int AmtSetTxGainFlag(   int argc, char *argv[])
{
    int ret;

    ret = AmtRfSetTxGainFlag(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_set_txgain_flag, AmtSetTxGainFlag, "amtSetTxGainFlag", "amtSetTxGainFlag", E_AMT);

static int AmtGetTxGainFlag(   int argc, char *argv[])
{
    AmtRfGetTxGainFlag();

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_get_txgain_flag, AmtGetTxGainFlag, "amtGetTxGainFlag", "amtGetTxGainFlag", E_AMT);
#endif

static int AmtRxStart(   int argc, char *argv[])
{
    int ret;

    ret = AmtMacbypRxStart(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_rx_start, AmtRxStart, "amtRxStart", "amtRxStart", E_AMT);

static int AmtRxStop(   int argc, char *argv[])
{
    AmtMacbypRxStop(argc, argv);

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_rx_stop, AmtRxStop, "amtRxStop", "amtRxStop", E_AMT);

static int AmtSetMac(   int argc, char *argv[])
{
    if (AmtRfSetMac(argc, argv)) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_set_mac, AmtSetMac, "AmtSetMac", "AmtSetMac", E_AMT);

static int AmtGetMac(   int argc, char *argv[])
{
    if (AmtRfGetMac(argc, argv)) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_get_mac, AmtGetMac, "AmtGetMac", "AmtGetMac", E_AMT);

static int AmtSet(   int argc, char *argv[])
{
    if (AmtRfSet(argc, argv)) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_set, AmtSet, "amtSet", "amtSet", E_AMT);

static int AmtGet(   int argc, char *argv[])
{
    if (AmtRfGet(argc, argv)) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_get, AmtGet, "amtGet", "amtGet", E_AMT);

static int AmtSetLOLeakage(   int argc, char *argv[])
{
    if (AmtLOLeakage(argc, argv)) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_set_lo_leakage, AmtSetLOLeakage, "amt_set_lo_leakage", "amt_set_lo_leakage", E_AMT);

#ifndef NX_ESWIN_SDIO
static int AmtSetFinish(   int argc, char *argv[])
{
    if (AmtSetRfFinish(argc, argv)) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_set_finish_flag, AmtSetFinish, "AmtSetFinish", "AmtSetFinish", E_AMT);

static int AmtGetFinish(   int argc, char *argv[])
{
    if (AmtGetRfFinish(argc, argv)) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_get_finish_flag, AmtGetFinish, "AmtGetFinish", "AmtGetFinish", E_ALL);
#endif

static int AmtSetCfo(   int argc, char *argv[])
{
    int ret;

    ret = AmtRfSetCfo(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_set_cfo, AmtSetCfo, "AmtSetCfo", "AmtSetCfo", E_AMT);

static int AmtSetFreqOffset(   int argc, char *argv[])
{
    int ret;

    ret = AmtRfSetFreqOffset(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_set_freqoffset, AmtSetFreqOffset, "AmtSetFreqOffset", "AmtSetFreqOffset", E_AMT);

static int AmtGetFreqOffset(   int argc, char *argv[])
{
    int ret;

    ret = AmtRfGetFreqOffset();
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_get_freqoffset, AmtGetFreqOffset, "AmtGetFreqOffset", "AmtGetFreqOffset", E_AMT);

static int AmtSetFreqOffsetFlag(   int argc, char *argv[])
{
    int ret;

    ret = AmtRfSetFreqOffsetFlag(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_set_freqoffset_flag, AmtSetFreqOffsetFlag, "AmtSetFreqOffsetFlag", "AmtSetFreqOffsetFlag", E_AMT);

static int AmtGetFreqOffsetFlag(   int argc, char *argv[])
{
    int ret;

    ret = AmtRfGetFreqOffsetFlag();
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_get_freqoffset_flag, AmtGetFreqOffsetFlag, "AmtGetFreqOffsetFlag", "AmtGetFreqOffsetFlag", E_AMT);

static int AmtSetPaBias(   int argc, char *argv[])
{
    int ret;

    ret = AmtRfSetPaBias(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_set_pa_bias, AmtSetPaBias, "AmtSetPaBias", "AmtSetPaBias", E_AMT);

#ifdef NX_ESWIN_SDIO
static int AmtSetDeltaGain(   int argc, char *argv[])
{
    int ret;

    ret = AmtRfSetTxDeltaGain(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_set_delta_gain, AmtSetDeltaGain, "AmtSetDeltaGain", "AmtSetDeltaGain", E_AMT);

static int AmtSaveResult(   int argc, char *argv[])
{
    int ret;

    ret = AmtRfSaveResult(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_save_result, AmtSaveResult, "AmtSaveResult", "AmtSaveResult", E_AMT);

static int AmtGetResult(   int argc, char *argv[])
{
    int ret;

    ret = AmtRfGetResult(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_get_result, AmtGetResult, "AmtGetResult", "AmtGetResult", E_AMT);
#endif // NX_ESWIN_SDIO
#endif

#if 0
static int AmtSetFreqOffset(   int argc, char *argv[])
{

    int ret;

    os_printf(LM_CMD, LL_INFO, "AmtSetFreqOffset Enter !\n");
    ret = AmtRfSetFreqOffset(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD(amtSetFreqOffset, AmtSetFreqOffset, "amtSetFreqOffset", "amtSetFreqOffset");

static int AmtGetFreqOffset(   int argc, char *argv[])
{
    int ret;

    os_printf(LM_CMD, LL_INFO, "AmtGetFreqOffset Enter !\n");
    ret = AmtRfGetFreqOffset();
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD(amtGetFreqOffset, AmtGetFreqOffset, "amtGetFreqOffset", "amtGetFreqOffset");

static int AmtSetFreqOffsetFlag(   int argc, char *argv[])
{

    int ret;

    os_printf(LM_CMD, LL_INFO, "AmtSetFreqOffsetFlag Enter !\n");
    ret = AmtRfSetFreqOffsetFlag(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD(amtSetFreqOffsetFlag, AmtSetFreqOffsetFlag, "amtSetFreqOffsetFlag", "amtSetFreqOffsetFlag");

static int AmtGetFreqOffsetAndFlag(   int argc, char *argv[])
{

    int ret;

    os_printf(LM_CMD, LL_INFO, "AmtGetFreqOffsetAndFlag Enter!\n");
    ret = AmtRfGetFreqOffsetAndFlag();
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD(amtGetFreqOffsetAndFlag, AmtGetFreqOffsetAndFlag, "amtGetFreqOffsetAndFlag", "amtGetFreqOffsetAndFlag");

static int AmtSetTxPowerCal(   int argc, char *argv[])
{

    int ret;

    os_printf(LM_CMD, LL_INFO, "AmtSetTxPowerCal Enter !\n");
    AmtRfSaveTxPower(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD(amtSetTxPowerCal, AmtSetTxPowerCal, "amtSetTxPowerCal", "amtSetTxPowerCal");

static int AmtSetTxPowerCalFlag(   int argc, char *argv[])
{

    int ret;

    os_printf(LM_CMD, LL_INFO, "AmtSetTxPowerCalFlag Enter !\n");
    ret = AmtRfSetTxPowerCalFlag(argc, argv);
    if (ret != CMD_RET_SUCCESS) {
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_CMD(amtSetTxPowerCalFlag, AmtSetTxPowerCalFlag, "AmtSetTxPowerCalFlag", "AmtSetTxPowerCalFlag");

static int AmtGetTxPowerCalFlag(   int argc, char *argv[])
{

    int txPowerFlag;
    os_printf(LM_CMD, LL_INFO, "AmtGetTxPowerCalFlag Enter !\n");
    txPowerFlag = AmtRfGetTxPowerCalFlag();
    os_printf(LM_CMD, LL_INFO, "%d,", txPowerFlag);

    return CMD_RET_SUCCESS;
}
CLI_CMD(amtGetTxPowerCalFlag, AmtGetTxPowerCalFlag, "amtGetTxPowerCalFlag", "amtGetTxPowerCalFlag");

static int AmtTxDC(   int argc, char *argv[])
{


    AmtdumpDC(argc, argv);

    return CMD_RET_SUCCESS;
}
CLI_CMD(amt_tx_dc, AmtTxDC, "amtTxdc", "amtTxdc");

static int AmtTxSingletone(   int argc, char *argv[])
{


    AmtTxSingletone1M(argc, argv);

    return CMD_RET_SUCCESS;
}
CLI_CMD(amt_tx_1M, AmtTxSingletone, "amtTxsingletone", "amtTxsingletone");

#endif
#if !CONFIG_WIFI_FHOST && !defined(NX_ESWIN_SDIO)
static int AmtTxDC(   int argc, char *argv[])
{


    AmtdumpDC(argc, argv);

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_tx_dc, AmtTxDC, "amtTxdc", "amtTxdc", E_AMT);

static int AmtTxSingletone(   int argc, char *argv[])
{


    AmtTxSingletone1M(argc, argv);

    return CMD_RET_SUCCESS;
}
CLI_CMD_M(amt_tx_1M, AmtTxSingletone, "amtTxsingletone", "amtTxsingletone", E_AMT);
#endif
