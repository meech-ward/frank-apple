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
static size_t acknowledged;
#ifndef SSH_ADAPTER_REAL_ENGINE
static size_t wire_used,wire_closed_count,receive_calls,fail_receive_call;
static uint8_t wire_bytes[SSH_TRANSPORT_WIRE_CAPACITY];
static bool wire_active;
#endif
static bool close_fails;
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

#ifndef SSH_ADAPTER_REAL_ENGINE
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
#endif

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
    ssh_platform_poll();assert(client==pcb && !pending_accept);return pcb;
}
#ifndef SSH_ADAPTER_REAL_ENGINE
static void peer_error(void) {
    struct tcp_pcb *pcb=client;assert(pcb);tcp_err_fn fn=pcb->errf;void *arg=pcb->callback_arg;
    ++pcbs_freed;free(pcb);fn(arg,ERR_RST);assert(!client && peer_closed);
}
int main(void) {
    assert(ssh_platform_listen());struct tcp_pcb *pcb=connect_client();
    /* Callback takes ownership; bounded poll consumes and credits each byte once. */
    size_t frees=pbufs_freed;struct pbuf *p=chain(3,4);
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);
    assert(pbufs_freed==frees && !wire_used && !acknowledged && pending_receive==p);
    ssh_platform_poll();
    assert(pbufs_freed==frees+2 && wire_used==7 && acknowledged==7);
    assert(!memcmp(wire_bytes,"aaabbbb",7));
    /* Consume the prefix that completes a packet, retaining only its tail. */
    wire_used=SSH_TRANSPORT_WIRE_CAPACITY-4;p=chain(3,3);frees=pbufs_freed;
    size_t ack=acknowledged;
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);ssh_platform_poll();
    assert(wire_used==SSH_TRANSPORT_WIRE_CAPACITY && pbufs_freed==frees+1);
    assert(acknowledged==ack+4 && receive_offset==1 && pending_receive->len==3);
    assert(!memcmp(wire_bytes+SSH_TRANSPORT_WIRE_CAPACITY-4,"aaab",4));
    /* lwIP retains a second chain without copy or credit until ownership clears. */
    struct pbuf *refused=chain(2,2);size_t calls=receive_calls;
    assert(pcb->recv(NULL,pcb,refused,ERR_OK)==ERR_MEM);
    ssh_platform_poll();assert(receive_calls==calls && refused->ref==1 && acknowledged==ack+4);
    wire_used=0;ssh_platform_poll();
    assert(!pending_receive && pbufs_freed==frees+2 && wire_used==2);
    assert(acknowledged==ack+6 && !memcmp(wire_bytes,"bb",2));
    assert(pcb->recv(NULL,pcb,refused,ERR_OK)==ERR_OK);ssh_platform_poll();
    assert(pbufs_freed==frees+4 && wire_used==6 && acknowledged==ack+10);
    assert(!memcmp(wire_bytes,"bbaabb",6));
    /* A coalesced TCP chain may exceed the SSH wire buffer: drain with bounded work. */
    wire_used=0;p=chain(8192,4096);frees=pbufs_freed;ack=acknowledged;
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);
    for(unsigned i=0;i<4;++i) { ssh_platform_poll();assert(acknowledged==ack+2048*(i+1)); }
    assert(wire_used==8192 && pending_receive && pbufs_freed==frees+1);
    for(size_t i=0;i<wire_used;++i)assert(wire_bytes[i]=='a');
    ssh_platform_poll();assert(acknowledged==ack+8192);
    wire_used=0;ssh_platform_poll();ssh_platform_poll();
    assert(wire_used==4096 && !pending_receive && pbufs_freed==frees+2 && acknowledged==ack+12288);
    for(size_t i=0;i<wire_used;++i)assert(wire_bytes[i]=='b');
    /* A separately retained reference keeps its unchanged head/payload/links. */
    wire_used=0;p=chain(2,3);struct pbuf *tail=p->next;pbuf_ref(p);frees=pbufs_freed;
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);ssh_platform_poll();
    assert(!pending_receive && pbufs_freed==frees && p->ref==1 && p->next==tail);
    assert(p->len==2 && p->tot_len==5 && tail->ref==1 && !memcmp(p->payload,"aa",2));
    assert(pbuf_free(p)==2 && pbufs_freed==frees+2);
    /* Empty nodes cannot cause an unbounded poll loop. */
    p=buffer(0,0);for(unsigned i=0;i<8;++i)pbuf_cat(p,buffer(0,0));
    pbuf_cat(p,buffer(1,'z'));frees=pbufs_freed;wire_used=0;ack=acknowledged;
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);ssh_platform_poll();
    assert(pbufs_freed==frees+8 && pending_receive && acknowledged==ack);
    ssh_platform_poll();assert(pbufs_freed==frees+10 && !pending_receive && wire_bytes[0]=='z');
    /* A hypothetical partial-copy failure aborts instead of duplicating on retry. */
    wire_used=0;p=chain(3,3);frees=pbufs_freed;fail_receive_call=receive_calls+2;ack=acknowledged;
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);ssh_platform_poll();
    assert(pbufs_freed==frees+2 && !client && !wire_active && acknowledged==ack+3);
    fail_receive_call=0;pcb=connect_client();
    /* Receive errors release both owned and incoming chains, with deferred crypto cleanup. */
    p=chain(2,3);frees=pbufs_freed;size_t cleanups=wire_closed_count;
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);p=chain(3,4);
    assert(pcb->recv(NULL,pcb,p,ERR_RST)==ERR_ABRT);
    assert(pbufs_freed==frees+4 && wire_closed_count==cleanups && !pending_receive);
    ssh_platform_poll();assert(wire_closed_count==cleanups+1);pcb=connect_client();
    /* tcp_err releases a partly consumed chain; same-cycle accept preserves new data. */
    cleanups=wire_closed_count;p=chain(3,3);frees=pbufs_freed;wire_used=8191;
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);ssh_platform_poll();assert(receive_offset==1);
    peer_error();assert(pbufs_freed==frees+2 && !pending_receive);
    pcb=tcp_new_ip_type(IPADDR_TYPE_V4);
    assert(accept_handler(NULL,pcb,ERR_OK)==ERR_OK);p=chain(1,2);frees=pbufs_freed;
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);assert(pbufs_freed==frees);
    ssh_platform_poll();assert(pbufs_freed==frees+2 && client==pcb && wire_active);
    assert(wire_closed_count==cleanups+1 && wire_used==3 && !memcmp(wire_bytes,"abb",3));
    /* FIN cleanup plus normal and failed-close shutdown can all relisten cleanly. */
    assert(pcb->recv(NULL,pcb,NULL,ERR_OK)==ERR_OK);ssh_platform_poll();assert(!wire_active && !client);
    /* A newly accepted peer that closes before first poll must not retain the slot. */
    pcb=tcp_new_ip_type(IPADDR_TYPE_V4);assert(accept_handler(NULL,pcb,ERR_OK)==ERR_OK);
    p=chain(1,2);frees=pbufs_freed;assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);
    assert(pcb->recv(NULL,pcb,NULL,ERR_OK)==ERR_OK);ssh_platform_poll();
    assert(!wire_active && !client && !pending_accept && pbufs_freed==frees+2);
    pcb=connect_client();p=chain(3,3);frees=pbufs_freed;wire_used=8191;
    assert(pcb->recv(NULL,pcb,p,ERR_OK)==ERR_OK);ssh_platform_poll();
    ssh_platform_stop();assert(!listener && !client && !wire_active && !pending_receive && pbufs_freed==frees+2);
    assert(ssh_platform_listen());pcb=connect_client();close_fails=true;
    /* Listener close normally cannot fail in lwIP; keep this fault on active PCB only. */
    close_client(false);close_fails=false;assert(!client && !wire_active);ssh_platform_stop();
    assert(ssh_platform_listen());connect_client();ssh_platform_stop();
    assert(pcbs_allocated==pcbs_freed && pbufs_allocated==pbufs_freed && aborted>=4);
    printf("lwIP adapter: bounded partial chains, exact TCP credit, shared pbuf ownership, retry, errors, reconnect and shutdown passed (%zu pbuf nodes, %zu PCBs)\n",pbufs_freed,pcbs_freed);
}
#else
static void u32(uint8_t *out,uint32_t value) {
    out[0]=(uint8_t)(value>>24);out[1]=(uint8_t)(value>>16);
    out[2]=(uint8_t)(value>>8);out[3]=(uint8_t)value;
}
static void ignore_packet(uint8_t *out,size_t length) {
    assert(length>=16 && length%8==0);memset(out,0,length);
    u32(out,(uint32_t)length-4);out[4]=4;out[5]=2; /* padding length, SSH_MSG_IGNORE */
    u32(out+6,(uint32_t)length-14); /* SSH string length, excluding header/padding */
}
static void offer(const void *bytes,size_t length) {
    struct pbuf *p=buffer(length,0);memcpy(p->payload,bytes,length);
    assert(client->recv(NULL,client,p,ERR_OK)==ERR_OK);
}
int main(void) {
    const uint8_t seed[32]={1};
    assert(ssh_transport_init(seed,"test-only-password"));assert(ssh_transport_set_enabled(true));
    connect_client();
    const char banner[]="SSH-2.0-partial-chain-test\r\n";
    offer(banner,sizeof(banner)-1);ssh_transport_poll();
    assert(ssh_transport_wire_receive_space()==SSH_TRANSPORT_WIRE_CAPACITY);
    uint8_t packet[SSH_TRANSPORT_WIRE_CAPACITY];ignore_packet(packet,sizeof(packet));
    /* The real engine cannot consume an incomplete frame. The next TCP chain
     * both completes it and starts the next; refusing that chain deadlocks. */
    offer(packet,sizeof(packet)-4);
    for(unsigned i=0;i<3;++i)ssh_transport_poll();
    assert(ssh_transport_wire_receive_space()==4 && !pending_receive);
    struct pbuf *p=chain(3,3);memcpy(p->payload,packet+sizeof(packet)-4,3);
    memset(p->next->payload,0,3); /* final padding byte and two next-frame bytes */
    assert(client->recv(NULL,client,p,ERR_OK)==ERR_OK);
    ssh_transport_poll();
    assert(!pending_receive && client && !ssh_transport_wire_should_close());
    assert(ssh_transport_wire_receive_space()==SSH_TRANSPORT_WIRE_CAPACITY-2);
    assert(acknowledged==sizeof(banner)-1+sizeof(packet)+2);
    /* Finish this next valid packet, then send three coalesced frames larger
     * than the SSH wire buffer. Every frame must drain without a buffer resize. */
    ignore_packet(packet,4096);offer(packet+2,4096-2);
    for(unsigned i=0;i<3;++i)ssh_transport_poll();
    assert(ssh_transport_wire_receive_space()==SSH_TRANSPORT_WIRE_CAPACITY);
    uint8_t coalesced[12288];for(size_t i=0;i<sizeof(coalesced);i+=4096)ignore_packet(coalesced+i,4096);
    offer(coalesced,sizeof(coalesced));
    for(unsigned i=0;i<6;++i)ssh_transport_poll();
    assert(!pending_receive && client && !ssh_transport_wire_should_close());
    assert(ssh_transport_wire_receive_space()==SSH_TRANSPORT_WIRE_CAPACITY);
    assert(acknowledged==sizeof(banner)-1+sizeof(packet)+4096+sizeof(coalesced));
    ssh_transport_set_enabled(false);
    assert(!client && !listener && pcbs_allocated==pcbs_freed && pbufs_allocated==pbufs_freed);
    puts("Real SSH engine + lwIP: incomplete 8188/8192-byte packet followed by six-byte chain progresses; 12 KiB coalesced frames drain with exact credit");
}
#endif
