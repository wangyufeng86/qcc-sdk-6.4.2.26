/*!
\copyright  Copyright (c) 2008 - 2017 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    
\file       av_headset_link_policy.c
\brief      Link policy manager
*/

#include <panic.h>
#include <connection.h>
#include <sink.h>

#include "av_headset.h"
#include "av_headset_log.h"
#include <app/bluestack/dm_prim.h>

/*! Make and populate a bluestack DM primitive based on the type. 

    \note that this is a multiline macro so should not be used after a
    control statement (if, while) without the use of braces
 */
#define MAKE_PRIM_C(TYPE) MESSAGE_MAKE(prim,TYPE##_T); prim->common.op_code = TYPE; prim->common.length = sizeof(TYPE##_T);

/*! Macro for creating a message based on the message name */
#define MAKE_LP_MESSAGE(TYPE) \
    TYPE##_T *message = PanicUnlessNew(TYPE##_T);

/*! Lower power table for A2DP */
static const lp_power_table powertable_a2dp[]=
{
    /* mode,        min_interval, max_interval, attempt, timeout, duration */
    {lp_active,     0,            0,            0,       0,       5},  /* Active mode for 5 sec */
    {lp_passive,    0,            0,            0,       0,       1},  /* Passive mode for 1 sec */
    {lp_sniff,      48,           400,          2,       4,       0}   /* Enter sniff mode*/
};

/*! Lower power table for AVRCP */
static const lp_power_table powertable_avrcp[]=
{
    /* mode,        min_interval, max_interval, attempt, timeout, duration */
    {lp_active,     0,            0,            0,       0,       1},  /* Active mode for 1 second */
    {lp_sniff,      48,           800,          2,       1,       0}   /* Enter sniff mode*/
};

/*! Lower power table for the HFP. */
static const lp_power_table powertable_hfp[]=
{
    /* mode,        min_interval, max_interval, attempt, timeout, duration */
    {lp_active,     0,            0,            0,       0,       1},  /* Active mode for 1 second */
    {lp_sniff,      48,           800,          2,       1,       0}   /* Enter sniff mode (30-500ms)*/
};

/*! Lower power table for the HFP when an audio connection is open
*/
static const lp_power_table powertable_hfp_sco[]=
{
    /* mode,        min_interval, max_interval, attempt, timeout, duration */
    {lp_passive,    0,            0,            0,       0,       1},  /* Passive mode */
    {lp_sniff,      48,           144,          2,       8,       0}   /* Enter sniff mode (30-90ms)*/
};

/*! Lower power table for TWS+ HFP when an audio connection is open */
static const lp_power_table powertable_twsplus_hfp_sco[]=
{
    /* mode,        min_interval, max_interval, attempt, timeout, duration */
    {lp_sniff,      48,           144,          2,       8,       0}   /* Stay in sniff mode */
};

/*! Lower power table for the A2DP with media streaming as TWS sink */
static const lp_power_table powertable_a2dp_streaming_tws_sink[]=
{
    /* mode,        min_interval, max_interval, attempt, timeout, duration */
    {lp_active,     0,            0,            0,       0,       5},  /* Active mode for 5 sec */
    {lp_passive,    0,            0,            0,       0,       1},  /* Passive mode for 1 sec */
    {lp_sniff,      48,           48,           2,       4,       0}   /* Enter sniff mode*/
};

/*! Lower power table for the A2DP with media streaming as sink */
static const lp_power_table powertable_a2dp_streaming_sink[]=
{
    /* mode,        min_interval, max_interval, attempt, timeout, duration */
    {lp_active,     0,            0,            0,       0,       5},  /* Active mode for 5 sec */
    {lp_passive,    0,            0,            0,       0,       1},  /* Passive mode for 1 sec */
    {lp_sniff,      48,           48,           2,       4,       0}   /* Enter sniff mode*/
};


/*! Power table for the peer link when both idle
*/
static const lp_power_table powertable_peer_idle[]=
{
    /* mode,        min_interval, max_interval, attempt, timeout, duration */
    {lp_sniff,      48,           800,          2,       8,       0}   /* Enter sniff mode (30-500ms)*/
};


/*! Power table for the peer link when streaming
*/
static const lp_power_table powertable_peer_streaming[]=
{
    /* mode,        min_interval, max_interval, attempt, timeout, duration */
    {lp_active,     0,            0,            0,       0,       5},  /* Active mode for 5 second */
    {lp_passive,    0,            0,            0,       0,       1},  /* Passive mode for 1 sec */
    {lp_sniff,      48,           144,          2,       8,       0}   /* Enter sniff mode (30-90ms)*/
};

/*! Lower power table for PEER Link wiht TWS+ and streaming */
static const lp_power_table powertable_twsplus_peer_streaming[]=
{
    /* mode,        min_interval, max_interval, attempt, timeout, duration */
    {lp_sniff,      80,           100,          2,       4,      0}   /* Stay in sniff mode */
};

/*! Power table for the peer link when not streaming
*/
static const lp_power_table powertable_peer_not_streaming[]=
{
    /* mode,        min_interval, max_interval, attempt, timeout, duration */
    {lp_active,     0,            0,            0,       0,       1},  /* Active mode for 1 second */
    {lp_sniff,      48,           288,          2,       8,       0}   /* Enter sniff mode (30-180ms)*/
};

/*! Lower power table for PEER Link with TWS+ and not streaming */
static const lp_power_table powertable_twsplus_peer_not_streaming[]=
{
    /* mode,        min_interval, max_interval, attempt, timeout, duration */
    {lp_sniff,      80,           200,           2,       4,      0}   /* Stay in sniff mode */
};


/*! \cond helper */
#define ARRAY_AND_DIM(ARRAY) (ARRAY), ARRAY_DIM(ARRAY)
/*! \endcond helper */

typedef enum
{
    LP_ROLE_STATE_GET,
    LP_ROLE_STATE_SET,
} lpRoleState;

typedef struct
{
    Sink sink;
    hci_role role:4;
    hci_role target_role:4;
    lpRoleState state:2;
} lpRoleTaskData;

/*! Structure for storing power tables */
struct powertable_data
{
    const lp_power_table *table;
    uint16 rows;
};

/*! Array of structs used to store the power tables for standard phones */
static const struct powertable_data powertables_standard[] = {
    [POWERTABLE_A2DP] =                    {ARRAY_AND_DIM(powertable_a2dp)},
    [POWERTABLE_AVRCP] =                   {ARRAY_AND_DIM(powertable_avrcp)},
    [POWERTABLE_HFP] =                     {ARRAY_AND_DIM(powertable_hfp)},
    [POWERTABLE_HFP_SCO] =                 {ARRAY_AND_DIM(powertable_hfp_sco)},
    [POWERTABLE_A2DP_STREAMING_TWS_SINK] = {ARRAY_AND_DIM(powertable_a2dp_streaming_tws_sink)},
    [POWERTABLE_A2DP_STREAMING_SINK] =     {ARRAY_AND_DIM(powertable_a2dp_streaming_sink)},
    [POWERTABLE_PEER_STREAMING] =          {ARRAY_AND_DIM(powertable_peer_streaming)},
    [POWERTABLE_PEER_IN_CASE] =            {ARRAY_AND_DIM(powertable_peer_idle)},
    [POWERTABLE_PEER_NOT_STREAMING] =      {ARRAY_AND_DIM(powertable_peer_not_streaming)},
};

/*! Array of structs used to store the power tables for TWS+ phones */
static const struct powertable_data powertables_twsplus[] = {
    [POWERTABLE_A2DP] =                    {ARRAY_AND_DIM(powertable_a2dp)},
    [POWERTABLE_AVRCP] =                   {ARRAY_AND_DIM(powertable_avrcp)},
    [POWERTABLE_HFP] =                     {ARRAY_AND_DIM(powertable_hfp)},
    [POWERTABLE_HFP_SCO] =                 {ARRAY_AND_DIM(powertable_twsplus_hfp_sco)},
    [POWERTABLE_A2DP_STREAMING_TWS_SINK] = {ARRAY_AND_DIM(powertable_a2dp_streaming_tws_sink)},
    [POWERTABLE_A2DP_STREAMING_SINK] =     {ARRAY_AND_DIM(powertable_a2dp_streaming_sink)},
    [POWERTABLE_PEER_STREAMING] =          {ARRAY_AND_DIM(powertable_twsplus_peer_streaming)},
    [POWERTABLE_PEER_IN_CASE] =            {ARRAY_AND_DIM(powertable_peer_idle)},
    [POWERTABLE_PEER_NOT_STREAMING] =      {ARRAY_AND_DIM(powertable_twsplus_peer_not_streaming)},
};

void appLinkPolicyUpdateLinkSupervisionTimeout(const bdaddr *bd_addr)
{
    uint16 timeout;

    if (appDeviceIsPeer(bd_addr))
        timeout = appConfigEarbudLinkSupervisionTimeout() * 1000UL / 625;
    else
        timeout = appConfigDefaultLinkSupervisionTimeout() * 1000UL / 625;

    MAKE_PRIM_C(DM_HCI_WRITE_LINK_SUPERV_TIMEOUT_REQ);
    prim->handle = 0;
    BdaddrConvertVmToBluestack(&prim->bd_addr, bd_addr);
    prim->timeout = timeout;
    VmSendDmPrim(prim);
}


static void appLinkPolicySetPowerTableIndex(const bdaddr *bd_addr, Sink sink, lpPowerTableIndex pt_index, bool is_tws_plus_handset)
{
    lpPowerTableSet powertable_set = is_tws_plus_handset ? POWERTABLE_SET_TWSPLUS :
                                                           POWERTABLE_SET_NORMAL;
    if (sink)
    {
        lpPerConnectionState old_lp_state;
        lpPerConnectionState new_lp_state;

        appConManagerGetLpState(bd_addr, &old_lp_state);
        new_lp_state.pt_index = pt_index;
        new_lp_state.table_set_used = powertable_set;

        if (!appConManagerLpStateSame(&old_lp_state, &new_lp_state) &&
            (pt_index != POWERTABLE_UNASSIGNED))
        {
            const struct powertable_data *selected = is_tws_plus_handset ?
                                                     &powertables_twsplus[pt_index] :
                                                     &powertables_standard[pt_index];

            ConnectionSetLinkPolicy(sink, selected->rows, selected->table);

            if (appDeviceIsPeer(bd_addr))
            {
                DEBUG_LOGF("appLinkPolicyUpdatePowerTable for peer, index=%d, prev=%d TWS+:%d x%p",
                                pt_index, old_lp_state.pt_index, is_tws_plus_handset, selected->table);
            }
            else
            {
                DEBUG_LOGF("appLinkPolicyUpdatePowerTable, index=%d, prev=%d TWS+:%d x%p",
                                pt_index, old_lp_state.pt_index, is_tws_plus_handset, selected->table);
            }

            appConManagerSetLpState(bd_addr, &new_lp_state);
        }
    }
}


void appLinkPolicyUpdatePeerPowerTable(lpEbInCase in_case, lpEbStreaming streaming)
{
    lpPowerTableIndex pt_index = POWERTABLE_UNASSIGNED;
    Sink sink = 0;

    /* Determine if we are in TWS+ moe as we have different settings */
    bdaddr handset_bd_addr;
    bool is_tws_plus_handset = appDeviceGetHandsetBdAddr(&handset_bd_addr) &&
                               appDeviceIsTwsPlusHandset(&handset_bd_addr);

    /* Update peer link power table if we are SCO forwarding */
    if (appConfigScoForwardingEnabled() && appScoFwdIsStreaming())
    {
        DEBUG_LOG("appLinkPolicyUpdatePowerTable, updating peer powertable due to SCO forwarding");
        pt_index = POWERTABLE_PEER_STREAMING;
        sink = appScoFwdGetSink();
    }
    else
    {
        if (streaming == EBS_STREAMING)
        {
            DEBUG_LOG("appLinkPolicyUpdatePowerTable, updating peer powertable due to streaming");
            pt_index = POWERTABLE_PEER_STREAMING;
            sink = appPeerSigGetSink();
        }
        else if (in_case == EBS_BOTH_IN_CASE)
        {
            DEBUG_LOG("appLinkPolicyUpdatePowerTable, updating peer powertable due to both in case");
            pt_index = POWERTABLE_PEER_IN_CASE;
            sink = appPeerSigGetSink();
        }
        else if (streaming == EBS_BOTH_NOT_STREAMING)
        {
            DEBUG_LOG("appLinkPolicyUpdatePowerTable, updating peer powertable due to both not streaming");
            pt_index = POWERTABLE_PEER_NOT_STREAMING;
            sink = appPeerSigGetSink();
        }
    }

    if (sink)
    {
        tp_bdaddr bd_addr;
        if (SinkGetBdAddr(sink, &bd_addr))
            appLinkPolicySetPowerTableIndex(&bd_addr.taddr.addr, sink, pt_index, is_tws_plus_handset);
    }
}

/* \brief Re-check and select link settings to reduce power consumption
    where possible

    This function checks what activity the application currently has,
    and decides what the best link settings are for the connection 
    to the specified device. This may include full power (#lp_active), 
    sniff (#lp_sniff), or passive(#lp_passive) where full power is 
    no longer required but the application would prefer not to enter
    sniff mode yet.

    \param bd_addr  Bluetooth address of the device to update link settings
*/
void appLinkPolicyUpdatePowerTable(const bdaddr *bd_addr)
{
    /* Always check peer power table */
    appLinkPolicyUpdatePeerPowerTable(EBS_IN_CASE_UNKNOWN, EBS_STREAMING_UNKNOWN);

    /* Return now if bd_addr was for peer */
    bool is_peer = appDeviceIsPeer(bd_addr);
    if (is_peer)
        return;

    bool is_tws_plus_handset = appDeviceIsTwsPlusHandset(bd_addr);
    lpPowerTableIndex pt_index = POWERTABLE_UNASSIGNED;
    avInstanceTaskData *av_inst = appAvInstanceFindFromBdAddr(bd_addr);
    Sink sink = 0;

#ifdef INCLUDE_HFP
    if (appHfpIsScoActive())
    {
        pt_index = POWERTABLE_HFP_SCO;
        sink = appHfpGetSink();
    }
#endif
#ifdef INCLUDE_AV
    if (pt_index == POWERTABLE_UNASSIGNED)
    {
        if (av_inst)
        {
            if (appA2dpIsStreaming(av_inst))
            {
                if (appA2dpIsSinkNonTwsCodec(av_inst))
                {
                    pt_index = POWERTABLE_A2DP_STREAMING_SINK;
                }
                else if (appA2dpIsSinkTwsCodec(av_inst))
                {
                    pt_index = POWERTABLE_A2DP_STREAMING_TWS_SINK;
                }
            }
            else if (!appA2dpIsDisconnected(av_inst))
            {
                pt_index = POWERTABLE_A2DP;
            }
            else if (appAvrcpIsConnected(av_inst))
            {
                pt_index = POWERTABLE_AVRCP;
            }
            sink = appAvGetSink(av_inst);
        }
    }
#endif

#ifdef INCLUDE_HFP
    if (pt_index == POWERTABLE_UNASSIGNED)
    {
        if (appHfpIsConnected())
        {
            pt_index = POWERTABLE_HFP;
            sink = appHfpGetSink();
        }
    }
#endif
    appLinkPolicySetPowerTableIndex(bd_addr, sink, pt_index, is_tws_plus_handset);
}

/*! \brief Allow role switching 

    Update the policy for the connection (if any) to the specified
    Bluetooth address, so as to allow future role switching.

    \param  bd_addr The Bluetooth address of the device
*/
void appLinkPolicyAllowRoleSwitch(const bdaddr *bd_addr)
{
    MAKE_PRIM_C(DM_HCI_WRITE_LINK_POLICY_SETTINGS_REQ);
    BdaddrConvertVmToBluestack(&prim->bd_addr, bd_addr);
    prim->link_policy_settings = ENABLE_MS_SWITCH | ENABLE_SNIFF;
    VmSendDmPrim(prim);
    DEBUG_LOG("appLinkPolicyAllowRoleSwitch");
}

/*! \brief Allow role switching
    \param sink The sink for which to allow role switching
*/
void appLinkPolicyAllowRoleSwitchForSink(Sink sink)
{
    tp_bdaddr bd_addr;
    if (SinkGetBdAddr(sink, &bd_addr))
    {
        appLinkPolicyAllowRoleSwitch(&bd_addr.taddr.addr);
    }
}

/*! \brief Prevent role switching 

    Update the policy for the connection (if any) to the specified
    Bluetooth address, so as to prevent any future role switching.

    \param  bd_addr The Bluetooth address of the device
*/
void appLinkPolicyPreventRoleSwitch(const bdaddr *bd_addr)
{
    MAKE_PRIM_C(DM_HCI_WRITE_LINK_POLICY_SETTINGS_REQ);
    BdaddrConvertVmToBluestack(&prim->bd_addr, bd_addr);
    prim->link_policy_settings = ENABLE_SNIFF;
    VmSendDmPrim(prim);
    DEBUG_LOG("appLinkPolicyPreventRoleSwitch");
}

/*! \brief Prevent role switching
    \param sink The sink for which to prevent role switching
*/
void appLinkPolicyPreventRoleSwitchForSink(Sink sink)
{
    tp_bdaddr bd_addr;
    if (SinkGetBdAddr(sink, &bd_addr))
    {
        appLinkPolicyPreventRoleSwitch(&bd_addr.taddr.addr);
    }
}

/*! \brief Update role of link

    This function is called whenver the role of a link has changed or been
    confirmed, it checks the Bluetooth Address of the updated link against
    the address of the HFP and A2DP links and updates the role of the matching
    link.
*/    
static void appLinkPolicyUpdateRole(Sink sink, hci_role role, hci_status status)
{
    lpTaskData *theLp = appGetLp();
               
    if (status == hci_success && role == hci_role_master)
    {
        tp_bdaddr bd_addr;
        if (SinkGetBdAddr(sink, &bd_addr))
            appLinkPolicyUpdateLinkSupervisionTimeout(&bd_addr.taddr.addr);

    }

    /* Find match in task list */
    Task task = 0;
    lpRoleTaskData data;
    while (appTaskListIterateWithData(theLp->role_task_list, &task, &data))
    {
        if (data.sink == sink)
        {
            DEBUG_LOGF("appLinkPolicyUpdateRole, sink %u, role %u, target_role %u",
                       sink, role, data.target_role);

            if (status == hci_success)
            {
                /* Set or get was successful, so update role */
                data.role = role;

                /* Check if role is now correct (or don't care) */
                if ((role == data.target_role) || (data.target_role == hci_role_dont_care))
                {
                    DEBUG_LOGF("appLinkPolicyUpdateRole, role correct");

                    /* Send message to task */
                    MAKE_LP_MESSAGE(APP_LP_ROLE_SWITCH_CFM);
                    message->role = role;
                    message->sink = sink;
                    MessageSend(task, APP_LP_ROLE_SWITCH_CFM, message);
                }
                else
                {
                    /* Role is incorrect, attempt role switch */
                    DEBUG_LOGF("appLinkPolicyUpdateRole, role incorrect, performing role-switch");
                    ConnectionSetRole(&theLp->task, data.sink, data.target_role);
                }
            }
            else if (status == hci_error_lmp_transaction_collision)
            {
                /* Collision, retry */
                if (data.state == LP_ROLE_STATE_GET)
                    ConnectionGetRole(&theLp->task, data.sink);
                else
                    ConnectionSetRole(&theLp->task, data.sink, data.target_role);
            }
            else
            {
                /* Give up, just mend message to task with current role */
                MAKE_LP_MESSAGE(APP_LP_ROLE_SWITCH_CFM);
                message->role = data.role;
                message->sink = sink;
                MessageSend(task, APP_LP_ROLE_SWITCH_CFM, message);
            }

            /* Update data associated with task */
            appTaskListSetDataForTask(theLp->role_task_list, task, &data);
            break;
        }
    }
}

/*! \brief Confirmation of link role

    This function is called to handle a CL_DM_ROLE_CFM message, this message is sent from the
    connection library in response to a call to ConnectionSetRole()/ConnectionGetRole().
    
    Extract the Bluetooth address of the link and call appLinkPolicyUpdateRole to update the
    role of the appropriate link.

    \param  cfm The received confirmation
*/    
static void appLinkPolicyHandleClDmRoleConfirm(CL_DM_ROLE_CFM_T *cfm)
{
    if (cfm->status == hci_success)
    {
        if (cfm->role == hci_role_master)
            DEBUG_LOGF("appLinkPolicyHandleClDmRoleConfirm, status=%d, role=master", cfm->status);
        else
            DEBUG_LOGF("appLinkPolicyHandleClDmRoleConfirm, status=%d, role=slave", cfm->status);
    }

    appLinkPolicyUpdateRole(cfm->sink, cfm->role, cfm->status);
}

/*! \brief Indication of link role

    This function is called to handle a CL_DM_ROLE_IND message, this message is sent from the
    connection library whenever the role of a link changes.
    
    Extract the Bluetooth address of the link and call appLinkPolicyUpdateRole to update the
    role of the appropriate link.  Call appLinkPolicyCheckRole to check if we need to perform
    a role switch.

    \param  ind The received indication
*/
static void appLinkPolicyHandleClDmRoleIndication(CL_DM_ROLE_IND_T *ind)
{
    if (ind->role == hci_role_master)
        DEBUG_LOG("appLinkPolicyHandleClDmRoleIndication, master");
    else
        DEBUG_LOG("appLinkPolicyHandleClDmRoleIndication, slave");
}


bool appLinkPolicyHandleConnectionLibraryMessages(MessageId id,Message message, bool already_handled)
{
    switch (id)
    {
        case CL_DM_ROLE_CFM:
            appLinkPolicyHandleClDmRoleConfirm((CL_DM_ROLE_CFM_T *)message);
            return TRUE;
        
        case CL_DM_ROLE_IND:
            appLinkPolicyHandleClDmRoleIndication((CL_DM_ROLE_IND_T *)message);
            return TRUE;
    }
    return already_handled;
}


void appLinkPolicySetSinkRole(Task client_task, Sink sink, hci_role role)
{
    lpRoleTaskData lp_data;
    lp_data.sink = sink;
    lp_data.target_role = role;
    lp_data.role = hci_role_dont_care;
    lp_data.state = LP_ROLE_STATE_GET;

    lpTaskData *theLp = appGetLp();

    /* Add client task to list along sith sink and required role */
    appTaskListRemoveTask(theLp->role_task_list, client_task);
    appTaskListAddTaskWithData(theLp->role_task_list, client_task, &lp_data);

    /* Get current role for this link */
    ConnectionGetRole(&theLp->task, sink);
}

void appLinkPolicyClearRole(Task client_task)
{
    lpTaskData *theLp = appGetLp();
    appTaskListRemoveTask(theLp->role_task_list, client_task);
}

/*! \brief Initialise link policy manager

    Call as startyp to initialise the link policy manager, set all
    the store rols to 'don't care'.
*/    
void appLinkPolicyInit(void)
{
    lpTaskData *theLp = appGetLp();   

    /* Initialise task list for holding pending role switch requests */
    theLp->role_task_list = appTaskListWithDataInit(sizeof(lpRoleTaskData));
}
