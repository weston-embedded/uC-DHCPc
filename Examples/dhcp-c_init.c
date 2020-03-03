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
*                                       DHCPc SINGLE INTERFACE
*
* Filename : dhcp-c_init.c
* Version  : V2.11.00
*********************************************************************************************************
* Note(s)  : (1) This example shows how to initialize uC/DHCPc, start DHCP negotiation on 1 interface,
*                request parameters from the server during the negotiation. The function returns only
*                the DHCP negotiation is completed.
*
*            (2) This example is for :
*
*                  (a) 1 interface.
*                  (b) uC/TCPIP - V3.00.01
*                  (b) uCOS-III - V3.00
*
*            (3) This file is an example about how to use uC/DHCPc, It may not cover all case needed by a real
*                application. Also some modification might be needed, insert the code to perform the stated
*                actions wherever 'TODO' comments are found.
*
*                (a) For example this example doesn't manage the link state (plugs and unplugs), this can
*                    be a problem when switching from a network to another network.
*
*                (b) This example is not fully tested, so it is not guaranteed that all cases are cover
*                    properly.
*********************************************************************************************************
*/

#include  <Source/os.h>                                         /* TODO OS header is required for time delay definition */

#include  <IP/IPv4/net_ipv4.h>
#include  <Source/net_type.h>
#include  <Source/net_ip.h>

#include  <Source/dhcp-c.h>


/*
*********************************************************************************************************
*                                            AppInit_DHCPc()
*
* Description : This function initialize uC/DHCPc and start DHCPc negotiation for the interface. This function
*               returns only the DHCP negotiation is completed.
*
* Argument(s) : if_nbr_tbl      Table that contains interface ID to be initialized with uC/DHCPc
*
*               nbr_if_cfgd     Number of interface to initialized (contained in tables)
*
*               if_dhcp_result  Table that will receive the DHCPc result of each interface to initialize.
*
* Return(s)   : DEF_OK,   Completed successfully.
*
*               DEF_FAIL, Initialization failed.
*
* Caller(s)   : Application.
*
* Note(s)     : (1) Prior to do any call to DHCPc the module must be initialized. If the process is successful,
*                   the DHCP client s tasks are started, and its various data structures are initialized.
*
*               (2) It is possible to request additional parameters from the DHCP server by setting a DHCPc options
*                   table, which must be passed to start function. Note that the server will not necessarily
*                   transmit those parameters.
*
*               (3) Start the DHCP management of the interfaces. Note that the interface is not configured yet upon
*                   returning from this function.
*
*               (4) An OS time delay must be applied between each call to DHCP to allow other task to run.
*
*               (5) Once the DHCP management of an interface has been started, the application may want to check the
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
*
*               (6) Once the DHCP negotiation is completed successfully, it is possible to retrieve the parameters requested
*                   during the start. If the function returns an error an invalid value, it means that the server might have
*                   not transmitted the requested parameters.
*
*               (7) It is possible to retrieve the address configured by the DHCP client by calling the appropriate TCP/IP
*                   stack API.
*********************************************************************************************************
*/

CPU_BOOLEAN  AppDHCPc_Init (NET_IF_NBR      if_nbr,
                            NET_IPv4_ADDR  *p_addr_cfgd,
                            NET_IPv4_ADDR  *p_addr_dns)
{
    DHCPc_STATUS      status;
    CPU_BOOLEAN       done;
    DHCPc_OPT_CODE    req_param[DHCPc_CFG_PARAM_REQ_TBL_SIZE];
    CPU_INT08U        req_param_qty;
    NET_IPv4_ADDR     addr_tbl[NET_IPv4_CFG_IF_MAX_NBR_ADDR];
    NET_IP_ADDRS_QTY  addr_ip_tbl_qty;
    CPU_INT16U        size;
    OS_ERR            err_os;
    NET_ERR           err_net;
    DHCPc_ERR         err;


                                                                /* --------------- INITIALIZE uC/DHCPc ---------------- */
    err = DHCPc_Init();                                         /* See Note #1.                                         */
    if (err != DHCPc_ERR_NONE) {
        return (DEF_FAIL);
    }


                                                                /* ---------- CFG DHCPC PARAMETER REQUESTED ----------- */
    req_param[0]   = DHCP_OPT_DOMAIN_NAME_SERVER;               /* Obtain DNS address. See Note #2                      */
    req_param_qty  = 1u;                                        /* 1 parameter requested.                               */


                                                                /* ----------- START DHCPC ON THE INTERFACE ----------- */
    DHCPc_Start(if_nbr, req_param, req_param_qty, &err);        /* See Note #3.                                         */
    if (err != DHCPc_ERR_NONE) {
        return (DEF_FAIL);
    }


                                                                /* ------- WAIT UNTIL NEGOTIATION IS COMPLETED -------- */
    done = DEF_NO;
    while (done == DEF_NO) {
        OSTimeDlyHMSM(0, 0, 0, 200, OS_OPT_TIME_DLY, &err_os);  /* TODO change following OS API. See Note #4.           */

        status = DHCPc_ChkStatus(if_nbr, &err);                 /* Check DHCP status. See Note #5.                      */
        switch (status) {
            case DHCP_STATUS_CFG_IN_PROGRESS:                   /* See Note #5a.                                        */
                 break;


            case DHCP_STATUS_CFGD:                              /* See Note #5b.                                        */
            case DHCP_STATUS_CFGD_NO_TMR:                       /* See Note #5c.                                        */
                 size = sizeof(NET_IPv4_ADDR);                  /* An address has been configured.                      */
                 DHCPc_GetOptVal(             if_nbr,           /* Get DNS address obtained by the DHCPc. See Note #6.  */
                                              DHCP_OPT_DOMAIN_NAME_SERVER,
                                (CPU_INT08U *)p_addr_dns,
                                             &size,
                                             &err);
                 /* Break intentionally omitted. */


            case DHCP_STATUS_CFGD_LOCAL_LINK:                   /* See Note #5d.                                        */
                 done = DEF_YES;
                 addr_ip_tbl_qty = sizeof(addr_tbl) / sizeof(NET_IPv4_ADDR);
                (void)NetIPv4_GetAddrHost(if_nbr,               /* See Note #7. Get current address configured.         */
                                         &addr_tbl[0],
                                         &addr_ip_tbl_qty,
                                         &err_net);
                 if (err_net != NET_IPv4_ERR_NONE) {
                     return (DEF_FAIL);
                 }

                *p_addr_cfgd = addr_tbl[0];
                 break;


            case DHCP_STATUS_FAIL:                              /* See Note #5e. No address has been configured.        */
                 DHCPc_Stop(if_nbr, &err);
                 return (DEF_FAIL);


            default:
                 break;
        }
    }

    return (DEF_OK);
}
