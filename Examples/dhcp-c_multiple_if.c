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
*                                      DHCPc MULTIPLE INTERFACE
*
* Filename : dhcp-c_multiple_if.c
* Version  : V2.11.00
*********************************************************************************************************
* Note(s)  : (1) This example shows how to initialize uC/DHCPc, start DHCP negotiation on many interface
*                and return only when all DHCP negotiation are completed.
*
*            (2) This example is for :
*
*                  (a) Many interfaces.
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

#include  <Source/net_type.h>
#include  <Source/dhcp-c.h>


/*
*********************************************************************************************************
*                                      AppDHCPc_InitMultipleIF()
*
* Description : This function initialize uC/DHCPc and start DHCPc negotiation for many interfaces. This
*               function returns only the DHCP negotiation is completed.
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
*               (2) Start the DHCP management of the interfaces. Note that the interface is not configured yet upon
*                   returning from this function.
*
*               (3) An OS time delay must be applied between each call to DHCP to allow other task to run.
*
*               (4) Once the DHCP management of an interface has been started, the application may want to check the
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

CPU_BOOLEAN  AppDHCPc_InitMultipleIF (NET_IF_NBR    if_nbr_tbl[],
                                      CPU_INT08U    nbr_if_cfgd,
                                      DHCPc_STATUS  if_dhcp_result[])
{
    NET_IF_NBR      if_nbr_cur;
    CPU_INT08U      nbr_if_init;
    CPU_INT08U      ix;
    DHCPc_STATUS    status;
    OS_ERR          os_err;
    DHCPc_ERR       err;


                                                                /* --------------- INITIALIZE uC/DHCPc ---------------- */
    err = DHCPc_Init();                                         /* See Note #1.                                         */
    if (err != DHCPc_ERR_NONE) {
        return (DEF_FAIL);
    }

                                                                /* ------------ START DHCPC EACH INTERFACE ------------ */
    for (ix = 0; ix < nbr_if_cfgd; ix++) {
        if_nbr_cur = if_nbr_tbl[ix];
        DHCPc_Start(if_nbr_cur,                                 /* See Note #2.                                         */
                    DEF_NULL,
                    0,
                   &err);
        if (err != DHCPc_ERR_NONE) {
            return (DEF_FAIL);
        }

        if_dhcp_result[ix] = DHCP_STATUS_CFG_IN_PROGRESS;
    }



    nbr_if_init = 0u;

                                                                /* ------ WAIT UNTIL NEGOTIATIONS ARE COMPLETED ------- */
    while (nbr_if_init < nbr_if_cfgd) {
        OSTimeDlyHMSM(0, 0, 0, 200, OS_OPT_TIME_DLY, &os_err);  /* TODO change following OS API. See Note #3.           */


        for (ix = 0; ix < nbr_if_cfgd; ix++) {
            if (if_dhcp_result[ix] == DHCP_STATUS_CFG_IN_PROGRESS) {
                if_nbr_cur = if_nbr_tbl[ix];
                status = DHCPc_ChkStatus(if_nbr_cur, &err);     /* Check DHCP status. See Note #4.                      */
                switch (status) {
                    case DHCP_STATUS_CFG_IN_PROGRESS:           /* See Note #4a.                                        */
                         break;

                    case DHCP_STATUS_CFGD:                      /* See Note #4b.                                        */
                    case DHCP_STATUS_CFGD_NO_TMR:               /* See Note #4c.                                        */
                    case DHCP_STATUS_CFGD_LOCAL_LINK:           /* See Note #4d.                                        */
                    case DHCP_STATUS_FAIL:                      /* See Note #4e.                                        */
                         if_dhcp_result[ix] = status;           /* Store negotiation result.                            */
                         nbr_if_init++;
                         break;

                    default:
                         break;
                }
            }
        }
    }

    return (DEF_OK);
}
