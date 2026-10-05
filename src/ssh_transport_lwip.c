/* SPDX-License-Identifier: MIT
 * All protocol parsing and cryptography run in ssh_transport_poll(), never
 * in these raw lwIP callbacks. TCP backpressure retains unconsumed pbufs. */
#include "ssh_transport_internal.h"
#include "lwip/tcp.h"
#include "pico/rand.h"
#include "pico/time.h"
#include <string.h>

static struct tcp_pcb *listener, *client;
static bool pending_accept, peer_closed;

static void error_callback(void *arg, err_t error) {
    (void)arg; (void)error;
    client=NULL; peer_closed=true; pending_accept=false;
}
static err_t abort_receive(struct tcp_pcb *pcb,struct pbuf *p) {
    /* A recv callback owns p on ERR_OK/ERR_ABRT, but lwIP retains it on ERR_MEM.
     * Abort rather than retrying an impossible chain or a partly copied chain.
     * Protocol/crypto cleanup remains deferred to ssh_platform_poll(). */
    tcp_arg(pcb,NULL); tcp_recv(pcb,NULL); tcp_err(pcb,NULL);
    if(client==pcb) client=NULL;
    pending_accept=false; peer_closed=true;
    if(p) pbuf_free(p);
    tcp_abort(pcb);
    return ERR_ABRT;
}
static err_t receive_callback(void *arg,struct tcp_pcb *pcb,struct pbuf *p,err_t error) {
    (void)arg;
    if(error!=ERR_OK) return abort_receive(pcb,p);
    if(!p) { peer_closed=true; return ERR_OK; }
    if(p->tot_len>SSH_TRANSPORT_WIRE_CAPACITY) return abort_receive(pcb,p);
    if(pending_accept || p->tot_len>ssh_transport_wire_receive_space()) return ERR_MEM;
    for(struct pbuf *part=p;part;part=part->next) {
        if(!ssh_transport_wire_receive(part->payload,part->len)) return abort_receive(pcb,p);
    }
    tcp_recved(pcb,p->tot_len); pbuf_free(p); return ERR_OK;
}
static void close_client(bool abort_now) {
    struct tcp_pcb *pcb=client; client=NULL; pending_accept=false;
    if(pcb) {
        tcp_arg(pcb,NULL); tcp_recv(pcb,NULL); tcp_err(pcb,NULL);
        if(abort_now || tcp_close(pcb)!=ERR_OK) tcp_abort(pcb);
    }
    ssh_transport_wire_closed();
}
static err_t accept_callback(void *arg,struct tcp_pcb *pcb,err_t error) {
    (void)arg;
    if(error!=ERR_OK || !pcb) return error;
    if(client) { tcp_abort(pcb); return ERR_ABRT; }
    client=pcb; pending_accept=true;
    tcp_nagle_disable(pcb); tcp_recv(pcb,receive_callback); tcp_err(pcb,error_callback);
    return ERR_OK;
}
bool ssh_platform_listen(void) {
    if(listener) return true;
    struct tcp_pcb *pcb=tcp_new_ip_type(IPADDR_TYPE_V4);
    if(!pcb) return false;
    if(tcp_bind(pcb,IP_ANY_TYPE,22)!=ERR_OK) { tcp_close(pcb); return false; }
    listener=tcp_listen_with_backlog(pcb,1);
    if(!listener) { tcp_close(pcb); return false; }
    tcp_accept(listener,accept_callback); return true;
}
void ssh_platform_stop(void) {
    if(listener) { tcp_accept(listener,NULL); tcp_close(listener); listener=NULL; }
    if(client || pending_accept || peer_closed) close_client(true);
    peer_closed=false;
}
void ssh_platform_poll(void) {
    if(peer_closed) {
        peer_closed=false;
        /* An old error callback may be followed by accept before this poll.
         * Clear the old protocol state without closing the new pending PCB. */
        if(pending_accept) ssh_transport_wire_closed();
        else { close_client(false); return; }
    }
    if(pending_accept) {
        pending_accept=false;
        if(!ssh_transport_wire_accept()) { close_client(true); return; }
    }
    if(!client) return;
    size_t length=0; const uint8_t *data=ssh_transport_wire_output(&length);
    size_t available=tcp_sndbuf(client);
    if(length>available) length=available;
    if(length>1460) length=1460;
    if(length && tcp_write(client,data,(u16_t)length,TCP_WRITE_FLAG_COPY)==ERR_OK) {
        ssh_transport_wire_sent(length); tcp_output(client);
    }
    if(ssh_transport_wire_should_close()) close_client(false);
}
void ssh_platform_random(uint8_t *out,size_t length) {
    while(length) {
        uint32_t value=get_rand_32(); size_t n=length<sizeof(value)?length:sizeof(value);
        memcpy(out,&value,n); out+=n; length-=n;
    }
}
uint32_t ssh_platform_milliseconds(void) { return to_ms_since_boot(get_absolute_time()); }
