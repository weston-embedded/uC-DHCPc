/*
*********************************************************************************************************
*                                              uC/DHCPc
*                             Dynamic Host Configuration Protocol Client
*
*                    Copyright 2004-2020 Silicon Laboratories Inc. www.silabs.com
*
*                                 SPDX-License-Identifier: APACHE-2.0
*
*               This software is subject to an open source license and is distributed by
*                Silicon Laboratories Inc. pursuant to the terms of the Apache License,
*                    Version 2.0 available at www.apache.org/licenses/LICENSE-2.0.
*
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*
*                                             DHCP CLIENT
*
* Filename : dhcp-c.c
* Version  : V2.11.00
*********************************************************************************************************
* Note(s)  : (1) Supports Dynamic Host Configuration Protocol as described in RFC #2131 with the
*                following features/restrictions/constraints :
*
*                (a) Dynamic Configuration of IPv4 Link-Local Addresses       RFC #3927
*                (b) Supports both infinite & temporary address leases,
*                        with automatic renewal of lease if necessary
*
*            (2) To protect the validity & prevent the corruption of shared DHCP client resources,
*                the primary tasks of the DHCP client are prevented from running concurrently
*                through the use of a global DHCPc lock implementing protection by mutual exclusion.
*
*                (a) The mechanism of protected mutual exclusion is irrelevant but MUST be implemented
*                    in the following two functions :
*
*                        DHCPc_OS_Lock()                       acquire access to DHCP client
*                        DHCPc_OS_Unlock()                     release access to DHCP client
*
*                    implemented in
*
*                        \<DHCPc>\OS\<os>\dhcp-c_os.*
*
*                        where
*                                <DHCPc>                       directory path for DHCPc module
*                                <os>                          directory name for specific OS
*
*                (b) Since this global lock implements mutual exclusion at the DHCP client task
*                    level, critical sections are NOT required to prevent task-level concurrency in
*                    the DHCP client.
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                            INCLUDE FILES
*********************************************************************************************************
*/

#define    MICRIUM_SOURCE
#define    DHCPc_MODULE
#include  "dhcp-c.h"
#include  <Source/net_util.h>
#include  <Source/net_cfg_net.h>
#include  <Source/net_sock.h>
#include  <IP/IPv4/net_ipv4.h>
#include  <IP/IPv4/net_arp.h>
#include  <IF/net_if.h>
#include  <IF/net_if_802x.h>
#include  <Source/net_app.h>


/*
*********************************************************************************************************
*                                            LOCAL DEFINES
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                           LOCAL CONSTANTS
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                          LOCAL DATA TYPES
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                            LOCAL TABLES
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                       LOCAL GLOBAL VARIABLES
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                      LOCAL FUNCTION PROTOTYPES
*********************************************************************************************************
*/

#ifdef  NET_IPv4_MODULE_EN
                                                                                    /* -------- IF INFO FNCTS --------- */
static  void            DHCPc_IF_InfoInit            (DHCPc_ERR          *perr);

static  DHCPc_IF_INFO  *DHCPc_IF_InfoGet             (NET_IF_NBR          if_nbr,
                                                      DHCPc_ERR          *perr);

static  DHCPc_IF_INFO  *DHCPc_IF_InfoGetCfgd         (NET_IF_NBR          if_nbr);

static  void            DHCPc_IF_InfoFree            (DHCPc_IF_INFO      *pif_info);

static  void            DHCPc_IF_InfoClr             (DHCPc_IF_INFO      *pif_info);



                                                                                    /* ---------- MSG FNCTS ----------- */
static  void            DHCPc_MsgInit                (DHCPc_ERR          *perr);

static  void            DHCPc_MsgRxHandler           (DHCPc_COMM         *pcomm);

static  DHCPc_MSG      *DHCPc_MsgGet                 (DHCPc_ERR          *perr);

static  CPU_INT08U     *DHCPc_MsgGetOpt              (DHCPc_OPT_CODE      opt_code,
                                                      CPU_INT08U         *pmsg_buf,
                                                      CPU_INT16U          msg_buf_size,
                                                      CPU_INT08U         *popt_val_len);

static  void            DHCPc_MsgFree                (DHCPc_MSG          *pmsg);

static  void            DHCPc_MsgClr                 (DHCPc_MSG          *pmsg);



                                                                                    /* ---------- COMM FNCTS ---------- */
static  void            DHCPc_CommInit               (DHCPc_ERR          *perr);

static  DHCPc_COMM     *DHCPc_CommGet                (NET_IF_NBR          if_nbr,
                                                      DHCPc_COMM_MSG      comm_msg,
                                                      DHCPc_ERR          *perr);

static  void            DHCPc_CommFree               (DHCPc_COMM         *pcomm);

static  void            DHCPc_CommClr                (DHCPc_COMM         *pcomm);



                                                                                    /* ---------- TMR FNCTS ----------- */
static  void            DHCPc_TmrInit                (DHCPc_ERR          *perr);

static  void            DHCPc_TmrCfg                 (DHCPc_IF_INFO      *pif_info,
                                                      DHCPc_COMM_MSG      tmr_msg,
                                                      CPU_INT32U          time_sec,
                                                      DHCPc_ERR          *perr);

static  DHCPc_TMR      *DHCPc_TmrGet                 (void               *pobj,
                                                      DHCPc_TMR_TICK      time,
                                                      DHCPc_ERR          *perr);

static  void            DHCPc_TmrFree                (DHCPc_TMR          *ptmr);

static  void            DHCPc_TmrClr                 (DHCPc_TMR          *ptmr);


                                                                                    /* ----- STATE HANDLER FNCTS ------ */
static  NET_SOCK_ID     DHCPc_InitSock               (NET_IPv4_ADDR       ip_addr_local,
                                                      NET_IF_NBR          if_nbr);


static  void            DHCPc_InitStateHandler       (DHCPc_IF_INFO      *pif_info,
                                                      DHCPc_ERR          *perr);

static  void            DHCPc_RenewRebindStateHandler(DHCPc_IF_INFO      *pif_info,
                                                      DHCPc_COMM_MSG      exp_tmr_msg,
                                                      DHCPc_ERR          *perr);

static  void            DHCPc_StopStateHandler       (DHCPc_IF_INFO      *pif_info,
                                                      DHCPc_ERR          *perr);


static  void            DHCPc_Discover               (NET_SOCK_ID         sock_id,
                                                      DHCPc_IF_INFO      *pif_info,
                                                      CPU_INT08U         *paddr_hw,
                                                      CPU_INT08U          addr_hw_len,
                                                      DHCPc_ERR          *perr);

static  void            DHCPc_Req                    (NET_SOCK_ID         sock_id,
                                                      DHCPc_IF_INFO      *pif_info,
                                                      CPU_INT08U         *paddr_hw,
                                                      CPU_INT08U          addr_hw_len,
                                                      DHCPc_ERR          *perr);

static  void            DHCPc_DeclineRelease         (NET_SOCK_ID         sock_id,
                                                      DHCPc_IF_INFO      *pif_info,
                                                      DHCPc_MSG_TYPE      msg_type,
                                                      CPU_INT08U         *paddr_hw,
                                                      CPU_INT08U          addr_hw_len,
                                                      DHCPc_ERR          *perr);


static  CPU_INT16U      DHCPc_CalcBackOff            (CPU_INT16U          timeout_ms);



                                                                                    /* ---------- ADDR FNCTS ---------- */
#if ((DHCPc_CFG_ADDR_VALIDATE_EN       == DEF_ENABLED) || \
     (DHCPc_CFG_DYN_LOCAL_LINK_ADDR_EN == DEF_ENABLED))
static  void            DHCPc_AddrValidate           (NET_IF_NBR          if_nbr,
                                                      NET_IPv4_ADDR       addr_target,
                                                      CPU_INT32U          dly_ms,
                                                      DHCPc_ERR          *perr);
#endif

static  void            DHCPc_AddrCfg                (DHCPc_IF_INFO      *pif_info,
                                                      DHCPc_ERR          *perr);

#if (DHCPc_CFG_DYN_LOCAL_LINK_ADDR_EN == DEF_ENABLED)
static  void            DHCPc_AddrLocalLinkCfg       (DHCPc_IF_INFO      *pif_info,
                                                      CPU_INT08U         *paddr_hw,
                                                      CPU_INT08U          addr_hw_len,
                                                      DHCPc_ERR          *perr);

static  NET_IPv4_ADDR   DHCPc_AddrLocalLinkGet       (CPU_INT08U         *paddr_hw,
                                                      CPU_INT08U          addr_hw_len);
#endif



                                                                                    /* --------- LEASE FNCTS ---------- */
static  void            DHCPc_LeaseTimeCalc          (DHCPc_IF_INFO      *pif_info,
                                                      DHCPc_ERR          *perr);

static  void            DHCPc_LeaseTimeUpdate        (DHCPc_IF_INFO      *pif_info,
                                                      DHCPc_COMM_MSG      exp_tmr_msg,
                                                      DHCPc_ERR          *perr);


                                                                                    /* ----------- RX FNCTS ----------- */
static  DHCPc_MSG_TYPE  DHCPc_RxReply                (NET_SOCK_ID         sock_id,
                                                      DHCPc_IF_INFO      *pif_info,
                                                      NET_IPv4_ADDR       server_id,
                                                      CPU_INT08U         *paddr_hw,
                                                      CPU_INT08U          addr_hw_len,
                                                      CPU_INT08U         *pmsg_buf,
                                                      CPU_INT16U         *pmsg_buf_len,
                                                      DHCPc_ERR          *perr);

static  CPU_INT16U      DHCPc_Rx                     (NET_SOCK_ID         sock_id,
                                                      void               *pdata_buf,
                                                      CPU_INT16U          data_buf_len,
                                                      NET_SOCK_ADDR      *paddr_remote,
                                                      NET_SOCK_ADDR_LEN  *paddr_remote_len,
                                                      DHCPc_ERR          *perr);


                                                                                    /* ----------- TX FNCTS ----------- */
static  CPU_INT16U      DHCPc_TxMsgPrepare           (DHCPc_IF_INFO      *pif_info,
                                                      DHCPc_MSG_TYPE      msg_type,
                                                      CPU_INT08U         *paddr_hw,
                                                      CPU_INT08U          addr_hw_len,
                                                      CPU_INT08U         *pmsg_buf,
                                                      CPU_INT16U          msg_buf_size,
                                                      DHCPc_ERR          *perr);

static  void            DHCPc_Tx                     (NET_SOCK_ID         sock_id,
                                                      void               *pdata_buf,
                                                      CPU_INT16U          data_buf_len,
                                                      NET_SOCK_ADDR      *paddr_remote,
                                                      NET_SOCK_ADDR_LEN   addr_remote_len,
                                                      DHCPc_ERR          *perr);

#endif


/*
*********************************************************************************************************
*                                           INITIALIZED DATA
*
* Note(s) : (1) This array is used for requesting parameters from the DHCP server.  Do NOT modify data type.
*********************************************************************************************************
*/

static  const  DHCPc_OPT_CODE  DHCPc_ReqParam[] = {
    DHCP_OPT_SUBNET_MASK,
    DHCP_OPT_ROUTER,
    DHCP_OPT_DOMAIN_NAME_SERVER,
    DHCP_OPT_TIME_OFFSET
};


/*
*********************************************************************************************************
*                                     LOCAL CONFIGURATION ERRORS
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                            DHCPc_Init()
*
* Description : (1) Initialize DHCP client :
*
*                   (a) Initialize DHCP client global variables
*                   (b) Initialize DHCP client information & message buffer
*                   (c) Initialize DHCP client counters
*                   (d) Initialize DHCP client global OS objects
*                   (e) Signal ALL DHCP client modules that DHCP client initialization is complete
*                   (f) Start      DHCP client timer
*
*
* Argument(s) : none.
*
* Return(s)   : DHCPc_ERR_NONE,                                   if NO errors.
*
*               Specific initialization error code (see Note #4), otherwise.
*
* Caller(s)   : Your Product's Application.
*
*               This function is a DHCP client initialization function & MAY be called by
*               application/initialization function(s).
*
* Note(s)     : (2) DHCPc_Init() MUST be called ...
*
*                   (a) AFTER  product's OS has been initialized
*                   (b) BEFORE product's application calls any DHCP client function(s)
*
*               (3) DHCPc_Init() MUST ONLY be called ONCE from product's application.
*
*               (4) (a) If any DHCP initialization error occurs, any remaining DHCP initialization is
*                       immediately aborted & the specific initialization error code is returned.
*
*                   (b) DHCP error codes are listed in 'dhcp-c.h'.  A search of the specific error code
*                       number(s) provides the corresponding error code label(s).  A search of the error
*                       code label(s) provides the source code location of the DHCP initialization
*                       error(s).
*********************************************************************************************************
*/

DHCPc_ERR  DHCPc_Init (void)
{
#ifdef  NET_IPv4_MODULE_EN
    CPU_INT08U  i;
    DHCPc_ERR   err;


    DHCPc_InitDone = DEF_NO;                                    /* Block DHCPc fncts/tasks until init complete.         */

                                                                /* -------------- INIT DHCPc GLOBAL VAR --------------- */


                                                                /* ------- INIT DHCPc INFO, MSG BUF, & COMM OBJ ------- */
    DHCPc_IF_InfoInit(&err);                                    /* Create DHCPc IF Info  pool.                          */
    if (err != DHCPc_ERR_NONE) {
        return (err);
    }

    DHCPc_MsgInit(&err);                                        /* Create DHCPc msg      pool.                          */
    if (err != DHCPc_ERR_NONE) {
        return (err);
    }

    DHCPc_CommInit(&err);                                       /* Create DHCPc comm obj pool.                          */
    if (err != DHCPc_ERR_NONE) {
        return (err);
    }

                                                                /* ----------------- INIT DHCPc CTRS ------------------ */
#if 0 && (DHCPc_CFG_CTR_EN     == DEF_ENABLED)
                                                                /* #### NOT implemented.                                */
#endif

                                                                /* --------------- INIT DHCPc ERR CTRS ---------------- */
#if 0 && (DHCPc_CFG_CTR_ERR_EN == DEF_ENABLED)
                                                                /* #### NOT implemented.                                */
#endif

                                                                /* -------------- PERFORM DHCPc/OS INIT --------------- */
    DHCPc_OS_Init(&err);                                        /* Create DHCPc obj(s).                                 */
    if (err != DHCPc_OS_ERR_NONE) {
        return (err);
    }

                                                                /* --------- INIT DHCPc TIMER & TASK MODULES ---------- */
    DHCPc_TmrInit(&err);
    if (err != DHCPc_ERR_NONE) {
        return (err);
    }

                                                                /* ------------ SIGNAL DHCPc INIT COMPLETE ------------ */
    DHCPc_InitDone = DEF_YES;                                   /* Signal     DHCPc fncts/tasks that init complete.     */

    for (i = 0; i < DHCPc_TASK_NBR; i++) {                      /* Signal ALL DHCPc tasks       that init complete.     */
        DHCPc_OS_InitSignal(&err);
        if (err != DHCPc_OS_ERR_NONE) {
            DHCPc_InitDone = DEF_NO;
            return (err);
        }
    }

                                                                /* ----------------- START DHCPc TMR ------------------ */
    DHCPc_OS_TmrStart(&err);
    if (err != DHCPc_OS_ERR_NONE) {
        DHCPc_InitDone = DEF_NO;
        return (err);
    }


    return (DHCPc_ERR_NONE);
#else
    return (DHCPc_ERR_IPv4_NOT_PRESENT);
#endif
}


/*
*********************************************************************************************************
*                                            DHCPc_Start()
*
* Description : (1) Start DHCP address configuration/management on specified interface :
*
*                   (a) Acquire  DHCPc lock
*                   (b) Get      interface information structure
*                   (c) Copy     requested DHCP options.
*                   (d) Post     message to DHCP client task
*                   (e) Release  DHCPc lock
*
*
* Argument(s) : if_nbr              Interface number to start DHCP configuration/management.
*
*               preq_param_tbl      Pointer to table of requested DHCP parameters.
*
*               req_param_tbl_qty   Size of requested parameter table.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  Address DHCP negotiation successfully started.
*                               DHCPc_ERR_NULL_PTR              Argument 'preq_param_tbl' passed a NULL pointer.
*                               DHCPc_ERR_INIT_INCOMPLETE       DHCP client initialization NOT complete.
*                               DHCPc_ERR_IF_INVALID            Interface invalid or disabled.
*                               DHCPc_ERR_PARAM_REQ_TBL_SIZE    Requested parameter table size too small.
*                               DHCPc_ERR_MSG_Q                 Error posting start command to message queue.
*
*                                                               -------- RETURNED BY DHCPc_OS_Lock() : ---------
*                               DHCPc_OS_ERR_LOCK               DHCPc access NOT acquired.
*
*                                                               ------- RETURNED BY DHCPc_IF_InfoGet() : -------
*                               DHCPc_ERR_IF_INFO_IF_USED       Interface information already in use.
*                               DHCPc_ERR_INVALID_HW_ADDR       Error retrieving interface's hardware address.
*                               DHCPc_ERR_IF_INFO_NONE_AVAIL    Interface information pool empty.
*
*                                                               -------- RETURNED BY DHCPc_CommGet() : ---------
*                               DHCPc_ERR_COMM_NONE_AVAIL       Communication object pool empty.
*
* Return(s)   : none.
*
* Caller(s)   : Application.
*
*               This function is a DHCP client application programming interface (API) function &
*               MAY be called by application function(s).
*
* Note(s)     : (2) DHCPc_Start() MUST be called AFTER the interface has been properly configured &
*                   enabled.  Failure to do so could cause unknown results.
*
*               (3) DHCPc_Start() NOT executed until DHCP client initialization completes.
*
*               (4) DHCPc_Start() blocks ALL other DHCP client tasks by pending on & acquiring the
*                   global DHCPc lock (see dhcp-c.h  Note #2').
*
*               (5) DHCPc_Start() execution is asynchronous--i.e. interface will NOT necessarily be
*                   started upon return from this function.  The application SHOULD periodically call
*                   DHCPc_ChkStatus() until the interface's DHCP management is successfully started
*                   and configured.
*********************************************************************************************************
*/

void  DHCPc_Start (NET_IF_NBR       if_nbr,
                   DHCPc_OPT_CODE  *preq_param_tbl,
                   CPU_INT08U       req_param_tbl_qty,
                   DHCPc_ERR       *perr)
{
#ifdef  NET_IPv4_MODULE_EN
    CPU_BOOLEAN      if_en;
    DHCPc_IF_INFO   *pif_info;
    DHCPc_COMM      *pcomm;
    DHCPc_COMM_MSG   comm_msg;
    NET_ERR          err_net;


#if (DHCPc_CFG_ARG_CHK_EXT_EN == DEF_ENABLED)
    if (req_param_tbl_qty > 0) {
        if (preq_param_tbl == (DHCPc_OPT_CODE *)0) {
           *perr = DHCPc_ERR_NULL_PTR;
            return;
        }
    }
#endif

    if (DHCPc_InitDone != DEF_YES) {                            /* If init NOT complete, exit (see Note #3).            */
       *perr = DHCPc_ERR_INIT_INCOMPLETE;
        return;
    }


    if_en  = NetIF_IsEnCfgd(if_nbr, &err_net);                  /* Validate IF en.                                      */
    if (if_en != DEF_YES) {
       *perr = DHCPc_ERR_IF_INVALID;
        return;
    }

    if (req_param_tbl_qty > DHCPc_CFG_PARAM_REQ_TBL_SIZE) {     /* If param req qty > param req tbl, ...                */
       *perr = DHCPc_ERR_PARAM_REQ_TBL_SIZE;                    /* ... rtn err.                                         */
        return;
    }

                                                                /* ---------------- ACQUIRE DHCPc LOCK ---------------- */
    DHCPc_OS_Lock(perr);                                        /* See Note #4.                                         */
    if (*perr != DHCPc_OS_ERR_NONE) {
        return;
    }

                                                                /* ------------------- GET IF INFO -------------------- */
    pif_info = DHCPc_IF_InfoGet(if_nbr, perr);
    if (*perr != DHCPc_ERR_NONE) {
        DHCPc_OS_Unlock();
        return;
    }

    pif_info->ClientState = DHCP_STATE_INIT;                    /* Client in INIT state.                                */


                                                                /* ----------------- COPY REQ DHCP OPT -----------------*/
    Mem_Copy((void     *)&pif_info->ParamReqTbl[0],
             (void     *) preq_param_tbl,
             (CPU_SIZE_T) req_param_tbl_qty);

    pif_info->ParamReqQty = req_param_tbl_qty;

                                                                /* -------------- POST MSG TO DHCP TASK --------------- */
    comm_msg = DHCPc_COMM_MSG_START;
    pcomm    = DHCPc_CommGet(if_nbr, comm_msg, perr);
    if (*perr != DHCPc_ERR_NONE) {
        DHCPc_IF_InfoFree(pif_info);
        DHCPc_OS_Unlock();
        return;
    }

    DHCPc_OS_MsgPost((void      *)pcomm,
                     (DHCPc_ERR *)perr);
    if (*perr != DHCPc_OS_ERR_NONE) {
       *perr = DHCPc_ERR_MSG_Q;
        DHCPc_CommFree(pcomm);
        DHCPc_IF_InfoFree(pif_info);
        DHCPc_OS_Unlock();
        return;
    }

                                                                /* ---------------- RELEASE DHCPc LOCK ---------------- */
    DHCPc_OS_Unlock();

   *perr = DHCPc_ERR_NONE;
#else
   *perr = DHCPc_ERR_IPv4_NOT_PRESENT;
#endif
}


/*
*********************************************************************************************************
*                                            DHCPc_Stop()
*
* Description : (1) Stop DHCP address configuration/management on specified interface :
*
*                   (a) Acquire  DHCPc lock
*                   (b) Post     message to DHCP client task
*                   (c) Release  DHCPc lock
*
*
* Argument(s) : if_nbr      Interface number to stop DHCP management.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  Interface DHCP configuration successfully
*                                                                   stopped.
*                               DHCPc_ERR_INIT_INCOMPLETE       DHCP client initialization NOT complete.
*                               DHCPc_ERR_IF_NOT_MANAGED        Interface NOT managed by the DHCP client.
*                               DHCPc_ERR_MSG_Q                 Error posting stop command to message queue.
*
*                                                               ------ RETURNED BY DHCPc_OS_Lock() : -------
*                               DHCPc_OS_ERR_LOCK               DHCPc access NOT acquired.
*
*                                                               ------ RETURNED BY DHCPc_CommGet() : -------
*                               DHCPc_ERR_COMM_NONE_AVAIL       Communication object pool empty.
*
* Return(s)   : none.
*
* Caller(s)   : Application.
*
*               This function is a DHCP client application programming interface (API) function & MAY be called by
*               application function(s).
*
* Note(s)     : (2) DHCPc_Stop() MUST be called PRIOR to disable any interface having been configured
*                   using DHCP.  Failure to do so could cause unknown behaviors.
*
*               (3) DHCPc_Stop() NOT executed until DHCP client initialization completes.
*
*               (4) DHCPc_Stop() blocks ALL other DHCP client tasks by pending on & acquiring the global
*                   DHCPc lock (see dhcp-c.h  Note #2').
*
*               (5) DHCPc_Stop() execution is asynchronous--i.e. interface will NOT necessarily be
*                   stopped upon return from this function.  The application SHOULD periodically call
*                   DHCPc_ChkStatus() until the interface's DHCP management is successfully stopped and
*                   un-configured.
*********************************************************************************************************
*/

void  DHCPc_Stop (NET_IF_NBR   if_nbr,
                  DHCPc_ERR   *perr)
{
#ifdef  NET_IPv4_MODULE_EN
    DHCPc_IF_INFO   *pif_info;
    DHCPc_COMM      *pcomm;
    DHCPc_COMM_MSG   comm_msg;


    if (DHCPc_InitDone != DEF_YES) {                            /* If init NOT complete, exit (see Note #3).            */
       *perr = DHCPc_ERR_INIT_INCOMPLETE;
        return;
    }

                                                                /* ---------------- ACQUIRE DHCPc LOCK ---------------- */
    DHCPc_OS_Lock(perr);                                        /* See Note #4.                                         */
    if (*perr != DHCPc_OS_ERR_NONE) {
        return;
    }

    pif_info = DHCPc_IF_InfoGetCfgd(if_nbr);
    if (pif_info == (DHCPc_IF_INFO *)0) {                       /* If IF NOT managed by DHCPc, ...                      */
       *perr = DHCPc_ERR_IF_NOT_MANAGED;                        /* ... rtn err.                                         */
        DHCPc_OS_Unlock();
        return;
    }

                                                                /* -------------- POST MSG TO DHCP TASK --------------- */
    comm_msg = DHCPc_COMM_MSG_STOP;
    pcomm    = DHCPc_CommGet(if_nbr, comm_msg, perr);
    if (*perr != DHCPc_ERR_NONE) {
        DHCPc_OS_Unlock();
        return;
    }

    DHCPc_OS_MsgPost((void      *)pcomm,
                     (DHCPc_ERR *)perr);
    if (*perr != DHCPc_OS_ERR_NONE) {
        *perr  = DHCPc_ERR_MSG_Q;
         DHCPc_CommFree(pcomm);
         DHCPc_OS_Unlock();
         return;
    }

                                                                /* ---------------- RELEASE DHCPc LOCK ---------------- */
    DHCPc_OS_Unlock();

   *perr = DHCPc_ERR_NONE;
#else
   *perr = DHCPc_ERR_IPv4_NOT_PRESENT;
#endif
}


/*
*********************************************************************************************************
*                                          DHCPc_ChkStatus()
*
* Description : Check an interface's DHCP status & last error.
*
* Argument(s) : if_nbr      Interface number to check status.
*
*               perr_last   Pointer to variable that will receive the last error code for the specified
*                           interface :
*
*                           DHCPc_ERR_NONE                      No error saved for this interface.
*                           DHCPc_ERR_INIT_INCOMPLETE           DHCP client initialization NOT complete.
*                           DHCPc_ERR_IF_NOT_MANAGED            Interface NOT managed by the DHCP client.
*
*                           Specific initialization error code (see Note #2).
*
* Return(s)   : DHCP status for the interface :
*
*                   DHCP_STATUS_NONE,               NO status information available since DHCPc
*                                                       services for this interface either :
*
*                                                           NOT successfully started
*                                                        OR     successfully stopped.
*
*                   DHCP_STATUS_CFG_IN_PROGRESS,    DHCPc configuration still in progress.
*
*                   DHCP_STATUS_CFGD,               DHCPc configuration successfully completed.
*                   DHCP_STATUS_CFGD_NO_TMR,        DHCPc configuration successfully completed;
*                                                       however, NO DHCP lease timer available.
*
*                   DHCP_STATUS_CFGD_LOCAL_LINK,    DHCPc failed to configure a globally-routable
*                                                       address, but successfully configured a
*                                                       Link-Local address.
*
*                   DHCP_STATUS_FAIL,               DHCPc configuration failed.
*
* Caller(s)   : Application.
*
*               This function is a DHCP client application programming interface (API) function & MAY be
*               called by application function(s).
*
* Note(s)     : (1) DHCPc_ChkStatus() NOT executed until DHCP client initialization completes.
*
*               (2) (a) If any DHCP error occurs, the specific error code is preserved.
*
*                   (b) DHCP error codes are listed in 'dhcp-c.h'.  A search of the specific error code
*                       number(s) provides the corresponding error code label(s).
*********************************************************************************************************
*/

DHCPc_STATUS  DHCPc_ChkStatus (NET_IF_NBR   if_nbr,
                               DHCPc_ERR   *perr_last)
{
#ifdef  NET_IPv4_MODULE_EN
    DHCPc_IF_INFO  *pif_info;
    DHCPc_STATUS    status;
    CPU_SR_ALLOC();


    if (DHCPc_InitDone != DEF_YES) {                            /* If init NOT complete, exit (see Note #1).            */
       *perr_last = DHCPc_ERR_INIT_INCOMPLETE;
        return (DHCP_STATUS_NONE);
    }

    CPU_CRITICAL_ENTER();
    pif_info = DHCPc_IF_InfoGetCfgd(if_nbr);
    if (pif_info != (DHCPc_IF_INFO *)0) {
       *perr_last = pif_info->LastErr;
        status    = pif_info->LeaseStatus;

    } else {
       *perr_last = DHCPc_ERR_IF_NOT_MANAGED;
        status    = DHCP_STATUS_NONE;
    }
    CPU_CRITICAL_EXIT();


    return (status);
#else
   *perr_last = DHCPc_ERR_IPv4_NOT_PRESENT;
    return (DHCP_STATUS_FAIL);
#endif
}


/*
*********************************************************************************************************
*                                          DHCPc_GetOptVal()
*
* Description : (1) Get the value of a specific DHCP option for a given interface :
*
*                   (a) Acquire DHCPc lock
*                   (b) Get interface information structure
*                   (c) Retrieve specific option's value
*                   (d) Release DHCPc lock
*
*
* Argument(s) : if_nbr          Interface number to get option value.
*
*               opt_code        Option code      to get value.
*
*               pval_buf        Pointer to buffer that will receive the option value.
*
*               pval_buf_len    Pointer to a variable to ... :
*
*                                   (a) Pass the size of the buffer, in octets, pointed to by 'pval_buf'.
*                                   (b) (1) Return the actual length of the option, if NO errors;
*                                       (2) Return an undefined value,              otherwise.
*
*                               See also Note #4.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  Option value successfully returned.
*                               DHCPc_ERR_NULL_PTR              Argument 'pval_buf/pval_buf_len' passed a NULL
*                                                                   pointer.
*                               DHCPc_ERR_INIT_INCOMPLETE       DHCP client initialization NOT complete.
*                               DHCPc_ERR_IF_NOT_MANAGED        Interface NOT managed by the DHCP client.
*                               DHCPc_ERR_IF_NOT_CFG            Interface NOT yet configured by the DHCP client.
*                               DHCPc_ERR_IF_OPT_NONE           Option NOT present.
*                               DHCPc_ERR_OPT_BUF_SIZE          Option value buffer size too small.
*
*                                                               -------- RETURNED BY DHCPc_OS_Lock() : ---------
*                               DHCPc_OS_ERR_LOCK               DHCPc access NOT acquired.
*
* Return(s)   : none.
*
* Caller(s)   : Application.
*
*               This function is a DHCP client application programming interface (API) function & MAY be
*               called by application function(s).
*
* Note(s)     : (2) DHCPc_ChkStatus() NOT executed until DHCP client initialization completes.
*
*               (3) DHCPc_Stop() blocks ALL other DHCP client tasks by pending on & acquiring the global
*                   DHCPc lock (see dhcp-c.h  Note #2').
*
*               (4) Since 'pval_buf_len' parameter is both an input & output parameter
*                   (see 'Argument(s) : pval_buf_len'), ... :
*
*                   (a) Its input value SHOULD be validated prior to use; ...
*
*                       (1) In the case that the 'pval_buf_len' parameter is passed a null pointer,
*                           NO input value is validated or used.
*
*                       (2) The length of the option value buffer MUST be greater than or equal to the
*                           length of the actual option value requested.
*
*                   (b) While its output value MUST be initially configured to return a default value
*                       PRIOR to all other validation or function handling in case of any error(s).
*********************************************************************************************************
*/

void  DHCPc_GetOptVal (NET_IF_NBR       if_nbr,
                       DHCPc_OPT_CODE   opt_code,
                       CPU_INT08U      *pval_buf,
                       CPU_INT16U      *pval_buf_len,
                       DHCPc_ERR       *perr)
{
#ifdef  NET_IPv4_MODULE_EN
    DHCPc_IF_INFO  *pif_info;
    DHCPc_MSG      *pmsg;
    CPU_INT08U     *popt_val;
    CPU_INT08U      opt_val_len;


                                                                /* -------------- VALIDATE BUF & BUF LEN -------------- */
#if (DHCPc_CFG_ARG_CHK_EXT_EN == DEF_ENABLED)
    if ((pval_buf     == (CPU_INT08U *)0) ||
        (pval_buf_len == (CPU_INT16U *)0)) {
       *perr = DHCPc_ERR_NULL_PTR;
        return;
    }
#endif

    if (DHCPc_InitDone != DEF_YES) {                            /* If init NOT complete, exit (see Note #2).            */
       *perr = DHCPc_ERR_INIT_INCOMPLETE;
        return;
    }

                                                                /* ---------------- ACQUIRE DHCPc LOCK ---------------- */
    DHCPc_OS_Lock(perr);                                        /* See Note #3.                                         */
    if (*perr != DHCPc_OS_ERR_NONE) {
        return;
    }

    pif_info = DHCPc_IF_InfoGetCfgd(if_nbr);
    if (pif_info == (DHCPc_IF_INFO *)0) {                       /* If IF NOT managed by DHCPc, ...                      */
       *perr = DHCPc_ERR_IF_NOT_MANAGED;                        /* ... rtn err.                                         */
        DHCPc_OS_Unlock();
        return;
    }

    if (pif_info->LeaseStatus != DHCP_STATUS_CFGD) {            /* If IF NOT cfg'd, ...                                 */
       *perr = DHCPc_ERR_IF_NOT_CFG;                            /* ... rtn err.                                         */
        DHCPc_OS_Unlock();
        return;
    }

    pmsg = pif_info->MsgPtr;
    if (pmsg == (DHCPc_MSG *)0) {                               /* If NO DHCP msg for IF, ...                           */
       *perr = DHCPc_ERR_IF_NOT_CFG;                            /* ... rtn err.                                         */
        DHCPc_OS_Unlock();
        return;
    }

                                                                /* ------------------- GET OPT VAL -------------------- */
    popt_val = DHCPc_MsgGetOpt( opt_code,
                               &pmsg->MsgBuf[0],
                                pmsg->MsgLen,
                               &opt_val_len);

    if (popt_val == (CPU_INT08U *)0) {                          /* If NO opt val rtn'd, ...                             */
       *perr = DHCPc_ERR_IF_OPT_NONE;                           /* ... rtn err.                                         */
        DHCPc_OS_Unlock();
        return;
    }

    if (opt_val_len > *pval_buf_len) {                          /* If opt val larger than val buf, ....                 */
       *perr = DHCPc_ERR_OPT_BUF_SIZE;                          /* ... rtn err.                                         */
        DHCPc_OS_Unlock();
        return;
    }

    Mem_Copy((void     *)pval_buf,                              /* Copy opt val into val buf ..                         */
             (void     *)popt_val,
             (CPU_SIZE_T)opt_val_len);

   *pval_buf_len = opt_val_len;                                 /* .. & set opt val len.                                */

                                                                /* ---------------- RELEASE DHCPc LOCK ---------------- */
    DHCPc_OS_Unlock();

   *perr = DHCPc_ERR_NONE;
#else
   *perr = DHCPc_ERR_IPv4_NOT_PRESENT;
#endif
}


/*
*********************************************************************************************************
*                                         DHCPc_TaskHandler()
*
* Description : (1) Handle lease management :
*
*                   (a) Wait for message from DHCP client timer
*                   (b) Acquire  DHCPc lock
*                   (c) Handle   received message
*                   (d) Release  DHCPc lock
*
*
* Argument(s) : none.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_OS_Task().
*
*               This function is a DHCP client to operating system (OS) function & SHOULD be called only
*               by appropriate DHCPc-operating system port function(s).
*
* Note(s)     : (2) DHCPc_TaskHandler() blocked until DHCP client initialization completes.
*
*               (3) DHCPc_TaskHandler() blocks ALL other DHCP client tasks by pending on & acquiring
*                   the global DHCPc lock (see dhcp-c.h  Note #2').
*********************************************************************************************************
*/

void  DHCPc_TaskHandler (void)
{
#ifdef  NET_IPv4_MODULE_EN
    void       *pmsg;
    DHCPc_ERR   err;


    if (DHCPc_InitDone != DEF_YES) {                            /* If init NOT complete, ...                            */
        DHCPc_OS_InitWait(&err);                                /* ... wait on DHCPc init (see Note #2).                */
        if (err != DHCPc_OS_ERR_NONE) {
            return;
        }
    }


    while (DEF_ON) {
                                                                /* ------------------- WAIT FOR MSG ------------------- */
        do {
            pmsg = DHCPc_OS_MsgWait(&err);
        } while ((err  !=  DHCPc_OS_ERR_NONE) &&
                 (pmsg != (void *)0));

                                                                /* ---------------- ACQUIRE DHCPc LOCK ---------------- */
        DHCPc_OS_Lock(&err);                                    /* See Note #3.                                         */
        if (err != DHCPc_OS_ERR_NONE) {
            continue;
        }

                                                                /* -------------------- HANDLE MSG -------------------- */
        DHCPc_MsgRxHandler((DHCPc_COMM *)pmsg);

                                                                /* ---------------- RELEASE DHCPc LOCK ---------------- */
        DHCPc_OS_Unlock();
    }
#endif
}


/*
*********************************************************************************************************
*                                       DHCPc_TmrTaskHandler()
*
* Description : (1) Handle DHCP timers in the DHCPc Timer List :
*
*                   (a) Wait for signal from the DHCPc timer
*
*                   (b) Acquire DHCPc lock (see Note #2)
*
*                   (c) Handle every  DHCPc timer in Timer List :
*                       (1) Decrement DHCPc timer(s)
*                       (2) For any timer that expires (see Note #3) :
*                           (A) Free from Timer List
*                           (B) Get  current time
*                           (C) Post message to DHCPc Task
*
*                   (d) Release DHCPc lock
*
*
* Argument(s) : none.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_OS_TmrTask() [see 'dhcp-c_os.c'].
*
*               This function is a DHCP client to operating system (OS) function & SHOULD be called only
*               by appropriate DHCPc-operating system port function(s).
*
* Note(s)     : (2) DHCPc_TaskHandler() blocked until DHCP client initialization completes.
*
*               (3) DHCPc_TmrTaskHandler() blocks ALL other DHCP client tasks by pending on & acquiring
*                   the global DHCPc lock (see dhcp-c.h  Note #2').
*
*               (4) Since the DHCPc task executes asynchronously from the timer task handler, the
*                   current time is kept into the interface information structure to prevent lease time
*                   drifting.
*
*               (5) When a DHCP timer expires, the timer SHOULD be freed PRIOR to executing the timer
*                   expiration function.  This ensures that at least one timer is available if the timer
*                   expiration function requires a timer.
*********************************************************************************************************
*/

void  DHCPc_TmrTaskHandler (void)
{
#ifdef  NET_IPv4_MODULE_EN
    DHCPc_TMR      *ptmr;
    DHCPc_TMR      *ptmr_next;
    DHCPc_COMM     *pcomm;
    DHCPc_IF_INFO  *pif_info;
    DHCPc_ERR       err;


    if (DHCPc_InitDone != DEF_YES) {                            /* If init NOT complete, ...                            */
        DHCPc_OS_InitWait(&err);                                /* ... wait on DHCPc init (see Note #2).                */
        if (err != DHCPc_OS_ERR_NONE) {
            return;
        }
    }


    while (DEF_ON) {
                                                                /* ----------------- WAIT TMR SIGNAL ------------------ */
        do {
            DHCPc_OS_TmrWait(&err);
        } while (err != DHCPc_OS_ERR_NONE);


                                                                /* ---------------- ACQUIRE DHCPc LOCK ---------------- */
        DHCPc_OS_Lock(&err);                                    /* See Note #3.                                         */
        if (err != DHCPc_OS_ERR_NONE) {
            continue;
        }

                                                                /* --------------- HANDLE TMR TASK LIST --------------- */
        ptmr = DHCPc_TmrListHead;                               /* Start @ Tmr List head.                               */
        while (ptmr != (DHCPc_TMR *)0) {                        /* Handle  Tmr List tmrs.                               */

            ptmr_next = ptmr->NextPtr;                          /* Set next tmr to update.                              */

            if (ptmr->TmrVal > 1) {                             /* If tmr val > 1, dec tmr val.                         */
                ptmr->TmrVal--;

            } else {                                            /* Else tmr expired,              ...                   */

                pcomm    = (DHCPc_COMM *)ptmr->Obj;             /* ... get obj                    ...                   */

                pif_info = DHCPc_IF_InfoGetCfgd(pcomm->IF_Nbr); /* ... get if info                ...                   */
                if (pif_info != ((DHCPc_IF_INFO *)0)) {
                                                                /* ... get cur time (see Note #4) ...                   */
                    pif_info->TmrExpirationTime = DHCPc_OS_TimeGet_tick();
                }

                DHCPc_TmrFree(ptmr);                            /* ... free tmr     (see Note #5) ...                   */
                pif_info->Tmr = (DHCPc_TMR *)0;                 /* Prevents a double-free of the timer.                 */

                DHCPc_OS_MsgPost((void      *) pcomm,           /* ... & post obj to DHCP client task.                  */
                                 (DHCPc_ERR *)&err);
            }

            ptmr = ptmr_next;
        }

                                                                /* ---------------- RELEASE DHCPc LOCK ---------------- */
        DHCPc_OS_Unlock();
    }
#endif
}


/*
*********************************************************************************************************
*********************************************************************************************************
*                                           LOCAL FUNCTIONS
*********************************************************************************************************
*********************************************************************************************************
*/

#ifdef  NET_IPv4_MODULE_EN

/*
*********************************************************************************************************
*                                         DHCPc_IF_InfoInit()
*
* Description : (1) Initialize DHCPc interface information :
*
*                   (a) Initialize interface information pool
*                   (b) Initialize interface information table
*                   (c) Initialize interface information list pointer
*
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  DHCPc interface information successfully
*                                                                   initialized.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_Init().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (2) Interface inforation pool MUST be initialized PRIOR to initializing the pool with
*                   pointers to interface information.
*********************************************************************************************************
*/

static  void  DHCPc_IF_InfoInit (DHCPc_ERR  *perr)
{
    DHCPc_IF_INFO      *pif_info;
    DHCPc_IF_INFO_QTY   i;

                                                                /* ---------------- INIT IF INFO POOL ----------------- */
    DHCPc_InfoPoolPtr = (DHCPc_IF_INFO *)0;                     /* Init-clr DHCPc IF info pool (see Note #2).           */


                                                                /* ----------------- INIT IF INFO TBL ----------------- */
    pif_info = &DHCPc_InfoTbl[0];
    for (i = 0; i < DHCPc_NBR_IF_INFO; i++) {
        pif_info->ID       = (DHCPc_IF_INFO_QTY)i;
        pif_info->Flags    =  DHCPc_FLAG_NONE;                  /* Init each IF info as NOT used.                       */

#if (DHCPc_DBG_CFG_MEM_CLR_EN == DEF_ENABLED)
        DHCPc_IF_InfoClr(pif_info);
#endif

        pif_info->NextPtr  =  DHCPc_InfoPoolPtr;                /* Free each IF info to info pool (see Note #2).        */
        DHCPc_InfoPoolPtr  =  pif_info;

        pif_info++;
    }

                                                                /* ---------------- INIT INFO LIST PTR ---------------- */
    DHCPc_InfoListHead = (DHCPc_IF_INFO *)0;


   *perr = DHCPc_ERR_NONE;
}


/*
*********************************************************************************************************
*                                         DHCPc_IF_InfoGet()
*
* Description : (1) Allocate & initialize a DHCPc interface information :
*
*                   (a) Validate interface NOT attributed (see Note #2).
*                   (b) Generate base transaction identifier
*                   (c) Get        interface information
*                   (d) Initialize interface information
*                   (e) Insert     interface information at head of interface information list
*                   (f) Return pointer to interface information
*                         OR
*                       Null pointer & error code, on failure
*
*
* Argument(s) : if_nbr      Interface number to get the interface information structure.
*               ------      Argument validated in DHCPc_Start().
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  Interface information successfully allocated
*                                                                   & initialized.
*                               DHCPc_ERR_IF_INFO_IF_USED       Interface information already in use.
*                               DHCPc_ERR_INVALID_HW_ADDR       Error retrieving interface's hardware address.
*                               DHCPc_ERR_IF_INFO_NONE_AVAIL    Interface information pool empty.
*
* Return(s)   : Pointer to interface information, if NO errors.
*
*               Pointer to NULL,                  otherwise.
*
* Caller(s)   : DHCPc_Start().
*
* Note(s)     : (2) Only one interface information may exist for a given interface number.
*
*               (3) #### This implementation of the DHCP client presumes an Ethernet hardware type.
*
*               (4) The transaction ID (xid) it generated by taking the 3 least significant bytes of
*                   the hardware address, left-shifted by one octet.
*
*               (5) The use of critical section is necessary to protect the data in the interface
*                   information structures, since the function DHCPc_ChkStatus() does NOT get the DHCPc
*                   lock when it access them.
*********************************************************************************************************
*/

static  DHCPc_IF_INFO  *DHCPc_IF_InfoGet (NET_IF_NBR   if_nbr,
                                          DHCPc_ERR   *perr)
{
    DHCPc_IF_INFO  *pif_info;
    CPU_INT08U      addr_hw_len;
    CPU_INT08U      addr_hw[NET_IF_ETHER_ADDR_SIZE];
    CPU_INT32U      transaction_id_base;
    NET_ERR         err_net;
    CPU_SR_ALLOC();


                                                                /* --------------- VALIDATE IF NBR USED --------------- */
    pif_info = DHCPc_IF_InfoGetCfgd(if_nbr);
    if (pif_info != ((DHCPc_IF_INFO *)0)) {                     /* If if nbr already has if info, ...                   */
       *perr = DHCPc_ERR_IF_INFO_IF_USED;                       /* ... rtn err (see Note #2).                           */
        return ((DHCPc_IF_INFO *)0);
    }

                                                                /* ---------- GENERATE TRANSACTION BASE NBR ----------- */
    addr_hw_len = NET_IF_ETHER_ADDR_SIZE;                       /* See Note #3.                                         */
    NetIF_AddrHW_Get(if_nbr,
                    &addr_hw[0],
                    &addr_hw_len,
                    &err_net);

    if ((err_net     != NET_IF_ERR_NONE) ||
        (addr_hw_len != NET_IF_ETHER_ADDR_SIZE)) {
       *perr = DHCPc_ERR_INVALID_HW_ADDR;
        return ((DHCPc_IF_INFO *)0);
    }

                                                                /* Generate base transaction ID (see Note #4).          */
    transaction_id_base = ((((CPU_INT32U)addr_hw[3]) << (3 * DEF_OCTET_NBR_BITS)) +
                           (((CPU_INT32U)addr_hw[4]) << (2 * DEF_OCTET_NBR_BITS)) +
                           (((CPU_INT32U)addr_hw[5]) << (1 * DEF_OCTET_NBR_BITS)));


                                                                /* ------------------- GET IF INFO -------------------- */
    if (DHCPc_InfoPoolPtr != (DHCPc_IF_INFO *)0) {              /* If if info pool NOT empty, get if info from pool.    */
        pif_info           = (DHCPc_IF_INFO *)DHCPc_InfoPoolPtr;
        DHCPc_InfoPoolPtr  = (DHCPc_IF_INFO *)pif_info->NextPtr;

    } else {                                                    /* If none avail, rtn err.                              */
       *perr = DHCPc_ERR_IF_INFO_NONE_AVAIL;
        return ((DHCPc_IF_INFO *)0);
    }

                                                                /* ------------------- INIT IF INFO ------------------- */
    DHCPc_IF_InfoClr(pif_info);
    pif_info->PrevPtr       = (DHCPc_IF_INFO *)0;
    pif_info->NextPtr       = (DHCPc_IF_INFO *)DHCPc_InfoListHead;
    pif_info->IF_Nbr        =  if_nbr;
    pif_info->LeaseStatus   =  DHCP_STATUS_CFG_IN_PROGRESS;
    pif_info->TransactionID =  transaction_id_base;
    DEF_BIT_SET(pif_info->Flags, DHCPc_FLAG_USED);              /* Set if info as used.                                 */

                                                                /* --------- INSERT IF INFO INTO IF INFO LIST --------- */
    CPU_CRITICAL_ENTER();                                       /* See Note #5.                                         */
    if (DHCPc_InfoListHead != (DHCPc_IF_INFO *)0) {             /* If list NOT empty, insert before head.               */
        DHCPc_InfoListHead->PrevPtr = pif_info;
    }
    DHCPc_InfoListHead = pif_info;                              /* Insert if info @ list head.                          */
    CPU_CRITICAL_EXIT();


   *perr =  DHCPc_ERR_NONE;

    return (pif_info);
}


/*
*********************************************************************************************************
*                                       DHCPc_IF_InfoGetCfgd()
*
* Description : Get interface information structure for configured interface.
*
* Argument(s) : if_nbr      Interface number to get the interface information structure.
*
* Return(s)   : Pointer to interface information structure, if interface configured with DHCP.
*
*               Pointer to NULL,                            otherwise.
*
* Caller(s)   : DHCPc_Stop(),
*               DHCPc_ChkStatus(),
*               DHCPc_TmrTaskHandler(),
*               DHCPc_MsgRxHandler(),
*               DHCPc_IF_InfoGet().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  DHCPc_IF_INFO  *DHCPc_IF_InfoGetCfgd (NET_IF_NBR  if_nbr)
{
    DHCPc_IF_INFO  *pif_info;
    CPU_BOOLEAN     if_cfgd;


    pif_info = DHCPc_InfoListHead;
    if_cfgd  = DEF_NO;

    while ((pif_info != (DHCPc_IF_INFO *)0) &&
           (if_cfgd  !=  DEF_YES)) {
        if (pif_info->IF_Nbr == if_nbr) {
            if_cfgd = DEF_YES;

        } else {
            pif_info = pif_info->NextPtr;
        }
    }

    if (if_cfgd != DEF_YES) {                                   /* If IF nbr NOT cfg'd, ...                             */
        return ((DHCPc_IF_INFO *)0);                            /* ... rtn NULL ptr.                                    */
    }

    return (pif_info);                                          /* Else, rth ptr to IF info struct for cfg'd IF.        */
}


/*
*********************************************************************************************************
*                                         DHCPc_IF_InfoFree()
*
* Description : (1) Free a DHCPc inteface information :
*
*                   (a) Remove interface information from    interface information list
*                   (b) Clear  interface information controls
*                   (c) Free   interface information back to interface information pool
*
*
* Argument(s) : pif_info    Pointer to a DHCPc interface information.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_Start(),
*               DHCPc_StopStateHandler().
*
*               This function is an INTERNAL network protocol suite function & SHOULD NOT be called by
*               application function(s).
*
* Note(s)     : (2) #### To prevent freeing an interface information already freed via previous interface
*                   information free, DHCPc_IF_InfoFree() checks the interface information's 'USED' flag
*                   BEFORE freeing the interface information.
*
*                   This prevention is only best-effort since any invalid duplicate interface information
*                   frees MAY be asynchronous to potentially valid interface information gets.  Thus the
*                   invalid interface information free(s) MAY corrupt the interface information's valid
*                   operation(s).
*
*                   However, since the primary tasks of the DHCP client are prevented from running
*                   concurrently (see 'dhcp-c.h  Note #2'), it is NOT necessary to protect DHCPc
*                   interface information resources from possible corruption since no asynchronous access
*                   from other task is possible.
*********************************************************************************************************
*/

static  void  DHCPc_IF_InfoFree (DHCPc_IF_INFO  *pif_info)
{
#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)
    CPU_BOOLEAN     used;
#endif
    DHCPc_IF_INFO  *pprev;
    DHCPc_IF_INFO  *pnext;
    CPU_SR_ALLOC();


                                                                /* ------------------ VALIDATE PTR -------------------- */
    if (pif_info == (DHCPc_IF_INFO *)0) {
        return;
    }

#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)
                                                                /* -------------- VALIDATE IF INFO USED --------------- */
    used = DEF_BIT_IS_SET(pif_info->Flags, DHCPc_FLAG_USED);
    if (used != DEF_YES) {                                      /* If IF info NOT used, ...                             */
        return;                                                 /* ... rtn but do NOT free (see Note #2).               */
    }
#endif

                                                                /* --------- REMOVE IF INFO FROM IF INFO LIST --------- */
    CPU_CRITICAL_ENTER();                                       /* See 'DHCPc_IF_InfoGet()  Note #5'.                   */
    pprev = pif_info->PrevPtr;
    pnext = pif_info->NextPtr;
    if (pprev != (DHCPc_IF_INFO *)0) {                          /* If pif_info is NOT   the head of IF info list, ...   */
        pprev->NextPtr    = pnext;                              /* ... set pprev's NextPtr to skip pif_info.            */
    } else {                                                    /* Else set pnext as head of IF info list.              */
        DHCPc_InfoListHead = pnext;
    }
    if (pnext != (DHCPc_IF_INFO *)0) {                          /* If pif_info is NOT @ the tail of IF info list, ...   */
        pnext->PrevPtr    = pprev;                              /* ... set pnext's PrevPtr to skip pif_info.            */
    }
    CPU_CRITICAL_EXIT();

                                                                /* ------------------- CLR IF INFO -------------------- */
    DEF_BIT_CLR(pif_info->Flags, DHCPc_FLAG_USED);              /* Set IF info as NOT used.                             */
#if (NET_DBG_CFG_MEM_CLR_EN == DEF_ENABLED)
    DHCPc_IF_InfoClr(pif_info);
#endif

                                                                /* ------------------- FREE IF INFO ------------------- */
    pif_info->NextPtr = DHCPc_InfoPoolPtr;
    DHCPc_InfoPoolPtr = pif_info;
}


/*
*********************************************************************************************************
*                                         DHCPc_IF_InfoClr()
*
* Description : Clear DHCPc interface information controls.
*
* Argument(s) : pif_info    Pointer to a DHCPc interface information.
*               --------    Argument validated in DHCPc_IF_InfoInit(),
*                                    checked   in DHCPc_IF_InfoGet(),
*                                    checked   in DHCPc_IF_InfoFree().
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_IF_InfoInit(),
*               DHCPc_IF_InfoGet(),
*               DHCPc_IF_InfoFree().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  void  DHCPc_IF_InfoClr (DHCPc_IF_INFO  *pif_info)
{
    pif_info->PrevPtr           = (DHCPc_IF_INFO *)0;
    pif_info->NextPtr           = (DHCPc_IF_INFO *)0;

    pif_info->IF_Nbr            =  NET_IF_NBR_NONE;
    pif_info->ServerID          =  NET_IPv4_ADDR_NONE;

    Mem_Clr((void     *)&pif_info->ParamReqTbl[0],
            (CPU_SIZE_T) DHCPc_CFG_PARAM_REQ_TBL_SIZE);

    pif_info->ParamReqQty       =  0;

    pif_info->MsgPtr            = (DHCPc_MSG     *)0;

    pif_info->ClientState       =  DHCP_STATE_NONE;
    pif_info->LeaseStatus       =  DHCP_STATUS_NONE;
    pif_info->LastErr           =  DHCPc_ERR_NONE;

    pif_info->TransactionID     =  0;

    pif_info->NegoStartTime     =  0;
    pif_info->TmrExpirationTime =  0;

    pif_info->LeaseTime_sec     =  0;
    pif_info->T1_Time_sec       =  0;
    pif_info->T2_Time_sec       =  0;

    pif_info->Tmr               = (DHCPc_TMR     *)0;

    pif_info->Flags             =  DHCPc_FLAG_NONE;
}


/*
*********************************************************************************************************
*                                           DHCPc_MsgInit()
*
* Description : (1) Initialize DHCPc messages :
*
*                   (a) Initialize message pool
*                   (b) Initialize message table
*                   (c) Initialize message list pointer
*
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  DHCPc message successfully initialized.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_Init().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (2) Message pool MUST be initialized PRIOR to initializing the pool with pointers to
*                   message.
*********************************************************************************************************
*/

static  void  DHCPc_MsgInit (DHCPc_ERR  *perr)
{
    DHCPc_MSG      *pmsg;
    DHCPc_MSG_QTY   i;

                                                                /* ------------------ INIT MSG POOL ------------------- */
    DHCPc_MsgPoolPtr = (DHCPc_MSG *)0;                          /* Init-clr DHCPc msg pool (see Note #2).               */


                                                                /* ------------------- INIT MSG TBL ------------------- */
    pmsg = &DHCPc_MsgTbl[0];
    for (i = 0; i < DHCPc_NBR_MSG_BUF; i++) {
        pmsg->ID         = (DHCPc_MSG_QTY)i;
        pmsg->Flags      =  DHCPc_FLAG_NONE;                    /* Init each msg as NOT used.                           */

#if (DHCPc_DBG_CFG_MEM_CLR_EN == DEF_ENABLED)
        DHCPc_MsgClr(pmsg);
#endif

        pmsg->NextPtr    =  DHCPc_MsgPoolPtr;                   /* Free each msg to msg pool (see Note #2).             */
        DHCPc_MsgPoolPtr =  pmsg;

        pmsg++;
    }

                                                                /* ---------------- INIT MSG LIST PTR ----------------- */
    DHCPc_MsgListHead = (DHCPc_MSG *)0;


   *perr = DHCPc_ERR_NONE;
}


/*
*********************************************************************************************************
*                                        DHCPc_MsgRxHandler()
*
* Description : (1) Handle messages received from timers and DHCP API functions :
*
*                   (a) Get interface information structure
*                   (b) Demultiplex message
*
*
* Argument(s) : pcomm       Pointer to DHCP communication object.
*               -----       Argument checked in DHCPc_TaskHandler().
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_TaskHandler().
*
* Note(s)     : (2) The communication object SHOULD be freed PRIOR to executing the appropriate action.
*                   This ensures that at least one communication object is available if the taken action
*                   requires a communication object.
*********************************************************************************************************
*/

static  void  DHCPc_MsgRxHandler (DHCPc_COMM  *pcomm)
{
    NET_IF_NBR       if_nbr;
    DHCPc_IF_INFO   *pif_info;
    DHCPc_COMM_MSG   msg;
    DHCPc_ERR        err;
    CPU_SR_ALLOC();


    if_nbr = pcomm->IF_Nbr;
    msg    = pcomm->CommMsg;

    DHCPc_CommFree(pcomm);                                      /* Free comm obj (see Note #2).                         */


                                                                /* ---------- GET IF INFO STRUCT FROM IF NBR ---------- */
    pif_info = DHCPc_IF_InfoGetCfgd(if_nbr);
    if (pif_info == ((DHCPc_IF_INFO *)0)) {                     /* If IF NOT cfg'd, ...                                 */
        return;                                                 /* ... rtn.                                             */
    }

                                                                /* -------------------- DEMUX MSG --------------------- */
    switch (msg) {
        case DHCPc_COMM_MSG_START:                              /* If nego starting ...                                 */
        case DHCPc_COMM_MSG_LEASE_EXPIRED:                      /* ... or lease expired, ...                            */
             CPU_CRITICAL_ENTER();
             pif_info->LeaseStatus = DHCP_STATUS_CFG_IN_PROGRESS;
             CPU_CRITICAL_EXIT();

             DHCPc_InitStateHandler(pif_info, &err);            /* ... go into INIT state.                              */
             switch (err) {
                 case DHCPc_ERR_NONE:
                      CPU_CRITICAL_ENTER();
                      pif_info->LeaseStatus = DHCP_STATUS_CFGD;
                      CPU_CRITICAL_EXIT();
                      break;


                 case DHCPc_ERR_NONE_NO_TMR:
                      CPU_CRITICAL_ENTER();
                      pif_info->LeaseStatus = DHCP_STATUS_CFGD_NO_TMR;
                      CPU_CRITICAL_EXIT();
                      break;


                 case DHCPc_ERR_NONE_LOCAL_LINK:
                      CPU_CRITICAL_ENTER();
                      pif_info->LeaseStatus = DHCP_STATUS_CFGD_LOCAL_LINK;
                      CPU_CRITICAL_EXIT();
                      break;


                 default:
                      CPU_CRITICAL_ENTER();
                      pif_info->LeaseStatus = DHCP_STATUS_FAIL;
                      pif_info->LastErr     = err;
                      CPU_CRITICAL_EXIT();
                      break;
             }
             break;


        case DHCPc_COMM_MSG_T1_EXPIRED:                         /* If T1 or T2 expired, ...                             */
        case DHCPc_COMM_MSG_T2_EXPIRED:
             DHCPc_RenewRebindStateHandler(pif_info, msg, &err);/* ... go into RENEWING/REBINDING state.                */
             switch (err) {
                 case DHCPc_ERR_NONE:
                 case DHCPc_ERR_INIT_SOCK:
                      break;                                    /* Already cfg'd, status set.                           */


                 case DHCPc_ERR_NONE_NO_TMR:
                      CPU_CRITICAL_ENTER();
                      pif_info->LeaseStatus = DHCP_STATUS_CFGD_NO_TMR;
                      CPU_CRITICAL_EXIT();
                      break;


                 default:
                      CPU_CRITICAL_ENTER();
                      pif_info->LeaseStatus = DHCP_STATUS_FAIL;
                      pif_info->LastErr     = err;
                      CPU_CRITICAL_EXIT();
                      break;
             }
             break;


        case DHCPc_COMM_MSG_STOP:                               /* If nego stopping, ...                                */
             DHCPc_StopStateHandler(pif_info, &err);            /* ... go into STOP state.                              */
             break;


        case DHCPc_COMM_MSG_NONE:                               /* Else, ...                                            */
        default:
             break;                                             /* ... do nothing.                                      */
    }
}


/*
*********************************************************************************************************
*                                           DHCPc_MsgGet()
*
* Description : (1) Allocate & initialize a DHCPc message :
*
*                   (a) Get        message
*                   (b) Initialize message
*                   (c) Insert     message at head of message list
*                   (d) Return pointer to message
*                         OR
*                       Null pointer & error code, on failure
*
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  Message successfully allocated & initialized.
*                               DHCPc_ERR_MSG_NONE_AVAIL        Message pool empty.
*
* Return(s)   : Pointer to message, if NO errors.
*
*               Pointer to NULL,    otherwise.
*
* Caller(s)   : DHCPc_Discover(),
*               DHCPc_Req(),
*               DHCPc_DeclineRelease().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  DHCPc_MSG  *DHCPc_MsgGet (DHCPc_ERR  *perr)
{
    DHCPc_MSG  *pmsg;

                                                                /* --------------------- GET MSG ---------------------- */
    if (DHCPc_MsgPoolPtr != (DHCPc_MSG *)0) {                   /* If msg pool NOT empty, get msg from pool.            */
        pmsg              = (DHCPc_MSG *)DHCPc_MsgPoolPtr;
        DHCPc_MsgPoolPtr  = (DHCPc_MSG *)pmsg->NextPtr;

    } else {                                                    /* If none avail, rtn err.                              */
       *perr = DHCPc_ERR_MSG_NONE_AVAIL;
        return ((DHCPc_MSG *)0);
    }

                                                                /* --------------------- INIT MSG --------------------- */
    DHCPc_MsgClr(pmsg);
    pmsg->PrevPtr = (DHCPc_MSG *)0;
    pmsg->NextPtr = (DHCPc_MSG *)DHCPc_MsgListHead;
    DEF_BIT_SET(pmsg->Flags, DHCPc_FLAG_USED);                  /* Set msg as used.                                     */

                                                                /* ------------- INSERT MSG INTO MSG LIST ------------- */
    if (DHCPc_MsgListHead != (DHCPc_MSG *)0) {                  /* If list NOT empty, insert before head.               */
        DHCPc_MsgListHead->PrevPtr = pmsg;
    }
    DHCPc_MsgListHead = pmsg;                                   /* Insert msg @ list head.                              */


   *perr =  DHCPc_ERR_NONE;

    return (pmsg);
}


/*
*********************************************************************************************************
*                                          DHCPc_MsgGetOpt()
*
* Description : Retrieve the specified option value from a DHCP message buffer.
*
* Argument(s) : opt_code            Option code to return value of.
*
*               pmsg_buf            Pointer to DHCP message buffer to search.
*
*               msg_buf_size        Size of message buffer (in octets).
*
*               popt_val_len        Pointer to variable that will receive the length of the option value.
*
* Return(s)   : Pointer to the specified option value, if option found without error.
*
*               Pointer tu NULL,                       otherwise.
*
* Caller(s)   : DHCPc_Discover(),
*               DHCPc_LeaseTimeCalc(),
*               DHCPc_AddrCfg(),
*               DHCPc_RxReply().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  CPU_INT08U  *DHCPc_MsgGetOpt (DHCPc_OPT_CODE   opt_code,
                                      CPU_INT08U      *pmsg_buf,
                                      CPU_INT16U       msg_buf_size,
                                      CPU_INT08U      *popt_val_len)
{
    CPU_INT32U    magic_cookie;
    CPU_BOOLEAN   opt_start;
    CPU_BOOLEAN   opt_found;
    CPU_INT08U   *popt;
    CPU_INT08U   *popt_val;
    CPU_INT08U   *pend_msg;


#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)                   /* ------------------- VALIDATE PTR ------------------- */
    if (popt_val_len == (CPU_INT08U *)0) {
        return ((CPU_INT08U *)0);
    }

    if (pmsg_buf == (CPU_INT08U *)0) {
        return ((CPU_INT08U *)0);
    }
#endif

   *popt_val_len =  0;                                          /* Cfg rtn opt val len for err.                         */


    popt = pmsg_buf + DHCP_MSG_HDR_SIZE;

                                                                /* -------- VALIDATE BEGINNING OF OPT SECTION --------- */
    magic_cookie = NET_UTIL_HOST_TO_NET_32(DHCP_MAGIC_COOKIE);
    opt_start = Mem_Cmp((void     *) popt,
                        (void     *)&magic_cookie,
                        (CPU_SIZE_T) DHCP_MAGIC_COOKIE_SIZE);

    if (opt_start != DEF_YES) {                                 /* If magic cookie NOT here, ...                        */
        return ((CPU_INT08U *)0);                               /* ... rtn.                                             */
    }

    popt += DHCP_MAGIC_COOKIE_SIZE;                             /* Go to first opt.                                     */


                                                                /* --------------------- SRCH OPT --------------------- */
    opt_found = DEF_NO;
    pend_msg  = pmsg_buf + msg_buf_size;

    while ((opt_found != DEF_YES)      &&                       /* Srch until opt found,                                */
           (*popt     != DHCP_OPT_END) &&                       /* & opt end    NOT reached,                            */
           ( popt     <= pend_msg)) {                           /* & end of msg NOT reached.                            */

        if (*popt == opt_code) {                                /* If popt equals srch'd opt code, ...                  */
            opt_found = DEF_YES;                                /* ... opt found.                                       */

        } else if (*popt == DHCP_OPT_PAD) {                     /* If popt is padding, ...                              */
            popt++;                                             /* ... advance.                                         */

        } else {                                                /* Else, another opt found,  ...                        */
                                                                /* ... skip to next opt.                                */
            popt += ((*(popt + DHCP_OPT_FIELD_CODE_LEN)) + DHCP_OPT_FIELD_HDR_LEN);
        }
    }


    if (opt_found != DEF_YES) {
        return ((CPU_INT08U *)0);
    }

   *popt_val_len = *(popt + DHCP_OPT_FIELD_CODE_LEN);          /* Set opt val len ...                                  */
    popt_val     =   popt + DHCP_OPT_FIELD_HDR_LEN;            /* ... & set opt val ptr.                               */

    return (popt_val);
}


/*
*********************************************************************************************************
*                                           DHCPc_MsgFree()
*
* Description : (1) Free a DHCPc message :
*
*                   (a) Remove message from    message list
*                   (b) Clear  message controls
*                   (c) Free   message back to message pool
*
*
* Argument(s) : pmsg        Pointer to a DHCPc message.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_StopStateHandler(),
*               DHCPc_Discover(),
*               DHCPc_Req(),
*               DHCPc_DeclineRelease().
*
*               This function is an INTERNAL network protocol suite function & SHOULD NOT be called by
*               application function(s).
*
* Note(s)     : (2) #### To prevent freeing a message already freed via previous message free,
*                   DHCPc_MsgFree() checks the message's 'USED' flag BEFORE freeing the message.
*
*                   This prevention is only best-effort since any invalid duplicate message frees MAY be
*                   asynchronous to potentially valid message gets.  Thus the invalid message free(s) MAY
*                   corrupt the message's valid operation(s).
*
*                   However, since the primary tasks of the DHCP client are prevented from running
*                   concurrently (see 'dhcp-c.h  Note #2'), it is NOT necessary to protect DHCPc
*                   message resources from possible corruption since no asynchronous access from other
*                   task is possible.
*********************************************************************************************************
*/

static  void  DHCPc_MsgFree (DHCPc_MSG  *pmsg)
{
#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)
    CPU_BOOLEAN   used;
#endif
    DHCPc_MSG    *pprev;
    DHCPc_MSG    *pnext;


                                                                /* ------------------ VALIDATE PTR -------------------- */
    if (pmsg == (DHCPc_MSG *)0) {
        return;
    }

#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)
                                                                /* ---------------- VALIDATE MSG USED ----------------- */
    used = DEF_BIT_IS_SET(pmsg->Flags, DHCPc_FLAG_USED);
    if (used != DEF_YES) {                                      /* If msg NOT used, ...                                 */
        return;                                                 /* ... rtn but do NOT free (see Note #2).               */
    }
#endif

                                                                /* ------------- REMOVE MSG FROM MSG LIST ------------- */
    pprev = pmsg->PrevPtr;
    pnext = pmsg->NextPtr;
    if (pprev != (DHCPc_MSG *)0) {                              /* If pmsg is NOT   the head of msg list, ...           */
        pprev->NextPtr    = pnext;                              /* ... set pprev's NextPtr to skip pmsg.                */
    } else {                                                    /* Else set pnext as head of msg list.                  */
        DHCPc_MsgListHead = pnext;
    }
    if (pnext != (DHCPc_MSG *)0) {                              /* If pmsg is NOT @ the tail of msg list, ...           */
        pnext->PrevPtr    = pprev;                              /* ... set pnext's PrevPtr to skip pmsg.                */
    }

                                                                /* ---------------------- CLR MSG --------------------- */
    DEF_BIT_CLR(pmsg->Flags, DHCPc_FLAG_USED);                  /* Set msg as NOT used.                                 */
#if (NET_DBG_CFG_MEM_CLR_EN == DEF_ENABLED)
    DHCPc_MsgClr(pmsg);
#endif

                                                                /* --------------------- FREE MSG --------------------- */
    pmsg->NextPtr    = DHCPc_MsgPoolPtr;
    DHCPc_MsgPoolPtr = pmsg;
}


/*
*********************************************************************************************************
*                                           DHCPc_MsgClr()
*
* Description : Clear DHCPc message controls.
*
* Argument(s) : pmsg        Pointer to a DHCPc message.
*               ----        Argument validated in DHCPc_MsgInit(),
*                                    checked   in DHCPc_MsgGet(),
*                                    checked   in DHCPc_MsgFree().
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_MsgInit(),
*               DHCPc_MsgGet(),
*               DHCPc_MsgFree().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  void  DHCPc_MsgClr (DHCPc_MSG  *pmsg)
{
    pmsg->PrevPtr = (DHCPc_MSG *)0;
    pmsg->NextPtr = (DHCPc_MSG *)0;

    Mem_Clr((void     *)&pmsg->MsgBuf[0],
            (CPU_SIZE_T) DHCP_MSG_BUF_SIZE);

    pmsg->MsgLen  =  0;
    pmsg->Flags   =  DHCPc_FLAG_NONE;
}


/*
*********************************************************************************************************
*                                          DHCPc_CommInit()
*
* Description : (1) Initialize DHCPc communication objects :
*
*                   (a) Initialize communication object pool
*                   (b) Initialize communication object table
*                   (c) Initialize communication object list pointer
*
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  DHCPc communication objects successfully
*                                                                   initialized.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_Init().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (2) Communication object pool MUST be initialized PRIOR to initializing the pool with
*                   pointers to communication objects.
*********************************************************************************************************
*/

static  void  DHCPc_CommInit (DHCPc_ERR  *perr)
{
    DHCPc_COMM      *pcomm;
    DHCPc_COMM_QTY   i;

                                                                /* ---------------- INIT COMM OBJ POOL ---------------- */
    DHCPc_CommPoolPtr = (DHCPc_COMM *)0;                        /* Init-clr DHCPc comm obj pool (see Note #2).          */


                                                                /* ----------------- INIT COMM OBJ TBL ---------------- */
    pcomm = &DHCPc_CommTbl[0];
    for (i = 0; i < DHCPc_NBR_COMM; i++) {
        pcomm->ID         = (DHCPc_COMM_QTY)i;
        pcomm->Flags      =  DHCPc_FLAG_NONE;                   /* Init each comm obj as NOT used.                      */

#if (DHCPc_DBG_CFG_MEM_CLR_EN == DEF_ENABLED)
        DHCPc_CommClr(pcomm);
#endif

        pcomm->NextPtr    = DHCPc_CommPoolPtr;                  /* Free each comm obj to comm obj pool (see Note #2).   */
        DHCPc_CommPoolPtr = pcomm;

        pcomm++;
    }

                                                                /* -------------- INIT COMM OBJ LIST PTR -------------- */
    DHCPc_CommListHead = (DHCPc_COMM *)0;


   *perr = DHCPc_ERR_NONE;
}


/*
*********************************************************************************************************
*                                           DHCPc_CommGet()
*
* Description : (1) Allocate & initialize a DHCPc communication object :
*
*                   (a) Get        communication object
*                   (b) Initialize communication object
*                   (c) Insert     communication object at head of communication object list
*                   (d) Return pointer to communication object
*                         OR
*                       Null pointer & error code, on failure
*
*
* Argument(s) : if_nbr          Interface number requesting a communication object.
*
*               comm_msg        Message to pass.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  Communication object successfully allocated
*                                                                   & initialized.
*                               DHCPc_ERR_COMM_NONE_AVAIL       Communication object pool empty.
*
* Return(s)   : Pointer to communication object, if NO errors.
*
*               Pointer to NULL,                 otherwise.
*
* Caller(s)   : DHCPc_Start(),
*               DHCPc_Stop(),
*               DHCPc_TmrCfg().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  DHCPc_COMM  *DHCPc_CommGet (NET_IF_NBR       if_nbr,
                                    DHCPc_COMM_MSG   comm_msg,
                                    DHCPc_ERR       *perr)
{
    DHCPc_COMM  *pcomm;


                                                                /* ------------------- GET COMM OBJ ------------------- */
    if (DHCPc_CommPoolPtr != (DHCPc_COMM *)0) {                 /* If comm obj pool NOT empty, get comm obj from pool   */
        pcomm              = (DHCPc_COMM *)DHCPc_CommPoolPtr;
        DHCPc_CommPoolPtr  = (DHCPc_COMM *)pcomm->NextPtr;

    } else {                                                    /* If none avail, rtn err.                              */
       *perr = DHCPc_ERR_COMM_NONE_AVAIL;
        return ((DHCPc_COMM *)0);
    }

                                                                /* ------------------ INIT COMM OBJ ------------------- */
    DHCPc_CommClr(pcomm);
    pcomm->PrevPtr = (DHCPc_COMM *)0;
    pcomm->NextPtr = (DHCPc_COMM *)DHCPc_CommListHead;
    pcomm->IF_Nbr  =  if_nbr;
    pcomm->CommMsg =  comm_msg;
    DEF_BIT_SET(pcomm->Flags, DHCPc_FLAG_USED);                 /* Set comm obj as used.                                */

                                                                /* -------- INSERT COMM OBJ INTO COMM OBJ LIST -------- */
    if (DHCPc_CommListHead != (DHCPc_COMM *)0) {                /* If list NOT empty, insert before head.               */
        DHCPc_CommListHead->PrevPtr = pcomm;
    }
    DHCPc_CommListHead = pcomm;                                 /* Insert comm obj @ list head.                         */


   *perr =  DHCPc_ERR_NONE;

    return (pcomm);
}


/*
*********************************************************************************************************
*                                          DHCPc_CommFree()
*
* Description : (1) Free a DHCPc communication object :
*
*                   (a) Remove communication object from    communication object list
*                   (b) Clear  communication object controls
*                   (c) Free   communication object back to communication object pool
*
*
* Argument(s) : pcomm       Pointer to a DHCPc communication object.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_Start(),
*               DHCPc_Stop(),
*               DHCPc_MsgRxHandler(),
*               DHCPc_TmrCfg().
*
*               This function is an INTERNAL network protocol suite function & SHOULD NOT be called by
*               application function(s).
*
* Note(s)     : (2) #### To prevent freeing a communication object already freed via previous communication
*                   object free, DHCPc_CommFree() checks the communication object's 'USED' flag BEFORE
*                   freeing the communication object.
*
*                   This prevention is only best-effort since any invalid duplicate communication object
*                   frees MAY be asynchronous to potentially valid communication object gets.  Thus the
*                   invalid communication object free(s) MAY corrupt the communicatino object's valid
*                   operation(s).
*
*                   However, since the primary tasks of the DHCP client are prevented from running
*                   concurrently (see 'dhcp-c.h  Note #2'), it is NOT necessary to protect DHCPc
*                   communication object resources from possible corruption since no asynchronous access
*                   from other task is possible.
*********************************************************************************************************
*/

static  void  DHCPc_CommFree (DHCPc_COMM  *pcomm)
{
#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)
    CPU_BOOLEAN   used;
#endif
    DHCPc_COMM   *pprev;
    DHCPc_COMM   *pnext;


                                                                /* ------------------ VALIDATE PTR -------------------- */
    if (pcomm == (DHCPc_COMM *)0) {
        return;
    }

#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)
                                                                /* -------------- VALIDATE COMM OBJ USED -------------- */
    used = DEF_BIT_IS_SET(pcomm->Flags, DHCPc_FLAG_USED);
    if (used != DEF_YES) {                                      /* If comm obj NOT used, ...                            */
        return;                                                 /* ... rtn but do NOT free (see Note #2).               */
    }
#endif

                                                                /* -------- REMOVE COMM OBJ FROM COMM OBJ LIST -------- */
    pprev = pcomm->PrevPtr;
    pnext = pcomm->NextPtr;
    if (pprev != (DHCPc_COMM *)0) {                             /* If pcomm is NOT   the head of comm obj list, ...     */
        pprev->NextPtr     = pnext;                             /* ... set pprev's NextPtr to skip pcomm.               */
    } else {                                                    /* Else set pnext as head of comm obj list.             */
        DHCPc_CommListHead = pnext;
    }
    if (pnext != (DHCPc_COMM *)0) {                             /* If pcomm is NOT @ the tail of comm obj list, ...     */
        pnext->PrevPtr     = pprev;                             /* ... set pnext's PrevPtr to skip pcomm.               */
    }

                                                                /* ------------------- CLR COMM OBJ ------------------- */
    DEF_BIT_CLR(pcomm->Flags, DHCPc_FLAG_USED);                 /* Set comm obj as NOT used.                            */
#if (NET_DBG_CFG_MEM_CLR_EN == DEF_ENABLED)
    DHCPc_CommClr(pcomm);
#endif

                                                                /* ------------------ FREE COMM OBJ ------------------- */
    pcomm->NextPtr    = DHCPc_CommPoolPtr;
    DHCPc_CommPoolPtr = pcomm;
}


/*
*********************************************************************************************************
*                                           DHCPc_CommClr()
*
* Description : Clear DHCPc communication object controls.
*
* Argument(s) : pcomm       Pointer to a DHCPc communication object.
*               ----        Argument validated in DHCPc_CommInit(),
*                                    checked   in DHCPc_CommGet(),
*                                    checked   in DHCPc_CommFree().
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_CommInit(),
*               DHCPc_CommGet(),
*               DHCPc_CommFree().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  void  DHCPc_CommClr (DHCPc_COMM  *pcomm)
{
    pcomm->PrevPtr = (DHCPc_COMM *)0;
    pcomm->NextPtr = (DHCPc_COMM *)0;

    pcomm->IF_Nbr  =  0;
    pcomm->CommMsg =  DHCPc_COMM_MSG_NONE;

    pcomm->Flags   =  DHCPc_FLAG_NONE;
}


/*
*********************************************************************************************************
*                                           DHCPc_TmrInit()
*
* Description : (1) Initialize DHCPc :
*
*                   (a) Perform Timer/OS initialization
*                   (b) Perform Task/OS  initialization
*                   (c) Initialize timer pool
*                   (d) Initialize timer table
*                   (e) Initialize timer list pointer
*
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                      DHCPc timer module successfully initialized.
*
*                                                                   ----- RETURNED BY DHCPc_OS_TmrInit() : ------
*                               DHCPc_OS_ERR_INIT_TMR               DHCPc timer                NOT successfully
*                                                                       initialized.
*                               DHCPc_OS_ERR_INIT_TMR_SIGNAL        DHCPc timer    signal      NOT successfully
*                                                                       initialized.
*                               DHCPc_OS_ERR_INIT_TMR_SIGNAL_NAME   DHCPc timer    signal name NOT successfully
*                                                                       initialized.
*                               DHCPc_OS_ERR_INIT_TMR_TASK          DHCPc timer    task        NOT successfully
*                                                                       initialized.
*                               DHCPc_OS_ERR_INIT_TMR_TASK_NAME     DHCPc timer    task name   NOT successfully
*                                                                       configured.
*
*                                                                   ----- RETURNED BY DHCPc_OS_TaskInit() : -----
*                               DHCPc_OS_ERR_INIT_TASK              DHCPc task      NOT successfully initialized.
*                               DHCPc_OS_ERR_INIT_TASK_NAME         DHCPc task name NOT successfully configured.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_Init().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (2) Timer pool MUST be initialized PRIOR to initializing the pool with pointers to timers.
*********************************************************************************************************
*/

static  void  DHCPc_TmrInit (DHCPc_ERR  *perr)
{
    DHCPc_TMR      *ptmr;
    DHCPc_TMR_QTY   i;


                                                                /* -------------- PERFORM TIMER/OS INIT --------------- */
    DHCPc_OS_TmrInit(perr);                                     /* Create DHCPc Tmr & Tmr Task.                         */
    if (*perr != DHCPc_OS_ERR_NONE) {
        return;
    }

                                                                /* --------------- PERFORM TASK/OS INIT --------------- */
    DHCPc_OS_TaskInit(perr);                                    /* Create DHCPc Task.                                   */
    if (*perr != DHCPc_OS_ERR_NONE) {
         return;
    }

                                                                /* ------------------ INIT TMR POOL ------------------- */
    DHCPc_TmrPoolPtr = (DHCPc_TMR *)0;                          /* Init-clr DHCPc tmr pool (see Note #2).               */


                                                                /* ------------------ INIT TMR TBL -------------------- */
    ptmr = &DHCPc_TmrTbl[0];
    for (i = 0; i < DHCPc_NBR_TMR; i++) {
        ptmr->ID    = (DHCPc_TMR_QTY)i;
        ptmr->Flags =  DHCPc_FLAG_NONE;                         /* Init each tmr as NOT used.                           */

#if (DHCPc_DBG_CFG_MEM_CLR_EN == DEF_ENABLED)
        DHCPc_TmrClr(ptmr);
#endif

        ptmr->NextPtr    = DHCPc_TmrPoolPtr;                    /* Free each tmr to tmr pool (see Note #2).             */
        DHCPc_TmrPoolPtr = ptmr;

        ptmr++;
    }

                                                                /* ---------------- INIT TMR LIST PTR ----------------- */
    DHCPc_TmrListHead = (DHCPc_TMR *)0;


   *perr = DHCPc_ERR_NONE;
}


/*
*********************************************************************************************************
*                                           DHCPc_TmrCfg()
*
* Description : (1) Configure & insert a timer.
*
*                   (a) Get communication object
*                   (b) Get timer
*
*
* Argument(s) : pif_info        Pointer to DHCP interface information structure.
*               --------        Argument checked   in DHCPc_MsgRxHandler(),
*                                        validated in DHCPc_LeaseTimeCalc(),
*                                                     DHCPc_LeaseTimeUpdate().
*
*               tmr_msg         Timer expiration message.
*
*               time_sec        Initial timer value (in seconds) [see Note #2].
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  Timer successfully configured.
*                               DHCPc_ERR_TMR_INVALID_MSG       Invalid timer message.
*
*                                                               --- RETURNED BY DHCPc_MsgGet() : ---
*                               DHCPc_ERR_COMM_NONE_AVAIL       Communication object pool empty.
*
*                                                               --- RETURNED BY DHCPc_TmrGet() : ---
*                               DHCPc_ERR_TMR_NONE_AVAIL        Timer pool empty.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_MsgRxHandler(),
*               DHCPc_LeaseTimeCalc(),
*               DHCPc_LeaseTimeUpdate().
*
* Note(s)     : (2) Timer value of 0 ticks/seconds allowed; next tick will expire timer.
*********************************************************************************************************
*/

static  void  DHCPc_TmrCfg (DHCPc_IF_INFO   *pif_info,
                            DHCPc_COMM_MSG   tmr_msg,
                            CPU_INT32U       time_sec,
                            DHCPc_ERR       *perr)
{
    DHCPc_COMM      *pcomm;
    DHCPc_TMR_TICK   time_tick;


                                                                /* ----------------- VALIDATE TMR MSG ----------------- */
    switch (tmr_msg) {
        case DHCPc_COMM_MSG_START:
        case DHCPc_COMM_MSG_T1_EXPIRED:
        case DHCPc_COMM_MSG_T2_EXPIRED:
        case DHCPc_COMM_MSG_LEASE_EXPIRED:
             break;


        default:
            *perr = DHCPc_ERR_TMR_INVALID_MSG;
             return;
    }


    if (time_sec == DHCP_LEASE_INFINITE) {                      /* If time infinite, ...                                */
       *perr = DHCPc_ERR_NONE;                                  /* ... rtn.                                             */
        return;
    }

                                                                /* --------------------- CFG TMR ---------------------- */
                                                                /* Get comm obj,                                        */
    pcomm = DHCPc_CommGet((NET_IF_NBR    )pif_info->IF_Nbr,
                          (DHCPc_COMM_MSG)tmr_msg,
                          (DHCPc_ERR    *)perr);
    if (*perr != DHCPc_ERR_NONE) {
         return;
    }

                                                                /* ... & set tmr.                                       */
    time_tick     = (time_sec / DHCPc_TMR_PERIOD_SEC);
    pif_info->Tmr =  DHCPc_TmrGet((void         *)pcomm,
                                  (DHCPc_TMR_TICK)time_tick,
                                  (DHCPc_ERR    *)perr);
    if (*perr != DHCPc_ERR_NONE) {
         DHCPc_CommFree(pcomm);
         return;
    }
}


/*
*********************************************************************************************************
*                                           DHCPc_TmrGet()
*
* Description : (1) Allocate & initialize a DHCPc timer :
*
*                   (a) Get        timer
*                   (b) Validate   timer
*                   (c) Initialize timer
*                   (d) Insert     timer at head of timer list
*                   (e) Return pointer to timer
*                         OR
*                       Null pointer & error code, on failure
*
*               (2) The timer pool is implemented as a stack :
*
*                   (a) 'DHCPc_TmrPoolPtr' points to the head of the timer pool.
*
*                   (b) Timers' 'NextPtr's link each timer to form   the timer pool stack.
*
*                   (c) Timers are inserted & removed at the head of the timer pool stack.
*
*
*                                        Timers are
*                                    inserted & removed
*                                        at the head
*                                      (see Note #2c)
*
*                                             |                NextPtr
*                                             |            (see Note #2b)
*                                             v                   |
*                                                                 |
*                                          -------       -------  v    -------       -------
*                         Timer Pool  ---->|     |------>|     |------>|     |------>|     |
*                          Pointer         |     |       |     |       |     |       |     |
*                                          |     |       |     |       |     |       |     |
*                       (see Note #2a)     -------       -------       -------       -------
*
*                                          |                                               |
*                                          |<------------ Pool of Free Timers ------------>|
*                                          |                 (see Note #2)                 |
*
*
* Argument(s) : pobj        Pointer to object that requests a timer.
*               ----        Argument validated in DHCPc_TmrCfg().
*
*               time_tick   Initial timer value (in 'DHCPc_TMR_TICK' ticks) [see Note #3].
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  Timer successfully allocated & initialized.
*                               DHCPc_ERR_TMR_NONE_AVAIL        Timer pool empty.
*
* Return(s)   : Pointer to timer, if NO errors.
*
*               Pointer to NULL,  otherwise.
*
* Caller(s)   : DHCPc_TmrCfg().
*
* Note(s)     : (3) Timer value of 0 ticks/seconds allowed; next tick will expire timer.
*********************************************************************************************************
*/

static  DHCPc_TMR  *DHCPc_TmrGet (void            *pobj,
                                  DHCPc_TMR_TICK   time_tick,
                                  DHCPc_ERR       *perr)
{
    DHCPc_TMR  *ptmr;


                                                                /* --------------------- GET TMR ---------------------- */
    if (DHCPc_TmrPoolPtr != (DHCPc_TMR *)0) {                   /* If tmr pool NOT empty, get tmr from pool.            */
        ptmr              = (DHCPc_TMR *)DHCPc_TmrPoolPtr;
        DHCPc_TmrPoolPtr  = (DHCPc_TMR *)ptmr->NextPtr;

    } else {                                                    /* If none avail, rtn err.                              */
       *perr = DHCPc_ERR_TMR_NONE_AVAIL;
        return ((DHCPc_TMR *)0);
    }


                                                                /* --------------------- INIT TMR --------------------- */
    DHCPc_TmrClr(ptmr);
    ptmr->PrevPtr = (DHCPc_TMR *)0;
    ptmr->NextPtr = (DHCPc_TMR *)DHCPc_TmrListHead;
    ptmr->Obj     = pobj;
    ptmr->TmrVal  = time_tick;                                  /* Set tmr val (in ticks).                              */
    DEF_BIT_SET(ptmr->Flags, DHCPc_FLAG_USED);                  /* Set tmr as used.                                     */

                                                                /* ------------- INSERT TMR INTO TMR LIST ------------- */
    if (DHCPc_TmrListHead != (DHCPc_TMR *)0) {                  /* If list NOT empty, insert before head.               */
        DHCPc_TmrListHead->PrevPtr = ptmr;
    }
    DHCPc_TmrListHead = ptmr;                                   /* Insert tmr @ list head.                              */


   *perr =  DHCPc_ERR_NONE;

    return (ptmr);

}


/*
*********************************************************************************************************
*                                           DHCPc_TmrFree()
*
* Description : (1) Free a DHCPc timer :
*
*                   (a) Remove timer from    timer list
*                   (b) Clear  timer controls
*                   (c) Free   timer back to timer pool
*
*
* Argument(s) : ptmr        Pointer to a DHCPc timer.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_TmrTaskHandler(),
*               DHCPc_StopStateHandler().
*
*               This function is an INTERNAL network protocol suite function & SHOULD NOT be called by
*               application function(s).
*
* Note(s)     : (2) #### To prevent freeing a timer already freed via previous timer free,
*                   DHCPc_TmrFree() checks the timer's 'USED' flag BEFORE freeing the timer.
*
*                   This prevention is only best-effort since any invalid duplicate timer frees MAY be
*                   asynchronous to potentially valid timer gets.  Thus the invalid timer free(s) MAY
*                   corrupt the timer's valid operation(s).
*
*                   However, since the primary tasks of the DHCP client are prevented from running
*                   concurrently (see 'dhcp-c.h  Note #2'), it is NOT necessary to protect DHCPc
*                   timer resources from possible corruption since no asynchronous access from other
*                   task is possible.
*********************************************************************************************************
*/

static  void  DHCPc_TmrFree (DHCPc_TMR  *ptmr)
{
#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)
    CPU_BOOLEAN   used;
#endif
    DHCPc_TMR    *pprev;
    DHCPc_TMR    *pnext;


                                                                /* ------------------ VALIDATE PTR -------------------- */
    if (ptmr == (DHCPc_TMR *)0) {
        return;
    }

#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)
                                                                /* ---------------- VALIDATE TMR USED ----------------- */
    used = DEF_BIT_IS_SET(ptmr->Flags, DHCPc_FLAG_USED);
    if (used != DEF_YES) {                                      /* If tmr NOT used, ...                                 */
        return;                                                 /* ... rtn but do NOT free (see Note #2).               */
    }
#endif

                                                                /* ------------- REMOVE TMR FROM MSG LIST ------------- */
    pprev = ptmr->PrevPtr;
    pnext = ptmr->NextPtr;
    if (pprev != (DHCPc_TMR *)0) {                              /* If ptmr is NOT   the head of tmr list, ...           */
        pprev->NextPtr    = pnext;                              /* ... set pprev's NextPtr to skip ptmr.                */
    } else {                                                    /* Else set pnext as head of tmr list.                  */
        DHCPc_TmrListHead = pnext;
    }
    if (pnext != (DHCPc_TMR *)0) {                              /* If ptmr is NOT @ the tail of tmr list, ...           */
        pnext->PrevPtr    = pprev;                              /* ... set pnext's PrevPtr to skip ptmr.                */
    }

                                                                /* ---------------------- CLR TMR --------------------- */
    DEF_BIT_CLR(ptmr->Flags, DHCPc_FLAG_USED);                  /* Set tmr as NOT used.                                 */
#if (NET_DBG_CFG_MEM_CLR_EN == DEF_ENABLED)
    DHCPc_TmrClr(ptmr);
#endif

                                                                /* --------------------- FREE TMR --------------------- */
    ptmr->NextPtr    = DHCPc_TmrPoolPtr;
    DHCPc_TmrPoolPtr = ptmr;
}


/*
*********************************************************************************************************
*                                           DHCPc_TmrClr()
*
* Description : Clear DHCPc timer controls.
*
* Argument(s) : ptmr        Pointer to a DHCPc timer.
*               ----        Argument validated in DHCPc_TmrInit(),
*                                    checked   in DHCPc_TmrGet(),
*                                    checked   in DHCPc_TmrFree().
*
* Return(s)   : none.
*
* Caller(s)   : NetTmr_Init(),
*               DHCPc_TmrGet(),
*               DHCPc_TmrFree().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  void  DHCPc_TmrClr (DHCPc_TMR  *ptmr)
{
    ptmr->PrevPtr = (DHCPc_TMR     *)0;
    ptmr->NextPtr = (DHCPc_TMR     *)0;

    ptmr->Obj     = (void          *)0;
    ptmr->TmrVal  =  0;

    ptmr->Flags   =  DHCPc_FLAG_NONE;
}


/*
*********************************************************************************************************
*                                          DHCPc_InitSock()
*
* Description : Initialize a socket.
*
* Argument(s) : ip_addr_local       Local IP address to bind to, in network order.
*
* Return(s)   : Socket descriptor/handle identifier, if NO errors.
*
*               NET_SOCK_BSD_ERR_OPEN,               otherwise.
*
* Caller(s)   : DHCPc_InitStateHandler(),
*               DHCPc_RenewRebindStateHandler(),
*               DHCPc_StopStateHandler().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  NET_SOCK_ID  DHCPc_InitSock (NET_IPv4_ADDR  ip_addr_local,
                                     NET_IF_NBR     if_nbr)
{
    NET_SOCK_ID         sock_id;
    NET_SOCK_ADDR_IPv4  local_addr;
    NET_SOCK_ADDR_LEN   local_addr_size;
    CPU_BOOLEAN         rtn_status;
    CPU_BOOLEAN         success;
    NET_ERR             err_net;


                                                                /* -------------------- OPEN SOCK --------------------- */
    sock_id = NetApp_SockOpen((NET_SOCK_PROTOCOL_FAMILY) NET_SOCK_ADDR_FAMILY_IP_V4,
                              (NET_SOCK_TYPE           ) NET_SOCK_TYPE_DATAGRAM,
                              (NET_SOCK_PROTOCOL       ) NET_SOCK_PROTOCOL_UDP,
                              (CPU_INT16U              ) 0,
                              (CPU_INT32U              ) 0,
                              (NET_ERR                *)&err_net);

    if (sock_id == NET_SOCK_BSD_ERR_OPEN) {
        return (NET_SOCK_BSD_ERR_OPEN);
    }

                                                                /* ------------ SET IF NBR FOR THE SOCKET ------------ */
    success = NetSock_CfgIF(sock_id,
                            if_nbr,
                           &err_net);
    if (success != DEF_OK) {
        return (NET_SOCK_BSD_ERR_OPEN);
    }

                                                                /* ------------------ SET LOCAL ADDR ------------------ */
    local_addr_size = sizeof(local_addr);
    Mem_Clr((void     *)&local_addr,
            (CPU_SIZE_T) local_addr_size);
    local_addr.AddrFamily = NET_SOCK_ADDR_FAMILY_IP_V4;
    local_addr.Addr       = ip_addr_local;
    local_addr.Port       = NET_UTIL_HOST_TO_NET_16(DHCPc_CFG_IP_PORT_CLIENT);


                                                                /* -------------------- BIND SOCK --------------------- */
    rtn_status = NetApp_SockBind((NET_SOCK_ID      ) sock_id,
                                 (NET_SOCK_ADDR   *)&local_addr,
                                 (NET_SOCK_ADDR_LEN) local_addr_size,
                                 (CPU_INT16U       ) 0,
                                 (CPU_INT32U       ) 0,
                                 (NET_ERR         *)&err_net);

    if (rtn_status != DEF_OK) {
        NetApp_SockClose((NET_SOCK_ID ) sock_id,
                         (CPU_INT32U  ) 0,
                         (NET_ERR    *)&err_net);

        return (NET_SOCK_BSD_ERR_OPEN);
    }


    return (sock_id);
}


/*
*********************************************************************************************************
*                                      DHCPc_InitStateHandler()
*
* Description : (1) Perform actions associated with the INIT state :
*
*                   (a) Get      interface's hardware address
*                   (b) Initialize socket
*                   (c) Start    interface's dynamic configuration
*                   (d) Transmit DISCOVER & select OFFER
*                   (e) Transmit REQUEST  & get    reply
*                   (f) Configure interface & lease timer
*
*
* Argument(s) : pif_info    Pointer to DHCP interface information.
*               --------    Argument validated in DHCPc_MsgRxHandler().
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  DHCP lease successfully negotiated &
*                                                                   interface configured (timer     set).
*                               DHCPc_ERR_NONE_NO_TMR           DHCP lease successfully negotiated &
*                                                                   interface configured (timer NOT set).
*                               DHCPc_ERR_NONE_LOCAL_LINK       DHCP lease negotiation error, interface
*                                                                   configured with a link-local address.
*                               DHCPc_ERR_LOCAL_LINK            Error configuring dynamic link-local address.
*                               DHCPc_ERR_IF_INVALID            Interface invalid or disabled.
*                               DHCPc_ERR_IF_CFG_STATE          Error setting interface configuration state.
*                               DHCPc_ERR_INVALID_HW_ADDR       Error retrieving interface's hardware address.
*                               DHCPc_ERR_INIT_SOCK             Error initializing socket.
*
*                                                               -------- RETURNED BY DHCPc_Discover() : ---------
*                               DHCPc_ERR_NULL_PTR              Argument(s) passed a NULL pointer.
*                               DHCPc_ERR_MSG_NONE_AVAIL        Message pool empty.
*                               DHCPc_ERR_INVALID_MSG_SIZE      Argument 'pmsg_buf' size invalid.
*                               DHCPc_ERR_INVALID_MSG           Invalid DHCP message.
*                               DHCPc_ERR_TX                    Transmit error.
*                               DHCPc_ERR_RX_MSG_TYPE           Error extracting message type from reply message.
*                               DHCPc_ERR_RX_OVF                Receive error, data buffer overflow.
*                               DHCPc_ERR_RX                    Receive error.
*
*                                                               ---------- RETURNED BY DHCPc_Req() : ------------
*                               DHCPc_ERR_NULL_PTR              Argument(s) passed a NULL pointer.
*                               DHCPc_ERR_RX_NAK                NAK message received from server.
*                               DHCPc_ERR_MSG_NONE_AVAIL        Message pool empty.
*                               DHCPc_ERR_INVALID_MSG_SIZE      Argument 'pmsg_buf' size invalid.
*                               DHCPc_ERR_INVALID_MSG           Invalid DHCP message.
*                               DHCPc_ERR_TX                    Transmit error.
*                               DHCPc_ERR_INVALID_MSG_SIZE      Argument 'pmsg_buf' size invalid.
*                               DHCPc_ERR_RX_MSG_TYPE           Error extracting message type from reply message.
*                               DHCPc_ERR_RX_OVF                Receive error, data buffer overflow.
*                               DHCPc_ERR_RX                    Receive error.
*
*                                                               -------- RETURNED BY DHCPc_AddrCfg() : ----------
*                               DHCPc_ERR_IF_CFG                Error configuring the interface's network.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_MsgRxHandler().
*
* Note(s)     : (2) #### This implementation of the DHCP client presumes an Ethernet hardware type.
*
*               (3) From RFC 2131, section 'Constructing and sending DHCP messages', "DHCP messages broadcast
*                   by a client prior to that client obtaining its IP address must have the source address
*                   field in the IP header set to 0".  Starting the dynamic configuration results in all
*                   addresses being removed from the interface and set to 0.
*
*               (4) RFC #2131, section 'Client-Server interaction - allocating a network address', states
*                   that "The client SHOULD wait a mininum of ten seconds before restarting the
*                   configuration process to avoid excessive network traffic in case of looping".
*********************************************************************************************************
*/

static  void  DHCPc_InitStateHandler (DHCPc_IF_INFO  *pif_info,
                                      DHCPc_ERR      *perr)
{
    NET_IF_NBR      if_nbr;
    CPU_BOOLEAN     if_en;
    CPU_INT08U      addr_hw_len;
    CPU_INT08U      addr_hw[NET_IF_ETHER_ADDR_SIZE];
    NET_SOCK_ID     sock_id;
    CPU_INT16U      nego_retry_cnt;
    CPU_BOOLEAN     nego_done;
    CPU_BOOLEAN     nego_dly;
#if (DHCPc_CFG_ADDR_VALIDATE_EN == DEF_ENABLED)
    DHCPc_MSG      *pmsg;
    DHCP_MSG_HDR   *pmsg_hdr;
    NET_IPv4_ADDR   proposed_addr;
#endif
    NET_ERR         err_net;


    if_nbr = pif_info->IF_Nbr;

    if_en  = NetIF_IsEnCfgd(if_nbr, &err_net);                  /* Validate IF en.                                      */
    if (if_en != DEF_YES) {                                     /* If IF NOT enabled, ...                               */
       *perr = DHCPc_ERR_IF_INVALID;                            /* ... rtn err.                                         */
        return;
    }

                                                                /* ------------------- GET HW ADDR -------------------- */
    addr_hw_len = NET_IF_ETHER_ADDR_SIZE;                       /* See Note #2.                                         */
    NetIF_AddrHW_Get( if_nbr,
                     &addr_hw[0],
                     &addr_hw_len,
                     &err_net);
    if ((err_net     != NET_IF_ERR_NONE) ||
        (addr_hw_len != NET_IF_ETHER_ADDR_SIZE)) {
       *perr = DHCPc_ERR_INVALID_HW_ADDR;
        return;
    }

                                                                /* -------------------- INIT SOCK --------------------- */
    sock_id = DHCPc_InitSock(NET_IPv4_ADDR_THIS_HOST, if_nbr);
    if (sock_id == NET_SOCK_BSD_ERR_OPEN) {
       *perr = DHCPc_ERR_INIT_SOCK;
        return;
    }

                                                                /* ---------------- START DYNAMIC CFG ----------------- */
    NetIPv4_CfgAddrAddDynamicStart(if_nbr, &err_net);           /* See Note #3.                                         */
    if (err_net != NET_IPv4_ERR_NONE) {
       *perr = DHCPc_ERR_IF_CFG_STATE;
        return;
    }


                                                                /* ------------ TX DISCOVER & SELECT OFFER ------------ */
    nego_retry_cnt = 0;
    nego_done      = DEF_NO;
    nego_dly       = DEF_NO;

    while ((nego_retry_cnt <  DHCPc_CFG_NEGO_RETRY_CNT) &&
           (nego_done      != DEF_YES)) {

        pif_info->ClientState = DHCP_STATE_INIT;

        if (nego_dly == DEF_YES) {                              /* Dly nego, if req'd (see Note #4).                    */
            KAL_Dly(DHCP_INIT_DLY_MS);
        }

        DHCPc_Discover(sock_id, pif_info, &addr_hw[0], addr_hw_len, perr);
        if (*perr != DHCPc_ERR_NONE) {
            nego_retry_cnt++;
            nego_dly = DEF_YES;

        } else {                                                /* DISCOVER tx'd & OFFER(s) rx'd, ..                    */
                                                                /* .. tx REQUEST & get reply.                           */
            pif_info->ClientState = DHCP_STATE_SELECTING;

            DHCPc_Req(sock_id, pif_info, &addr_hw[0], addr_hw_len, perr);

            switch (*perr) {
                case DHCPc_ERR_NONE:
#if (DHCPc_CFG_ADDR_VALIDATE_EN == DEF_ENABLED)
                                                                /* Get proposed addr.                                   */
                     pmsg     = (DHCPc_MSG    *) pif_info->MsgPtr;
                     pmsg_hdr = (DHCP_MSG_HDR *)&pmsg->MsgBuf[0];

                     NET_UTIL_VAL_COPY_32(&proposed_addr, &pmsg_hdr->yiaddr);

                                                                /* Validate proposed addr.                              */
                     DHCPc_AddrValidate(               if_nbr,
                                        (NET_IPv4_ADDR)proposed_addr,
                                        (CPU_INT32U   )DHCP_ADDR_VALIDATE_WAIT_TIME_MS,
                                        (DHCPc_ERR   *)perr);
                     switch (*perr) {
                         case DHCPc_ERR_NONE:
                         case DHCPc_ERR_ADDR_VALIDATE:
                              nego_done = DEF_YES;
                              break;


                         case DHCPc_ERR_ADDR_USED:
                         default:
                             DHCPc_DeclineRelease((NET_SOCK_ID    ) sock_id,
                                                  (DHCPc_IF_INFO *) pif_info,
                                                  (DHCPc_MSG_TYPE ) DHCP_MSG_DECLINE,
                                                  (CPU_INT08U    *)&addr_hw[0],
                                                  (CPU_INT08U     ) addr_hw_len,
                                                  (DHCPc_ERR     *) perr);
                             nego_retry_cnt++;
                             nego_dly = DEF_YES;
                             break;
                     }
#else
                     nego_done = DEF_YES;
#endif
                     break;


                case DHCPc_ERR_RX_NAK:
                     nego_retry_cnt++;
                     nego_dly = DEF_YES;
                     break;


                default:
                     nego_done = DEF_YES;
                     break;
            }
        }
    }

    NetApp_SockClose((NET_SOCK_ID ) sock_id,                    /* Close sock.                                          */
                     (CPU_INT32U  ) 0,
                     (NET_ERR    *)&err_net);


                                                                /* ------------- CFG IF WITH NEGO'D LEASE ------------- */
    switch (*perr) {
        case DHCPc_ERR_NONE:                                    /* If lease successfully acquired, ...                  */

             DHCPc_AddrCfg(pif_info, perr);                     /* ... cfg net addr                ...                  */
             if (*perr == DHCPc_ERR_NONE) {
                 DHCPc_LeaseTimeCalc(pif_info, perr);           /* ... calc lease time & set tmr.                       */
                 if (*perr != DHCPc_ERR_NONE) {                 /* If err setting tmr, ...                              */
                    *perr = DHCPc_ERR_NONE_NO_TMR;              /* ... rtn err         ...                              */
                 }

                 pif_info->ClientState = DHCP_STATE_BOUND;      /* ... & set client state to BOUND.                     */

             } else {                                           /* If err cfg'ing IF, ...                               */
                                                                /* ... stop dynamic cfg & set client state to NONE.     */
                 NetIPv4_CfgAddrAddDynamicStop(if_nbr, &err_net);
                 pif_info->ClientState = DHCP_STATE_NONE;
             }
             break;


        case DHCPc_ERR_RX_NAK:                                  /* ... Else if err, ..                                  */
        default:
#if (DHCPc_CFG_DYN_LOCAL_LINK_ADDR_EN == DEF_ENABLED)           /*     .. & dyn link local ENABLED, ...                 */
                                                                /*        ... cfg using link local addr.                */
             DHCPc_AddrLocalLinkCfg(pif_info, &addr_hw[0], addr_hw_len, perr);
             if (*perr == DHCPc_ERR_NONE) {
                 pif_info->ClientState = DHCP_STATE_LOCAL_LINK;
                *perr                  = DHCPc_ERR_NONE_LOCAL_LINK;

             } else {
                 NetIPv4_CfgAddrAddDynamicStop(if_nbr, &err_net);

                 pif_info->ClientState = DHCP_STATE_NONE;
                *perr                  = DHCPc_ERR_LOCAL_LINK;
             }
#else
             NetIPv4_CfgAddrAddDynamicStop(if_nbr, &err_net);
             pif_info->ClientState = DHCP_STATE_NONE;
#endif
             break;
    }
}


/*
*********************************************************************************************************
*                                   DHCPc_RenewRebindStateHandler()
*
* Description : (1) Perform actions associated with the RENEW/REBIND state :
*
*                   (b) Get interface's hardware address
*                   (c) Initialize socket
*                   (e) Transmit REQUEST & get reply
*                   (f) Configure lease timer
*
*
* Argument(s) : pif_info        Pointer to DHCP interface information.
*               --------        Argument validated in DHCPc_MsgRxHandler().
*
*               exp_tmr_msg     Expired timer message.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  DHCP lease successfully renewed/rebound.
*                               DHCPc_ERR_NONE_NO_TMR           Error setting timer, lease might NOT have
*                                                                   been renewed/rebound (see Note #4).
*                               DHCPc_ERR_INVALID_MSG           Invalid timer expiration message.
*                               DHCPc_ERR_IF_INVALID            Interface invalid or disabled.
*                               DHCPc_ERR_INVALID_HW_ADDR       Error retrieving interface's hardware address.
*                               DHCPc_ERR_INIT_SOCK             Error initializing socket.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_MsgRxHandler().
*
* Note(s)     : (2) #### This implementation of the DHCP client presumes an Ethernet hardware type.
*
*               (3) If the socket cannot be opened, the DHCP lease is updated, and a new timer is set
*                   so that the renewing/rebinding process can take place later.
*
*               (4) A DHCPc_ERR_NONE_NO_TMR error indicates that there was an error either calculating
*                   the new lease time if it was successfully  renewed/rebound, or the update lease time
*                   was not successful should the lease not be renewed/rebound.
*
*                   In both cases, the lease then becomes technically infinite since NO timer is set.
*                   This could cause an expired lease to still be used by this host, which would violate
*                   RFC #2131.
*********************************************************************************************************
*/

static  void  DHCPc_RenewRebindStateHandler (DHCPc_IF_INFO   *pif_info,
                                             DHCPc_COMM_MSG   exp_tmr_msg,
                                             DHCPc_ERR       *perr)
{
#if (DHCPc_CFG_BROADCAST_BIT_EN != DEF_ENABLED)
    DHCPc_MSG      *pmsg;
    DHCP_MSG_HDR   *pmsg_hdr;
#endif
    NET_IF_NBR      if_nbr;
    CPU_BOOLEAN     if_en;
    CPU_INT08U      addr_hw_len;
    CPU_INT08U      addr_hw[NET_IF_ETHER_ADDR_SIZE];
    NET_IPv4_ADDR   addr_host;
    NET_SOCK_ID     sock_id;
    NET_ERR         err_net;


    switch (exp_tmr_msg) {                                      /* Set cur client state.                                */
        case DHCPc_COMM_MSG_T1_EXPIRED:
             pif_info->ClientState = DHCP_STATE_RENEWING;
             break;


        case DHCPc_COMM_MSG_T2_EXPIRED:
             pif_info->ClientState = DHCP_STATE_REBINDING;
             break;


        default:
            *perr = DHCPc_ERR_INVALID_MSG;
             return;
    }


    if_nbr = pif_info->IF_Nbr;

    if_en  = NetIF_IsEnCfgd(if_nbr, &err_net);                  /* Validate IF en.                                      */
    if (if_en != DEF_YES) {                                     /* If IF NOT enabled, ...                               */
       *perr = DHCPc_ERR_IF_INVALID;                            /* ... rtn err.                                         */
        return;
    }

                                                                /* ------------------- GET HW ADDR -------------------- */
    addr_hw_len = NET_IF_ETHER_ADDR_SIZE;                       /* See Note #2.                                         */
    NetIF_AddrHW_Get( if_nbr,
                     &addr_hw[0],
                     &addr_hw_len,
                     &err_net);
    if ((err_net     != NET_IF_ERR_NONE) ||
        (addr_hw_len != NET_IF_ETHER_ADDR_SIZE)) {
       *perr = DHCPc_ERR_INVALID_HW_ADDR;
        return;
    }

                                                                /* -------------------- INIT SOCK --------------------- */
#if (DHCPc_CFG_BROADCAST_BIT_EN != DEF_ENABLED)
    pmsg     = (DHCPc_MSG    *) pif_info->MsgPtr;               /* Get host addr from cur OFFER.                        */
    pmsg_hdr = (DHCP_MSG_HDR *)&pmsg->MsgBuf[0];
    NET_UTIL_VAL_COPY_32(&addr_host, &pmsg_hdr->yiaddr);
#else
    addr_host = NET_IPv4_ADDR_THIS_HOST;
#endif

    sock_id = DHCPc_InitSock(addr_host, if_nbr);
    if (sock_id == NET_SOCK_BSD_ERR_OPEN) {                     /* If sock NOT opened,            ...                   */
        DHCPc_LeaseTimeUpdate(pif_info, exp_tmr_msg, perr);     /* ... update cur lease & cfg tmr ...                   */
        if (*perr == DHCPc_ERR_NONE) {
           *perr = DHCPc_ERR_INIT_SOCK;                         /* ... & set err (see Note #3).                         */

        } else {
           *perr = DHCPc_ERR_NONE_NO_TMR;
        }

        return;
    }

                                                                /* -------------- TX REQUEST & GET REPLY -------------- */

                                                                /* Tx REQUEST & get reply.                              */
    DHCPc_Req(sock_id, pif_info, &addr_hw[0], addr_hw_len, perr);

    NetApp_SockClose((NET_SOCK_ID ) sock_id,
                     (CPU_INT32U  ) 0,
                     (NET_ERR    *)&err_net);

    if (*perr == DHCPc_ERR_NONE) {                              /* If lease renewed/rebound,       ...                  */
        DHCPc_LeaseTimeCalc(pif_info, perr);                    /* ... calc lease time & cfg tmr.                       */

    } else {                                                    /* Else lease NOT renewed/rebound, ...                  */
        DHCPc_LeaseTimeUpdate(pif_info, exp_tmr_msg, perr);     /* ... update cur lease & cfg tmr.                      */
    }


    if (*perr != DHCPc_ERR_NONE) {                              /* If err setting tmr, ...                              */
       *perr = DHCPc_ERR_NONE_NO_TMR;                           /* ... rtn err (see Note #4).                           */
    }

    pif_info->ClientState = DHCP_STATE_BOUND;
}


/*
*********************************************************************************************************
*                                      DHCPc_StopStateHandler()
*
* Description : (1) Perform actions associated with the STOPPING state :
*
*                   (a) Transmit RELEASE message, if necessary
*                   (b) Free     interface's  objects
*                   (c) Remove   interface IP address
*
*
* Argument(s) : pif_info    Pointer to DHCP interface information.
*               --------    Argument checked   in DHCPc_MsgRxHandler(),
*                                    validated in DHCPc_InitStateHandler(),
*                                              in DHCPc_RenewRebindStateHandler().
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  Interface DHCP configuration successfully
*                                                                   stopped.
*                               DHCPc_ERR_IF_CFG                Error removing interface IP address from
*                                                                   stack.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_MsgRxHandler().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (2) #### This implementation of the DHCP client presumes an Ethernet hardware type.
*
*               (3) RFC #2131, section 'DHCP client behaviour - DHCPRELEASE', states that "if the client
*                   no longer requires use of its assigned network address [...], the client sends a
*                   DHCPRELEASE message to the server.  Note that the correct operation of DHCP does not
*                   depend on the transmission of DHCPRELEASE messages."
*
*                   Hence, if an error occurs while attempting to transmit a DHCPRELEASE message, no
*                   error handling is performed.
*********************************************************************************************************
*/

static  void  DHCPc_StopStateHandler (DHCPc_IF_INFO  *pif_info,
                                      DHCPc_ERR      *perr)
{
    DHCPc_STATE     client_state;
    CPU_BOOLEAN     tx_decline;
    CPU_INT08U      addr_hw_len;
    CPU_INT08U      addr_hw[NET_IF_ETHER_ADDR_SIZE];
    DHCPc_MSG      *pmsg;
    DHCP_MSG_HDR   *pmsg_hdr;
    NET_IPv4_ADDR   addr_host;
    NET_SOCK_ID     sock_id;
    DHCPc_TMR      *ptmr;
    DHCPc_COMM     *pcomm;
    NET_IF_NBR      if_nbr;
    NET_ERR         err_net;


    if_nbr                = pif_info->IF_Nbr;
    client_state          = pif_info->ClientState;              /* Get client state ...                                 */
    pif_info->ClientState = DHCP_STATE_STOPPING;                /* ... and set it to STOPPING.                          */

                                                                /* -------------------- TX RELEASE -------------------- */
    switch (client_state) {
        case DHCP_STATE_REQUESTING:
        case DHCP_STATE_BOUND:
        case DHCP_STATE_RENEWING:
        case DHCP_STATE_REBINDING:
             tx_decline = DEF_YES;
             break;


        default:
             tx_decline = DEF_NO;
             break;
    }

    if (tx_decline == DEF_YES) {
                                                                /* ------------------- GET HW ADDR -------------------- */
        addr_hw_len = NET_IF_ETHER_ADDR_SIZE;                   /* See Note #2.                                         */
        NetIF_AddrHW_Get( if_nbr,
                         &addr_hw[0],
                         &addr_hw_len,
                         &err_net);
        if ((err_net     == NET_IF_ERR_NONE) &&                 /* See Note #3.                                         */
            (addr_hw_len == NET_IF_ETHER_ADDR_SIZE)) {

                                                                /* -------------------- INIT SOCK --------------------- */
            pmsg     = (DHCPc_MSG    *) pif_info->MsgPtr;       /* Get host addr from cur OFFER.                        */
            pmsg_hdr = (DHCP_MSG_HDR *)&pmsg->MsgBuf[0];
            NET_UTIL_VAL_COPY_32(&addr_host, &pmsg_hdr->yiaddr);

            sock_id = DHCPc_InitSock(addr_host, if_nbr);
            if (sock_id != NET_SOCK_BSD_ERR_OPEN) {

                DHCPc_DeclineRelease((NET_SOCK_ID    ) sock_id,
                                     (DHCPc_IF_INFO *) pif_info,
                                     (DHCPc_MSG_TYPE ) DHCP_MSG_RELEASE,
                                     (CPU_INT08U    *)&addr_hw[0],
                                     (CPU_INT08U     ) addr_hw_len,
                                     (DHCPc_ERR     *) perr);

                                                                /* Dly to resolve dest addr.                            */
                KAL_Dly((CPU_INT32U)(DHCP_RELEASE_DLY_S * DEF_TIME_NBR_mS_PER_SEC));

                NetApp_SockClose((NET_SOCK_ID ) sock_id,        /* Close sock.                                          */
                                 (CPU_INT32U  ) 0,
                                 (NET_ERR    *)&err_net);
            }
        }
    }

                                                                /* ---------------- FREE IF'S DATA OBJ ---------------- */
    ptmr = pif_info->Tmr;
    if (ptmr != (DHCPc_TMR *)0) {                               /* If lease tmr not NULL, ...                           */
        pcomm = (DHCPc_COMM *)ptmr->Obj;
        if (pcomm != (DHCPc_COMM *)0) {
            DHCPc_CommFree(pcomm);                              /* ...    free comm       ...                           */
        }

        DHCPc_TmrFree(ptmr);                                    /* ...  & free tmr.                                     */
        pif_info->Tmr = (DHCPc_TMR *)0;                         /* Prevents a double-free of the timer.                 */
    }

    pmsg = pif_info->MsgPtr;
    if (pmsg != (DHCPc_MSG *)0) {                               /* If msg       not NULL, ...                           */
        DHCPc_MsgFree(pmsg);                                    /* ... free msg.                                        */
    }

    DHCPc_IF_InfoFree(pif_info);

                                                                /* ----------------- REM IF'S IP ADDR ----------------- */
    NetIPv4_CfgAddrRemoveAll(if_nbr, &err_net);
    if (err_net != NET_IPv4_ERR_NONE) {
       *perr = DHCPc_ERR_IF_CFG;

    } else {
       *perr = DHCPc_ERR_NONE;
    }
}


/*
*********************************************************************************************************
*                                          DHCPc_Discover()
*
* Description : (1) Perform the DISCOVER phase of the lease negotiation :
*
*                   (a) Get message
*                   (b) Generate new 'xid' (see Note #2)
*                   (c) Prepare  DISCOVER message
*                   (d) Transmit DISCOVER message
*                   (e) Get reply from server(s)
*                   (f) Copy lease OFFER
*
*
* Argument(s) : sock_id         Socket ID of socket to transmit & receive DHCPc data.
*
*               pif_info        Pointer to DHCP interface information.
*               --------        Argument checked in DHCPc_InitStateHandler().
*
*               paddr_hw        Pointer to hardware address buffer.
*
*               addr_hw_len     Length of the hardware address buffer pointed to by 'paddr_hw'.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  DISCOVER successfully transmitted & OFFER(S)
*                                                                   received (see Note #2).
*                               DHCPc_ERR_NULL_PTR              Argument(s) passed a NULL pointer.
*                               DHCPc_ERR_INVALID_HW_ADDR       Argument 'paddr_hw' has an invalid length.
*
*                                                               --------- RETURNED BY DHCPc_MsgGet() : ----------
*                               DHCPc_ERR_MSG_NONE_AVAIL        Message pool empty.
*
*                                                               ------ RETURNED BY DHCPc_TxMsgPrepare() : -------
*                               DHCPc_ERR_INVALID_MSG_SIZE      Argument 'pmsg_buf' size invalid.
*                               DHCPc_ERR_INVALID_MSG           Invalid DHCP message.
*
*                                                               ----------- RETURNED BY DHCPc_Tx() : ------------
*                               DHCPc_ERR_TX                    Transmit error.
*
*                                                               -------- RETURNED BY DHCPc_RxReply() : ----------
*                               DHCPc_ERR_INVALID_MSG_SIZE      Argument 'pmsg_buf' size invalid.
*                               DHCPc_ERR_RX_MSG_TYPE           Error extracting message type from reply message.
*                               DHCPc_ERR_RX_OVF                Receive error, data buffer overflow.
*                               DHCPc_ERR_RX                    Receive error.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_InitStateHandler().
*
* Note(s)     : (1) RFC #2131, section 'Constructing and sending DHCP messages', states that "selecting
*                   a new 'xid' for each retransmission is an implementation decision.  A client may
*                   choose to reuse the same 'xid' or select a new 'xid' for each retransmitted message".
*
*                   This implementation increments the previously used 'xid' and used that new value as
*                   the transaction ID.
*
*               (2) When the function returns DHCPc_ERR_NONE, the OFFER message's parameters are copied
*                   into the structure pointed to by 'pif_info' so that a REQUEST message can be crafted.
*
*               (3) If NO DHCP OFFER is received following a DHCP DISCOVER transmission, the caller is
*                   responsible of the retransmission handling--i.e. this function will NOT attempt to
*                   send another DISCOVER.
*
*               (4) In the event of a surge of DHCP OFFER datagrams coming from multiple hosts on the
*                   network attempting to acquire an IP address from a DHCP server, the socket receive
*                   queue should be closed during a backoff delay in order to prevent Rx buffers from
*                   being consumed. This is due to the fact that DHCP OFFER datagrams are sent to the
*                   broadcast address (255.255.255.255) on port 68 and thus the uC/TCPIP stack will accept
*                   them. Since the DHCP client does not process any offers during a backoff delay, these
*                   incoming datagrams will get queued until the client consumes them after the delay
*                   which may last up to 64 seconds (See Note #1 in DHCPc_CalcBackOff()). Closing the
*                   socket receive queue prompts the TCP IP stack to drop those datagrams before they're
*                   ever queued.
*********************************************************************************************************
*/

static  void  DHCPc_Discover (NET_SOCK_ID     sock_id,
                              DHCPc_IF_INFO  *pif_info,
                              CPU_INT08U     *paddr_hw,
                              CPU_INT08U      addr_hw_len,
                              DHCPc_ERR      *perr)
{
    DHCPc_MSG           *pmsg;
    CPU_INT16U           discover_retry_cnt;
    CPU_BOOLEAN          discover_done;
    CPU_BOOLEAN          discover_dly;
    CPU_INT16U           dly_ms;
    CPU_INT16U           discover_msg_len;
    NET_SOCK_ADDR_IPv4   addr_server;
    NET_SOCK_ADDR_LEN    addr_server_size;
    CPU_BOOLEAN          rx_done;
    DHCPc_MSG_TYPE       dhcp_rx_msg_type;
    CPU_INT08U          *popt;
    CPU_INT08U           opt_val_len;
    NET_ERR              net_err;


#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)                   /* --------------- VALIDATE PTR & ARGS ---------------- */
    if (paddr_hw == (CPU_INT08U *)0) {
       *perr = DHCPc_ERR_NULL_PTR;
        return;
    }

    if (addr_hw_len != NET_IF_ETHER_ADDR_SIZE) {
       *perr = DHCPc_ERR_INVALID_HW_ADDR;
        return;
    }
#endif

                                                                /* --------------------- GET MSG ---------------------- */
    pmsg = DHCPc_MsgGet(perr);
    if (*perr != DHCPc_ERR_NONE) {
        return;
    }


    discover_retry_cnt = 0;
    discover_done      = DEF_NO;
    discover_dly       = DEF_NO;
    dly_ms             = 0;

                                                                /* While DISCOVER retry < max retry ...                 */
                                                                /* ... & DISCOVER NOT done,         ...                 */
    while ((discover_retry_cnt <  DHCPc_CFG_DISCOVER_RETRY_CNT) &&
           (discover_done      != DEF_YES)) {

        if (discover_dly == DEF_YES) {                          /* Dly DISCOVER, if req'd.                              */
            dly_ms = DHCPc_CalcBackOff(dly_ms);
                                                                /* Close Rx Q before delay to prevent Rx buffers ...    */
            (void)NetSock_CfgRxQ_Size( sock_id,                 /* ... from exhausting. (See Note #4).                  */
                                       NET_SOCK_DATA_SIZE_MIN,
                                      &net_err);
            KAL_Dly(dly_ms);

            (void)NetSock_CfgRxQ_Size( sock_id,                 /* Re-configure Rx Q size to its original value.        */
                                       NET_SOCK_CFG_RX_Q_SIZE_OCTET,
                                      &net_err);
        }

                                                                /* ------------------ GENERATE 'XID' ------------------ */
        pif_info->TransactionID++;                              /* Inc last transaction ID (see Note #1).               */


                                                                /* --------------- PREPARE DISCOVER MSG --------------- */
        discover_msg_len = DHCPc_TxMsgPrepare((DHCPc_IF_INFO *) pif_info,
                                              (DHCPc_MSG_TYPE ) DHCP_MSG_DISCOVER,
                                              (CPU_INT08U    *) paddr_hw,
                                              (CPU_INT08U     ) addr_hw_len,
                                              (CPU_INT08U    *)&pmsg->MsgBuf[0],
                                              (CPU_INT16U     ) DHCP_MSG_BUF_SIZE,
                                              (DHCPc_ERR     *) perr);
        if (*perr != DHCPc_ERR_NONE) {
            DHCPc_MsgFree(pmsg);
            return;
        }

                                                                /* ---------------------- TX MSG ---------------------- */
        addr_server_size = sizeof(addr_server);
        Mem_Clr((void     *)&addr_server,
                (CPU_SIZE_T) addr_server_size);
        addr_server.AddrFamily = NET_SOCK_ADDR_FAMILY_IP_V4;
        addr_server.Addr       = NET_UTIL_HOST_TO_NET_32(NET_IPv4_ADDR_BROADCAST);
        addr_server.Port       = NET_UTIL_HOST_TO_NET_16(DHCPc_CFG_IP_PORT_SERVER);

        DHCPc_Tx((NET_SOCK_ID      ) sock_id,
                 (void            *)&pmsg->MsgBuf[0],
                 (CPU_INT16U       ) discover_msg_len,
                 (NET_SOCK_ADDR   *)&addr_server,
                 (NET_SOCK_ADDR_LEN) addr_server_size,
                 (DHCPc_ERR       *) perr);
        if (*perr != DHCPc_ERR_NONE) {
            discover_done = DEF_YES;

        } else {

                                                                /* ------------- RX REPLY FROM SERVER(S) -------------- */
            rx_done = DEF_FALSE;


            while (rx_done != DEF_YES) {

                pmsg->MsgLen = DHCP_MSG_BUF_SIZE;
                dhcp_rx_msg_type = DHCPc_RxReply((NET_SOCK_ID    ) sock_id,
                                                 (DHCPc_IF_INFO *) pif_info,
                                                 (NET_IPv4_ADDR  ) NET_IPv4_ADDR_NONE,
                                                 (CPU_INT08U    *) paddr_hw,
                                                 (CPU_INT08U     ) addr_hw_len,
                                                 (CPU_INT08U    *)&pmsg->MsgBuf[0],
                                                 (CPU_INT16U    *)&pmsg->MsgLen,
                                                 (DHCPc_ERR     *) perr);

                switch (*perr) {
                    case DHCPc_ERR_NONE:                        /* If NO err                    ...                     */

                         switch (dhcp_rx_msg_type) {
                             case DHCP_MSG_OFFER:               /* ... & rx'd msg is OFFER,     ...                     */
                                  rx_done       = DEF_YES;      /* ... rx         done          ...                     */
                                  discover_done = DEF_YES;      /* ... & DISCOVER done.                                 */
                                  break;


                             default:                           /* ... Else rx'd msg NOT OFFER, ...                     */
                                  rx_done = DEF_NO;             /* ... rx NOT done.                                     */
                                  break;
                         }
                         break;


                    default:                                    /* If rx err,                   ...                     */
                         rx_done      = DEF_YES;
                         discover_dly = DEF_YES;
                         discover_retry_cnt++;                  /* ... restart DISCOVER.                                */
                         break;
                }
            }
        }
    }

    if (*perr != DHCPc_ERR_NONE) {
        DHCPc_MsgFree(pmsg);
        return;
    }

                                                                /* ----------- COPY OFFER IN IF INFO STRUCT ----------- */
    if (pif_info->MsgPtr != (DHCPc_MSG *)0) {                   /* If msg ptr NOT NULL, ...                             */
        DHCPc_MsgFree(pif_info->MsgPtr);                        /* ... free msg         ...                             */
    }

    pif_info->MsgPtr = pmsg;                                    /* ... & set msg ptr to rx'd OFFER.                     */

                                                                /* Get server id.                                       */
    popt = DHCPc_MsgGetOpt((DHCPc_OPT_CODE) DHCP_OPT_SERVER_IDENTIFIER,
                           (CPU_INT08U   *)&pmsg->MsgBuf[0],
                           (CPU_INT16U    ) pmsg->MsgLen,
                           (CPU_INT08U   *)&opt_val_len);
    if (popt != (CPU_INT08U *)0) {
        NET_UTIL_VAL_COPY_32(&pif_info->ServerID, popt);
    }
}


/*
*********************************************************************************************************
*                                             DHCPc_Req()
*
* Description : (1) Perform the REQUEST phase of the lease negotiation :
*
*                   (a) Get              message
*                   (b) Prepare  REQUEST message from last received OFFER
*                   (c) Transmit REQUEST message
*                   (d) Get reply from server(s)
*                   (e) Copy ACK.
*
*
* Argument(s) : sock_id         Socket ID of socket to transmit & receive DHCPc data.
*
*               pif_info        Pointer to DHCP interface information.
*               --------        Argument checked in DHCPc_InitStateHandler().
*
*               paddr_hw        Pointer to hardware address buffer.
*
*               addr_hw_len     Length of the hardware address buffer pointed to by 'paddr_hw'.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  REQUEST successfully transmitted & ACK received.
*                               DHCPc_ERR_NULL_PTR              Argument(s) passed a NULL pointer.
*                               DHCPc_ERR_INVALID_HW_ADDR       Argument 'paddr_hw' has an invalid length.
*                               DHCPc_ERR_RX_NAK                NAK message received from server.
*
*                                                               --------- RETURNED BY DHCPc_MsgGet() : ----------
*                               DHCPc_ERR_MSG_NONE_AVAIL        Message pool empty.
*
*                                                               ------ RETURNED BY DHCPc_TxMsgPrepare() : -------
*                               DHCPc_ERR_INVALID_MSG_SIZE      Argument 'pmsg_buf' size invalid.
*                               DHCPc_ERR_INVALID_MSG           Invalid DHCP message.
*
*                                                               ----------- RETURNED BY DHCPc_Tx() : ------------
*                               DHCPc_ERR_TX                    Transmit error.
*
*                                                               -------- RETURNED BY DHCPc_RxReply() : ----------
*                               DHCPc_ERR_INVALID_MSG_SIZE      Argument 'pmsg_buf' size invalid.
*                               DHCPc_ERR_RX_MSG_TYPE           Error extracting message type from reply message.
*                               DHCPc_ERR_RX_OVF                Receive error, data buffer overflow.
*                               DHCPc_ERR_RX                    Receive error.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_InitStateHandler(),
*               DHCPc_RenewRebindStateHandler()
*
* Note(s)     : (2) If NO DHCP message is received following a DHCP REQUEST transmission, the caller is
*                   responsible of the retransmission handling--i.e. this function will NOT attempt to
*                   send another REQUEST.
*********************************************************************************************************
*/

static  void  DHCPc_Req (NET_SOCK_ID     sock_id,
                         DHCPc_IF_INFO  *pif_info,
                         CPU_INT08U     *paddr_hw,
                         CPU_INT08U      addr_hw_len,
                         DHCPc_ERR      *perr)
{
    DHCPc_MSG           *pmsg;
    CPU_INT16U           request_retry_cnt;
    CPU_BOOLEAN          request_done;
    CPU_BOOLEAN          request_dly;
    CPU_INT16U           dly_ms;
    CPU_INT16U           request_msg_len;
    NET_IPv4_ADDR        addr_server_ip;
    NET_SOCK_ADDR_IPv4   addr_server;
    NET_SOCK_ADDR_LEN    addr_server_size;
    CPU_BOOLEAN          rx_done;
    DHCPc_MSG_TYPE       dhcp_rx_msg_type;
    NET_ERR              net_err;


#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)                   /* --------------- VALIDATE PTR & ARGS ---------------- */
    if (paddr_hw == (CPU_INT08U *)0) {
       *perr = DHCPc_ERR_NULL_PTR;
        return;
    }

    if (addr_hw_len != NET_IF_ETHER_ADDR_SIZE) {
       *perr = DHCPc_ERR_INVALID_HW_ADDR;
        return;
    }
#endif

                                                                /* --------------------- GET MSG ---------------------- */
    pmsg = DHCPc_MsgGet(perr);
    if (*perr != DHCPc_ERR_NONE) {
        return;
    }


    request_retry_cnt = 0;
    request_done      = DEF_NO;
    request_dly       = DEF_NO;
    dly_ms            = 0;

                                                                /* While REQUEST retry < max retry ...                  */
                                                                /* ... & REQUEST NOT done,         ...                  */
    while ((request_retry_cnt <  DHCPc_CFG_REQUEST_RETRY_CNT) &&
           (request_done      != DEF_YES)) {

        if (request_dly == DEF_YES) {                           /* Dly REQUEST, if req'd.                               */
            dly_ms = DHCPc_CalcBackOff(dly_ms);
                                                                /* Close Rx Q before delay to prevent Rx buffers ...    */
                                                                /* from exhaustion.                                     */
            (void)NetSock_CfgRxQ_Size( sock_id,
                                       NET_SOCK_DATA_SIZE_MIN,
                                      &net_err);
            KAL_Dly(dly_ms);

            (void)NetSock_CfgRxQ_Size( sock_id,                 /* Re-configure Rx Q size to its original value.        */
                                       NET_SOCK_CFG_RX_Q_SIZE_OCTET,
                                      &net_err);
        }

                                                                /* --------------- PREPARE REQUEST MSG ---------------- */
        request_msg_len = DHCPc_TxMsgPrepare((DHCPc_IF_INFO *) pif_info,
                                             (DHCPc_MSG_TYPE ) DHCP_MSG_REQUEST,
                                             (CPU_INT08U    *) paddr_hw,
                                             (CPU_INT08U     ) addr_hw_len,
                                             (CPU_INT08U    *)&pmsg->MsgBuf[0],
                                             (CPU_INT16U     ) DHCP_MSG_BUF_SIZE,
                                             (DHCPc_ERR     *) perr);
        if (*perr != DHCPc_ERR_NONE) {
            DHCPc_MsgFree(pmsg);
            return;
        }


                                                                /* ---------------------- TX MSG ---------------------- */
        if (pif_info->ClientState == DHCP_STATE_RENEWING) {     /* If client in RENEWING state, ...                     */
            addr_server_ip = pif_info->ServerID;                /* ... tx unicast   msg.                                */
        } else {                                                /* Else,                        ...                     */
            addr_server_ip = NET_IPv4_ADDR_BROADCAST;           /* ... tx broadcast msg.                                */
        }

        addr_server_size = sizeof(addr_server);
        Mem_Clr((void     *)&addr_server,
                (CPU_SIZE_T) addr_server_size);
        addr_server.AddrFamily = NET_SOCK_ADDR_FAMILY_IP_V4;
        addr_server.Addr       = addr_server_ip;
        addr_server.Port       = NET_UTIL_HOST_TO_NET_16(DHCPc_CFG_IP_PORT_SERVER);

        DHCPc_Tx((NET_SOCK_ID      ) sock_id,
                 (void            *)&pmsg->MsgBuf[0],
                 (CPU_INT16U       ) request_msg_len,
                 (NET_SOCK_ADDR   *)&addr_server,
                 (NET_SOCK_ADDR_LEN) addr_server_size,
                 (DHCPc_ERR       *) perr);
        if (*perr != DHCPc_ERR_NONE) {
            request_done = DEF_YES;

        } else {

                                                                /* ------------- RX REPLY FROM SERVER(S) -------------- */
            rx_done = DEF_FALSE;


            while (rx_done != DEF_YES) {

                pmsg->MsgLen = DHCP_MSG_BUF_SIZE;
                dhcp_rx_msg_type = DHCPc_RxReply((NET_SOCK_ID    ) sock_id,
                                                 (DHCPc_IF_INFO *) pif_info,
                                                 (NET_IPv4_ADDR  ) NET_IPv4_ADDR_NONE,
                                                 (CPU_INT08U    *) paddr_hw,
                                                 (CPU_INT08U     ) addr_hw_len,
                                                 (CPU_INT08U    *)&pmsg->MsgBuf[0],
                                                 (CPU_INT16U    *)&pmsg->MsgLen,
                                                 (DHCPc_ERR     *) perr);

                switch (*perr) {
                    case DHCPc_ERR_NONE:                        /* If NO err                          ...               */

                         switch (dhcp_rx_msg_type) {
                             case DHCP_MSG_ACK:                 /* ... & rx'd msg is ACK,             ...               */
                                  rx_done      = DEF_YES;       /* ... rx        done                 ...               */
                                  request_done = DEF_YES;       /* ... & REQUEST done.                                  */
                                  break;


                             case DHCP_MSG_NAK:                 /* ... else if rx'd msg is NAK,       ...               */
                                  rx_done      = DEF_YES;       /* ... rx        done,                ...               */
                                  request_done = DEF_YES;       /* ... REQUEST done,                  ...               */
                                 *perr = DHCPc_ERR_RX_NAK;      /* ... & rtn err.                                       */
                                  break;


                             default:                           /* ... Else rx'd msg NOT ACK nor NAK, ...               */
                                  rx_done = DEF_NO;             /* ... rx NOT done.                                     */
                                  break;
                         }
                         break;


                    default:                                    /* If rx err,         ...                               */
                         rx_done     = DEF_YES;
                         request_dly = DEF_YES;
                         request_retry_cnt++;                   /* ... restart REQUEST.                                 */
                         break;
                }
            }
        }
    }

    if (*perr != DHCPc_ERR_NONE) {
        DHCPc_MsgFree(pmsg);
        return;
    }

                                                                /* ------------ COPY ACK IN IF INFO STRUCT ------------ */
    if (pif_info->MsgPtr != (DHCPc_MSG *)0) {                   /* If msg ptr NOT NULL, ...                             */
        DHCPc_MsgFree(pif_info->MsgPtr);                        /* ... free msg         ...                             */
    }

    pif_info->MsgPtr = pmsg;                                    /* ... & set msg ptr to rx'd ACK.                       */
}


/*
*********************************************************************************************************
*                                       DHCPc_DeclineRelease()
*
* Description : (1) Perform the DECLINE or RELEASE phase of the lease negotiation :
*
*                   (a) Get                      message
*                   (b) Prepare  DECLINE/RELEASE message
*                   (c) Transmit DECLINE/RELEASE message
*
*
* Argument(s) : sock_id         Socket ID of socket to transmit & receive DHCPc data.
*
*               pif_info        Pointer to DHCP interface information.
*               --------        Argument validated in DHCPc_InitStateHandler(),
*                                                     DHCPc_StopStateHandler().
*
*               msg_type        DHCP message type to prepare :
*
*                                   DHCP_MSG_DECLINE        Decline message
*                                   DHCP_MSG_RELEASE        Release message
*
*               paddr_hw        Pointer to hardware address buffer.
*
*               addr_hw_len     Length of the hardware address buffer pointed to by 'paddr_hw'.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  DECLINE/RELEASE successfully transmitted
*                               DHCPc_ERR_NULL_PTR              Argument(s) passed a NULL pointer.
*                               DHCPc_ERR_INVALID_HW_ADDR       Argument 'paddr_hw' has an invalid length.
*                               DHCPc_ERR_INVALID_MSG           Invalid DHCP message.
*
*                                                               ------ RETURNED BY DHCPc_MsgGet() : ------
*                               DHCPc_ERR_MSG_NONE_AVAIL        Message pool empty.
*
*                                                               --- RETURNED BY DHCPc_TxMsgPrepare() : ---
*                               DHCPc_ERR_INVALID_MSG_SIZE      Argument 'pmsg_buf' size invalid.
*                               DHCPc_ERR_INVALID_MSG           Invalid DHCP message.
*
*                                                               -------- RETURNED BY DHCPc_Tx() : --------
*                               DHCPc_ERR_TX                    Transmit error.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_InitStateHandler(),
*               DHCPc_StopStateHandler().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  void  DHCPc_DeclineRelease (NET_SOCK_ID      sock_id,
                                    DHCPc_IF_INFO   *pif_info,
                                    DHCPc_MSG_TYPE   msg_type,
                                    CPU_INT08U      *paddr_hw,
                                    CPU_INT08U       addr_hw_len,
                                    DHCPc_ERR       *perr)
{
    DHCPc_MSG           *pmsg;
    CPU_INT16U           release_msg_len;
    NET_IPv4_ADDR        addr_ip_server;
    NET_SOCK_ADDR_IPv4   addr_server;
    NET_SOCK_ADDR_LEN    addr_server_size;


#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)                   /* --------------- VALIDATE PTR & ARGS ---------------- */
    if (paddr_hw == (CPU_INT08U *)0) {
       *perr = DHCPc_ERR_NULL_PTR;
        return;
    }

    if (addr_hw_len != NET_IF_ETHER_ADDR_SIZE) {
       *perr = DHCPc_ERR_INVALID_HW_ADDR;
        return;
    }
#endif

    switch (msg_type) {
        case DHCP_MSG_DECLINE:
             addr_ip_server = NET_IPv4_ADDR_BROADCAST;
             break;


        case DHCP_MSG_RELEASE:
             addr_ip_server = pif_info->ServerID;
             break;


        default:
            *perr = DHCPc_ERR_INVALID_MSG;
             return;
    }

                                                                /* --------------------- GET MSG ---------------------- */
    pmsg = DHCPc_MsgGet(perr);
    if (*perr != DHCPc_ERR_NONE) {
        return;
    }

                                                                /* --------------- PREPARE RELEASE MSG ---------------- */
    release_msg_len = DHCPc_TxMsgPrepare((DHCPc_IF_INFO *) pif_info,
                                         (DHCPc_MSG_TYPE ) msg_type,
                                         (CPU_INT08U    *) paddr_hw,
                                         (CPU_INT08U     ) addr_hw_len,
                                         (CPU_INT08U    *)&pmsg->MsgBuf[0],
                                         (CPU_INT16U     ) DHCP_MSG_BUF_SIZE,
                                         (DHCPc_ERR     *) perr);
    if (*perr != DHCPc_ERR_NONE) {
        DHCPc_MsgFree(pmsg);
        return;
    }


                                                                /* ---------------------- TX MSG ---------------------- */
    addr_server_size = sizeof(addr_server);
    Mem_Clr((void     *)&addr_server,
            (CPU_SIZE_T) addr_server_size);
    addr_server.AddrFamily = NET_SOCK_ADDR_FAMILY_IP_V4;
    addr_server.Addr       = addr_ip_server;
    addr_server.Port       = NET_UTIL_HOST_TO_NET_16(DHCPc_CFG_IP_PORT_SERVER);

    DHCPc_Tx((NET_SOCK_ID      ) sock_id,
             (void            *)&pmsg->MsgBuf[0],
             (CPU_INT16U       ) release_msg_len,
             (NET_SOCK_ADDR   *)&addr_server,
             (NET_SOCK_ADDR_LEN) addr_server_size,
             (DHCPc_ERR       *) perr);


    DHCPc_MsgFree(pmsg);
}


/*
*********************************************************************************************************
*                                         DHCPc_CalcBackOff()
*
* Description : Calculate next backed-off retransmit/retry timeout value.
*
* Argument(s) : timeout_ms      Current timeout value (in milliseconds).
*
* Return(s)   : Backed-off re-transmit/retry timeout value (in milliseconds).
*
* Caller(s)   : DHCPc_Discover(),
*               DHCPc_Req().
*
* Note(s)     : (1) RFC #2131, Section 4.1 'Constructing and sending DHCP messages' states that "the
*                   client MUST adopt a retransmission strategy that incorporates a randomized
*                   exponential backoff algorithm to determine the delay between retransmissions".
*
*                   It also stipulates that "the retransmission delay SHOULD be double with subsequent
*                   retransmissions up to a maximum of 64 seconds".
*
*                   This implementation takes some distance from the RFC by setting the initial delay
*                   value to 2 seconds instead of the proposed 4 seconds.  It also does NOT randomize
*                   the delay value.
*********************************************************************************************************
*/

static  CPU_INT16U  DHCPc_CalcBackOff (CPU_INT16U  timeout_ms)
{
    CPU_INT32U  timeout_calcd;


    if (timeout_ms == 0) {
        timeout_calcd = (CPU_INT32U)DHCPc_BACKOFF_DLY_INITIAL_MS;

    } else {
        timeout_calcd = (timeout_ms < (CPU_INT32U)DHCPc_BACKOFF_DLY_MAX_MS)
                      ? (timeout_ms * (CPU_INT32U)DHCPc_BACKOFF_DLY_SCALAR)
                      : (CPU_INT32U)DHCPc_BACKOFF_DLY_MAX_MS;

        timeout_calcd = DEF_MIN((CPU_INT32U)timeout_calcd,
                                (CPU_INT32U)DHCPc_BACKOFF_DLY_MAX_MS);
    }

    return ((CPU_INT16U)timeout_calcd);
}


/*
*********************************************************************************************************
*                                        DHCPc_AddrValidate()
*
* Description : (1) Validate IP address not already used on the network.
*
*                   (a) Probe addr on network
*                   (b) Wait for reply
*                   (c) Get HW addr from ARP cache
*
*
* Argument(s) : if_nbr          IF on which to perform address validation.
*
*               addr_target     IP address to validate, in network-order.
*
*               dly_ms          Delay to wait for a reply, in milliseconds.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  No error, address NOT used on the network.
*                               DHCPc_ERR_ADDR_VALIDATE         Error validating address.
*                               DHCPc_ERR_ADDR_USED             Address already used on the network.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_InitStateHandler().
*
* Note(s)     : (2) If ARP is not present (NET_ARP_MODULE_EN not defined), or if any other error
*                   happens when attempting to check the address, DHCPc_ERR_ADDR_VALIDATE is returned,
*                   meaning that the check does not allow to conclude anything.
*********************************************************************************************************
*/

#if ((DHCPc_CFG_ADDR_VALIDATE_EN       == DEF_ENABLED) || \
     (DHCPc_CFG_DYN_LOCAL_LINK_ADDR_EN == DEF_ENABLED))
static  void  DHCPc_AddrValidate (NET_IF_NBR      if_nbr,
                                  NET_IPv4_ADDR   addr_target,
                                  CPU_INT32U      dly_ms,
                                  DHCPc_ERR      *perr)
{
#ifdef  NET_ARP_MODULE_EN
    CPU_INT08U        hw_addr_sender[NET_CACHE_HW_ADDR_LEN_ETHER];
    NET_IPv4_ADDR     addr_this_host;
    NET_ARP_ADDR_LEN  addr_len;
#endif
    NET_ERR           err_net;


   (void)&err_net;                                              /* Prevent possible 'variable unused' warning.          */

   *perr = DHCPc_ERR_ADDR_VALIDATE;                             /* Dflt rtn val.                                        */

#ifdef  NET_ARP_MODULE_EN
    addr_this_host = NET_IPv4_ADDR_NONE;
    addr_len       = sizeof(addr_target);

                                                                /* ---------------- PROBE ADDR ON NET ----------------- */
    NetARP_CacheProbeAddrOnNet((NET_PROTOCOL_TYPE) NET_PROTOCOL_TYPE_IP_V4,
                               (CPU_INT08U      *)&addr_this_host,
                               (CPU_INT08U      *)&addr_target,
                               (NET_ARP_ADDR_LEN ) addr_len,
                               (NET_ERR         *)&err_net);

    if (err_net != NET_ARP_ERR_NONE) {
        return;                                                 /* See Note #2.                                         */
    }
                                                                /* ------------------ WAIT FOR REPLY ------------------ */
    KAL_Dly(dly_ms);

                                                                /* ------------ GET HW ADDR FROM ARP CACHE ------------ */
    NetARP_CacheGetAddrHW(                   if_nbr,
                          (CPU_INT08U     *)&hw_addr_sender[0],
                          (NET_ARP_ADDR_LEN) NET_CACHE_HW_ADDR_LEN_ETHER,
                          (CPU_INT08U     *)&addr_target,
                          (NET_ARP_ADDR_LEN) addr_len,
                          (NET_ERR        *)&err_net);

    switch (err_net) {
        case NET_ARP_ERR_CACHE_NOT_FOUND:                       /* If cache NOT found or cache pending, ...             */
        case NET_ARP_ERR_CACHE_PEND:
        case NET_CACHE_ERR_PEND:
            *perr = DHCPc_ERR_NONE;                             /* ... addr NOT used.                                   */
             break;


        case NET_ARP_ERR_NONE:                                  /* If NO err, ...                                       */
            *perr = DHCPc_ERR_ADDR_USED;                        /* ... hw addr in cache resolved (addr used).          */
             break;


        default:
            *perr = DHCPc_ERR_ADDR_VALIDATE;
             break;
    }


#endif
}
#endif


/*
*********************************************************************************************************
*                                           DHCPc_AddrCfg()
*
* Description : Configure the interface's network parameters with the last accepted OFFER.
*
* Argument(s) : pif_info        Pointer to DHCP interface information.
*               --------        Argument validated in DHCPc_InitStateHandler().
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  Interface's network successfully configured.
*                               DHCPc_ERR_IF_CFG                Error configuring the interface's network.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_InitStateHandler().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  void  DHCPc_AddrCfg (DHCPc_IF_INFO  *pif_info,
                             DHCPc_ERR      *perr)
{
    DHCPc_MSG      *pmsg;
    DHCP_MSG_HDR   *pmsg_hdr;
    CPU_INT08U     *popt;
    CPU_INT08U      opt_val_len;
    CPU_BOOLEAN     cfgd;
    NET_IF_NBR      if_nbr;
    NET_IPv4_ADDR   addr_host;
    NET_IPv4_ADDR   addr_subnet_mask;
    NET_IPv4_ADDR   addr_dflt_gateway;
    NET_ERR         err_net;


    pmsg     = (DHCPc_MSG    *) pif_info->MsgPtr;
    pmsg_hdr = (DHCP_MSG_HDR *)&pmsg->MsgBuf[0];

                                                                /* -------------------- GET PARAM --------------------- */
    addr_subnet_mask  = NET_IPv4_ADDR_NONE;
    addr_dflt_gateway = NET_IPv4_ADDR_NONE;

                                                                /* Get assign'd addr.                                   */
    NET_UTIL_VAL_COPY_GET_NET_32(&addr_host, &pmsg_hdr->yiaddr);

                                                                /* Get assign'd subnet mask.                            */
    popt = DHCPc_MsgGetOpt((DHCPc_OPT_CODE) DHCP_OPT_SUBNET_MASK,
                           (CPU_INT08U   *)&pmsg->MsgBuf[0],
                           (CPU_INT16U    ) pmsg->MsgLen,
                           (CPU_INT08U   *)&opt_val_len);
    if (popt != (CPU_INT08U *)0) {
        NET_UTIL_VAL_COPY_GET_NET_32(&addr_subnet_mask, popt);
    }

    popt = DHCPc_MsgGetOpt((DHCPc_OPT_CODE) DHCP_OPT_ROUTER,    /* Get assign'd dflt gateway.                           */
                           (CPU_INT08U   *)&pmsg->MsgBuf[0],
                           (CPU_INT16U    ) pmsg->MsgLen,
                           (CPU_INT08U   *)&opt_val_len);
    if (popt != (CPU_INT08U *)0) {
        NET_UTIL_VAL_COPY_GET_NET_32(&addr_dflt_gateway, popt);
    }

                                                                /* ------------------- CFG IF ADDR -------------------- */
    if_nbr = pif_info->IF_Nbr;
    cfgd   = NetIPv4_CfgAddrAddDynamic(if_nbr, addr_host, addr_subnet_mask, addr_dflt_gateway, &err_net);
    if (cfgd != DEF_OK) {                                       /* If cfg invalid, ...                                  */
       *perr = DHCPc_ERR_IF_CFG;                                /* ... rtn err.                                         */
        return;
    }

   *perr = DHCPc_ERR_NONE;
}


/*
*********************************************************************************************************
*                                      DHCPc_AddrLocalLinkCfg()
*
* Description : (1) Perform action associated with dynamic link-local address configuration
*
*                   (a) Get random address
*                   (b) Test address
*                   (c) Interpret test result
*                   (d) Configure interface and announce IP address
*
*
* Argument(s) : pif_info        Pointer to DHCP interface information.
*               --------        Argument checked in DHCPc_InitStateHandler().
*
*               paddr_hw        Pointer to hardware address buffer.
*
*               addr_hw_len     Length of the hardware address buffer pointed to by 'paddr_hw'.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  Interface configured using a link-local address.
*                               DHCPc_ERR_IF_CFG                Error configuring the interface's network.
*
*                                                               ------ RETURNED BY DHCPc_AddrValidate() : ------
*                               DHCPc_ERR_ADDR_VALIDATE         Error validating address.
*                               DHCPc_ERR_ADDR_USED             Address already used on the network.
*
* Return(s)   : none.
*
* Caller(s)   : none.
*
* Note(s)     : (2) From RFC #3027, section 'Probe details' :
*
*                   (a) "If the number of conflicts [experienced in the process of trying to acquire an
*                       address] exceeds MAX_CONFLICTS, then the host MUST limit the rate at which it
*                       probes for new addresses to no more than one new address per RATE_LIMIT_INTERVAL".
*
*                   (b) "When ready to begin probing, the host should then wait for a random time interval
*                       selected uniformly in the range zero to PROBE_WAIT seconds, and should then send
*                       PROBE_NUM probe packets, each of these probe packets spaced randomly, PROBE_MIN to
*                       PROBE_MAX seconds apart".
*
*                       This implementation takes some distance from the RFC by waiting PROBE_WAIT
*                       seconds before sending the first probe packet.  As for the retransmission of ARP
*                       packets, this if left to the ARP layer.
*
*               (3) From RFC #3027, section 'Announcing an Address', "[...] the host MUST announce its
*                   claimed address by broadcasting ANNOUNCE_NUM ARP announcements, spaced
*                   ANNOUNCE_INTERVAL seconds apart".
*
*                   This is being done to make sure hosts on the network do NOT have ARP cache entries
*                   from other host that had been previously using the same address.
*********************************************************************************************************
*/

#if (DHCPc_CFG_DYN_LOCAL_LINK_ADDR_EN == DEF_ENABLED)
static  void  DHCPc_AddrLocalLinkCfg (DHCPc_IF_INFO  *pif_info,
                                      CPU_INT08U     *paddr_hw,
                                      CPU_INT08U      addr_hw_len,
                                      DHCPc_ERR      *perr)
{
    NET_IPv4_ADDR  addr_host;
    NET_IPv4_ADDR  addr_net;
    CPU_INT08U     addr_len;
    CPU_BOOLEAN    addr_srch_done;
    CPU_INT08U     nbr_conflicts;
    CPU_BOOLEAN    cfgd;
    CPU_INT08U     announce_nbr;
    CPU_BOOLEAN    announce_done;
    NET_ERR        err_net;


                                                                /* ------------------ GET RANDOM ADDR ----------------- */
    nbr_conflicts  = 0;
    addr_srch_done = DEF_NO;

    while ((addr_srch_done != DEF_YES) &&
           (nbr_conflicts  <=  DHCPc_CFG_LOCAL_LINK_MAX_RETRY)) {

        if (nbr_conflicts > DHCP_LOCAL_LINK_MAX_CONFLICTS) {
                                                                /* See Note #2a.                                        */
            KAL_Dly((CPU_INT32U)(DHCP_LOCAL_LINK_RATE_LIMIT_INTERVAL_S * DEF_TIME_NBR_mS_PER_SEC));
        }

        addr_host =  DHCPc_AddrLocalLinkGet(paddr_hw, addr_hw_len);
        addr_net  = (NET_IPv4_ADDR)NET_UTIL_HOST_TO_NET_32(addr_host);


                                                                /* -------------- VALIDATE ADDR NOT USED -------------- */
                                                                /* Dly (see Note #2b).                                  */
        KAL_Dly((CPU_INT32U)(DHCP_LOCAL_LINK_PROBE_WAIT_S * DEF_TIME_NBR_mS_PER_SEC));

                                                                /* Probe addr.                                          */
        DHCPc_AddrValidate(                pif_info->IF_Nbr,
                           (NET_IPv4_ADDR) addr_net,
                           (CPU_INT32U   )(DHCP_LOCAL_LINK_ANNOUNCE_WAIT_S * DEF_TIME_NBR_mS_PER_SEC),
                           (DHCPc_ERR   *) perr);
        switch(*perr) {
            case DHCPc_ERR_NONE:                                /* If addr not used, ...                                */
                 addr_srch_done = DEF_YES;                      /* ... addr validated.                                  */
                 break;


            case DHCPc_ERR_ADDR_USED:                           /* Else if addr used, ...                               */
                 nbr_conflicts++;                               /* ... restart process.                                 */
                 break;


            case DHCPc_ERR_ADDR_VALIDATE:                       /* Else if any other error, ...                         */
            default:
                 addr_srch_done = DEF_YES;                      /* ... stop link-local cfg.                             */
        }
    }

    if (*perr != DHCPc_ERR_NONE) {
        return;
    }

                                                                /* -------------- CFG IF & ANNOUNCE ADDR -------------- */
    cfgd = NetIPv4_CfgAddrAddDynamic((NET_IF_NBR   ) pif_info->IF_Nbr,
                                     (NET_IPv4_ADDR) addr_host,
                                     (NET_IPv4_ADDR) NET_IPv4_ADDR_LOCAL_LINK_MASK,
                                     (NET_IPv4_ADDR) NET_IPv4_ADDR_NONE,
                                     (NET_ERR     *)&err_net);
    if (cfgd != DEF_OK) {
       *perr = DHCPc_ERR_IF_CFG;
        return;
    }

    addr_len      = sizeof(NET_IPv4_ADDR);
    announce_nbr  = 0;
    announce_done = DEF_NO;


    while ((announce_nbr  < DHCP_LOCAL_LINK_ANNOUNCE_NUM) &&    /* See Note #3.                                         */
           (announce_done != DEF_YES)) {

        NetARP_TxReqGratuitous((NET_PROTOCOL_TYPE) NET_PROTOCOL_TYPE_IP_V4,
                               (CPU_INT08U      *)&addr_net,
                               (CPU_INT08U       ) addr_len,
                               (NET_ERR         *)&err_net);

        if (err_net == NET_ARP_ERR_NONE) {
            KAL_Dly((CPU_INT32U)(DHCP_LOCAL_LINK_ANNOUNCE_INTERVAL_S * DEF_TIME_NBR_mS_PER_SEC));

        } else {
            announce_done = DEF_YES;
        }

        announce_nbr++;
    }


   *perr = DHCPc_ERR_NONE;
}
#endif


/*
*********************************************************************************************************
*                                      DHCPc_AddrLocalLinkGet()
*
* Description : (1) Generate a pseudo-random IPv4 address in the Link-Local reserved range :
*
*                   (a) Generate a seed from the hardware address
*                   (b) Generate a seed from the current time
*                   (c) Get pseudo-random number from seeds
*                   (d) Generate address
*
*
* Argument(s) : paddr_hw        Pointer to hardware address buffer.
*               --------        Argument validated in DHCPc_AddrLocalLinkCfg().
*
*               addr_hw_len     Length of the hardware address buffer pointed to by 'paddr_hw'.
*
* Return(s)   : IPv4 Link-Local address.
*
* Caller(s)   : DHCPc_AddrLocalLinkCfg().
*
* Note(s)     : (2) The seeds are generated by creating 32-bit values from :
*
*                   (a) The two least significant bytes of the hardware address, shifted by 16 bits &
*                   (b) The     least significant byte  of the current time.
*
*               (3) The random address returned from this function is obtained by adding an "offset"
*                   generated from the random number to the link-local base address (defined by
*                   NET_IP_ADDR_LOCAL_LINK_HOST_MIN).
*********************************************************************************************************
*/

#if (DHCPc_CFG_DYN_LOCAL_LINK_ADDR_EN == DEF_ENABLED)
static  NET_IPv4_ADDR  DHCPc_AddrLocalLinkGet (CPU_INT08U  *paddr_hw,
                                               CPU_INT08U   addr_hw_len)
{
    CPU_INT32U     time_cur;
    CPU_INT32U     seed_hw_addr;
    CPU_INT32U     seed_time;
    CPU_INT32U     random;
    NET_IPv4_ADDR  addr;


                                                                /* ------------------ GENERATE SEEDS ------------------ */
    seed_hw_addr = 0;                                           /* See Note #2a.                                        */
    seed_hw_addr = (((CPU_INT32U)*(paddr_hw + addr_hw_len - 2) << 24) |
                    ((CPU_INT32U)*(paddr_hw + addr_hw_len - 1) << 16));


    time_cur  =  DHCPc_OS_TimeGet_tick();                       /* See Note #2b.                                        */
    seed_time = (time_cur & 0x0000FFFF);

                                                                /* ---------------- GET PSEUDO-RAND NBR --------------- */
    random = (seed_time | seed_hw_addr);                        /* OR the two seeds.                                    */

                                                                /* ------------------- GENERATE ADDR ------------------ */
                                                                /* See Note #3.                                         */
    addr =            NET_IPv4_ADDR_LOCAL_LINK_HOST_MIN +
           (random % (NET_IPv4_ADDR_LOCAL_LINK_HOST_MAX - NET_IPv4_ADDR_LOCAL_LINK_HOST_MIN + 1));

    return (addr);
}
#endif


/*
*********************************************************************************************************
*                                        DHCPc_LeaseTimeCalc()
*
* Description : (1) Calculate the lease time & renewing/rebinding times for last accepted lease :
*
*                   (a) Get           lease time from ACK message
*                   (b) Get/calculate times T1 & T2
*                   (c) Update        times with negotiation duration & set minimum
*                   (d) Configure     timer
*
*
* Argument(s) : pif_info        Pointer to DHCP interface information.
*               --------        Argument validated in DHCPc_InitStateHandler(),
*                                                     DHCPc_RenewRebindStateHandler().
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  Lease time successfully calculated.
*                               DHCPc_ERR_TMR_CFG               Configuration timer error.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_InitStateHandler(),
*               DHCPc_RenewRebindStateHandler().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  void  DHCPc_LeaseTimeCalc (DHCPc_IF_INFO  *pif_info,
                                   DHCPc_ERR      *perr)
{
    DHCPc_MSG   *pmsg;
    CPU_INT08U  *popt;
    CPU_INT08U   opt_val_len;
    CPU_INT32U   time_lease;
    CPU_INT32U   time_t1;
    CPU_INT32U   time_t2;
    CPU_INT32U   time_nego_stop;
    CPU_INT32U   time_nego_sec;


    time_lease = DHCP_LEASE_INFINITE;

                                                                /* ------------- GET LEASE TIME FROM ACK -------------- */
    pmsg = pif_info->MsgPtr;
                                                                /* Get lease time.                                      */
    popt = DHCPc_MsgGetOpt((DHCPc_OPT_CODE) DHCP_OPT_IP_ADDRESS_LEASE_TIME,
                           (CPU_INT08U   *)&pmsg->MsgBuf[0],
                           (CPU_INT16U    ) pmsg->MsgLen,
                           (CPU_INT08U   *)&opt_val_len);
    if (popt != (CPU_INT08U *)0) {
        NET_UTIL_VAL_COPY_GET_NET_32(&time_lease, popt);
    }

    if (time_lease == DHCP_LEASE_INFINITE) {                    /* If lease time infinite, ...                          */
        pif_info->LeaseTime_sec = DHCP_LEASE_INFINITE;
        pif_info->T1_Time_sec   = DHCP_LEASE_INFINITE;
        pif_info->T2_Time_sec   = DHCP_LEASE_INFINITE;

       *perr = DHCPc_ERR_NONE;                                  /* ... NO tmr to set.                                   */
        return;
    }

                                                                /* Get renewal time.                                    */
    popt = DHCPc_MsgGetOpt((DHCPc_OPT_CODE) DHCP_OPT_RENEWAL_TIME_VALUE,
                           (CPU_INT08U   *)&pmsg->MsgBuf[0],
                           (CPU_INT16U    ) pmsg->MsgLen,
                           (CPU_INT08U   *)&opt_val_len);
    if (popt != (CPU_INT08U *)0) {
        NET_UTIL_VAL_COPY_GET_NET_32(&time_t1, popt);

    } else {
        time_t1 = (CPU_INT32U)(time_lease * DHCP_T1_LEASE_FRACTION);
    }

                                                                /* Get rebinding time.                                  */
    popt = DHCPc_MsgGetOpt((DHCPc_OPT_CODE) DHCP_OPT_REBINDING_TIME_VALUE,
                           (CPU_INT08U   *)&pmsg->MsgBuf[0],
                           (CPU_INT16U    ) pmsg->MsgLen,
                           (CPU_INT08U   *)&opt_val_len);
    if (popt != (CPU_INT08U *)0) {
        NET_UTIL_VAL_COPY_GET_NET_32(&time_t2, popt);

    } else {
        time_t2 = (CPU_INT32U)(time_lease * DHCP_T2_LEASE_FRACTION);
    }


                                                                /* ----------------- CALC LEASE TIME ------------------ */
    time_nego_stop = DHCPc_OS_TimeGet_tick();
    time_nego_sec  = DHCPc_OS_TimeCalcElapsed_sec(pif_info->NegoStartTime,
                                                  time_nego_stop);

    if (time_t1 >  time_nego_sec) {                             /* Subst nego time.                                     */
        time_t1 -= time_nego_sec;
    }

    if (time_t2 >  time_nego_sec) {
        time_t2 -= time_nego_sec;
    }

    if (time_lease >  time_nego_sec) {
        time_lease -= time_nego_sec;
    }

    pif_info->T1_Time_sec   = time_t1;
    pif_info->T2_Time_sec   = time_t2;
    pif_info->LeaseTime_sec = time_lease;


                                                                /* --------------------- CFG TMR ---------------------- */
    DHCPc_TmrCfg((DHCPc_IF_INFO *)pif_info,
                 (DHCPc_COMM_MSG )DHCPc_COMM_MSG_T1_EXPIRED,
                 (CPU_INT32U     )pif_info->T1_Time_sec,
                 (DHCPc_ERR     *)perr);

    if (*perr != DHCPc_ERR_NONE) {
        *perr  = DHCPc_ERR_TMR_CFG;
    }
}


/*
*********************************************************************************************************
*                                       DHCPc_LeaseTimeUpdate()
*
* Description : (1) Update the lease time & renewing/rebinding times following lease extension failure :
*
*                   (a) Update lease times
*                   (b) Determine timer value & message
*                   (c) Configure timer
*
*
* Argument(s) : pif_info        Pointer to DHCP interface information.
*               --------        Argument validated in DHCPc_RenewRebindStateHandler().
*
*               exp_tmr_msg     Message for expired timer.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  Lease time successfully updated & timer set.
*                               DHCPc_ERR_TMR_INVALID_MSG       Invalid expiration timer message.
*                               DHCPc_ERR_TMR_CFG               Configuration timer error.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_RenewRebindStateHandler().
*
* Note(s)     : (2) From RFC #2131, section 'Reacquisition and expiration', "In both RENEWING and
*                   REBINDING states, if the client receives no response to its DHCPREQUEST message, the
*                   client SHOULD wait one-half of the remaining time until T2 (in RENEWING state) and
*                   one-half of the remaining lease time (in REBINDING state), down to a minimum of 60
*                   seconds, before retransmitting the DHCPREQUEST message".
*
*                   However, in order to prevent a miss on one of the followed times (T1, T2, or the
*                   lease itself), this implementation waits down to a remaining time of 5 minutes until
*                   T2 (RENEWING) or the lease time (REBINDING).
*********************************************************************************************************
*/

static  void  DHCPc_LeaseTimeUpdate (DHCPc_IF_INFO   *pif_info,
                                     DHCPc_COMM_MSG   exp_tmr_msg,
                                     DHCPc_ERR       *perr)
{
    CPU_INT32U      time_nego_stop;
    CPU_INT32U      time_nego_sec;
    CPU_INT32U      tmr_val_sec;
    DHCPc_COMM_MSG  tmr_msg;


    time_nego_stop = DHCPc_OS_TimeGet_tick();
    time_nego_sec  = DHCPc_OS_TimeCalcElapsed_sec(pif_info->NegoStartTime, time_nego_stop);

                                                                /* ---------------- UPDATE LEASE TIMES ---------------- */
                                                                /* Dec expired tmr time from other times.               */
    switch (exp_tmr_msg) {
        case DHCPc_COMM_MSG_T1_EXPIRED:
             if (pif_info->LeaseTime_sec >  pif_info->T1_Time_sec) {
                 pif_info->LeaseTime_sec -= pif_info->T1_Time_sec;
             } else {
                 pif_info->LeaseTime_sec  = 0;
             }

             if (pif_info->T2_Time_sec >  pif_info->T1_Time_sec) {
                 pif_info->T2_Time_sec -= pif_info->T1_Time_sec;
             } else {
                 pif_info->T2_Time_sec  = 0;
             }

             pif_info->T1_Time_sec = 0;
             break;


        case DHCPc_COMM_MSG_T2_EXPIRED:
             if (pif_info->LeaseTime_sec >  pif_info->T2_Time_sec) {
                 pif_info->LeaseTime_sec -= pif_info->T2_Time_sec;
             } else {
                 pif_info->LeaseTime_sec  = 0;
             }

             pif_info->T2_Time_sec = 0;
             pif_info->T1_Time_sec = 0;
             break;


        default:
            *perr = DHCPc_ERR_TMR_INVALID_MSG;
             return;
    }

                                                                /* Dec time elapsed since tmr expired.                  */
    if (pif_info->LeaseTime_sec >  time_nego_sec) {
        pif_info->LeaseTime_sec -= time_nego_sec;
    }

    if (pif_info->T2_Time_sec >  time_nego_sec) {
        pif_info->T2_Time_sec -= time_nego_sec;
    }


                                                                /* ------------- DETERMINE TMR VAL & MSG -------------- */
    switch (exp_tmr_msg) {                                      /* See Note #2.                                         */
        case DHCPc_COMM_MSG_T1_EXPIRED:
             if (pif_info->T2_Time_sec > (2 * DHCP_MIN_RETX_TIME_S)) {
                 tmr_val_sec           = (pif_info->T2_Time_sec / 2);
                 pif_info->T1_Time_sec =  tmr_val_sec;

                 tmr_msg     = DHCPc_COMM_MSG_T1_EXPIRED;

             } else {
                 tmr_val_sec = pif_info->T2_Time_sec;
                 tmr_msg     = DHCPc_COMM_MSG_T2_EXPIRED;
             }
             break;


        case DHCPc_COMM_MSG_T2_EXPIRED:
             if (pif_info->LeaseTime_sec > (2 * DHCP_MIN_RETX_TIME_S)) {
                 tmr_val_sec           = (pif_info->LeaseTime_sec / 2);
                 pif_info->T2_Time_sec =  tmr_val_sec;

                 tmr_msg     = DHCPc_COMM_MSG_T2_EXPIRED;

             } else {
                 tmr_val_sec = pif_info->LeaseTime_sec;
                 tmr_msg     = DHCPc_COMM_MSG_LEASE_EXPIRED;
             }
             break;


        default:
            *perr = DHCPc_ERR_TMR_INVALID_MSG;
             return;
    }

                                                                /* --------------------- CFG TMR ---------------------- */
    DHCPc_TmrCfg(pif_info, tmr_msg, tmr_val_sec, perr);
    if (*perr != DHCPc_ERR_NONE) {
        *perr  = DHCPc_ERR_TMR_CFG;
    }
}


/*
*********************************************************************************************************
*                                           DHCPc_RxReply()
*
* Description : (1) Receive DHCP reply message :
*
*                   (a) Receive           message data from server
*
*                   (b) Validate received message
*
*                       (1) opcode
*                       (2) hardware address
*                       (3) transaction ID
*                       (4) server      ID
*
*                   (c) Retrieve received message type
*
*
* Argument(s) : sock_id             Socket ID of socket to receive DHCP reply message.
*
*               pif_info            Pointer to DHCP interface information.
*               --------            Argument checked in DHCPc_Discover(),
*                                                       DHCPc_Req().
*
*               server_id           Server identifier.
*
*               paddr_hw            Pointer to hardware address buffer.
*
*               addr_hw_len         Length of the hardware address buffer pointed to by 'paddr_hw'.
*
*               pmsg_buf            Pointer to DHCP message buffer to receive reply.
*
*               pmsg_buf_len        Pointer to a variable to ... :
*
*                                       (a) Pass the size of the message buffer pointed to by 'pmsg_buf'.
*                                       (b) (1) Return the actual size of the received message buffer, if NO errors;
*                                           (2) Return 0,                                              otherwise.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  DHCPc reply successfully received.
*                               DHCPc_ERR_NULL_PTR              Argument(s) passed a NULL pointer.
*                               DHCPc_ERR_INVALID_MSG_SIZE      Argument 'pmsg_buf' size invalid.
*                               DHCPc_ERR_INVALID_HW_ADDR       Argument 'paddr_hw' has an invalid length.
*                               DHCPc_ERR_RX_MSG_TYPE           Error extracting message type from reply message.
*
*                                                               ----------- RETURNED BY DHCPc_Rx() : ------------
*                               DHCPc_ERR_RX_OVF                Receive error, data buffer overflow.
*                               DHCPc_ERR_RX                    Receive error.
*
* Return(s)   : Type of received message, if NO error.
*
*               DHCP_MSG_NONE,            otherwise.
*
* Caller(s)   : DHCPc_Discover(),
*               DHCPc_Req().
*
* Note(s)     : (2) #### This implementation of the DHCP client presumes an Ethernet hardware type.
*
*               (3) Received messages smaller than the minimum size allowed are silently discarded.
*
*                   See also 'dhcp-c.h  DHCP MESSAGE DEFINES  Note #2'.
*********************************************************************************************************
*/

static  DHCPc_MSG_TYPE  DHCPc_RxReply (NET_SOCK_ID     sock_id,
                                       DHCPc_IF_INFO  *pif_info,
                                       NET_IPv4_ADDR   server_id,
                                       CPU_INT08U     *paddr_hw,
                                       CPU_INT08U      addr_hw_len,
                                       CPU_INT08U     *pmsg_buf,
                                       CPU_INT16U     *pmsg_buf_len,
                                       DHCPc_ERR      *perr)
{
    CPU_BOOLEAN         remote_match;
    CPU_BOOLEAN         opcode_reply;
    CPU_BOOLEAN         addr_hw_match;
    CPU_BOOLEAN         transaction_id_match;
    CPU_BOOLEAN         rx_err;
    NET_SOCK_ADDR       addr_remote;
    NET_SOCK_ADDR_LEN   addr_remote_size;
    CPU_INT16U          rx_msg_len;
    DHCP_MSG_HDR       *pmsg_hdr;
    CPU_INT32U          rx_xid;
    CPU_INT08U         *popt;
    CPU_INT08U         *opt_val_len;
    NET_IPv4_ADDR       addr_server;
    DHCPc_MSG_TYPE      msg_type;


#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)                   /* --------------- VALIDATE PTR & ARGS ---------------- */
    if ((paddr_hw     == (CPU_INT08U *)0) ||
        (pmsg_buf     == (CPU_INT08U *)0) ||
        (pmsg_buf_len == (CPU_INT16U *)0)) {
       *pmsg_buf_len = 0;
       *perr         = DHCPc_ERR_NULL_PTR;
        return (DHCP_MSG_NONE);
    }

    if (*pmsg_buf_len < DHCP_MSG_RX_MIN_LEN) {
        *pmsg_buf_len = 0;
        *perr         = DHCPc_ERR_INVALID_MSG_SIZE;
        return (DHCP_MSG_NONE);
    }

    if (addr_hw_len != NET_IF_ETHER_ADDR_SIZE) {
       *pmsg_buf_len = 0;
       *perr         = DHCPc_ERR_INVALID_HW_ADDR;
        return (DHCP_MSG_NONE);
    }

#else
   (void)&addr_hw_len;                                          /* Prevent 'variable unused' compiler warning.          */
#endif


                                                                /* ------------ RX MESSAGE FROM SERVER(S) ------------- */
    remote_match         = DEF_NO;
    opcode_reply         = DEF_NO;
    addr_hw_match        = DEF_NO;
    transaction_id_match = DEF_NO;
    rx_err               = DEF_NO;

    while (((remote_match         != DEF_YES)  ||
            (opcode_reply         != DEF_YES)  ||
            (addr_hw_match        != DEF_YES)  ||
            (transaction_id_match != DEF_YES)) &&
            (rx_err               != DEF_YES)) {

        addr_remote_size = sizeof(addr_remote);

        rx_msg_len = DHCPc_Rx((NET_SOCK_ID        ) sock_id,
                              (void              *) pmsg_buf,
                              (CPU_INT16U         )*pmsg_buf_len,
                              (NET_SOCK_ADDR     *)&addr_remote,
                              (NET_SOCK_ADDR_LEN *)&addr_remote_size,
                              (DHCPc_ERR         *) perr);

        if (*perr == DHCPc_ERR_NONE) {
                                                                /* ------------------- VALIDATE MSG ------------------- */
            if (rx_msg_len >= DHCP_MSG_RX_MIN_LEN) {            /* See Note #3.                                         */
                pmsg_hdr = (DHCP_MSG_HDR *)pmsg_buf;

                                                                /* Validate opcode.                                     */
                opcode_reply = (pmsg_hdr->op == DHCP_OP_REPLY) ? DEF_YES : DEF_NO;


                                                                /* Validate HW addr.                                    */
                addr_hw_match = Mem_Cmp((void     *)pmsg_hdr->chaddr,
                                        (void     *)paddr_hw,
                                        (CPU_SIZE_T)NET_IF_ETHER_ADDR_SIZE);


                                                                /* Validate transaction ID.                             */
                NET_UTIL_VAL_COPY_GET_NET_32(&rx_xid, &pmsg_hdr->xid);
                transaction_id_match = (rx_xid == pif_info->TransactionID) ? DEF_YES : DEF_NO;


                                                                /* Validate server id.                                  */
                if (server_id != NET_IPv4_ADDR_NONE) {          /* If server id known,    ...                           */
                                                                /* ... get server id opt, ...                           */
                    popt = DHCPc_MsgGetOpt((DHCPc_OPT_CODE) DHCP_OPT_SERVER_IDENTIFIER,
                                           (CPU_INT08U   *) pmsg_buf,
                                           (CPU_INT16U    ) rx_msg_len,
                                           (CPU_INT08U   *)&opt_val_len);

                    if (popt == (CPU_INT08U *)0) {
                        remote_match = DEF_NO;

                    } else {                                    /* ... & compare with lease server id.                  */
                        NET_UTIL_VAL_COPY_32(&addr_server, popt);
                        remote_match = (addr_server == server_id ? DEF_YES : DEF_NO);
                    }

                } else {
                    remote_match = DEF_YES;
                }
            }

        } else {
            rx_err = DEF_YES;
        }
    }

    if (*perr != DHCPc_ERR_NONE) {
       *pmsg_buf_len = 0;
        return (DHCP_MSG_NONE);
    }

                                                                /* ------------------- GET MSG TYPE ------------------- */
    popt = DHCPc_MsgGetOpt((DHCPc_OPT_CODE) DHCP_OPT_DHCP_MESSAGE_TYPE,
                           (CPU_INT08U   *) pmsg_buf,
                           (CPU_INT16U    ) rx_msg_len,
                           (CPU_INT08U   *)&opt_val_len);

    if (popt == (CPU_INT08U *)0) {
       *pmsg_buf_len = 0;
       *perr         = DHCPc_ERR_RX_MSG_TYPE;
        return (DHCP_MSG_NONE);
    }

                                                                /* Retrieve msg type opt val.                           */
    msg_type     = (DHCPc_MSG_TYPE)(*popt);
   *pmsg_buf_len =  rx_msg_len;

    return (msg_type);
}


/*
*********************************************************************************************************
*                                             DHCPc_Rx()
*
* Description : Receive DHCPc data via socket.
*
* Argument(s) : sock_id             Socket ID of socket to receive DHCPc data.
*
*               pdata_buf           Pointer to DHCPc data buffer to receive data.
*               ---------           Argument checked in DHCPc_RxReply().
*
*               data_buf_len        Length  of DHCPc data buffer to receive data.
*
*               paddr_remote        Pointer to an address buffer that will receive the socket address
*                                       structure with the received data's remote address.
*
*               paddr_remote_len    Pointer to a variable to ... :
*
*                                       (a) Pass the size of the address buffer pointed to by 'paddr_remote'.
*                                       (b) Return the actual size of socket address structure with the
*                                               received data's remote address.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  DHCPc data successfully received.
*                               DHCPc_ERR_RX_OVF                Receive error, data buffer overflow.
*                               DHCPc_ERR_RX                    Receive error.
*
* Return(s)   : Length of DHCPc data received (in octets), if no error.
*
*               0,                                         otherwise.
*
* Caller(s)   : DHCPc_RxReply().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  CPU_INT16U  DHCPc_Rx (NET_SOCK_ID         sock_id,
                              void               *pdata_buf,
                              CPU_INT16U          data_buf_len,
                              NET_SOCK_ADDR      *paddr_remote,
                              NET_SOCK_ADDR_LEN  *paddr_remote_len,
                              DHCPc_ERR          *perr)
{
    CPU_INT16U  rx_len;
    NET_ERR     err_net;


                                                                /* ------------------- RX APP DATA -------------------- */
    rx_len = NetApp_SockRx((NET_SOCK_ID        ) sock_id,
                           (void              *) pdata_buf,
                           (CPU_INT16U         ) data_buf_len,
                           (CPU_INT16U         ) 0,
#if (NET_VERSION >= 21200u)
                           (NET_SOCK_API_FLAGS ) NET_SOCK_FLAG_NONE,
#else
                           (CPU_INT16S         ) NET_SOCK_FLAG_NONE,
#endif
                           (NET_SOCK_ADDR     *) paddr_remote,
                           (NET_SOCK_ADDR_LEN *) paddr_remote_len,
                           (CPU_INT16U         ) DHCPc_RX_MAX_RETRY,
                           (CPU_INT32U         ) DHCPc_CFG_MAX_RX_TIMEOUT_MS,
                           (CPU_INT32U         ) DHCPc_RX_TIME_DLY_MS,
                           (NET_ERR           *)&err_net);

    switch (err_net) {
        case NET_APP_ERR_NONE:
            *perr = DHCPc_ERR_NONE;
             break;


        case NET_APP_ERR_DATA_BUF_OVF:
            *perr = DHCPc_ERR_RX_OVF;
             break;


        case NET_APP_ERR_CONN_CLOSED:
        case NET_APP_ERR_FAULT:
        case NET_APP_ERR_INVALID_ARG:
        case NET_APP_ERR_INVALID_OP:
        case NET_ERR_RX:
        default:
             rx_len = 0;
            *perr   = DHCPc_ERR_RX;
             break;
    }

    return (rx_len);
}


/*
*********************************************************************************************************
*                                        DHCPc_TxMsgPrepare()
*
* Description : Prepare DHCP message.
*
* Argument(s) : pif_info        Pointer to DHCP interface information.
*               --------        Argument checked   in DHCPc_Discover(),
*                                                     DHCPc_Req(),
*                                        validated in DHCPc_DeclineRelease().
*
*               msg_type        Type of message to be prepared :
*
*                                   DHCP_MSG_DISCOVER
*                                   DHCP_MSG_REQUEST
*                                   DHCP_MSG_DECLINE
*                                   DHCP_MSG_RELEASE
*
*               paddr_hw        Pointer to hardware address buffer.
*
*               addr_hw_len     Length of the hardware address buffer pointed to by 'paddr_hw'.
*
*               pmsg_buf        Pointer to DHCP transmit message buffer.
*
*               msg_buf_size    Size of message buffer (in octets).
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  DHCPc message successfully prepared.
*                               DHCPc_ERR_NULL_PTR              Argument(s) 'paddr_hw/pmsg_buf' passed a
*                                                                   NULL pointer.
*                               DHCPc_ERR_INVALID_HW_ADDR       Argument 'paddr_hw' has an invalid length.
*                               DHCPc_ERR_INVALID_MSG_SIZE      Argument 'pmsg_buf' size invalid.
*                               DHCPc_ERR_INVALID_MSG           Invalid DHCP message.
*
* Return(s)   : Size of the message (in octets).
*
* Caller(s)   : DHCPc_Discover(),
*               DHCPc_Req(),
*               DHCPc_DeclineRelease().
*
* Note(s)     : (2) #### This implementation of the DHCP client presumes an Ethernet hardware type.
*
*               (3) From RFC #2131, section 'Protocol Summary', "To work around some clients that cannot
*                   accept IP unicast datagrams before the TCP/IP software is configured [...], DHCP uses
*                   the 'flags' field".  Section 'Constructing and sending DHCP messages' continues by
*                   adding that "a client that cannot receive unicast IP datagrams until its protocol
*                   software has been configured with an IP address SHOULD set the BROADCAST bit in the
*                   'flags' field to 1".
*
*                  "A server [...] sending [...] a DHCP message directly to a DHCP client SHOULD examine
*                   [that bit] in the 'flags' field.  If this bit is set to 1, the DHCP message SHOULD be
*                   sent as an IP broadcast using an IP broadcast address as the IP destination address
*                   and the link-layer broadcast address as the link-layer destination address".
*
*                   #### Since the Micrium uC/TCP-IP stack is NOT able to receive a packet on an
*                   unconfigured interface, the BROADCAST bit is always set in the 'flags' field of a DHCP
*                   message, when permitted by the RFC.
*
*               (4) #### The application requested parameters are NOT checked agains the system requested
*                   ones, so it is possible that the same parameter be requested twice.  However, this
*                   CANNOT lead to potential problem.
*
*               (5) The vendor-specific options MUST be large enough so the DHCP message is at least
*                   DHCP_MSG_TX_MIN_LEN octets.
*
*                   See also 'dhcp-c.h  DHCP MESSAGE DEFINES  Note #2'.
*********************************************************************************************************
*/

static  CPU_INT16U  DHCPc_TxMsgPrepare (DHCPc_IF_INFO   *pif_info,
                                        DHCPc_MSG_TYPE   msg_type,
                                        CPU_INT08U      *paddr_hw,
                                        CPU_INT08U       addr_hw_len,
                                        CPU_INT08U      *pmsg_buf,
                                        CPU_INT16U       msg_buf_size,
                                        DHCPc_ERR       *perr)
{
    DHCP_MSG_HDR  *pmsg_hdr;
    CPU_INT08U    *pmsg_opt;
    DHCPc_MSG     *pmsg_last_rx;
    DHCP_MSG_HDR  *pmsg_last_rx_hdr;
    CPU_INT16U     msg_size;
    CPU_INT16U     flag;
    CPU_INT32U     ciaddr;
    CPU_BOOLEAN    get_local_addr;
    CPU_BOOLEAN    wr_req_ip_addr;
    CPU_BOOLEAN    wr_server_id;
    CPU_BOOLEAN    req_param;
    CPU_INT08U     req_param_qty;
    CPU_INT08U    *popt;
    CPU_INT16U     opt_len;
    CPU_INT16U     opt_pad_len;
#if (CPU_CFG_NAME_EN == DEF_ENABLED)
    CPU_CHAR       host_name[CPU_CFG_NAME_SIZE];
    CPU_SIZE_T     host_name_len;
    CPU_ERR        err_cpu;
#endif


#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)                   /* --------------- VALIDATE PTR & ARGS ---------------- */
    if ((paddr_hw == (CPU_INT08U    *)0) ||
        (pmsg_buf == (CPU_INT08U    *)0)) {
       *perr = DHCPc_ERR_NULL_PTR;
        return (0);
    }

    if (addr_hw_len != NET_IF_ETHER_ADDR_SIZE) {                /* See Note #2.                                         */
       *perr = DHCPc_ERR_INVALID_HW_ADDR;
        return (0);
    }

    if (msg_buf_size < DHCP_MSG_BUF_SIZE) {
       *perr = DHCPc_ERR_INVALID_MSG_SIZE;
        return (0);
    }

#else
   (void)&addr_hw_len;                                          /* Prevent 'variable unused' compiler warning.          */
#endif


                                                                /* ---------- GET SPECIFIC FIELDS & OPT VAL ----------- */
    flag = 0;
    switch (msg_type) {
        case DHCP_MSG_DISCOVER:
#if (DHCPc_CFG_BROADCAST_BIT_EN == DEF_ENABLED)                 /* See Note #3.                                         */
             flag           = DHCP_FLAG_BROADCAST;
#endif
             get_local_addr = DEF_NO;
             wr_req_ip_addr = DEF_NO;
             wr_server_id   = DEF_NO;
             req_param      = DEF_YES;
             break;


        case DHCP_MSG_REQUEST:
#if (DHCPc_CFG_BROADCAST_BIT_EN == DEF_ENABLED)                 /* See Note #3.                                         */
             flag      = DHCP_FLAG_BROADCAST;
#endif
             req_param = DEF_YES;

             switch (pif_info->ClientState) {
                 case DHCP_STATE_SELECTING:
                      get_local_addr = DEF_NO;
                      wr_req_ip_addr = DEF_YES;
                      wr_server_id   = DEF_YES;
                      break;


                 case DHCP_STATE_INIT_REBOOT:
                      get_local_addr = DEF_NO;
                      wr_req_ip_addr = DEF_YES;
                      wr_server_id   = DEF_NO;
                      break;


                 case DHCP_STATE_BOUND:
                 case DHCP_STATE_RENEWING:
                 case DHCP_STATE_REBINDING:
                      get_local_addr = DEF_YES;
                      wr_req_ip_addr = DEF_NO;
                      wr_server_id   = DEF_NO;
                      break;


                 default:
                      get_local_addr = DEF_NO;
                      wr_req_ip_addr = DEF_NO;
                      wr_server_id   = DEF_NO;
                      break;
             }
             break;


        case DHCP_MSG_DECLINE:
             get_local_addr = DEF_NO;
             wr_req_ip_addr = DEF_YES;
             wr_server_id   = DEF_YES;
             req_param      = DEF_NO;
             break;


        case DHCP_MSG_RELEASE:
             get_local_addr = DEF_YES;
             wr_req_ip_addr = DEF_NO;
             wr_server_id   = DEF_YES;
             req_param      = DEF_NO;
             break;


        default:                                                /* Unsupported msg, ...                                 */
            *perr = DHCPc_ERR_INVALID_MSG;
             return (0);                                        /* ... rtn.                                             */
    }

    pmsg_last_rx     = (DHCPc_MSG    *) pif_info->MsgPtr;
    pmsg_last_rx_hdr = (DHCP_MSG_HDR *)&pmsg_last_rx->MsgBuf[0];

    if (get_local_addr == DEF_YES) {
        NET_UTIL_VAL_COPY_32(&ciaddr, &pmsg_last_rx_hdr->yiaddr);

    } else {
        ciaddr = 0;
    }


    Mem_Clr((void     *)pmsg_buf,                               /* Clr msg buf.                                         */
            (CPU_SIZE_T)msg_buf_size);

                                                                /* --------------- SETTING DHCP MSG HDR --------------- */
    pmsg_hdr        = (DHCP_MSG_HDR *)&pmsg_buf[0];

    pmsg_hdr->op    = DHCP_OP_REQUEST;
    pmsg_hdr->htype = DHCP_HTYPE_ETHER;                         /* See Note #2.                                         */
    pmsg_hdr->hlen  = NET_IF_ETHER_ADDR_SIZE;
    pmsg_hdr->hops  = 0;

    NET_UTIL_VAL_COPY_SET_NET_32(&pmsg_hdr->xid, &pif_info->TransactionID);
    NET_UTIL_VAL_SET_NET_32(&pmsg_hdr->secs, 0);

    NET_UTIL_VAL_COPY_SET_NET_16(&pmsg_hdr->flags, &flag);

    NET_UTIL_VAL_COPY_32(&pmsg_hdr->ciaddr, &ciaddr);           /* Already in net order.                                */
    NET_UTIL_VAL_SET_NET_32(&pmsg_hdr->yiaddr, 0);
    NET_UTIL_VAL_SET_NET_32(&pmsg_hdr->siaddr, 0);
    NET_UTIL_VAL_SET_NET_32(&pmsg_hdr->giaddr, 0);

    Mem_Copy((void     *) pmsg_hdr->chaddr,                     /* Copy hw addr (see Note #2).                          */
             (void     *) paddr_hw,
             (CPU_SIZE_T) NET_IF_ETHER_ADDR_SIZE);


                                                                /* --------------- SETTING DHCP MSG OPT --------------- */
    pmsg_opt = &pmsg_buf[DHCP_MSG_HDR_SIZE];
    popt     =  pmsg_opt;

    NET_UTIL_VAL_SET_NET_32(popt, DHCP_MAGIC_COOKIE);           /* DHCP Magic cookie.                                   */
    popt    += DHCP_MAGIC_COOKIE_SIZE;

   *popt++   = DHCP_OPT_DHCP_MESSAGE_TYPE;                      /* DHCP Message Type.                                   */
   *popt++   = 1;                                               /* Msg type opt len.                                    */
   *popt++   = msg_type;

    if (wr_req_ip_addr == DEF_YES) {                            /* Requested IP address.                                */
       *popt++  = DHCP_OPT_REQUESTED_IP_ADDRESS;
       *popt++  = 4;
        NET_UTIL_VAL_COPY_32(popt, &pmsg_last_rx_hdr->yiaddr);
        popt   += 4;
    }

    if (wr_server_id == DEF_YES) {                              /* Server ID.                                           */
       *popt++  = DHCP_OPT_SERVER_IDENTIFIER;
       *popt++  = 4;
        NET_UTIL_VAL_COPY_32(popt, &pif_info->ServerID);
        popt   += 4;
    }

#if (CPU_CFG_NAME_EN == DEF_ENABLED)                            /* Host name.                                           */
    CPU_NameGet(host_name, &err_cpu);
    if (err_cpu == CPU_ERR_NONE) {
        host_name_len = Str_Len(host_name);
        if (host_name_len > 0) {
           *popt++  = DHCP_OPT_HOST_NAME;
           *popt++  = host_name_len;
            Mem_Copy(popt, host_name, host_name_len);
            popt   += host_name_len;
        }
    }
#endif

    if (req_param == DEF_YES) {                                 /* Req'd param.                                         */
        req_param_qty = sizeof(DHCPc_ReqParam);

       *popt++ = DHCP_OPT_PARAMETER_REQUEST_LIST;
       *popt++ = pif_info->ParamReqQty + req_param_qty;

        Mem_Copy((void     *) popt,                             /* Copy system req'd param.                             */
                 (void     *)&DHCPc_ReqParam[0],
                 (CPU_SIZE_T) req_param_qty);
        popt += req_param_qty;

        if (pif_info->ParamReqQty > 0) {
            Mem_Copy((void     *) popt,                         /* Copy app    req'd param (see Note #4).               */
                     (void     *)&pif_info->ParamReqTbl[0],
                     (CPU_SIZE_T) pif_info->ParamReqQty);
            popt += pif_info->ParamReqQty;
        }
    }

   *popt++   = DHCP_OPT_END;                                    /* End of options.                                      */


                                                                /* -------------------- GET MSG LEN ------------------- */
    opt_len = popt - pmsg_opt;
    if (opt_len < (DHCP_MSG_TX_MIN_LEN - DHCP_MSG_HDR_SIZE)) {  /* See Note #4.                                         */
        opt_pad_len = ((DHCP_MSG_TX_MIN_LEN - DHCP_MSG_HDR_SIZE) - opt_len);

        Mem_Set((void     *)popt,
                (CPU_INT08U)DHCP_OPT_PAD,
                (CPU_SIZE_T)opt_pad_len);

        opt_len = (DHCP_MSG_TX_MIN_LEN - DHCP_MSG_HDR_SIZE);
    }

                                                                /* ------------------- GET CUR TIME ------------------- */
    pif_info->NegoStartTime = DHCPc_OS_TimeGet_tick();


    msg_size = DHCP_MSG_HDR_SIZE + opt_len;
   *perr     = DHCPc_ERR_NONE;

    return (msg_size);
}


/*
*********************************************************************************************************
*                                             DHCPc_Tx()
*
* Description : Transmit DHCPc data via socket.
*
* Argument(s) : sock_id             Socket ID of socket to transmit DHCPc data.
*
*               pdata_buf           Pointer to DHCPc data buffer to transmit.
*
*               data_buf_len        Length  of DHCPc data buffer to transmit.
*
*               paddr_remote        Pointer to the socket address structure of the remote address
*
*               addr_remote_len     Length  of the socket address structure pointed to by 'paddr_remote'.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_ERR_NONE                  DHCPc data successfully transmitted.
*                               DHCPc_ERR_NULL_PTR              Argument(s) 'pdata_buf/paddr_remote' passed
*                                                                   a NULL pointer.
*                               DHCPc_ERR_TX                    Transmit error.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_Discover(),
*               DHCPc_Req(),
*               DHCPc_DeclineRelease().
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  void  DHCPc_Tx (NET_SOCK_ID         sock_id,
                        void               *pdata_buf,
                        CPU_INT16U          data_buf_len,
                        NET_SOCK_ADDR      *paddr_remote,
                        NET_SOCK_ADDR_LEN   addr_remote_len,
                        DHCPc_ERR          *perr)
{
    NET_ERR  err_net;


#if (DHCPc_CFG_ARG_CHK_DBG_EN == DEF_ENABLED)                   /* ------------------ VALIDATE PTRS ------------------- */
    if ((pdata_buf    == (void          *)0) ||
        (paddr_remote == (NET_SOCK_ADDR *)0)) {
       *perr = DHCPc_ERR_NULL_PTR;
        return;
    }
#endif


                                                                /* ------------------- TX APP DATA -------------------- */
   (void)NetApp_SockTx((NET_SOCK_ID       ) sock_id,
                       (void             *) pdata_buf,
                       (CPU_INT16U        ) data_buf_len,
#if (NET_VERSION >= 21200u)
                       (NET_SOCK_API_FLAGS) NET_SOCK_FLAG_NONE,
#else
                       (CPU_INT16S        ) NET_SOCK_FLAG_NONE,
#endif
                       (NET_SOCK_ADDR    *) paddr_remote,
                       (NET_SOCK_ADDR_LEN ) addr_remote_len,
                       (CPU_INT16U        ) DHCPc_TX_MAX_RETRY,
                       (CPU_INT32U        ) 0,
                       (CPU_INT32U        ) DHCPc_TX_TIME_DLY_MS,
                       (NET_ERR          *)&err_net);

    if (err_net == NET_APP_ERR_NONE) {
       *perr = DHCPc_ERR_NONE;

    } else {
       *perr = DHCPc_ERR_TX;
    }
}

#endif
