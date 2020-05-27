/*!
\copyright  Copyright (c) 2017-2018  Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    
\file
\brief      Kymera ANC code
*/

#include <audio_clock.h>
#include <audio_power.h>
#include <vmal.h>
#include <file.h>
#include <cap_id_prim.h>
#include <opmsg_prim.h>

#include "av_headset_kymera_private.h"
#include "av_headset_peer_signalling.h"


#define ANC_TUNING_SINK_USB_LEFT      0 /*can be any other backend device. PCM used in this tuning graph*/
#define ANC_TUNING_SINK_USB_RIGHT     1
#define ANC_TUNING_SINK_FBMON_LEFT    2 /*reserve slots for FBMON tap. Always connected.*/
#define ANC_TUNING_SINK_FBMON_RIGHT   3
#define ANC_TUNING_SINK_MIC1_LEFT     4 /* must be connected to internal ADC. Analog or digital */
#define ANC_TUNING_SINK_MIC1_RIGHT    5
#define ANC_TUNING_SINK_MIC2_LEFT     6
#define ANC_TUNING_SINK_MIC2_RIGHT    7

#define ANC_TUNING_SOURCE_USB_LEFT    0 /*can be any other backend device. USB used in the tuning graph*/
#define ANC_TUNING_SOURCE_USB_RIGHT   1
#define ANC_TUNING_SOURCE_DAC_LEFT    2 /* must be connected to internal DAC */
#define ANC_TUNING_SOURCE_DAC_RIGHT   3


/*! Macro for creating messages */
#define MAKE_KYMERA_MESSAGE(TYPE) \
    TYPE##_T *message = PanicUnlessNew(TYPE##_T);

/* size of ANC message in bytes.
   byte0: [State+Mode+linkloss value] .... byte1: [Gain value] */
#define ANC_MSG_SIZE                  2
/*! Byte offset to ANC state and Mode type field. */
#define ANC_STATE_MODE_TYPE_OFFSET    0
/*! Byte offset to ANC gain parameter. */
#define ANC_GAIN_TYPE_OFFSET    1

#define ANC_BIT_MASK_MODE             0x000F
#define ANC_BIT_MASK_CMD              0x00FF
#define ANC_BIT_MASK_GAIN_VALUE       0xFF00

typedef enum
{
    ANC_DISABLE,
    ANC_ENABLE,
    ANC_SET_GAIN,
    ANC_SET_MODE1,
    ANC_SET_MODE2,
    ANC_SET_MODE3,
    ANC_SET_MODE4,
    ANC_SET_MODE5,
    ANC_SET_MODE6,
    ANC_SET_MODE7,
    ANC_SET_MODE8,
    ANC_SET_MODE9,
    ANC_SET_MODE10,
} kymera_anc_event;


/* Send the ANC command to peer */
static void appKymeraAncSynchronizeWithPeer(const uint16 anc_cmd)
{
    DEBUG_LOGF("appKymeraAncSynchronizeWithPeer send anc_cmd %x ", anc_cmd);

    peerSigTaskData *peer_sig = appGetPeerSig();

    if (peer_sig->state == PEER_SIG_STATE_CONNECTED)
    {
        appKymeraAncTaskData *data = appGetAncDataLocal();
        uint8 anc_message[ANC_MSG_SIZE] = {0};

        data->anc_cmd = anc_cmd; /* store local command sent to peer */
        anc_message[ANC_STATE_MODE_TYPE_OFFSET] = anc_cmd & ANC_BIT_MASK_CMD;
        anc_message[ANC_GAIN_TYPE_OFFSET] = (anc_cmd >> BIT_POS_ANC_GAIN) & ANC_BIT_MASK_CMD;

        appPeerSigMsgChannelTxRequest(&data->task,
                                      &peer_sig->peer_addr,
                                      PEER_SIG_MSG_CHANNEL_ANC,
                                      anc_message,
                                      ANC_MSG_SIZE);
    }
    else
    {
        appKymeraAncProcessCommand(anc_cmd, KYMERA_ANC_NO_DELAY);
    }
}

/*! \brief Handle ANC message channel Rx indication received at peer */
static void appKymeraAncHandlePeerSigMsgRxInd(const PEER_SIG_MSG_CHANNEL_RX_IND_T *ind)
{
    DEBUG_LOGF("appKymeraAncHandlePeerSigMsgRxInd");

    if (ind->msg_size != ANC_MSG_SIZE)
    {
        DEBUG_LOGF("appKymeraAncHandlePeerSigMsgRxInd, bad RX len %u", ind->msg_size);
        return;
    }

    uint16 anc_payload = ind->msg[ANC_STATE_MODE_TYPE_OFFSET] + ((uint16)ind->msg[ANC_GAIN_TYPE_OFFSET] << BIT_POS_ANC_GAIN);
    if (GET_ANC_LINKLOSS_STATE(anc_payload))
    {
        /* If link loss, then peer sync ANC State, Mode and Gain parameters */
        appKymeraAncProcessCommand(GET_ANC_ON_STATE(anc_payload), KYMERA_ANC_NO_DELAY);
        /* Mode and Gain can be set in ANC_ON state only */
        if (GET_ANC_ON_STATE(anc_payload))
        {
            appKymeraAncProcessCommand(GET_ANC_MODE(((anc_payload >> BIT_POS_ANC_MODE) & ANC_BIT_MASK_MODE)),
                                       KYMERA_ANC_NO_DELAY);
            appKymeraAncProcessCommand(GET_ANC_GAIN_EVENT_CMD(((anc_payload >> BIT_POS_ANC_GAIN) & ANC_BIT_MASK_CMD)),
                                       KYMERA_ANC_NO_DELAY);
        }
    }
    else
    {
        appKymeraAncProcessCommand(anc_payload, KYMERA_ANC_DELAY);
    }
}

/*! \brief Handle ANC message channel data tx confirmation from remote peer */
static void appKymeraAncHandlePeerSigMsgTxCfm(const PEER_SIG_MSG_CHANNEL_TX_CFM_T *cfm)
{
    DEBUG_LOGF("appKymeraAncHandlePeerSigMsgTxCfm status:%d\n", cfm->status);
    
    appKymeraAncTaskData *data = appGetAncDataLocal();

    if (cfm->status == peerSigStatusSuccess && !GET_ANC_LINKLOSS_STATE(data->anc_cmd))
    {
        appKymeraAncProcessCommand(data->anc_cmd, KYMERA_ANC_NO_DELAY);
    }
    else
    {
        DEBUG_LOGF("Failed to process ANC payload: %u", data->anc_cmd);
    }
}

/* Update ANC payload following reconnect after linkloss to sync state, mode from active local peer */
static void appKymeraAncHandlePeerSigConnectInd(const PEER_SIG_CONNECTION_IND_T* ind)
{
    DEBUG_LOGF("appKymeraAncHandlePeerSigConnectInd status:%d\n", ind->status);

    peerSigTaskData *peer_sig = appGetPeerSig();

    if (ind->status == peerSigStatusConnected && peer_sig->link_loss_occurred)
    {
        appKymeraAncTaskData *data = appGetAncDataLocal();
        /* linkloss ANC payload.... byte0[State+Mode+linkloss flag value] + byte1[Gain value] */
        uint16 anc_payload = (data->anc_gain << BIT_POS_ANC_GAIN) | 
                             (peer_sig->link_loss_occurred << BIT_POS_ANC_STATE_LINK_LOSS) | 
                             (data->anc_mode << BIT_POS_ANC_MODE) | 
                             (GET_ANC_ON_STATE(data->anc_state_on) << BIT_POS_ANC_STATE);

        DEBUG_LOGF("Send current ANC state, mode and gain value:%x to peer after link-loss reconnect", anc_payload);
        appKymeraAncSynchronizeWithPeer(anc_payload);
    }
}

/* get corresponding Kymera events based on ANC commands */
static kymera_anc_event appKymeraAncGetInternalEvent(uint16 anc_cmd)
{
    kymera_anc_event ancEvent;
    appKymeraAncTaskData *data = appGetAncDataLocal();

    switch(anc_cmd & ANC_BIT_MASK_CMD)
    {
        case ANC_DISABLE:
           ancEvent = KYMERA_INTERNAL_ANC_OFF;
           data->anc_state_on = FALSE;    
           break;
 
        case ANC_ENABLE:
           ancEvent = KYMERA_INTERNAL_ANC_ON;
           data->anc_state_on = TRUE;
           break;

        case ANC_SET_GAIN:
           ancEvent = KYMERA_INTERNAL_ANC_SET_GAIN;
           data->anc_gain = (anc_cmd & ANC_BIT_MASK_GAIN_VALUE) >> BIT_POS_ANC_GAIN;
           break;

        case ANC_SET_MODE1:
           ancEvent = KYMERA_INTERNAL_ANC_SET_MODE;
           data->anc_mode = anc_mode_1;
           break;

        case ANC_SET_MODE2:
           ancEvent = KYMERA_INTERNAL_ANC_SET_MODE;
           data->anc_mode = anc_mode_2;
           break;

        case ANC_SET_MODE3:
           ancEvent = KYMERA_INTERNAL_ANC_SET_MODE;
           data->anc_mode = anc_mode_3;
           break;

        case ANC_SET_MODE4:
           ancEvent = KYMERA_INTERNAL_ANC_SET_MODE;
           data->anc_mode = anc_mode_4;
           break;

        case ANC_SET_MODE5:
           ancEvent = KYMERA_INTERNAL_ANC_SET_MODE;
           data->anc_mode = anc_mode_5;
           break;

        case ANC_SET_MODE6:
           ancEvent = KYMERA_INTERNAL_ANC_SET_MODE;
           data->anc_mode = anc_mode_6;
           break;

        case ANC_SET_MODE7:
           ancEvent = KYMERA_INTERNAL_ANC_SET_MODE;
           data->anc_mode = anc_mode_7;
           break;

        case ANC_SET_MODE8:
           ancEvent = KYMERA_INTERNAL_ANC_SET_MODE;
           data->anc_mode = anc_mode_8;
           break;

        case ANC_SET_MODE9:
           ancEvent = KYMERA_INTERNAL_ANC_SET_MODE;
           data->anc_mode = anc_mode_9;
           break;

        case ANC_SET_MODE10:
           ancEvent = KYMERA_INTERNAL_ANC_SET_MODE;
           data->anc_mode = anc_mode_10;
           break;

        default:
           ancEvent = KYMERA_INTERNAL_ANC_OFF;
           data->anc_state_on = FALSE;
           break;
    }
    return ancEvent;
}

/*! \brief Message Handler
    This function is the main message handler for the Kymera ANC module.
*/
static void appKymeraAncHandleMessage(Task task, MessageId id, Message message)
{
    UNUSED(task);

    switch (id)
    {
       case PEER_SIG_MSG_CHANNEL_RX_IND:
           appKymeraAncHandlePeerSigMsgRxInd((const PEER_SIG_MSG_CHANNEL_RX_IND_T *)message);
           break;

       case PEER_SIG_MSG_CHANNEL_TX_CFM:
           appKymeraAncHandlePeerSigMsgTxCfm((const PEER_SIG_MSG_CHANNEL_TX_CFM_T *)message);
           break;

       case PEER_SIG_CONNECTION_IND:
           appKymeraAncHandlePeerSigConnectInd((const PEER_SIG_CONNECTION_IND_T *)message);
           break;

        default:
            DEBUG_LOGF("appKymeraAncHandleMessage: (%x) unhandled",id);
            break;
    }
}

void appKymeraAncInit(void)
{
    kymeraTaskData *theKymera = appGetKymera();
    DEBUG_LOGF("appKymeraAncInit");

    theKymera->anc_state = KYMERA_ANC_UNINITIALISED;
    if (appConfigAncPathEnable() != all_disabled)
    {
        memset(&theKymera->anc_mic_params, 0, sizeof(anc_mic_params_t));
        theKymera->anc_mic_params.mic_gain_step_size = appConfigAncMicGainStepSize();

        if (appConfigAncPathEnable() & feed_forward_left)
        {
            PanicFalse(appConfigAncFeedForwardMic() != NO_MIC);
            theKymera->anc_mic_params.enabled_mics |= feed_forward_left;
            theKymera->anc_mic_params.feed_forward_left = theKymera->mic_params[appConfigAncFeedForwardMic()];
        }
        if (appConfigAncPathEnable() & feed_back_left)
        {
            PanicFalse(appConfigAncFeedBackMic() != NO_MIC);
            theKymera->anc_mic_params.enabled_mics |= feed_back_left;
            theKymera->anc_mic_params.feed_back_left = theKymera->mic_params[appConfigAncFeedBackMic()];
        }

        if (AncInit(&theKymera->anc_mic_params, appConfigAncMode(), appConfigAncSidetoneGain()))
        {
            appKymeraAncTaskData *data = appGetAncDataLocal();
            data->task.handler = appKymeraAncHandleMessage;
            
            theKymera->anc_state = KYMERA_ANC_OFF;
            
            /* Register a channel for peer signalling */
            appPeerSigMsgChannelTaskRegister(&data->task, PEER_SIG_MSG_CHANNEL_ANC);

            /* Register for peer signalling notifications */
            appPeerSigClientRegister(&data->task);
        }
     }   
}

void appKymeraAncEnable(void)
{
    kymeraTaskData *theKymera = appGetKymera();
    DEBUG_LOGF("appKymeraAncEnable, state %u", theKymera->anc_state);

    if (theKymera->anc_state == KYMERA_ANC_OFF)
    {
        if (AncEnable(TRUE))
        {
            theKymera->anc_state = KYMERA_ANC_ON;
            appKymeraExternalAmpControl(TRUE);
        }
    }
}

void appKymeraAncDisable(void)
{
    kymeraTaskData *theKymera = appGetKymera();
    DEBUG_LOGF("appKymeraAncDisable, state %u", theKymera->anc_state);

    if (theKymera->anc_state == KYMERA_ANC_ON)
    {
        if (AncEnable(FALSE))
        {
            theKymera->anc_state = KYMERA_ANC_OFF;
            appKymeraExternalAmpControl(FALSE);
        }
    }
}

void appKymeraAncSetMode(anc_mode_t mode)
{
    kymeraTaskData *theKymera = appGetKymera();
    DEBUG_LOGF("appKymeraAncSetMode, state %u, mode %u", theKymera->anc_state, mode);

    if (theKymera->anc_state == KYMERA_ANC_ON)
    {
        if (!AncSetMode(mode))
        {            
            DEBUG_LOGF("appKymeraAncSetMode failed to set ucid %u ", mode);
        }
    }
    else
    {
        DEBUG_LOGF("appKymeraAncSetMode failed as not in right ANC state ");
    }
}

anc_mode_t appKymeraAncGetMode(void)
{
    appKymeraAncTaskData *data = appGetAncDataLocal();
    return (anc_mode_t)data->anc_mode;
}

/* Set ANC fine gain for FeedForward LPF gain path.
   FFA path is used in FeedForward mode and FFB path in Hybrid modes when using external MIC configuration */
void appKymeraAncSetGain(uint8 gain)
{
    kymeraTaskData *theKymera = appGetKymera();
    bool ret_val = FALSE;
    DEBUG_LOGF("appKymeraAncSetGain, state %u, gain value %u", theKymera->anc_state, gain);

    if (theKymera->anc_state == KYMERA_ANC_ON)
    {
        anc_path_enable anc_path = appConfigAncPathEnable();
        switch(anc_path)
        {
               case hybrid_mode_left_only:
                   ret_val = AncConfigureFFBPathGain(AUDIO_ANC_INSTANCE_0, gain);
                   break;

               case feed_forward_mode_left_only:
                   ret_val = AncConfigureFFAPathGain(AUDIO_ANC_INSTANCE_0, gain);
                   break;

                default:
                   DEBUG_LOGF("appKymeraAncSetGain, cannot set fine gain for anc_path:  %u", anc_path);
                   break;
        }
    }
    else
    {
        DEBUG_LOGF("appKymeraAncSetGain failed as not in right ANC state ");
    }

    if(!ret_val)
    {
       DEBUG_LOGF("appKymeraAncSetGain failed!");
    }
}

uint8 appKymeraAncGetGain(void)
{
    appKymeraAncTaskData *data = appGetAncDataLocal();
    return data->anc_gain;
}

bool appKymeraAncIsEnabled(void)
{
    kymeraTaskData *theKymera = appGetKymera();
    return (theKymera->anc_state == KYMERA_ANC_ON);
}

void appKymeraAncTuningStart(uint16 usb_rate)
{
    kymeraTaskData *theKymera = appGetKymera();
    DEBUG_LOG("appKymeraAncTuningStart");
    MAKE_KYMERA_MESSAGE(KYMERA_INTERNAL_ANC_TUNING_START);
    message->usb_rate = usb_rate;
    MessageSendConditionally(&theKymera->task, KYMERA_INTERNAL_ANC_TUNING_START, message, &theKymera->anc_lock);
}

void appKymeraAncTuningStop(void)
{
    kymeraTaskData *theKymera = appGetKymera();
    DEBUG_LOG("appKymeraAncTuningStop");
    MessageSendConditionally(&theKymera->task, KYMERA_INTERNAL_ANC_TUNING_STOP, NULL, &theKymera->anc_lock);
}

static void appKymeraAncGetMics(uint8 *ffa_mic, uint8 *ffb_mic)
{
    *ffa_mic = NO_MIC;
    *ffb_mic = NO_MIC;

    anc_path_enable anc_path = appConfigAncPathEnable();
    
    switch(anc_path)
    {
        case hybrid_mode_left_only:
            *ffa_mic = appConfigAncFeedBackMic();
            *ffb_mic = appConfigAncFeedForwardMic();
            break;

        case feed_back_mode_left_only:
            *ffa_mic = appConfigAncFeedBackMic();
            break;

        case feed_forward_mode_left_only:
            *ffa_mic = appConfigAncFeedForwardMic();
            break;

        default:
            *ffa_mic = NO_MIC;
            *ffb_mic = NO_MIC;
            break;
    }
}

void appKymeraAncTuningCreateChain(uint16 usb_rate)
{
    kymeraTaskData *theKymera = appGetKymera();
    theKymera->usb_rate = usb_rate;

    const char anc_tuning_edkcs[] = "download_anc_tuning.edkcs";
    DEBUG_LOG("appKymeraAncTuningCreateChain, rate %u", usb_rate);

    /* Only 48KHz supported */
    PanicFalse(usb_rate == 48000);

    /* Turn on audio subsystem */
    OperatorFrameworkEnable(1);

    /* Move to ANC tuning state, this prevents A2DP and HFP from using kymera */
    appKymeraSetState(KYMERA_STATE_ANC_TUNING);

    /* Create ANC tuning operator */
    FILE_INDEX index = FileFind(FILE_ROOT, anc_tuning_edkcs, strlen(anc_tuning_edkcs));
    PanicFalse(index != FILE_NONE);
    theKymera->anc_tuning_bundle_id = PanicZero (OperatorBundleLoad(index, 0)); /* 0 is processor ID */
    theKymera->anc_tuning = (Operator)PanicFalse(VmalOperatorCreate(CAP_ID_DOWNLOAD_ANC_TUNING));

    /* Create the operators for USB Rx & Tx audio */
    uint16 usb_config[] =
    {
        OPMSG_USB_AUDIO_ID_SET_CONNECTION_CONFIG,
        0,              // data_format
        usb_rate / 25,  // sample_rate
        2,              // number_of_channels
        16,             // subframe_size
        16,             // subframe_resolution
    };

/* On Aura 2.1 usb rx, tx capabilities are downloaded */
#ifdef DOWNLOAD_USB_AUDIO
    const char usb_audio_edkcs[] = "download_usb_audio.edkcs";
    index = FileFind(FILE_ROOT, usb_audio_edkcs, strlen(usb_audio_edkcs));
    PanicFalse(index != FILE_NONE);
    theKymera->usb_audio_bundle_id = PanicZero (OperatorBundleLoad(index, 0)); /* 0 is processor ID */
#endif

    theKymera->usb_rx = (Operator)PanicFalse(VmalOperatorCreate(EB_CAP_ID_USB_AUDIO_RX));
    PanicFalse(VmalOperatorMessage(theKymera->usb_rx,
                                   usb_config, SIZEOF_OPERATOR_MESSAGE(usb_config),
                                   NULL, 0));

    theKymera->usb_tx = (Operator)PanicFalse(VmalOperatorCreate(EB_CAP_ID_USB_AUDIO_TX));
    PanicFalse(VmalOperatorMessage(theKymera->usb_tx,
                                   usb_config, SIZEOF_OPERATOR_MESSAGE(usb_config),
                                   NULL, 0));

    /* Get the DAC output sinks */
    Sink DAC_L = (Sink)PanicFalse(StreamAudioSink(AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0, AUDIO_CHANNEL_A));
    PanicFalse(SinkConfigure(DAC_L, STREAM_CODEC_OUTPUT_RATE, usb_rate));

    /* Get the ANC microphone sources */
    Source ffa_mic_source, ffb_mic_source;
    uint8 ffa_mic, ffb_mic;
    appKymeraAncGetMics(&ffa_mic, &ffb_mic);
    appKymeraMicSetup(ffa_mic, &ffa_mic_source, ffb_mic, &ffb_mic_source, usb_rate);

    /* Get the ANC tuning monitor microphone sources */
    Source fb_mon0;
    appKymeraMicSetup(appConfigAncTuningMonitorMic(), &fb_mon0, NO_MIC, NULL, usb_rate);
    PanicFalse(SourceSynchronise(ffa_mic_source, fb_mon0));


    uint16 anc_tuning_frontend_config[3] =
    {
        OPMSG_ANC_TUNING_ID_FRONTEND_CONFIG,        // ID
        0,                                          // 0 = mono, 1 = stereo
        ffb_mic_source ? 1 : 0                             // 0 = 1-mic, 1 = 2-mic
    };
    PanicFalse(VmalOperatorMessage(theKymera->anc_tuning,
                                   &anc_tuning_frontend_config, SIZEOF_OPERATOR_MESSAGE(anc_tuning_frontend_config),
                                   NULL, 0));

    /* Connect microphone source to ANC Tuning operator FFA path sink */
    PanicFalse(StreamConnect(ffa_mic_source,
                             StreamSinkFromOperatorTerminal(theKymera->anc_tuning, ANC_TUNING_SINK_MIC1_LEFT)));
    /* Connect microphone source to ANC Tuning operator FFB path sink */
    if (ffb_mic_source)
        PanicFalse(StreamConnect(ffb_mic_source,
                                 StreamSinkFromOperatorTerminal(theKymera->anc_tuning, ANC_TUNING_SINK_MIC2_LEFT)));

    /* Connect microphone source to ANC Tuning operator FBMON path sink */
    PanicFalse(StreamConnect(fb_mon0,
                             StreamSinkFromOperatorTerminal(theKymera->anc_tuning, ANC_TUNING_SINK_FBMON_LEFT)));

    /* Connect speaker */
    PanicFalse(StreamConnect(StreamSourceFromOperatorTerminal(theKymera->anc_tuning, ANC_TUNING_SOURCE_DAC_LEFT),
                             DAC_L));

    /* Connect backend (USB) out */
    PanicFalse(StreamConnect(StreamSourceFromOperatorTerminal(theKymera->anc_tuning, ANC_TUNING_SOURCE_USB_LEFT),
                             StreamSinkFromOperatorTerminal(theKymera->usb_tx, 0)));
    PanicFalse(StreamConnect(StreamSourceFromOperatorTerminal(theKymera->anc_tuning, ANC_TUNING_SOURCE_USB_RIGHT),
                             StreamSinkFromOperatorTerminal(theKymera->usb_tx, 1)));

    /* Connect backend (USB) in */
    PanicFalse(StreamConnect(StreamSourceFromOperatorTerminal(theKymera->usb_rx, 0),
                             StreamSinkFromOperatorTerminal(theKymera->anc_tuning, ANC_TUNING_SINK_USB_LEFT)));
    PanicFalse(StreamConnect(StreamSourceFromOperatorTerminal(theKymera->usb_rx, 1),
                             StreamSinkFromOperatorTerminal(theKymera->anc_tuning, ANC_TUNING_SINK_USB_RIGHT)));

    /* Connect USB ISO in endpoint to USB Rx operator */
    PanicFalse(StreamConnect(StreamUsbEndPointSource(end_point_iso_in),
                             StreamSinkFromOperatorTerminal(theKymera->usb_rx, 0)));

    /* Connect USB Tx operator to USB ISO out endpoint */
    PanicFalse(StreamConnect(StreamSourceFromOperatorTerminal(theKymera->usb_tx, 0),
                             StreamUsbEndPointSink(end_point_iso_out)));

    /* Finally start the operators */
    Operator op_list[] = {theKymera->usb_rx, theKymera->anc_tuning, theKymera->usb_tx};
    PanicFalse(OperatorStartMultiple(3, op_list, NULL));

    /* Ensure audio amp is on */
    appKymeraExternalAmpControl(TRUE);
}

void appKymeraAncTuningDestroyChain(void)
{
    kymeraTaskData *theKymera = appGetKymera();

    /* Turn audio amp is off */
    appKymeraExternalAmpControl(FALSE);

    /* Stop the operators */
    Operator op_list[] = {theKymera->usb_rx, theKymera->anc_tuning, theKymera->usb_tx};
    PanicFalse(OperatorStopMultiple(3, op_list, NULL));

    /* Disconnect USB ISO in endpoint */
    StreamDisconnect(StreamUsbEndPointSource(end_point_iso_in), 0);

    /* Disconnect USB ISO out endpoint */
    StreamDisconnect(0, StreamUsbEndPointSink(end_point_iso_out));

    /* Get the DAC output sinks */
    Sink DAC_L = (Sink)PanicFalse(StreamAudioSink(AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0, AUDIO_CHANNEL_A));

    /* Get the ANC microphone sources */
    Source ffa_mic_source, ffb_mic_source;
    uint8 ffa_mic, ffb_mic;
    appKymeraAncGetMics(&ffa_mic, &ffb_mic);
    appKymeraMicSetup(ffa_mic, &ffa_mic_source, ffb_mic, &ffb_mic_source, theKymera->usb_rate);
    
    /* Get the ANC tuning monitor microphone sources */
    Source fb_mon0;
    appKymeraMicSetup(appConfigAncTuningMonitorMic(), &fb_mon0, NO_MIC, NULL, theKymera->usb_rate);

    /* Disconnect FFA and FFB microphones */
    StreamDisconnect(ffa_mic_source, 0);
    if (ffb_mic_source)
        StreamDisconnect(ffb_mic_source, 0);

    /* Disconnect FBMON microphone */
    StreamConnect(fb_mon0, 0);

    /* Disconnect speaker */
    StreamDisconnect(0, DAC_L);

    /* Distroy operators */
    OperatorsDestroy(op_list, 3);

    /* Unload bundle */
    PanicFalse(OperatorBundleUnload(theKymera->anc_tuning_bundle_id));
    #ifdef DOWNLOAD_USB_AUDIO
    PanicFalse(OperatorBundleUnload(theKymera->usb_audio_bundle_id));
    #endif

    /* Clear kymera lock and go back to idle state to allow other uses of kymera */
    appKymeraSetState(KYMERA_STATE_IDLE);

    /* Turn off audio subsystem */
    OperatorFrameworkEnable(0);
}

/* Get corresponding internal Mode event based on ANC commands */
uint16 appKymeraAncGetModeEvent(anc_mode_t anc_mode)
{
    kymera_anc_event ancModeEvent;
    switch(anc_mode)
    {
        case anc_mode_1:
           ancModeEvent = ANC_SET_MODE1;
           break;
          
        case anc_mode_2:
           ancModeEvent = ANC_SET_MODE2;
           break;

        case anc_mode_3:
           ancModeEvent = ANC_SET_MODE3;
           break;

        case anc_mode_4:
           ancModeEvent = ANC_SET_MODE4;
           break;

        case anc_mode_5:
           ancModeEvent = ANC_SET_MODE5;
           break;

        case anc_mode_6:
           ancModeEvent = ANC_SET_MODE6;
           break;

        case anc_mode_7:
           ancModeEvent = ANC_SET_MODE7;
           break;

        case anc_mode_8:
           ancModeEvent = ANC_SET_MODE8;
           break;

        case anc_mode_9:
           ancModeEvent = ANC_SET_MODE9;
           break;

        case anc_mode_10:
           ancModeEvent = ANC_SET_MODE10;
           break;
 
        default:
           ancModeEvent = ANC_SET_MODE1;
           break;
    }
    return ancModeEvent;
}

/* Get corresponding internal Gain event ANC payload which is combination of,
   byte0: [ANC_SET_GAIN event] + byte1: [Gain value] */
uint16 appKymeraAncGetGainEventCmd(uint8 gain)
{
    uint16 ancGainEventCmd = (gain << BIT_POS_ANC_GAIN) | (ANC_SET_GAIN & ANC_BIT_MASK_CMD);
    
    return ancGainEventCmd;
}

/* Process and execute the ANC command locally */
void appKymeraAncProcessCommand(const uint16 anc_cmd, uint16 delay)
{
    DEBUG_LOG("appKymeraAncProcessCommand anc_cmd: %x \n", anc_cmd);
 
    /* Don't process anc cmd when state is in-case and just store it */
    MessageId anc_event = appKymeraAncGetInternalEvent(anc_cmd);

    if (!appSmIsInCase())
    {
        kymeraTaskData *theKymera = appGetKymera();
        MessageSendLater(&theKymera->task, anc_event, NULL, delay);
    }
}

/* Enable ANC by sending commands via peer signalling */
void appKymeraAncEnableSynchronizeWithPeer(void)
{
    appKymeraAncSynchronizeWithPeer(ANC_ENABLE);
}

/* Disable ANC by sending commands via peer signalling */
void appKymeraAncDisableSynchronizeWithPeer(void)
{
    appKymeraAncSynchronizeWithPeer(ANC_DISABLE);
}

/* Set ANC mode (ucid 0 to 9) via peer signalling */
void appKymeraAncSetModeSynchronizeWithPeer(anc_mode_t mode)
{
    if (appKymeraAncIsEnabled())
    {
        appKymeraAncSynchronizeWithPeer(GET_ANC_MODE(mode));
    }
    else
    {
        DEBUG_LOGF("appKymeraAncSetModeSynchronizeWithPeer: Failed as not in ANC_ON state ");
    }
}

/* Set the ANC fine gain for FeedForward LPF gain path via peer signalling */
bool appKymeraAncSetGainSynchronizeWithPeer(uint8 gain)
{
    bool ret_val = FALSE;
    
    if (appKymeraAncIsEnabled())
    {
        appKymeraAncSynchronizeWithPeer(GET_ANC_GAIN_EVENT_CMD(gain));
        ret_val = TRUE;
    }
    else
    {
        DEBUG_LOGF("appKymeraAncSetGainSynchronizeWithPeer: Failed as not in right ANC_ON state ");
    }
    return ret_val;
}
