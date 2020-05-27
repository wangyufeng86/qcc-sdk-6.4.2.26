/*!
\copyright  Copyright (c) 2019  Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    
\file
\brief      Kymera AEC leakthrough code
*/
#ifdef INCLUDE_AEC_LEAKTHROUGH
#include <audio_clock.h>
#include <audio_power.h>
#include <vmal.h>
#include <file.h>
#include <cap_id_prim.h>
#include <opmsg_prim.h>

#include "av_headset_kymera_private.h"
#include "av_headset_kymera.h"
#include "av_headset_av.h"
#include "chains/chain_leakthrough_aec.h"


/* Configuration */
#define RATE (16000U)
#define AEC_TASK_PERIOD (2000U) //2msec
#define AEC_DECIM_FACTOR (1U) //sidetone path task period: AEC_TASK_PERIOD/AEC_DECIM_FACTOR i.e., 2msec/1 => 2msec

/* Operator terminals */
#define AEC_REF_SPK_L_INPUT_TERMINAL (0U)
#define AEC_REF_MIC1_INPUT_TERMINAL (2U)
#define AEC_REF_SPK_L_OUTPUT_TERMINAL (1U)
#define AEC_REF_MIC1_OUTPUT_TERMINAL (3U)
#define AEC_REF_MIC_REF_OUTPUT_TERMINAL (0U)

#define PT_SPK_PATH_INPUT_TERMINAL (0U)
#define PT_SPK_PATH_OUTPUT_TERMINAL (0U)

#define PT_MIC_PATH_INPUT_TERMINAL (0U)
#define PT_MIC_PATH_OUTPUT_TERMINAL (0U)

/* size of LEAKTHROUGH message in bytes. */
#define LEAKTHROUGH_MSG_SIZE                  2
/*! Byte offset to LEAKTHROUGH state and Mode type field. */
#define LEAKTHROUGH_STATE_MODE_TYPE_OFFSET    0

/*The value 0xFFFFFFF3UL and 0x2128ACA9UL corresponds to value of -90dB and were found out using QACT*/
#define SIDETONE_EXP_MINIMUM (0xFFFFFFF3UL)
#define SIDETONE_MANTISSA_MINIMUM (0x2128ACA9UL)


/*The value 0x00000001UL and 0x40000000 corresponds to value of 0dB and were found out using QACT*/
#define SIDETONE_EXP_DEFAULT (0x00000001UL)
#define SIDETONE_MANTISSA_DEFAULT (0x40000000UL)


typedef enum
{
    LEAKTHROUGH_DISABLE,
    LEAKTHROUGH_ENABLE,
    LEAKTHROUGH_SET_MODE1,
    LEAKTHROUGH_SET_MODE2,
    LEAKTHROUGH_SET_MODE3,
} kymera_leakthrough_event;

uint32 leakthrough_mic_sample_rate = MIC_RATE;

static uint32 appKymerGetLeakthroughMicSampleRate(void)
{
    return leakthrough_mic_sample_rate;
}


/* Send the LEAKTHROUGH command to peer */
static void appKymeraLeakthroughSynchronizeWithPeer(const uint16 leakthrough_cmd)
{
    DEBUG_LOGF("appKymeraLeakthroughSynchronizeWithPeer send leakthrough_cmd %x ", leakthrough_cmd);

    peerSigTaskData *peer_sig = appGetPeerSig();
    appKymeraLeakthroughTaskData *data = appGetLeakthroughDataLocal();
    uint8 leakthrough_message[LEAKTHROUGH_MSG_SIZE] = {0};

    data->leakthrough_cmd = leakthrough_cmd; /* store local command sent to peer */
    leakthrough_message[LEAKTHROUGH_STATE_MODE_TYPE_OFFSET] = leakthrough_cmd & 0xFF;
    leakthrough_message[LEAKTHROUGH_STATE_MODE_TYPE_OFFSET + 1] = (leakthrough_cmd >> 8) & 0xFF;

    appPeerSigMsgChannelTxRequest(&data->task,
                                      &peer_sig->peer_addr,
                                      PEER_SIG_MSG_CHANNEL_LEAKTHROUGH,
                                      leakthrough_message,
                                      LEAKTHROUGH_MSG_SIZE);
}

/*! \brief Handle LEAKTHROUGH message channel Rx indication received at peer */
static void appKymeraLeakthroughHandlePeerSigMsgRxInd(const PEER_SIG_MSG_CHANNEL_RX_IND_T *ind)
{
    DEBUG_LOGF("appKymeraLeakthroughHandlePeerSigMsgRxInd");

    if (ind->msg_size != LEAKTHROUGH_MSG_SIZE)
    {
        DEBUG_LOGF("appKymeraLeakthroughHandlePeerSigMsgRxInd, bad RX len %u", ind->msg_size);
        return;
    }

    uint16 leakthrough_payload = ind->msg[LEAKTHROUGH_STATE_MODE_TYPE_OFFSET] + ((uint16)ind->msg[LEAKTHROUGH_STATE_MODE_TYPE_OFFSET+1] << 8);
    if (GET_LEAKTHROUGH_LINKLOSS_STATE(leakthrough_payload))
    {
        /* If link loss, then peer sync both LEAKTHROUGH State and Mode */
        appKymeraHandleLeakThroughSynchronization(GET_LEAKTHROUGH_ON_STATE(leakthrough_payload), KYMERA_LEAKTHROUGH_NO_DELAY);
        /* Mode can be set in LEAKTHROUGH_ON state only */
        if (appKymeraIsLeakthroughEnabled())
        {
            appKymeraHandleLeakThroughSynchronization(GET_LEAKTHROUGH_MODE(((leakthrough_payload >> BIT_POS_LEAKTHROUGH_MODE) & 0x000f)),
                                       KYMERA_LEAKTHROUGH_NO_DELAY);
        }
    }
    else
    {
        appKymeraHandleLeakThroughSynchronization(leakthrough_payload, KYMERA_LEAKTHROUGH_DELAY);
    }
}

/*! \brief Handle LEAKTHROUGH message channel data tx confirmation from remote peer */
static void appKymeraLeakthroughHandlePeerSigMsgTxCfm(const PEER_SIG_MSG_CHANNEL_TX_CFM_T *cfm)
{
    DEBUG_LOGF("appKymeraLeakthroughHandlePeerSigMsgTxCfm status:%d\n", cfm->status);

    appKymeraLeakthroughTaskData *data = appGetLeakthroughDataLocal();

    if (cfm->status == peerSigStatusSuccess && !GET_LEAKTHROUGH_LINKLOSS_STATE(data->leakthrough_cmd))
    {
        appKymeraHandleLeakThroughSynchronization(data->leakthrough_cmd, KYMERA_LEAKTHROUGH_NO_DELAY);
    }
    else
    {
        DEBUG_LOGF("Failed to process LEAKTHROUGH payload: %u", data->leakthrough_cmd);
    }
}

/* Update LEAKTHROUGH payload following reconnect after linkloss to sync state, mode from active local peer */
static void appKymeraLeakthroughHandlePeerSigConnectInd(const PEER_SIG_CONNECTION_IND_T* ind)
{
    DEBUG_LOGF("appKymeraLeakthroughHandlePeerSigConnectInd status:%d\n", ind->status);

    peerSigTaskData *peer_sig = appGetPeerSig();

    if (ind->status == peerSigStatusConnected && peer_sig->link_loss_occurred)
    {
        appKymeraLeakthroughTaskData *data = appGetLeakthroughDataLocal();
        uint16 leakthrough_payload = (peer_sig->link_loss_occurred << BIT_POS_LEAKTHROUGH_STATE_LINK_LOSS) |
                             (data->leakthrough_mode << BIT_POS_LEAKTHROUGH_MODE) |
                             (GET_LEAKTHROUGH_ON_STATE(data->leakthrough_state_on) << BIT_POS_LEAKTHROUGH_STATE);

        DEBUG_LOGF("Send current LEAKTHROUGH state & mode value:%x to peer after link-loss reconnect", leakthrough_payload);
        appKymeraLeakthroughSynchronizeWithPeer(leakthrough_payload);
    }
}

/* get corresponding Kymera events based on LEAKTHROUGH commands */
static kymera_leakthrough_event appKymeraLeakthroughGetInternalEvent(uint16 leakthrough_cmd)
{
    kymera_leakthrough_event leakthroughEvent;
    appKymeraLeakthroughTaskData *data = appGetLeakthroughDataLocal();

    switch(leakthrough_cmd)
    {
        case LEAKTHROUGH_DISABLE:
           leakthroughEvent = KYMERA_INTERNAL_LEAKTHROUGH_OFF;
           data->leakthrough_state_on = FALSE;
           break;

        case LEAKTHROUGH_ENABLE:
           leakthroughEvent = KYMERA_INTERNAL_LEAKTHROUGH_ON;
           data->leakthrough_state_on = TRUE;
           break;

        case LEAKTHROUGH_SET_MODE1:
           leakthroughEvent = KYMERA_INTERNAL_LEAKTHROUGH_SET_MODE;
           data->leakthrough_mode = LEAKTHROUGH_MODE_1;
           break;

        case LEAKTHROUGH_SET_MODE2:
           leakthroughEvent = KYMERA_INTERNAL_LEAKTHROUGH_SET_MODE;
           data->leakthrough_mode = LEAKTHROUGH_MODE_2;
           break;

        case LEAKTHROUGH_SET_MODE3:
           leakthroughEvent = KYMERA_INTERNAL_LEAKTHROUGH_SET_MODE;
           data->leakthrough_mode = LEAKTHROUGH_MODE_3;
           break;

        default:
           leakthroughEvent = KYMERA_INTERNAL_LEAKTHROUGH_OFF;
           data->leakthrough_state_on = FALSE;
           break;
		   
    }
    DEBUG_LOGF("LEAKTHROUGH COMMAND RECEIVED IS %d",leakthroughEvent);
    return leakthroughEvent;
}

/*! \brief Message Handler
    This function is the main message handler for the Kymera LEAKTHROUGH module.
*/
static void appKymeraLeakthroughHandleMessage(Task task, MessageId id, Message message)
{
    UNUSED(task);

    switch (id)
    {
       case PEER_SIG_MSG_CHANNEL_RX_IND:
           appKymeraLeakthroughHandlePeerSigMsgRxInd((const PEER_SIG_MSG_CHANNEL_RX_IND_T *)message);
           break;

       case PEER_SIG_MSG_CHANNEL_TX_CFM:
           appKymeraLeakthroughHandlePeerSigMsgTxCfm((const PEER_SIG_MSG_CHANNEL_TX_CFM_T *)message);
           break;

       case PEER_SIG_CONNECTION_IND:
           appKymeraLeakthroughHandlePeerSigConnectInd((const PEER_SIG_CONNECTION_IND_T *)message);
           break;

       case KYMERA_INTERNAL_LEAKTHROUGH_SIDETONE_ENABLE:
            appKymeraLeakthroughEnableAecSideTonePath(TRUE);
            break;

        default:
            DEBUG_LOGF("appKymeraLeakthroughHandleMessage: (%x) unhandled",id);
            break;
    }
}


static void appKymeraConnectLeakthroughMic(void)
{
    Source mic_in0;
    uint8 mic0;
    Operator op_aec;
    uint32 sample_rate;
    kymeraTaskData *theKymera = appGetKymera();

    sample_rate=appKymerGetLeakthroughMicSampleRate();

    op_aec = ChainGetOperatorByRole(theKymera->chain_leakthrough_handle,OPR_LEAKTHROUGH_AEC);

    /* Configure leakthrough microphone */
    mic0 = appConfigLeakthroughMic();
    appKymeraMicSetup(mic0, &mic_in0, NO_MIC, NULL, sample_rate);

    /* Connect mic source to aec_ref operator mic1 input terminal */
    DEBUG_LOG("Connect mic source to aec_ref operator mic1 input terminal");
    PanicFalse(StreamConnect(mic_in0, StreamSinkFromOperatorTerminal(op_aec, AEC_REF_MIC1_INPUT_TERMINAL)));
}

static void appKymeraDisconnectLeakthroughMic(void)
{
    Source mic_in0;
    uint8 mic0=appConfigLeakthroughMic();

    appKymeraMicSetup(mic0, &mic_in0, NO_MIC, NULL, RATE);
    /* Disconnect Mic */
    StreamDisconnect(mic_in0, 0);
}

/* In SCO aec ucid two dimensional array,
 * the first dimension maps to leakthrough mode(appKymeraLeakthroughMode) and
 * second dimension maps to negotiated SCO codec (appKymeraScoMode).
 * Note: Both indices 0 and 2 of second dimension are being used for wide-band due to current limitation in SCO modes */
const uint8 leakthrough_sco_aec_ucid[appConfigMaxLeakthroughModes()][SCO_UWB+1] = {{UCID_AEC_WB_LEAKTHROUGH_MODE_1, UCID_AEC_NB_LEAKTHROUGH_MODE_1, UCID_AEC_WB_LEAKTHROUGH_MODE_1, UCID_AEC_SWB_LEAKTHROUGH_MODE_1, UCID_AEC_UWB_LEAKTHROUGH_MODE_1},
                             {UCID_AEC_WB_LEAKTHROUGH_MODE_2, UCID_AEC_NB_LEAKTHROUGH_MODE_2, UCID_AEC_WB_LEAKTHROUGH_MODE_2, UCID_AEC_SWB_LEAKTHROUGH_MODE_2, UCID_AEC_UWB_LEAKTHROUGH_MODE_2},
                             {UCID_AEC_WB_LEAKTHROUGH_MODE_3, UCID_AEC_NB_LEAKTHROUGH_MODE_3, UCID_AEC_WB_LEAKTHROUGH_MODE_3, UCID_AEC_SWB_LEAKTHROUGH_MODE_3, UCID_AEC_UWB_LEAKTHROUGH_MODE_3} };

const uint8 leakthrough_aec_ucid[appConfigMaxLeakthroughModes()] = { UCID_AEC_LEAKTHROUGH_MODE_1, UCID_AEC_LEAKTHROUGH_MODE_2, UCID_AEC_LEAKTHROUGH_MODE_3};

static void appKymeraUpdateStandaloneChainStatus(bool status);
static void appKymeraUpdateLeakthroughMode(appKymeraLeakthroughMode mode);

bool leakthrough_status=DISABLED;
bool leakthrough_standaloneChainStatus=DISABLED;

appKymeraLeakthroughMode leakthrough_mode = LEAKTHROUGH_MODE_1;

appKymeraLeakthroughMode appKymeraLeakthroughGetMode(void)
{
    DEBUG_LOG("appKymeraLeakthroughGetMode: current leakthrough mode is %u",leakthrough_mode);
    return leakthrough_mode;
}

static void appKymeraUpdateLeakthroughMode(appKymeraLeakthroughMode mode)
{
    DEBUG_LOG("appKymeraUpdateLeakthroughMode");
    leakthrough_mode = mode;
}

static void appKymeraUpdateStandaloneChainStatus(bool status)
{
    DEBUG_LOG("appKymeraUpdateStandaloneChainStatus");
    leakthrough_standaloneChainStatus = status;
}

static bool appKymeraIsScoStreamingActive(void)
{
    DEBUG_LOG("appKymeraIsScoStreamingActive ");
    kymeraTaskData *theKymera = appGetKymera();

    if((theKymera->state == KYMERA_STATE_SCO_ACTIVE)|| (theKymera->state == KYMERA_STATE_SCO_ACTIVE_WITH_FORWARDING) || (theKymera->state == KYMERA_STATE_SCO_SLAVE_ACTIVE) )
    {
        return TRUE;
    }

    return FALSE;
}

static void appKymeraLeakthroughSetDefaultSidetoneGain(void)
{
    kymeraTaskData *theKymera = appGetKymera();
    Operator aec_ref;
    DEBUG_LOG("appKymeraLeakthroughSetDefaultSidetoneGain");

    if(appKymeraIsScoStreamingActive())
    {
        aec_ref = ChainGetOperatorByRole(appKymeraGetScoChain(), OPR_SCO_AEC);
    }
    else
    {
        aec_ref = ChainGetOperatorByRole(theKymera->chain_leakthrough_handle, OPR_LEAKTHROUGH_AEC);
    }

    if(aec_ref)
    {
        OperatorsAecSetSidetoneGain(aec_ref,SIDETONE_EXP_DEFAULT,SIDETONE_MANTISSA_DEFAULT);
    }
    else
    {
        DEBUG_LOG("Sidetone gain is not updated");
    }
}

void appKymeraUpdateLeakthroughMicSampleRate(uint32 sample_rate)
{
    leakthrough_mic_sample_rate = sample_rate;
}

void appKymeraLeakthroughSetMinSidetoneGain(void)
{
    kymeraTaskData *theKymera = appGetKymera();
    Operator aec_ref;
    DEBUG_LOG("appKymeraLeakthroughSetMinSidetoneGain");

    if(appKymeraIsScoStreamingActive())
    {
        aec_ref = ChainGetOperatorByRole(appKymeraGetScoChain(), OPR_SCO_AEC);
    }
    else
    {
        aec_ref = ChainGetOperatorByRole(theKymera->chain_leakthrough_handle, OPR_LEAKTHROUGH_AEC);
    }

    if(aec_ref)
    {
        OperatorsAecSetSidetoneGain(aec_ref,SIDETONE_EXP_MINIMUM,SIDETONE_MANTISSA_MINIMUM);
    }
    else
    {
        DEBUG_LOG("Sidetone gain is not updated");
    }
}

void appKymeraEnableAecSideToneAfterTimeout(void)
{
    #define AEC_REF_SETTLING_TIME 100

    appKymeraLeakthroughTaskData *leakthrough = appGetLeakthroughDataLocal();
    MessageCancelAll(&leakthrough->task,KYMERA_INTERNAL_LEAKTHROUGH_SIDETONE_ENABLE);
    MessageSendLater(&leakthrough->task,KYMERA_INTERNAL_LEAKTHROUGH_SIDETONE_ENABLE,NULL,AEC_REF_SETTLING_TIME);
}


void appKymeraLeakThroughCreateChain(void)
{
    kymeraTaskData *theKymera = appGetKymera();
    appKymeraUpdateLeakThroughStatus(ENABLED);
    appKymeraUpdateStandaloneChainStatus(ENABLED);
    theKymera->output_rate = RATE;
    OperatorsFrameworkSetKickPeriod(KICK_PERIOD_LEATHROUGH);
    appKymeraCreateOutputChain(RATE,KICK_PERIOD_LEATHROUGH,0,0);
    appKymeraSetOperatorUcids(FALSE, NO_SCO);
    /* Enable external amplifier if required */
    appKymeraExternalAmpControl(TRUE);

    /* Configure DSP for low power */
    appKymeraConfigureDspPowerMode(FALSE);

    ChainStart(theKymera->chain_leakthrough_handle);
    ChainStart(theKymera->chainu.output_vol_handle);

    appKymeraEnableAecSideToneAfterTimeout();
}

void appKymeraLeakThroughDestroyChain(void)
{
    kymeraTaskData *theKymera = appGetKymera();

    DEBUG_LOG("appKymeraLeakThroughDestroyChain");

    /* Set Minimum sidetone gain for AEC ref Operator*/
    appKymeraLeakthroughSetMinSidetoneGain();

    /* Stop the chain */
    ChainStop(theKymera->chainu.output_vol_handle);
    ChainStop(theKymera->chain_leakthrough_handle);

    /* Disable the external amp */
    appKymeraExternalAmpControl(FALSE);

    /* Get the DAC */
    Sink DAC_SNK_L = (Sink)PanicFalse(StreamAudioSink(AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0, AUDIO_CHANNEL_A));

    /* Microphone sources */
    Source mic_in0;
    uint8 mic0;

    mic0 = appConfigLeakthroughMic();
    appKymeraMicSetup(mic0, &mic_in0, NO_MIC, NULL, RATE);

    /* Disconnect Mic */
    StreamDisconnect(mic_in0, 0);

    /* Disconnect Dac */
    StreamDisconnect(0, DAC_SNK_L);

    /* Destroy the operators */
    ChainDestroy(theKymera->chainu.output_vol_handle);
    ChainDestroy(theKymera->chain_leakthrough_handle);

    theKymera->chainu.output_vol_handle = NULL;
    theKymera->chain_leakthrough_handle = NULL;

    theKymera->output_rate = 0;
    appKymeraUpdateLeakThroughStatus(DISABLED);
    appKymeraUpdateStandaloneChainStatus(DISABLED);
    appKymeraSetState(KYMERA_STATE_IDLE);
}

/* Get corresponding internal Mode event based on LEKATHROUGH commands */
uint16 appKymeraLeakthroughGetModeEvent(appKymeraLeakthroughMode leakthrough_mode)
{
    kymera_leakthrough_event leakthroughModeEvent;
    switch(leakthrough_mode)
    {
        case LEAKTHROUGH_MODE_1:
           leakthroughModeEvent = LEAKTHROUGH_SET_MODE1;
           break;

        case LEAKTHROUGH_MODE_2:
           leakthroughModeEvent = LEAKTHROUGH_SET_MODE2;
           break;

        case LEAKTHROUGH_MODE_3:
           leakthroughModeEvent = LEAKTHROUGH_SET_MODE3;
           break;

        default:
           leakthroughModeEvent = LEAKTHROUGH_SET_MODE1;
           break;
    }
    return leakthroughModeEvent;
}


void appKymeraHandleLeakThroughSynchronization(const uint16 Leakthrough_cmd,uint16 delay)
{
    DEBUG_LOGF("appKymeraHandleLeakThroughSynchronization Leakthrough_cmd : %x", Leakthrough_cmd);
    /* Don't process leakthrough cmd when state is in-case and just store it */
    MessageId Leakthrough_event = appKymeraLeakthroughGetInternalEvent(Leakthrough_cmd);
    if (appSmIsOutOfCase())
    {
        kymeraTaskData *theKymera = appGetKymera();
        MessageSendLater(&theKymera->task, Leakthrough_event, NULL, delay);
    }

}

void appKymeraEnableLeakThrough(void)
{
    DEBUG_LOGF("appKymeraEnableLeakThrough");
    if(!appKymeraIsLeakthroughEnabled())
    {
        peerSigTaskData *peer_sig = appGetPeerSig();
        if (peer_sig->state == PEER_SIG_STATE_CONNECTED && (appSmIsOutOfCase()))
        {
            appKymeraLeakthroughSynchronizeWithPeer(LEAKTHROUGH_ENABLE);
        }
        else
        {
            appKymeraLeakthroughTaskData *data = appGetLeakthroughDataLocal();
            data->leakthrough_state_on = TRUE;
            appKymeraEnableLeakThroughLocally();
        }
    }
    else
    {
        DEBUG_LOGF("appKymeraEnableLeakThrough: Failed as not in LEAKTHROUGH_OFF state ");
    }
}

void appKymeraDisableLeakThrough(void)
{
    DEBUG_LOGF("appKymeraDisableLeakThrough");
    if(appKymeraIsLeakthroughEnabled())
    {
        peerSigTaskData *peer_sig = appGetPeerSig();
        appKymeraLeakthroughTaskData *leakthrough = appGetLeakthroughDataLocal();

        /* Disable any message requesting enabling the sidetone path*/
        MessageCancelAll(&leakthrough->task,KYMERA_INTERNAL_LEAKTHROUGH_SIDETONE_ENABLE);

        if (peer_sig->state == PEER_SIG_STATE_CONNECTED && (appSmIsOutOfCase()))
        {
            appKymeraLeakthroughSynchronizeWithPeer(LEAKTHROUGH_DISABLE);
        }
        else
        {
            if(appSmIsOutOfCase())
            {
                appKymeraLeakthroughTaskData *data = appGetLeakthroughDataLocal();
                data->leakthrough_state_on = FALSE;
            }
            appKymeraDisableLeakThroughLocally();
        }
    }
    else
    {
        DEBUG_LOGF("appKymeraDisableLeakThrough: Failed as not in LEAKTHROUGH_ON state ");
    }
}


void appKymeraLeakthroughSetMode(appKymeraLeakthroughMode mode)
{
    DEBUG_LOG("appKymeraLeakthroughSetMode Mode=%d", mode);
    if(appKymeraIsLeakthroughEnabled())
    {
        peerSigTaskData *peer_sig = appGetPeerSig();

        if (peer_sig->state == PEER_SIG_STATE_CONNECTED && (appSmIsOutOfCase()))
        {
            appKymeraLeakthroughSynchronizeWithPeer(GET_LEAKTHROUGH_MODE(mode));
        }
        else
        {
            appKymeraLeakthroughTaskData *data = appGetLeakthroughDataLocal();
            data->leakthrough_mode = mode;
            appKymeraLeakthroughSetModeLocally(mode);
        }
    }
    else
    {
        DEBUG_LOGF("appKymeraLeakthroughSetMode: Failed as not in LEAKTHROUGH_ON state ");
    }
}

void appKymeraLeakthroughSetModeLocally(appKymeraLeakthroughMode mode)
{
    DEBUG_LOG("appKymeraLeakthroughSetModeLocally Mode=%d", mode);

    appKymeraLeakthroughUpdateAecOperatorUcid(mode);

    /* update the mode */
    appKymeraUpdateLeakthroughMode(mode);

    /*Update the sidetone Path */
    appKymeraEnableAecSideToneAfterTimeout();

}

void appKymeraLeakthroughConfigureAecChain(kymera_chain_handle_t chain,uint32 sample_rate,uint16 aec_terminal_buffer_size)
{
    Operator op_aec;
    Operator op_mic_path_passthrough;

    /* Configure Aec Chain operators */
    op_aec = ChainGetOperatorByRole(chain,OPR_LEAKTHROUGH_AEC);
    OperatorsAecSetSampleRate(op_aec, sample_rate, sample_rate);
    appKymeraSetTerminalBufferSize(op_aec, sample_rate, aec_terminal_buffer_size, 1 << 0, 0);
    OperatorsAecSetSameInputOutputClkSource(op_aec, TRUE);

    op_mic_path_passthrough = ChainGetOperatorByRole(chain, OPR_LEAKTHROUGH_INPUT_SPC);
    /* Switched Passthrough Configuration */
    OperatorsStandardSetBufferSizeWithFormat(op_mic_path_passthrough, SPC_BUFFER_SIZE, operator_data_format_pcm);
    appKymeraConfigureSpcDataFormat(op_mic_path_passthrough, ADF_PCM);

    /*Configure AEC_REF task period*/
    OperatorsAecSetTaskPeriod(op_aec, AEC_TASK_PERIOD, AEC_DECIM_FACTOR);

    /*Update the mic sample rate*/
    appKymeraUpdateLeakthroughMicSampleRate(sample_rate);
    if(appKymeraIsLeakthroughEnabled())
    {
        appKymeraConnectLeakthroughMic();
    }

}

void appKymeraEnableLeakThroughLocally(void)
{
    DEBUG_LOG("appKymeraEnableLeakThroughLocally");
    kymeraTaskData *theKymera = appGetKymera();

    switch(theKymera->state)
    {
        case KYMERA_STATE_IDLE:
             MessageSendConditionally(&theKymera->task,KYMERA_INTERNAL_AEC_LEAKTHROUGH_CREATE_STANDALONE_CHAIN,NULL,&theKymera->leakthrough_lock);
        break;

        case KYMERA_STATE_A2DP_STREAMING:
        case KYMERA_STATE_A2DP_STREAMING_WITH_FORWARDING:
            appKymeraConnectLeakthroughMic();
            //fallthrough as microphone is already connected in sco case.
        case KYMERA_STATE_SCO_ACTIVE:
        case KYMERA_STATE_SCO_ACTIVE_WITH_FORWARDING:
        case KYMERA_STATE_SCO_SLAVE_ACTIVE:
            appKymeraUpdateLeakThroughStatus(ENABLED);
            appKymeraLeakthroughSetDefaultSidetoneGain();
            appKymeraLeakthroughUpdateAecOperatorUcid(appKymeraLeakthroughGetMode());
            appKymeraEnableAecSideToneAfterTimeout();

        break;

        default:
        break;
    }

}

void appKymeraDisableLeakThroughLocally(void)
{
    DEBUG_LOG("appKymeraDisableLeakThroughLocally");
    kymeraTaskData *theKymera = appGetKymera();

    switch (theKymera->state)
    {
        case KYMERA_STATE_STANDALONE_LEAKTHROUGH:
            MessageSendConditionally(&theKymera->task,KYMERA_INTERNAL_AEC_LEAKTHROUGH_DESTROY_STANDALONE_CHAIN,NULL,&theKymera->leakthrough_lock);
        break;

        case KYMERA_STATE_A2DP_STREAMING:
        case KYMERA_STATE_A2DP_STREAMING_WITH_FORWARDING:
            appKymeraLeakthroughSetMinSidetoneGain();
            appKymeraLeakthroughEnableAecSideTonePath(FALSE);
            appKymeraDisconnectLeakthroughMic();
            appKymeraUpdateLeakThroughStatus(DISABLED);
        break;

        case KYMERA_STATE_SCO_ACTIVE:
        case KYMERA_STATE_SCO_ACTIVE_WITH_FORWARDING:
        case KYMERA_STATE_SCO_SLAVE_ACTIVE:
            appKymeraLeakthroughSetMinSidetoneGain();
            appKymeraLeakthroughEnableAecSideTonePath(FALSE);
            appKymeraUpdateLeakThroughStatus(DISABLED);
        break;

        default:
        break;
    }
}


void appKymeraConfigureSpc(kymera_chain_handle_t chain, unsigned chain_role, uint16 buffer_size)
{
    Operator spc_op = ChainGetOperatorByRole(chain, chain_role);

    if(spc_op)
    {
        OperatorsStandardSetBufferSizeWithFormat(spc_op,buffer_size,operator_data_format_pcm);
        appKymeraConfigureSpcDataFormat(spc_op,ADF_PCM);
        appKymeraConfigureSpcMode(spc_op,FALSE);
    }
}

void appKymeraConfigureBpt(kymera_chain_handle_t chain, unsigned chain_role, uint16 buffer_size)
{
    Operator op_bpt = ChainGetOperatorByRole(chain,chain_role);

    if(op_bpt)
    {
        OperatorsStandardSetBufferSizeWithFormat(op_bpt,buffer_size,operator_data_format_pcm);
    }
}

void appKymeraUpdateLeakThroughStatus(bool status)
{
    DEBUG_LOG("appKymeraUpdateLeakThroughStatus ");
    leakthrough_status = status;
}

bool appKymeraIsLeakthroughEnabled(void)
{
    DEBUG_LOG("appKymeraIsLeakthroughEnabled ");
    return leakthrough_status;
}

bool appKymeraIsStandaloneLeakthroughActive(void)
{
    DEBUG_LOG("appKymeraIsStandaloneLeakthroughActive");
    return leakthrough_standaloneChainStatus;
}

void appKymeraLeakthroughStopChainIfRunning(void)
{
    if(appKymeraIsStandaloneLeakthroughActive())
    {
        appKymeraLeakThroughDestroyChain();
        appKymeraUpdateLeakThroughStatus(ENABLED);
    }
}

void appKymeraLeakthroughResumeChainIfSuspended(void)
{
    kymeraTaskData *theKymera = appGetKymera();
    DEBUG_LOG("appKymeraLeakthroughResumeChainIfSuspended");
    DEBUG_LOG("appKymeraState is %u", theKymera->state);
    if(appKymeraIsLeakthroughEnabled() && (theKymera->state == KYMERA_STATE_IDLE))
    {
        appKymeraCreateStandAloneChain();
        appKymeraSetState(KYMERA_STATE_STANDALONE_LEAKTHROUGH);
    }
}

void appKymeraCreateStandAloneChain(void)
{
    appKymeraLeakThroughCreateChain();
}

void appKymeraLeakthroughEnableAecSideTonePath(bool enable)
{
    kymeraTaskData *theKymera = appGetKymera();
    Operator aec_ref;

    if(appKymeraIsScoStreamingActive())
    {
        aec_ref = ChainGetOperatorByRole(appKymeraGetScoChain(), OPR_SCO_AEC);
    }
    else
    {
        aec_ref = ChainGetOperatorByRole(theKymera->chain_leakthrough_handle, OPR_LEAKTHROUGH_AEC);
    }

    if(aec_ref)
    {
        OperatorsAecEnableSidetonePath(aec_ref, enable);
    }
}


void appKymeraLeakthroughUpdateAecOperatorUcid(appKymeraLeakthroughMode mode)
{
    if(appKymeraIsLeakthroughEnabled())
    {
        uint8 ucid;
        kymeraTaskData *theKymera = appGetKymera();
        Operator aec_ref;

        if(appHfpIsScoActive())
        {
            aec_ref = ChainGetOperatorByRole(theKymera->chainu.sco_handle,OPR_SCO_AEC);
            appKymeraScoMode sco_mode = theKymera->sco_info->mode;
            ucid = leakthrough_sco_aec_ucid[mode][sco_mode];
        }
        /* A2dp or Standalone leakthrough active */
        else
        {
            aec_ref = ChainGetOperatorByRole(theKymera->chain_leakthrough_handle,OPR_LEAKTHROUGH_AEC);
            ucid = leakthrough_aec_ucid[mode];
        }

        if(aec_ref)
        {
            DEBUG_LOG("appKymeraLeakthroughUpdateAecOperatorUcid, ucid = %d", ucid);
            OperatorsStandardSetUCID(aec_ref, ucid);
        }
        else
        {
            DEBUG_LOG("appKymeraLeakthroughUpdateAecOperatorUcid, ignored");
        }
    }
}

void appKymeraLeakthroughInit(void)
{
    appKymeraLeakthroughTaskData *data = appGetLeakthroughDataLocal();
    data->task.handler = appKymeraLeakthroughHandleMessage;

    /* Register a channel for peer signalling */
    appPeerSigMsgChannelTaskRegister(&data->task, PEER_SIG_MSG_CHANNEL_LEAKTHROUGH);

    /* Register for peer signalling notifications */
    appPeerSigClientRegister(&data->task);
}

#endif
