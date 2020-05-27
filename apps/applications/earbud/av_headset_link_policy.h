/*!
\copyright  Copyright (c) 2008 - 2017 Qualcomm Technologies International, Ltd.\n
            All Rights Reserved.\n
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    
\file       av_headset_link_policy.h
\brief	    Header file for the Link policy manager
*/

#ifndef __AV_HEADSET_LINK_POLICY_H
#define __AV_HEADSET_LINK_POLICY_H

#include "av_headset.h"

/*! Link policy task structure */
typedef struct
{
    TaskData task;              /*!< Link policy manager task */
    TaskList *role_task_list;   /*!< List of tasks with sinks that a requesting role switch */
} lpTaskData;

/*! Power table indexes */
typedef enum lp_power_table_index
{
    POWERTABLE_A2DP,
    POWERTABLE_AVRCP,
    POWERTABLE_HFP,
    POWERTABLE_HFP_SCO,
    POWERTABLE_A2DP_STREAMING_TWS_SINK,
    POWERTABLE_A2DP_STREAMING_SINK,

    POWERTABLE_PEER_STREAMING,      /* Used for peer links when SCO forwarding or A2DP streaming */
    POWERTABLE_PEER_IN_CASE,        /* Used for peer links when both EB are idle (in-case) */
    POWERTABLE_PEER_NOT_STREAMING,  /* Used for peer links when both EB are not streaming */

    /*! Must be the final value */
    POWERTABLE_UNASSIGNED,
} lpPowerTableIndex;

/*! Power table types */
typedef enum lp_power_table_set
{
    POWERTABLE_SET_NORMAL,      /*!< Power table set used is for normal use */
    POWERTABLE_SET_TWSPLUS,     /*!< Power table set that is used for TWS+ phones */
} lpPowerTableSet;


/*! Link policy state per ACL connection, stored for us by the connection manager. */
typedef struct
{
    lpPowerTableIndex pt_index;         /*!< Current powertable in use */
    lpPowerTableSet   table_set_used;   /*!< Set of powertables used */
} lpPerConnectionState;

/*! Enumeration for Earbuds in case state */
typedef enum
{
    EBS_NOT_BOTH_IN_CASE,   /*!< At least one Earbud is not in case */
    EBS_BOTH_IN_CASE,       /*!< Both Earbuds are in case */
    EBS_IN_CASE_UNKNOWN     /*!< Unknown */
} lpEbInCase;

/*! Enumeration for Earbuds streaming state */
typedef enum
{
    EBS_BOTH_NOT_STREAMING, /*!< Both Earbuds are not streaming */
    EBS_STREAMING,          /*!< At least one Earbud is streaming */
    EBS_STREAMING_UNKNOWN   /*!< Unknown */
} lpEbStreaming;



/*! \brief Message IDs from link policy to client tasks */
enum av_headset_lp_messages
{
    APP_LP_ROLE_SWITCH_CFM = APP_LP_MESSAGE_BASE,
    APP_LP_MESSAGE_TOP
};


/*! Message sent to client task indicating result of role-switch. */
typedef struct
{
    Sink sink;
    hci_role role;
} APP_LP_ROLE_SWITCH_CFM_T;



extern void appLinkPolicyInit(void);

/*! @brief Update the link supervision timeout.
    @param bd_addr The Bluetooth address of the peer device.
*/
extern void appLinkPolicyUpdateLinkSupervisionTimeout(const bdaddr *bd_addr);

/*! @brief
    @param in_case In case state of both Earbuds
    @param streaming Streaming state of both Earbuds
*/
extern void appLinkPolicyUpdatePeerPowerTable(lpEbInCase in_case, lpEbStreaming streaming);

/*! @brief Update the link power table based on the system state.
    @param bd_addr The Bluetooth address of the peer device.
*/
extern void appLinkPolicyUpdatePowerTable(const bdaddr *bd_addr);
extern void appLinkPolicyAllowRoleSwitch(const bdaddr *bd_addr);
extern void appLinkPolicyAllowRoleSwitchForSink(Sink sink);
extern void appLinkPolicyPreventRoleSwitch(const bdaddr *bd_addr);
extern void appLinkPolicyPreventRoleSwitchForSink(Sink sink);
extern void appLinkPolicyUpdateRoleFromSink(Sink sink);
extern void appLinkPolicySetSinkRole(Task client_task, Sink sink, hci_role role);
extern void appLinkPolicyClearRole(Task client_task);

/*! Handler for connection library messages not sent directly

    This function is called to handle any connection library messages sent to
    the application that the link policy module is interested in. If a message 
    is processed then the function returns TRUE.

    \note Some connection library messages can be sent directly as the 
        request is able to specify a destination for the response.

    \param  id              Identifier of the connection library message 
    \param  message         The message content (if any)
    \param  already_handled Indication whether this message has been processed by
                            another module. The handler may choose to ignore certain
                            messages if they have already been handled.

    \returns TRUE if the message has been processed, otherwise returns the
        value in already_handled
 */
extern bool appLinkPolicyHandleConnectionLibraryMessages(MessageId id,Message message, bool already_handled);


#endif /* __AV_HEADSET_LINK_POLICY_H */
