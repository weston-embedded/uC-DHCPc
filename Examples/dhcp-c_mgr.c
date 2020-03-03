/*
*********************************************************************************************************
*                                            EXAMPLE CODE
*
*               This file is provided as an example on how to use Micrium products.
*
*               Please feel free to use any application code labeled as 'EXAMPLE CODE' in
*               your application products.  Example code may be used as is, in whole or in
*               part, or may be used as a reference only. This file can be modified as
*               required to meet the end-product requirements.
*
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*
*                                               EXAMPLE
*
*                                            DHCPc MANAGER
*
* Filename : dhcp-c_mgr.c
* Version  : V2.11.00
*********************************************************************************************************
* Note(s)  : (1) This example shows how to initialize uC/DHCPc and manage DHCP following the interface link
*                state change (restart DHCP negotiation when the link move UP to DOWN to UP). This example use
*                an OS timer and it notifies the upper application when a new IP address is configured using a
*                callback function.
*
*            (2) This example can support :
*
*                  (a) 1 interface.
*                  (b) uC/TCPIP - V3.00.01
*                  (b) uCOS-III - V3.00
*
*            (2) This file is an example about how to use uC/DHCPc, It may not cover all case needed by a real
*                application. Also some modification might be needed, insert the code to perform the stated
*                actions wherever 'TODO' comments are found.
*
*                (a) For example, changes are required to support many interfaces or if DHCPc parameter must
*                    be requested.
*
*                (b) This example is not fully tested, so it is not guaranteed that all cases are cover
*                    properly.
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*********************************************************************************************************
*                                            INCLUDE FILES
*********************************************************************************************************
*********************************************************************************************************
*/

#include  <Source/os.h>
#include  <IP/IPv4/net_ipv4.h>
#include  <IF/net_if.h>
#include  <Source/dhcp-c.h>


/*
*********************************************************************************************************
*********************************************************************************************************
*                                              DATA TYPE
*********************************************************************************************************
*********************************************************************************************************
*/

typedef  void  (*APP_DHCP_CALLBACK)(NET_IF_LINK_STATE link_state,
                                    DHCPc_STATUS      status,
                                    NET_IPv4_ADDR     host_addr);


/*
*********************************************************************************************************
*********************************************************************************************************
*                                          GLOBAL VARIABLES
*********************************************************************************************************
*********************************************************************************************************
*/

OS_TMR             AppDHCPcMgr_Tmr;
NET_IF_NBR         AppDHCPcMgr_IF_Nbr;
APP_DHCP_CALLBACK  AppDHCPcMgr_Callback;
DHCPc_STATUS       AppDHCPcMgr_LastStatus = DHCP_STATUS_NONE;


/*
*********************************************************************************************************
*********************************************************************************************************
*                                          FUNCTION PROTOTYPE
*********************************************************************************************************
*********************************************************************************************************
*/

static  CPU_BOOLEAN  AppDHCPcMgr_Start            (NET_IF_NBR          if_nbr);
static  CPU_BOOLEAN  AppDHCPcMgr_Stop             (NET_IF_NBR          if_nbr);

static  void         AppDHCPcMgr_IF_LinkSubscriber(NET_IF_NBR          if_nbr,
                                                   NET_IF_LINK_STATE   state);

static  void         AppDHCPcMgr_CheckState       (void               *p_tmr,
                                                   void               *p_arg);


/*
*********************************************************************************************************
*                                         AppDHCPcMgr_Init()
*
* Description : (1) This function initialize uC/DHCPc, DHCPc Manager objects and start DHCPc for the interface if
*                   the link is up:
*
*                   (a)
*
* Argument(s) : if_nbr      ID of the interface to manage.
*
*               callback    Callback function to call when an address has been obtained or the process has failed.
*
* Return(s)   : DEF_OK,   Successfully initialized uc/DHCPc and the interface will be managed.
*
*               DEF_FAIL, Initialization failed.
*
* Caller(s)   : Application.
*
* Note(s)     : (1) Prior to performing any calls to DHCPc, the module must first be initialized. If the process
*                   succeeds, the DHCP client tasks are started, and its various data structures are initialized.
*********************************************************************************************************
*/

CPU_BOOLEAN  AppDHCPcMgr_Init (NET_IF_NBR         if_nbr,
                               APP_DHCP_CALLBACK  callback)
{
    NET_IF_LINK_STATE  state;
    CPU_BOOLEAN        result;
    OS_ERR             err_os;
    DHCPc_ERR          err_dhcp;
    NET_ERR            err_net;


    AppDHCPcMgr_Callback = callback;
    AppDHCPcMgr_IF_Nbr   = if_nbr;


                                                                /* --------------- INITIALIZE uC/DHCPc ---------------- */
    err_dhcp = DHCPc_Init();                                    /* See Note #1.                                         */
    if (err_dhcp != DHCPc_ERR_NONE) {
        return (DEF_FAIL);
    }


                                                                /* ------- SUBCRIBE TO LINK CHANGE NOTIFICATION ------- */
    NetIF_LinkStateSubscribe(if_nbr,                            /* Subscribe a function to be notified when the link    */
                            &AppDHCPcMgr_IF_LinkSubscriber,     /* state of the interface change.                       */
                            &err_net);
    if (err_net != NET_IF_ERR_NONE) {
        return (DEF_FAIL);
    }



                                                                /* ---- CREATE AN OS TIMER TO MONITOR DHCPc STATUS ---- */
#if (OS_VERSION > 30000)
    OSTmrCreate(&AppDHCPcMgr_Tmr,
                "App DHCPc Mgr Timer",
                 0,
                 OSCfg_TickRate_Hz,
                 OS_OPT_TMR_PERIODIC,
                &AppDHCPcMgr_CheckState,
                &AppDHCPcMgr_IF_Nbr,
                &err_os);
#else
    AppDHCPcMgr_Tmr = *OSTmrCreate(0u,
                                   OS_TMR_CFG_TICKS_PER_SEC,
                                   OS_TMR_OPT_PERIODIC,
                                   &AppDHCPcMgr_CheckState,
                                   &AppDHCPcMgr_IF_Nbr,
                                   "App DHCPc Mgr Timer",
                                   &err_os);
#endif
    if (err_os != OS_ERR_NONE) {
        return (DEF_FAIL);
    }


                                                                /* ------------ GET CURRENT IF LINK STATE ------------- */
    state  = NetIF_LinkStateGet(if_nbr, &err_net);
    switch (state) {
        case NET_IF_LINK_UP:                                    /* If link is already up ...                            */
             result = AppDHCPcMgr_Start(if_nbr);                /* Start DHCPc on this interface.                       */
             break;


        case NET_IF_LINK_DOWN:                                  /* If link is down, let the subscriber function ...     */
             result = DEF_OK;                                   /* start DHCPc on the interface when the link will ...  */
             break;                                             /* come up.                                             */

        default:
             return (DEF_FAIL);
    }

    return (result);
}


/*
*********************************************************************************************************
*********************************************************************************************************
*                                           LOCAL FUNCTIONS
*********************************************************************************************************
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                          AppDHCPcMgr_Start()
*
* Description : This function start DHCP negotiation on the interface and start the manager timer which is
*               responsible to monitor the DHCP result and call the callback function.
*
* Argument(s) : if_nbr  Interface ID.
*
* Return(s)   : DEF_OK,   Successfully started.
*
*               DEF_FAIL, Start failed.
*
* Caller(s)   : AppInit_DHCPc(),
*               AppLinkStateSubscriber().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  CPU_BOOLEAN  AppDHCPcMgr_Start (NET_IF_NBR  if_nbr)
{
    OS_ERR     err_os;
    DHCPc_ERR  err_dhcp;


                                                                /* ------ START DHCP FOR THE SELECTED INTERFACE ------- */
    DHCPc_Start(if_nbr, DEF_NULL, 0, &err_dhcp);
    if (err_dhcp != DHCPc_ERR_NONE) {
        return (DEF_FAIL);
    }


                                                                /* ------------- START THE MANAGER TIMER -------------- */
    (void)OSTmrStart(&AppDHCPcMgr_Tmr, &err_os);
    if (err_os != OS_ERR_NONE) {
        return (DEF_FAIL);
    }

    return (DEF_OK);
}


/*
*********************************************************************************************************
*                                          AppDHCPcMgr_Stop()
*
* Description : This function stop DHCP negotiation on the interface and stop the manager timer.
*
* Argument(s) : if_nbr  Interface ID.
*
* Return(s)   : DEF_OK,   Successfully stopped.
*
*               DEF_FAIL, Stop failed.
*
* Caller(s)   : AppLinkStateSubscriber().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  CPU_BOOLEAN  AppDHCPcMgr_Stop (NET_IF_NBR  if_nbr)
{
    OS_ERR     err_os;
    DHCPc_ERR  err_dhcp;


                                                                /* --------- STOP DHCP ON SELECTED INTERFACE ---------- */
    DHCPc_Stop(if_nbr, &err_dhcp);


                                                                /* -------------- STOP THE MANAGER TIMER -------------- */
#if (OS_VERSION > 30000)
   (void)OSTmrStop(&AppDHCPcMgr_Tmr, OS_OPT_TMR_NONE, DEF_NULL, &err_os);
#else
   (void)OSTmrStop(&AppDHCPcMgr_Tmr, OS_TMR_OPT_NONE, DEF_NULL, &err_os);
#endif
    if (err_os != OS_ERR_NONE) {
        return (DEF_FAIL);
    }

    AppDHCPcMgr_LastStatus = DHCP_STATUS_NONE;
    return (DEF_OK);
}



/*
*********************************************************************************************************
*                                   AppDHCPcMgr_LinkStateSubscriber()
*
* Description : This function is called every time the link stage of the interface change.
*
* Argument(s) :  if_nbr  Interface ID.
*
*               state    Current link state
*
* Return(s)   : none.
*
* Caller(s)   : uC/TCP-IP - Created by AppDHCPcMgr_Init().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  void  AppDHCPcMgr_IF_LinkSubscriber (NET_IF_NBR         if_nbr,
                                             NET_IF_LINK_STATE  state)
{
    switch (state) {
        case NET_IF_LINK_UP:                                    /* Link is back to up ...                               */
             (void)AppDHCPcMgr_Start(if_nbr);                   /* Start acquiring an address on this interface.        */
             break;

        case NET_IF_LINK_DOWN:                                  /* Link is down ...                                     */
             (void)AppDHCPcMgr_Stop(if_nbr);                    /* Stop DHCP and remove address.                        */
             if (AppDHCPcMgr_Callback != DEF_NULL) {            /* Notify the application about it.                     */
                 AppDHCPcMgr_Callback(NET_IF_LINK_DOWN, DHCP_STATUS_NONE, NET_IPv4_ADDR_NONE);
             }
             break;

        default:
            break;
    }
}


/*
*********************************************************************************************************
*                                       AppDHCPcMgr_CheckState()
*
* Description : This function is periodically called to monitor the DHCPc status and result. The callback
*               function is called when an address is configured or the negotiation has failed.
*
* Argument(s) : p_tmr   Pointer to OS timer
*
*               p_arg   Function argument pointer.
*
* Return(s)   : none.
*
* Created by  : AppDHCPcMgr_Init().
*
* Note(s)     : (1) Once the DHCP management of an interface has been started, the application may want to check the
*                   status of the lease negotiation in order to determine whether or not the interface has been properly
*                   configured:
*
*                   (a) Status DHCP_STATUS_CFG_IN_PROGRESS means that the negotiation is still underway.
*
*                   (b) Status DHCP_STATUS_CFGD indicates that the DHCP negotiation is done and that the interface is
*                       properly configured.
*
*                   (c) Status DHCP_STATUS_CFGD_NO_TMR specifies that the DHCP negotiation is done and that the interface
*                       is properly configured, but no timer has been set for renewing the lease. The effect of this is
*                       that the lease is going to be permanent, even though the server might have set a time limit for it.
*
*                   (d) Status DHCP_STATUS_CFGD_LOCAL_LINK means that the DHCP negotiation was not successful, and that a
*                       link-local address has been attributed to the interface. It is important to note that the DHCP
*                       client will not try to negotiate a lease with a server at this point.
*
*                   (e) Status DHCP_STATUS_FAIL denotes a negotiation error. At this point, the application should call
*                       the DHCPc_Stop() function and decide what to do next.
*********************************************************************************************************
*/

static  void  AppDHCPcMgr_CheckState (void *p_tmr,
                                      void *p_arg)
{
    NET_IPv4_ADDR     addr_tbl[NET_IPv4_CFG_IF_MAX_NBR_ADDR];
    NET_IP_ADDRS_QTY  addr_ip_tbl_qty;
    NET_IF_NBR       *p_if_nbr;
    DHCPc_STATUS      status;
    NET_ERR           err_net;
    DHCPc_ERR         err_dhcp;


    p_if_nbr = (NET_IF_NBR *)p_arg;
    status   =  DHCPc_ChkStatus(*p_if_nbr, &err_dhcp);          /* See Note #1.                                         */
    switch (status) {
        case DHCP_STATUS_CFGD:                                  /* IF an IP address has been configured.                */
        case DHCP_STATUS_CFGD_NO_TMR:
        case DHCP_STATUS_CFGD_LOCAL_LINK:
             if ((AppDHCPcMgr_LastStatus   != status) &&        /* The state has changed.                               */
                 (AppDHCPcMgr_Callback != DEF_NULL)){
                  addr_ip_tbl_qty = sizeof(addr_tbl) / sizeof(NET_IPv4_ADDR);
                 (void)NetIPv4_GetAddrHost(*p_if_nbr,           /* Get current address configured.                      */
                                           &addr_tbl[0],
                                           &addr_ip_tbl_qty,
                                           &err_net);

                  AppDHCPcMgr_Callback(NET_IF_LINK_UP,          /* Notify the application about the address configured. */
                                           status,
                                           addr_tbl[0]);
             }
             break;


        case DHCP_STATUS_FAIL:                                  /* Acquiring an address has failed.                     */
             if (AppDHCPcMgr_Callback != DEF_NULL) {
                 AppDHCPcMgr_Callback(NET_IF_LINK_UP,           /* Notify the application about the failing ...         */
                                          status,               /* The application could configure a static address ... */
                                          NET_IPv4_ADDR_NONE);  /* in the callback function when it fails.              */
             }
             AppDHCPcMgr_Stop(*p_if_nbr);
             break;


        case DHCP_STATUS_NONE:
        case DHCP_STATUS_CFG_IN_PROGRESS:
        default:
             break;
    }


    AppDHCPcMgr_LastStatus = status;
}

