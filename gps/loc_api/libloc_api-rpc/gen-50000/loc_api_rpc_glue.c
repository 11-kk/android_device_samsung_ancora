/* Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*=====================================================================

                     INCLUDE FILES FOR MODULE

======================================================================*/
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include <rpc/rpc.h>
#include <rpc/clnt.h>

/* Include RPC headers */
#include "loc_api_rpc_glue.h"

/* Callback init */
#include "loc_apicb_appinit.h"

/* Logging */
#define LOG_TAG "loc_api_rpc_glue"
#define LOG_NDDEBUG 0
#include <utils/Log.h>

/* Uncomment to force ALOGD messages */
// #define ALOGD ALOGI

/*=====================================================================
     External declarations
======================================================================*/

CLIENT* loc_api_clnt = NULL;

/* Callback ID and pointer */
#define LOC_API_CB_MAX_CLIENTS 16
typedef struct
{
    uint32 cb_id;                        /* same as rpc/types.h */
    loc_event_cb_f_type *cb_func;      /* callback func */
    rpc_loc_client_handle_type handle; /* stores handle for client closing */
} loc_glue_cb_entry_s_type;

loc_glue_cb_entry_s_type loc_glue_callback_table[LOC_API_CB_MAX_CLIENTS];

#define RPC_FUNC_VERSION_BASE(a,b) a ## b
#define RPC_FUNC_VERSION(a,b) RPC_FUNC_VERSION_BASE(a,b)

#define RPC_CALLBACK_FUNC_VERSION_BASE(a,v,b) a ## v ## b
#define RPC_CALLBACK_FUNC_VERSION(a,v,b) RPC_CALLBACK_FUNC_VERSION_BASE(a,v,b)

#define LOC_GLUE_CHECK_INIT(ret_type) \
   if (loc_api_clnt == NULL) { return (ret_type) RPC_LOC_API_RPC_FAILURE; }

#define LOC_GLUE_CHECK_RESULT(stat, ret_type) \
   if (stat != RPC_SUCCESS) { return (ret_type) RPC_LOC_API_RPC_FAILURE; }

/* Callback functions */
/* Returns 1 if successful */
bool_t rpc_loc_event_cb_f_type_svc(
      rpc_loc_event_cb_f_type_args *argp,
      rpc_loc_event_cb_f_type_rets *ret,
      struct svc_req *req)
{
   // The lower word of cd_id is the index
   int index = argp->cb_id & 0xFFFF;

   /* Callback not registered, or unexpected ID (shouldn't happen) */
   if (index > LOC_API_CB_MAX_CLIENTS || loc_glue_callback_table[index].cb_func == NULL)
   {
      ALOGE("Warning: No callback handler.\n");
      ret->loc_event_cb_f_type_result = 0;
      return 1; /* simply return */
   }

   ALOGV("proc: %x  prog: %x  vers: %x\n",
         (int) req->rq_proc,
         (int) req->rq_prog,
         (int) req->rq_vers);

   ALOGV("Callback received: %x (cb_id=0x%X handle=%d ret_ptr=%d)\n",
         (int) argp->loc_event,
         (int) argp->cb_id,
         (int) argp->loc_handle,
         (int) ret);

   /* Forward callback to real callback procedure */
   rpc_loc_client_handle_type        loc_handle = argp->loc_handle;
   rpc_loc_event_mask_type           loc_event  = argp->loc_event;
   const rpc_loc_event_payload_u_type*  loc_event_payload =
      (const rpc_loc_event_payload_u_type*) argp->loc_event_payload;

   /* Gives control to synchronous call handler */
   loc_api_callback_process_sync_call(loc_handle, loc_event, loc_event_payload);

   int32 rc = (loc_glue_callback_table[index].cb_func)(loc_handle, loc_event, loc_event_payload);

   ALOGV("cb_func=0x%x", (unsigned) loc_glue_callback_table[index].cb_func);

   ret->loc_event_cb_f_type_result = rc;

   return 1; /* ok */
}

int loc_apicbprog_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
   xdr_free (xdr_result, result);

   /*
    * Insert additional freeing code here, if needed
    */
   // ALOGD("***** loc_apicbprog_freeresult\n");

   return 1;
}

/*===========================================================================

FUNCTION rpc_loc_event_cb_f_type_<version>_svc (MACRO)

DESCRIPTION
   Callback function for Loc API

RETURN VALUE
   1 for success
   0 for failure

===========================================================================*/
bool_t RPC_CALLBACK_FUNC_VERSION(rpc_loc_event_cb_f_type_, RPC_LOC_EVENT_CB_F_TYPE_VERSION, _svc) (
      rpc_loc_event_cb_f_type_args *argp,
      rpc_loc_event_cb_f_type_rets *ret,
      struct svc_req *req)
{
   return rpc_loc_event_cb_f_type_svc(argp, ret, req);
}

/*===========================================================================

FUNCTION loc_apicbprog_<version>_freeresult (MACRO)

DESCRIPTION
   Free up RPC data structure

RETURN VALUE
   1 for success
   0 for failure

===========================================================================*/
#define VERSION_CONCAT(MAJOR,MINOR) MAJOR##MINOR
#define loc_apicb_prog_VER_freeresult(M,N) \
int RPC_CALLBACK_FUNC_VERSION(loc_apicbprog_, VERSION_CONCAT(M,N), _freeresult) \
(SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result) \
{ \
   return loc_apicbprog_freeresult(transp, xdr_result, result); \
}

/* Define all of the possible minors */
loc_apicb_prog_VER_freeresult(RPC_LOC_API_API_MAJOR_NUM, 0001);
loc_apicb_prog_VER_freeresult(RPC_LOC_API_API_MAJOR_NUM, 0002);
loc_apicb_prog_VER_freeresult(RPC_LOC_API_API_MAJOR_NUM, 0003);
loc_apicb_prog_VER_freeresult(RPC_LOC_API_API_MAJOR_NUM, 0004);

/*===========================================================================

FUNCTION rpc_loc_api_cb_null_<version>_svc (MACRO) [Patch for wrong RPCGEN stubs]

DESCRIPTION
   Null callback function for Loc API

RETURN VALUE
   1 for success

===========================================================================*/
#define rpc_loc_api_cb_null_VER_svc(M,N) \
bool_t RPC_CALLBACK_FUNC_VERSION(rpc_loc_api_cb_null_, VERSION_CONCAT(M,N), _svc) ( \
      void *a, int *b, struct svc_req *req) \
{ \
   return 1; \
}

/* Define all of the possible minors */
rpc_loc_api_cb_null_VER_svc(RPC_LOC_API_API_MAJOR_NUM, 0001);
rpc_loc_api_cb_null_VER_svc(RPC_LOC_API_API_MAJOR_NUM, 0002);
rpc_loc_api_cb_null_VER_svc(RPC_LOC_API_API_MAJOR_NUM, 0003);
rpc_loc_api_cb_null_VER_svc(RPC_LOC_API_API_MAJOR_NUM, 0004);

/*===========================================================================

FUNCTION loc_api_glue_init

DESCRIPTION
   Initiates the RPC client

RETURN VALUE
   1 for success
   0 for failure

===========================================================================*/
int loc_api_glue_init(void)
{
   if (loc_api_clnt == NULL)
   {
      /* Initialize data */
      int i;
      int pid = getpid();
      for (i = 0; i < LOC_API_CB_MAX_CLIENTS; i++)
      {
          loc_glue_callback_table[i].cb_id = i | (pid << 16);
          loc_glue_callback_table[i].cb_func = NULL;
          loc_glue_callback_table[i].handle = -1;
      }

      /* Print msg */
      ALOGV("Trying to create RPC client...\n");
      loc_api_clnt = clnt_create(NULL, LOC_APIPROG, LOC_APIVERS, NULL);
      ALOGV("Created loc_api_clnt ---- %x\n", (unsigned int)loc_api_clnt);

      if (loc_api_clnt == NULL)
      {
         ALOGE("Error: cannot create RPC client.\n");
         return 0;
      }

      /* Init RPC callbacks */
      loc_api_sync_call_init();

      int rc = loc_apicb_app_init();
      if (rc >= 0)
      {
         ALOGD("Loc API RPC client initialized.\n");
      }
      else {
         ALOGE("Loc API callback initialization failed.\n");
         return 0;
      }
   }

   return 1;
}

rpc_loc_client_handle_type loc_open (
      rpc_loc_event_mask_type       event_reg_mask,
      loc_event_cb_f_type      *event_callback
)
{
   LOC_GLUE_CHECK_INIT(rpc_loc_client_handle_type);

   rpc_loc_open_args args;
   args.event_reg_mask = event_reg_mask;

   int i;
   for (i = 0; i < LOC_API_CB_MAX_CLIENTS; i++)
   {
          if (loc_glue_callback_table[i].cb_func == event_callback)
          {
              ALOGW("Client already opened service (callback=0x%X)...\n",
                (unsigned int) event_callback);
              break;
          }
   }

   if (i == LOC_API_CB_MAX_CLIENTS)
   {
       for (i = 0; i < LOC_API_CB_MAX_CLIENTS; i++)
       {
           if (loc_glue_callback_table[i].cb_func == NULL)
           {
               loc_glue_callback_table[i].cb_func = event_callback;
               break;
           }
       }
   }

   if (i == LOC_API_CB_MAX_CLIENTS)
   {
      ALOGE("Too many clients opened at once...\n");
      return RPC_LOC_CLIENT_HANDLE_INVALID;
   }

   args.event_callback = loc_glue_callback_table[i].cb_id;
   ALOGV("cb_id=%d, func=0x%x", i, (unsigned int) event_callback);

   rpc_loc_open_rets rets;
   enum clnt_stat stat = RPC_SUCCESS;

   stat = RPC_FUNC_VERSION(rpc_loc_open_, RPC_LOC_OPEN_VERSION)(&args, &rets, loc_api_clnt);
   LOC_GLUE_CHECK_RESULT(stat, int32);

   /* save the handle in the table */
   loc_glue_callback_table[i].handle = (rpc_loc_client_handle_type) rets.loc_open_result;

   return (rpc_loc_client_handle_type) rets.loc_open_result;
}

int32 loc_close
(
      rpc_loc_client_handle_type handle
)
{
   LOC_GLUE_CHECK_INIT(int32);

   rpc_loc_close_args args;
   args.handle = handle;

   rpc_loc_close_rets rets;
   enum clnt_stat stat = RPC_SUCCESS;

   stat = RPC_FUNC_VERSION(rpc_loc_close_, RPC_LOC_CLOSE_VERSION)(&args, &rets, loc_api_clnt);

   /* Clean the client's callback function in callback table */
   int i;
   for (i = 0; i < LOC_API_CB_MAX_CLIENTS; i++)
   {
          if (loc_glue_callback_table[i].handle == handle)
          {
              /* Found the client */
              loc_glue_callback_table[i].cb_func = NULL;
              loc_glue_callback_table[i].handle = -1;
              break;
          }
   }

   if (i == LOC_API_CB_MAX_CLIENTS)
   {
       ALOGW("Handle not found (handle=%d)...\n", (int) handle);
   }

   LOC_GLUE_CHECK_RESULT(stat, int32);

   if (loc_api_clnt != NULL)
       clnt_destroy(loc_api_clnt);

   loc_api_clnt = NULL;

   return (int32) rets.loc_close_result;
}

int32 loc_start_fix
(
      rpc_loc_client_handle_type handle
)
{
   LOC_GLUE_CHECK_INIT(int32);

   rpc_loc_start_fix_args args;
   args.handle = handle;

   rpc_loc_start_fix_rets rets;
   enum clnt_stat stat = RPC_SUCCESS;

   stat = RPC_FUNC_VERSION(rpc_loc_start_fix_, RPC_LOC_START_FIX_VERSION)(&args, &rets, loc_api_clnt);
   LOC_GLUE_CHECK_RESULT(stat, int32);

   return (int32) rets.loc_start_fix_result;
}

int32 loc_stop_fix
(
      rpc_loc_client_handle_type handle
)
{
   LOC_GLUE_CHECK_INIT(int32);

   rpc_loc_stop_fix_args args;
   args.handle = handle;

   rpc_loc_stop_fix_rets rets;
   enum clnt_stat stat = RPC_SUCCESS;

   stat = RPC_FUNC_VERSION(rpc_loc_stop_fix_, RPC_LOC_STOP_FIX_VERSION)(&args, &rets, loc_api_clnt);
   LOC_GLUE_CHECK_RESULT(stat, int32);

   return (int32) rets.loc_stop_fix_result;
}

int32 loc_ioctl
(
      rpc_loc_client_handle_type           handle,
      rpc_loc_ioctl_e_type                 ioctl_type,
      rpc_loc_ioctl_data_u_type*           ioctl_data
)
{
   LOC_GLUE_CHECK_INIT(int32);

   rpc_loc_ioctl_args args;
   args.handle = handle;
   args.ioctl_data = ioctl_data;
   args.ioctl_type = ioctl_type;
   if (ioctl_data != NULL)
   {
      /* Assign ioctl union discriminator */
      ioctl_data->disc = ioctl_type;

      /* In case the user hasn't filled in other disc fields,
         automatically fill them in here */
      switch (ioctl_type)
      {
      case RPC_LOC_IOCTL_GET_API_VERSION:
         break;
      case RPC_LOC_IOCTL_SET_FIX_CRITERIA:
         break;
      case RPC_LOC_IOCTL_GET_FIX_CRITERIA:
         break;
      case RPC_LOC_IOCTL_INFORM_NI_USER_RESPONSE:
         break;
      case RPC_LOC_IOCTL_INJECT_PREDICTED_ORBITS_DATA:
         break;
      case RPC_LOC_IOCTL_QUERY_PREDICTED_ORBITS_DATA_VALIDITY:
         break;
      case RPC_LOC_IOCTL_QUERY_PREDICTED_ORBITS_DATA_SOURCE:
         break;
      case RPC_LOC_IOCTL_SET_PREDICTED_ORBITS_DATA_AUTO_DOWNLOAD:
         break;
      case RPC_LOC_IOCTL_INJECT_UTC_TIME:
         break;
      case RPC_LOC_IOCTL_INJECT_RTC_VALUE:
         break;
      case RPC_LOC_IOCTL_INJECT_POSITION:
         break;
      case RPC_LOC_IOCTL_QUERY_ENGINE_STATE:
         break;
      case RPC_LOC_IOCTL_INFORM_SERVER_OPEN_STATUS:
         break;
      case RPC_LOC_IOCTL_INFORM_SERVER_CLOSE_STATUS:
         break;
      case RPC_LOC_IOCTL_SET_ENGINE_LOCK:
         break;
      case RPC_LOC_IOCTL_GET_ENGINE_LOCK:
         break;
      case RPC_LOC_IOCTL_SET_SBAS_CONFIG:
         break;
      case RPC_LOC_IOCTL_GET_SBAS_CONFIG:
         break;
      case RPC_LOC_IOCTL_SET_NMEA_TYPES:
         break;
      case RPC_LOC_IOCTL_GET_NMEA_TYPES:
         break;
      case RPC_LOC_IOCTL_SET_CDMA_PDE_SERVER_ADDR:
      case RPC_LOC_IOCTL_SET_CDMA_MPC_SERVER_ADDR:
      case RPC_LOC_IOCTL_SET_UMTS_SLP_SERVER_ADDR:
      case RPC_LOC_IOCTL_SET_CUSTOM_PDE_SERVER_ADDR:
         args.ioctl_data->rpc_loc_ioctl_data_u_type_u.server_addr.addr_info.disc =
            args.ioctl_data->rpc_loc_ioctl_data_u_type_u.server_addr.addr_type;
         break;
      case RPC_LOC_IOCTL_GET_CDMA_PDE_SERVER_ADDR:
      case RPC_LOC_IOCTL_GET_CDMA_MPC_SERVER_ADDR:
      case RPC_LOC_IOCTL_GET_UMTS_SLP_SERVER_ADDR:
      case RPC_LOC_IOCTL_GET_CUSTOM_PDE_SERVER_ADDR:
         break;
      case RPC_LOC_IOCTL_SET_ON_DEMAND_LPM:
         break;
      case RPC_LOC_IOCTL_GET_ON_DEMAND_LPM:
         break;
      case RPC_LOC_IOCTL_DELETE_ASSIST_DATA:
         break;
      default:
         break;
      } /* switch */
   } /* ioctl_data != NULL */

   rpc_loc_ioctl_rets rets;
   enum clnt_stat stat = RPC_SUCCESS;

   stat = RPC_FUNC_VERSION(rpc_loc_ioctl_, RPC_LOC_IOCTL_VERSION)(&args, &rets, loc_api_clnt);
   LOC_GLUE_CHECK_RESULT(stat, int32);

   return (int32) rets.loc_ioctl_result;
}

/* Returns 0 if error */
int32 loc_api_null(void)
{
   LOC_GLUE_CHECK_INIT(int32);

   int32 rets;
   enum clnt_stat stat = RPC_SUCCESS;

   stat = RPC_FUNC_VERSION(rpc_loc_api_null_, RPC_LOC_API_NULL_VERSION)(NULL, &rets, loc_api_clnt);
   LOC_GLUE_CHECK_RESULT(stat, int32);

   return (int32) rets;
}
