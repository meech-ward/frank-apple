/* SPDX-License-Identifier: MIT
 * Actual adapter and actual SDK pbuf chains, with a controllable TCP transport.
 * ASan catches ownership errors; custom pbuf destructors assert every allocation
 * is freed exactly once. This does not claim hardware/CYW43 execution. */
#include "../src/ssh_transport_lwip.c"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

const ip_addr_t ip_addr_any={0};
static size_t pcbs_allocated,pcbs_freed,pbufs_allocated,pbufs_freed;
static size_t wire_used,wire_closed_count,receive_calls,fail_receive_call,acknowledged;
static uint8_t wire_bytes[SSH_TRANSPORT_WIRE_CAPACITY];
static bool wire_active,close_fails;
static tcp_accept_fn accept_handler;
static unsigned aborted;

struct tcp_pcb *tcp_new_ip_type(u8_t type) {
    (void)type;struct tcp_pcb *pcb=calloc(1,sizeof(*pcb));assert(pcb);
    ++pcbs_allocated;pcb->snd_buf=4096;return pcb;
}
void tcp_arg(struct tcp_pcb *pcb,void *arg) { pcb->callback_arg=arg; }
void tcp_recv(struct tcp_pcb *pcb,tcp_recv_fn fn) { pcb->recv=fn; }
void tcp_err(struct tcp_pcb *pcb,tcp_err_fn fn) { pcb->errf=fn; }
void tcp_accept(struct tcp_pcb *pcb,tcp_accept_fn fn) { (void)pcb;accept_handler=fn; }
err_t tcp_bind(struct tcp_pcb *pcb,const ip_addr_t *ip,u16_t port) {
    (void)pcb;(void)ip;assert(port==22);return ERR_OK;
}
struct tcp_pcb *tcp_listen_with_backlog(struct tcp_pcb *pcb,u8_t backlog) {
    assert(backlog==1);pcb->state=LISTEN;return pcb;
}
void tcp_recved(struct tcp_pcb *pcb,u16_t len) { assert(pcb==client);acknowledged+=len; }
err_t tcp_write(struct tcp_pcb *pcb,const void *data,u16_t length,u8_t flags) {
    (void)pcb;(void)data;(void)length;assert(flags==TCP_WRITE_FLAG_COPY);return ERR_OK;
}
err_t tcp_output(struct tcp_pcb *pcb) { (void)pcb;return ERR_OK; }
err_t tcp_close(struct tcp_pcb *pcb) {
    if(close_fails)return ERR_MEM;
    ++pcbs_freed;free(pcb);return ERR_OK;
}
void tcp_abort(struct tcp_pcb *pcb) {
    tcp_err_fn callback=pcb->errf;void *arg=pcb->callback_arg;
    ++aborted;++pcbs_freed;free(pcb);if(callback)callback(arg,ERR_ABRT);
}
uint32_t get_rand_32(void) { return 123; }
absolute_time_t get_absolute_time(void) { return 456; }
uint32_t to_ms_since_boot(absolute_time_t value) { return (uint32_t)value; }

bool ssh_transport_wire_accept(void) {
    if(wire_active)return false;
    wire_active=true;wire_used=0;return true;
}
size_t ssh_transport_wire_receive_space(void) { return sizeof(wire_bytes)-wire_used; }
bool ssh_transport_wire_receive(const uint8_t *data,size_t length) {
    ++receive_calls;
    if(receive_calls==fail_receive_call)return false;
    if(!wire_active || length>sizeof(wire_bytes)-wire_used)return false;
    memcpy(wire_bytes+wire_used,data,length);wire_used+=length;return true;
}
const uint8_t *ssh_transport_wire_output(size_t *length) { *length=0;return wire_bytes; }
void ssh_transport_wire_sent(size_t length) { (void)length; }
bool ssh_transport_wire_should_close(void) { return false; }
void ssh_transport_wire_closed(void) {
    ++wire_closed_count;wire_active=false;wire_used=0;
}

struct test_pbuf {
    struct pbuf_custom custom;
    unsigned char *data;
};
static void free_test_pbuf(struct pbuf *p) {
    struct test_pbuf *node=(struct test_pbuf*)p;
    ++pbufs_freed;free(node->data);free(node);
}
static struct pbuf *buffer(size_t length,uint8_t fill) {
    struct test_pbuf *node=calloc(1,sizeof(*node));assert(node);
    node->data=malloc(length?length:1);assert(node->data);memset(node->data,fill,length);
    node->custom.custom_free_function=free_test_pbuf;
    struct pbuf *p=pbuf_alloced_custom(PBUF_RAW,(u16_t)length,PBUF_REF,&node->custom,node->data,(u16_t)length);
    assert(p);++pbufs_allocated;return p;
}
static struct pbuf *chain(size_t a,size_t b) {
    struct pbuf *p=buffer(a,'a'),*tail=buffer(b,'b');pbuf_cat(p,tail);return p;
}
static struct tcp_pcb *connect_client(void) {
    struct tcp_pcb *pcb=tcp_new_ip_type(IPADDR_TYPE_V4);
    assert(accept_handler(NULL,pcb,ERR_OK)==ERR_OK);assert(client==pcb);
    ssh_platform_poll();assert(wire_active && client==pcb);return pcb;
}
static void peer_error(void) {
    struct tcp_pcb *pcb=client;assert(pcb);tcp_err_fn fn=pcb->errf;void *arg=pcb->callback_arg;
    ++pcbs_freed;free(pcb);fn(arg,ERR_RST);assert(!client && peer_closed);
}
int main(void) {
    assert(ssh_platform_listen());struct tcp_pcb *pcb=connect_client();
    /* Chained delivery consumes every byte and each node exactly once. */
    size_t frees=pbufs_freed;struct pbuf *p=chain(3,4);
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);
    assert(pbufs_freed==frees+2 && wire_used==7 && acknowledged==7);
    assert(!memcmp(wire_bytes,"aaabbbb",7));
    /* Temporary capacity shortage retains the whole chain without any copy. */
    wire_used=SSH_TRANSPORT_WIRE_CAPACITY-4;p=chain(3,3);frees=pbufs_freed;
    size_t calls=receive_calls;
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_MEM);
    assert(receive_calls==calls && pbufs_freed==frees && p->ref==1);
    wire_used=0;assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);
    assert(pbufs_freed==frees+2 && wire_used==6 && !memcmp(wire_bytes,"aaabbb",6));
    /* An impossible chain is aborted, not refused forever. No callback runs crypto cleanup. */
    p=chain(4096,4097);frees=pbufs_freed;size_t cleanups=wire_closed_count;
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_ABRT);
    assert(pbufs_freed==frees+2 && !client && peer_closed && wire_closed_count==cleanups);
    /* Reaccept before deferred cleanup preserves the new PCB and resets only old protocol. */
    pcb=connect_client();assert(wire_closed_count==cleanups+1 && client==pcb);
    /* A hypothetical partial-copy failure aborts instead of duplicating on retry. */
    p=chain(3,3);frees=pbufs_freed;fail_receive_call=receive_calls+2;
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_ABRT);assert(pbufs_freed==frees+2);
    ssh_platform_poll();assert(!wire_active);fail_receive_call=0;pcb=connect_client();
    /* Non-OK receive errors free the received chain and PCB exactly once. */
    p=chain(2,3);frees=pbufs_freed;cleanups=wire_closed_count;
    assert(pcb->recv(NULL,pcb,p,ERR_RST)==ERR_ABRT);
    assert(pbufs_freed==frees+2 && wire_closed_count==cleanups);
    ssh_platform_poll();assert(wire_closed_count==cleanups+1);pcb=connect_client();
    /* Independent tcp_err frees PCB before callback; a same-cycle accept is safe. */
    cleanups=wire_closed_count;peer_error();pcb=connect_client();
    assert(wire_closed_count==cleanups+1 && wire_active);
    /* New data before poll initializes a pending accept is retained and later delivered. */
    peer_error();ssh_platform_poll();pcb=tcp_new_ip_type(IPADDR_TYPE_V4);
    assert(accept_handler(NULL,pcb,ERR_OK)==ERR_OK);p=chain(1,2);frees=pbufs_freed;
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_MEM);assert(pbufs_freed==frees);
    ssh_platform_poll();assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);assert(pbufs_freed==frees+2);
    /* FIN cleanup plus normal and failed-close shutdown can all relisten cleanly. */
    assert(pcb->recv(NULL,pcb,NULL,ERR_OK)==ERR_OK);ssh_platform_poll();assert(!wire_active && !client);
    pcb=connect_client();(void)pcb;ssh_platform_stop();assert(!listener && !client && !wire_active);
    assert(ssh_platform_listen());pcb=connect_client();close_fails=true;
    /* Listener close normally cannot fail in lwIP; keep this fault on active PCB only. */
    close_client(false);close_fails=false;assert(!client && !wire_active);ssh_platform_stop();
    assert(ssh_platform_listen());connect_client();ssh_platform_stop();
    assert(pcbs_allocated==pcbs_freed && pbufs_allocated==pbufs_freed && aborted>=4);
    printf("lwIP adapter: chained pbuf ownership, oversize/error abort, retry, partial-copy failure, reconnect and shutdown passed (%zu pbuf nodes, %zu PCBs)\n",pbufs_freed,pcbs_freed);
}
