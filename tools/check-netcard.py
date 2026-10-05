#!/usr/bin/env python3
"""Exercise the actual network-card driver with deterministic lwIP/clock fixtures.

The public HTTP parser is linked unchanged. Live TLS/WiFi tests remain separate.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = re.sub(r'^#include .*$', '', (root / 'src/mii_netcard.c').read_text(), flags=re.M)
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "net_http.h"
#include "wifi_config.h"
#define NETCARD_TLS 0
#define NETCARD_TLS_VERIFY 0
#define NETCARD_REALTIME 0
#define NETCARD_WEB_CONTROL 0
#define NETCARD_SSH 0
#define MII_DEBUG_PRINTF(...) ((void)0)
#define MI_DRIVER_REGISTER(x)
#define CYW43_ITF_STA 0
#define CYW43_LINK_UP 3
#define CYW43_AUTH_WPA2_AES_PSK 1
#define CYW43_AUTH_OPEN 0
#define CYW43_LINK_BADAUTH -3
#define CYW43_LINK_NONET -2
#define IPADDR_TYPE_V4 0
#define LWIP_DNS_ADDRTYPE_IPV4 0
#define TCP_WRITE_FLAG_COPY 1
typedef int err_t;
typedef uint16_t u16_t;
enum { ERR_OK=0, ERR_ABRT=-1, ERR_INPROGRESS=-2, ERR_MEM=-3 };
typedef struct { unsigned value; } ip_addr_t;
typedef struct {} mii_t;
struct mii_slot_t { void *drv_priv; };
typedef struct { const char *name, *desc; int (*init)(mii_t *, struct mii_slot_t *);
    uint8_t (*access)(mii_t *,struct mii_slot_t *,uint16_t,uint8_t,bool); } mii_slot_drv_t;
struct pbuf { struct pbuf *next; void *payload; u16_t len, tot_len; };
struct altcp_pcb { void *arg; err_t (*recv)(void *,struct altcp_pcb *,struct pbuf *,err_t);
    void (*err)(void *,err_t); err_t (*connected)(void *,struct altcp_pcb *,err_t); bool closed; };
static struct altcp_pcb connections[100];
static unsigned opened, closed, aborted, freed;
static bool close_fails;
static uint32_t now;
static char sent[NH_REQUEST_MAX], dns_name[254];
static err_t dns_result = ERR_OK;
static void (*dns_callback)(const char *, const ip_addr_t *, void *);
static void *dns_arg;
static int cyw43_state;
static void *netif_default;
static uint32_t time_us_32(void) { return now; }
static unsigned radio_inits, joins;
static unsigned hotspot_starts;
static bool hotspot_ok=true, hotspot_active;
static enum wifi_mode config_mode=WIFI_MODE_STATION;
static enum wifi_config_status config_status = WIFI_CONFIG_READY;
enum wifi_config_status wifi_config_load(wifi_config *out) {
    memset(out,0,sizeof(*out));
    if(config_status==WIFI_CONFIG_READY){strcpy(out->ssid,"fixture");strcpy(out->password,"fixture-password");out->mode=config_mode;}
    return config_status;
}
static int cyw43_arch_init(void) { radio_inits++; return 0; }
static void cyw43_arch_poll(void) {}
static void cyw43_arch_enable_sta_mode(void) {}
static bool wifi_access_point_start(const wifi_config *config) {assert(config->mode==WIFI_MODE_HOTSPOT);hotspot_starts++;hotspot_active=hotspot_ok;return hotspot_active;}
static bool wifi_access_point_active(void) {return hotspot_active;}
static int cyw43_arch_wifi_connect_async(const char *a,const char *b,int c) { assert(!strcmp(a,"fixture")&&!strcmp(b,"fixture-password")); joins++; return 0; }
static int cyw43_tcpip_link_status(void *p,int n) { return CYW43_LINK_UP; }
static int cyw43_wifi_get_rssi(void *p,int32_t *r) { *r=-40;return 0; }
#define netif_ip4_addr(x) (x)
#define ip4addr_ntoa(x) "127.0.0.1"
static void altcp_arg(struct altcp_pcb *p,void *arg) { p->arg=arg; }
static void altcp_recv(struct altcp_pcb *p,err_t (*fn)(void *,struct altcp_pcb *,struct pbuf *,err_t)) { p->recv=fn; }
static void altcp_err(struct altcp_pcb *p,void (*fn)(void *,err_t)) { p->err=fn; }
static void altcp_poll(struct altcp_pcb *p,void *fn,int interval) {}
static err_t altcp_close(struct altcp_pcb *p) { if(close_fails)return ERR_MEM;assert(!p->closed);p->closed=true;closed++;return ERR_OK; }
static void altcp_abort(struct altcp_pcb *p) { assert(!p->closed);p->closed=true;aborted++; }
static void altcp_recved(struct altcp_pcb *p,u16_t n) {}
static void pbuf_free(struct pbuf *p) { freed++; }
static struct altcp_pcb *altcp_tcp_new_ip_type(int type) { assert(opened<100);return &connections[opened++]; }
static err_t altcp_connect(struct altcp_pcb *p,const ip_addr_t *ip,unsigned port,err_t (*fn)(void *,struct altcp_pcb *,err_t)) { p->connected=fn;return ERR_OK; }
static err_t altcp_write(struct altcp_pcb *p,const void *data,u16_t n,int flags) { assert(n<sizeof(sent));memcpy(sent,data,n);sent[n]=0;return ERR_OK; }
static void altcp_output(struct altcp_pcb *p) {}
static err_t dns_gethostbyname_addrtype(const char *host,ip_addr_t *ip,
    void (*fn)(const char *,const ip_addr_t *,void *),void *arg,int type) {
    strcpy(dns_name,host);dns_callback=fn;dns_arg=arg;ip->value=1;return dns_result;
}
'''+source+r'''
static void write_reg(int reg, unsigned value) { _netcard_access(NULL,NULL,0xc090+reg,value,true); }
static unsigned read_reg(int reg) { return _netcard_access(NULL,NULL,0xc090+reg,0,false); }
static void stage(const char *url) { write_reg(6,0);for(;*url;url++)write_reg(7,(uint8_t)*url); }
static void connect_ready(void) { struct altcp_pcb *p=s_nc.pcb;assert(p);assert(p->connected(p->arg,p,ERR_OK)==ERR_OK); }
static err_t reply(const void *data, size_t n) {
    struct altcp_pcb *p=s_nc.pcb;assert(p);
    struct pbuf buffer={.payload=(void *)data,.len=n,.tot_len=n};
    return p->recv(p->arg,p,data?&buffer:NULL,ERR_OK);
}
int main(void) {
    for(config_status=WIFI_CONFIG_MISSING;config_status<=WIFI_CONFIG_IO;config_status++) {
        if(config_status==WIFI_CONFIG_READY) continue;
        netcard_init();netcard_poll();assert(!radio_inits&&!joins&&!s_nc.init_ok);
        assert(netcard_wifi_status());
    }
    config_status=WIFI_CONFIG_READY;
    netcard_init();assert(radio_inits==1&&joins==1);assert(read_reg(6)==1&&read_reg(15)==2);
    stage("http://httpbin.org/uuid");write_reg(6,1);assert(read_reg(10)==NH_WIFI);
    s_nc.link=1;stage("bad-url");write_reg(6,1);assert(read_reg(10)==NH_URL);
    stage("");for(int i=0;i<512;i++)write_reg(7,'a');write_reg(6,1);assert(read_reg(10)==NH_URL_LONG);
    stage("http://httpbin.org/get?message=Hello");write_reg(6,1);assert(read_reg(1)==NC_FETCHING);
    unsigned count=opened;write_reg(6,1);assert(opened==count); /* busy start ignored */
    connect_ready();assert(strstr(sent,"message=Hello")&&!strstr(sent,"PRIVATE_TEST_KEY"));
    const char bytes[]="HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n\0\x80\n";
    assert(reply(bytes,sizeof(bytes)-1)==ERR_OK);assert(read_reg(1)==NC_READY&&read_reg(8)==200);
    assert(read_reg(3)==3&&read_reg(7)==0&&read_reg(3)==2&&read_reg(7)==128&&read_reg(7)==10);
    write_reg(2,0);assert(read_reg(2)==0&&read_reg(2)==0&&read_reg(2)==13); /* legacy text conversion */
    assert(read_reg(3)==0&&read_reg(7)==0);

    /* Cancelling a DNS query must not let its late callback connect a new request. */
    dns_result=ERR_INPROGRESS;stage("http://old.test/");write_reg(6,1);
    void *old_arg=dns_arg;netcard_cancel();assert(read_reg(10)==NH_CANCELLED);
    stage("http://new.test/");write_reg(6,1);count=opened;
    ip_addr_t ip={1};nc_dns_cb("old.test",&ip,old_arg);assert(opened==count&&!s_nc.pcb);
    nc_dns_cb(dns_name,&ip,dns_arg);connect_ready();
    write_reg(6,2);assert(read_reg(1)==NC_ERROR&&read_reg(10)==NH_CANCELLED);
    stage("http://timeout.test/");write_reg(6,1);now+=NC_TIMEOUT_US;netcard_poll();assert(read_reg(10)==NH_TIMEOUT);
    stage("http://missing.test/");write_reg(6,1);nc_dns_cb(dns_name,NULL,dns_arg);assert(read_reg(10)==NH_DNS);
    dns_result=ERR_OK;

    write_reg(0,1);assert(read_reg(10)==NH_UNSUPPORTED); /* no unverified TLS fallback */
    assert(!strcmp(s_nc.url.host,"httpbin.org"));
    stage("http://first.test/");write_reg(6,1);connect_ready();
    const char *redirect="HTTP/1.1 302 Found\r\nLocation: http://other.test/get\r\n\r\n";
    reply(redirect,strlen(redirect));assert(s_nc.follow&&read_reg(1)==NC_FETCHING);
    netcard_poll();connect_ready();assert(strstr(sent,"Host: other.test")&&!strstr(sent,"PRIVATE_TEST_KEY"));
    const char *ok="HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
    reply(ok,strlen(ok));assert(read_reg(1)==NC_READY);

    stage("http://httpbin.org/status/404");write_reg(6,1);connect_ready();
    const char *missing="HTTP/1.1 404 Missing\r\nContent-Length: 0\r\n\r\n";
    reply(missing,strlen(missing));assert(read_reg(1)==NC_READY&&read_reg(8)+256*read_reg(9)==404&&read_reg(10)==0);


    stage("http://loop.test/");write_reg(6,1);
    for(int i=0;i<4;i++){connect_ready();reply(redirect,strlen(redirect));netcard_poll();}
    assert(read_reg(1)==NC_ERROR&&read_reg(10)==NH_REDIRECT);
    stage("http://big.test/");write_reg(6,1);connect_ready();
    const char *big="HTTP/1.1 200 OK\r\nContent-Length: 4097\r\n\r\n";
    assert(reply(big,strlen(big))==ERR_ABRT&&read_reg(10)==NH_TOO_LARGE);
    stage("http://close.test/");write_reg(6,1);connect_ready();close_fails=true;
    assert(reply(ok,strlen(ok))==ERR_ABRT&&read_reg(1)==NC_READY);close_fails=false;

    /* Staging/validation, all verbs, protected headers, busy writes and raw bodies. */
    for(unsigned method=NH_POST;method<NH_METHOD_COUNT;method++) {
        stage("http://api.test/rows");write_reg(13,method);
        const char *headers="Content-Type: application/json\r\napikey: PUBLIC_FIXTURE\rPrefer: return=representation\n";
        for(const char *p=headers;*p;p++)write_reg(14,*p);
        const uint8_t body[]={0,128,'{','}'};
        for(unsigned i=0;i<sizeof(body);i++)write_reg(15,body[i]);
        write_reg(6,1);assert(read_reg(1)==NC_FETCHING);
        write_reg(13,NH_GET);write_reg(14,'x');write_reg(15,'x');
        assert(read_reg(13)==method&&s_nc.body_len==4&&s_nc.headers_len==strlen(headers));
        connect_ready();assert(strstr(sent,"Content-Length: 4\r\n")&&strstr(sent,"apikey: PUBLIC_FIXTURE"));
        const char *r="HTTP/1.1 307 Temporary Redirect\r\nLocation: http://other.test/\r\nContent-Length: 4\r\n\r\nWAIT";
        count=opened;reply(r,strlen(r));netcard_poll();
        assert(opened==count&&!s_nc.follow&&read_reg(1)==NC_READY&&read_reg(8)+256*read_reg(9)==307&&read_reg(3)==4);
    }
    stage("http://api.test/");const char *auth="Authorization: Bearer USER_FIXTURE";
    for(const char *p=auth;*p;p++)write_reg(14,*p);
    write_reg(6,1);connect_ready();count=opened;
    const char *r="HTTP/1.1 302 Found\r\nLocation: /elsewhere\r\nContent-Length: 0\r\n\r\n";
    reply(r,strlen(r));netcard_poll();assert(opened==count&&read_reg(1)==NC_READY);
    stage("http://api.test/");assert(!s_nc.headers_len&&!s_nc.body_len&&read_reg(13)==NH_GET);
    write_reg(13,255);write_reg(6,1);assert(read_reg(10)==NH_REQUEST);
    stage("http://api.test/");write_reg(15,'x');write_reg(6,1);assert(read_reg(10)==NH_REQUEST);
    stage("http://api.test/");for(int i=0;i<=NH_REQUEST_HEADERS_MAX;i++)write_reg(14,'x');
    write_reg(6,1);assert(read_reg(10)==NH_TOO_LARGE);
    stage("http://api.test/");write_reg(13,NH_POST);for(int i=0;i<=NH_REQUEST_BODY_MAX;i++)write_reg(15,0);
    write_reg(6,1);assert(read_reg(10)==NH_TOO_LARGE);
    stage("http://api.test/");write_reg(14,0);write_reg(6,1);assert(read_reg(10)==NH_TOO_LARGE);
    stage("http://api.test/");const char *owned="Content-Length: 999";
    for(const char *p=owned;*p;p++)write_reg(14,*p);
    count=opened;write_reg(6,1);assert(read_reg(10)==NH_REQUEST&&opened==count);
    assert(closed+aborted==opened);
    config_mode=WIFI_MODE_HOTSPOT;unsigned previous_joins=joins;
    netcard_init();assert(hotspot_starts==1&&s_nc.link&&joins==previous_joins);
    now+=NC_LINK_JOIN_TIMEOUT_US;netcard_poll();assert(!netcard_wifi_status()&&joins==previous_joins);
    hotspot_ok=false;netcard_init();assert(hotspot_starts==2&&!s_nc.link&&joins==previous_joins);
    assert(strstr(netcard_wifi_status(),"hotspot"));netcard_poll();assert(joins==previous_joins);
    puts("PASS: register protocol, request lifecycle, raw bytes, legacy compatibility, credentials, cancellation, stale DNS, timeouts, redirects and teardown.");
}
'''
with tempfile.TemporaryDirectory(prefix='badge-card-') as directory:
    p = Path(directory)
    (p/'check.c').write_text(fixture)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                    '-Wno-unused-variable','-g','-fsanitize=address,undefined',
                    '-I',str(root/'src'),str(p/'check.c'),
                    str(root/'src/net_http.c'),'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check')],check=True)
