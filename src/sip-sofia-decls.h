#ifndef _SIP_SOFIA_DECLS_H_
#define _SIP_SOFIA_DECLS_H_

/* note: As one Sofia-SIP NUA instance is created per SIP connection,
 *       SIPConnection is used as the primary context pointer. See
 *       {top}/docs/design.txt for further information.
 *
 *       Each NUA handle representing a call is mapped as follows:
 *       - A SIPMediaChannel has a pointer to a call NUA handle, which may
 *         start as NULL.
 *       - A call NUA handle has hmagic, which is either a pointer to a
 *         SIPMediaChannel, or NULL.
 *       - When the media channel is created because of an incoming call,
 *         its NUA handle is initialized to the call's NUA handle
 *       - When the media channel is created by user request (for an outgoing
 *         call), its NUA handle is initially NULL, then is set to the call's
 *         NUA handle once the call actually starts
 *
 *       In either case, as soon as the SIPMediaChannel's NUA handle becomes
 *       non-NULL, the NUA handle's hmagic is set to the SIPMediaChannel.
 *
 *       The NUA handle survives at least as long as the SIPMediaChannel.
 *       When the SIPMediaChannel is closed, the NUA handle's hmagic is set
 *       to the special value SIP_NH_EXPIRED.
 */

#define NUA_MAGIC_T      struct _SIPConnectionSofia
#define SU_ROOT_MAGIC_T  struct _SIPConnectionManager
#define SU_TIMER_ARG_T   struct _SIPConnection
#define NUA_HMAGIC_T     void

#include <sofia-sip/nua.h>
#include <sofia-sip/su.h>
#include <sofia-sip/su_glib.h>
#include <sofia-sip/sdp.h>
#include <sofia-sip/tport_tag.h>
#include <sofia-sip/stun_tag.h>
#include <sofia-sip/sresolv.h>

G_BEGIN_DECLS

/* a magical distinct value for nua_hmagic_t */
extern NUA_HMAGIC_T * const _sip_nh_expired;
#define SIP_NH_EXPIRED (_sip_nh_expired)

G_END_DECLS

#endif /* _SIP_SOFIA_DECLS_H_*/
