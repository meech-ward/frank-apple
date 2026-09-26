/* Small, bounded HTTP controller. No sockets/threads: lwIP and the emulator use core 0.
 * Four clients, one request per connection, 512-byte commands / 1 KiB upload chunks, 10s idle timeout.
 * Physical/session opt-in; same-origin custom-header POSTs prevent ordinary cross-site forms.
 * Intended for trusted LANs, not exposed Internet service. Disk imports use bounded chunks and a separate staging file; no firmware endpoint.
 */
#include "web_control.h"
#include "typing.h"
#include "disk_ui.h"
#include "disk_library.h"
#include "netcard.h"
#include "debug_log.h"
#include "pico/time.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include "web_page.h"

#define CLIENTS 4
#define REQUEST_CAP (1024 + LIBRARY_CHUNK + 8)
#define BODY_CAP 6400
#define INPUT_CAP 512

typedef struct {
    struct tcp_pcb *pcb;
    char request[REQUEST_CAP + 1];
    size_t used;
    char header[256];
    char body[BODY_CAP];
    const unsigned char *payload;
    size_t header_len, body_len, queued, acked;
    uint64_t activity;
    bool responding;
} web_client_t;

static web_client_t clients[CLIENTS];
static struct tcp_pcb *listener;
static bool enabled;
static uint8_t pending_key;
static char screen_text[1945];
static char screen_inverse[1945];
static struct {
    char name[LIBRARY_NAME_MAX + 1];
    int drive;
    bool boot, pending, ok;
    uint32_t id;
    uint64_t after;
} mount_request;

static uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static bool decimal(const char *p,size_t n,uint32_t *value) {
    *value=0;
    if(!n || n>10) return false;
    for(size_t i=0;i<n;i++) {
        if(p[i]<'0' || p[i]>'9' || *value>(UINT32_MAX-(unsigned)(p[i]-'0'))/10) return false;
        *value=*value*10+(unsigned)(p[i]-'0');
    }
    return true;
}

static err_t release_client(web_client_t *c, bool abort_now) {
    struct tcp_pcb *pcb = c->pcb;
    c->pcb = NULL;
    if (!pcb) return ERR_OK;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_poll(pcb, NULL, 0);
    if (!abort_now && tcp_close(pcb) == ERR_OK) return ERR_OK;
    tcp_abort(pcb);
    return ERR_ABRT;
}

static void send_more(web_client_t *c) {
    if (!c->pcb || !c->responding) return;
    size_t total = c->header_len + c->body_len;
    while (c->queued < total) {
        const unsigned char *p;
        size_t remain;
        if (c->queued < c->header_len) {
            p = (const unsigned char *)c->header + c->queued;
            remain = c->header_len - c->queued;
        } else {
            size_t off = c->queued - c->header_len;
            p = c->payload + off;
            remain = c->body_len - off;
        }
        size_t n = remain < 512 ? remain : 512;
        if (n > tcp_sndbuf(c->pcb)) n = tcp_sndbuf(c->pcb);
        if (!n || tcp_write(c->pcb, p, (u16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK) break;
        c->queued += n;
    }
    tcp_output(c->pcb);
}

static void respond(web_client_t *c, int code, const char *reason,
                    const char *type, const unsigned char *body, size_t len) {
    c->body_len = len;
    c->payload = body;
    c->header_len = (size_t)snprintf(c->header, sizeof(c->header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
        "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n",
        code, reason, type, (unsigned)len);
    c->responding = true;
    send_more(c);
}

static void message(web_client_t *c, int code, const char *reason, const char *msg) {
    snprintf(c->body, sizeof(c->body), "%s\n", msg);
    respond(c, code, reason, "text/plain; charset=utf-8",
            (const unsigned char *)c->body, strlen(c->body));
}

// Library handlers write their bounded reply directly into this client's buffer.
static void library_reply(web_client_t *c,int status) {
    respond(c,status,status<400?"OK":"Error",status<400?"application/json":"text/plain; charset=utf-8",
            (const unsigned char *)c->body,strlen(c->body));
}
static void mount_reply(web_client_t *c,int status) {
    snprintf(c->body,sizeof(c->body),"{\"id\":%lu,\"pending\":%s,\"ok\":%s}",
             (unsigned long)mount_request.id,mount_request.pending?"true":"false",mount_request.ok?"true":"false");
    library_reply(c,status);
}
static void disk_request(web_client_t *c,const char *path,const char *body,size_t length) {
    int status=400; uint32_t id;
    if(mount_request.pending) { message(c,409,"Conflict","Wait for the disk to finish loading."); return; }
    if(!strcmp(path,"/upload/start")) {
        const char *sep=memchr(body,'\n',length); uint32_t size;
        if(!sep || !decimal(body,(size_t)(sep-body),&size) || length-(size_t)(sep+1-body)>LIBRARY_NAME_MAX || memchr(body,0,length)) {
            message(c,400,"Bad Request","Send size, a line break, and the disk filename."); return;
        }
        char name[LIBRARY_NAME_MAX+1]; size_t n=length-(size_t)(sep+1-body);
        memcpy(name,sep+1,n); name[n]=0;
        status=library_begin(name,size,c->body,sizeof(c->body));
    } else if(!strcmp(path,"/upload/chunk")) {
        if(length<=8) { message(c,400,"Bad Request","Empty upload chunk."); return; }
        const unsigned char *p=(const unsigned char *)body;
        status=library_chunk(read_le32(p),read_le32(p+4),p+8,length-8,c->body,sizeof(c->body));
    } else if(!strcmp(path,"/upload/finish") || !strcmp(path,"/upload/cancel")) {
        if(!decimal(body,length,&id)) { message(c,400,"Bad Request","Invalid upload id."); return; }
        status=!strcmp(path,"/upload/finish")?library_finish(id,c->body,sizeof(c->body)):library_cancel(id,c->body,sizeof(c->body));
    } else if(!strcmp(path,"/disks/mount")) {
        // Body is boot|insert, newline, zero-based drive, newline, filename.
        const char *sep=memchr(body,'\n',length);
        if(!sep || (size_t)(sep-body)+4>length || memchr(body,0,length)) { message(c,400,"Bad Request","Invalid disk selection."); return; }
        bool boot=sep-body==4 && !memcmp(body,"boot",4);
        if(!boot && !(sep-body==6 && !memcmp(body,"insert",6))) { message(c,400,"Bad Request","Choose Boot or Insert."); return; }
        int drive=sep[1]-'0'; size_t n=length-(size_t)(sep+3-body);
        if((drive!=0 && drive!=1) || sep[2]!='\n' || (boot && drive!=0) || n>LIBRARY_NAME_MAX) { message(c,400,"Bad Request","Invalid drive or filename."); return; }
        char name[LIBRARY_NAME_MAX+1]; memcpy(name,sep+3,n); name[n]=0;
        if(!library_name_valid(name)) { message(c,400,"Bad Request","Unsupported disk filename."); return; }
        if(library_uploading() || typing_pending() || pending_key) { message(c,409,"Conflict","Wait for uploading or typing to finish before changing disks."); return; }
        strcpy(mount_request.name,name); mount_request.drive=drive; mount_request.boot=boot;
        mount_request.pending=true; mount_request.ok=false; mount_request.id++;
        // Let the acceptance response leave lwIP before a first mount converts tracks.
        mount_request.after=time_us_64()+250000;
        mount_reply(c,202); return;
    } else { message(c,404,"Not Found","Not found"); return; }
    library_reply(c,status);
}

static void state_reply(web_client_t *c) {
    int rt = 0;
    size_t n = (size_t)snprintf(c->body, sizeof(c->body),
        "{\"queued\":%u,\"menu\":%s,\"graphics\":%s,\"realtime\":%d,\"basic_prompt\":%s,\"columns\":%u,\"screen\":\"",
        (unsigned)typing_pending(), disk_ui_is_visible() ? "true" : "false",
        remote_control_graphics() ? "true" : "false", rt,
        remote_control_basic_prompt() ? "true" : "false", remote_control_columns());
    size_t len = remote_control_screen(screen_text, screen_inverse, sizeof(screen_text));
    for (size_t i = 0; i < len && n + 8 < sizeof(c->body); ++i) {
        unsigned char ch = (unsigned char)screen_text[i];
        if (ch == '\n') { c->body[n++] = '\\'; c->body[n++] = 'n'; }
        else if (ch == '"' || ch == '\\') { c->body[n++] = '\\'; c->body[n++] = ch; }
        else c->body[n++] = ch;
    }
    memcpy(c->body+n, "\",\"inverse\":\"", 13); n += 13;
    memcpy(c->body+n, screen_inverse, len); n += len;
    c->body[n++] = '"'; c->body[n++] = '}'; c->body[n] = 0;
    respond(c, 200, "OK", "application/json", (const unsigned char *)c->body, n);
}

static uint8_t key_code(const char *p, size_t n) {
    static const struct { const char *name; uint8_t code; } keys[] = {
        {"enter",13}, {"escape",27}, {"space",32}, {"up",11}, {"down",10},
        {"left",8}, {"right",21}, {"break",3}, {"menu",29}
    };
    for (size_t i=0; i<sizeof(keys)/sizeof(keys[0]); ++i)
        if (strlen(keys[i].name)==n && !memcmp(p,keys[i].name,n)) return keys[i].code;
    return 0;
}

static void parse_request(web_client_t *c) {
    char *end = strstr(c->request, "\r\n\r\n");
    if (!end) {
        if (c->used > 1024) message(c,431,"Headers Too Large","Headers too large");
        return;
    }
    size_t header_bytes = (size_t)(end - c->request) + 4;
    if (header_bytes > 1024) { message(c,431,"Headers Too Large","Headers too large"); return; }
    char method[8], path[64], version[16], extra;
    char *line_end = strstr(c->request, "\r\n");
    if (!line_end || line_end - c->request >= 96) { message(c,400,"Bad Request","Bad request line"); return; }
    char first[96]; memcpy(first,c->request,(size_t)(line_end-c->request)); first[line_end-c->request]=0;
    if (sscanf(first,"%7s %63s %15s %c",method,path,version,&extra)!=3 ||
        (strcmp(version,"HTTP/1.1") && strcmp(version,"HTTP/1.0"))) {
        message(c,400,"Bad Request","Bad request line"); return;
    }
    size_t input_limit=!strcmp(path,"/upload/chunk")?LIBRARY_CHUNK+8:INPUT_CAP;
    size_t length=0; bool have_length=false, control=false;
    for (char *p=line_end+2; p<end; ) {
        char *q=strstr(p,"\r\n");
        if (!q || q>end) { message(c,400,"Bad Request","Bad header"); return; }
        char *colon=memchr(p,':',(size_t)(q-p));
        if (!colon) { message(c,400,"Bad Request","Bad header"); return; }
        size_t name_len=(size_t)(colon-p);
        char *v=colon+1; while(v<q && (*v==' ' || *v=='\t')) ++v;
        char *vend=q; while(vend>v && (vend[-1]==' ' || vend[-1]=='\t')) --vend;
        if (name_len==14 && !strncasecmp(p,"Content-Length",14)) {
            if(have_length || v==vend) { message(c,400,"Bad Request","Bad length"); return; }
            have_length=true;
            for(char *d=v; d<vend; ++d) {
                if(!isdigit((unsigned char)*d)) { message(c,400,"Bad Request","Bad length"); return; }
                length=length*10+(unsigned)(*d-'0');
                if(length>input_limit) { message(c,413,"Payload Too Large","Request exceeds this endpoint's size limit."); return; }
            }
        } else if(name_len==17 && !strncasecmp(p,"Transfer-Encoding",17)) {
            message(c,400,"Bad Request","Chunked requests are not supported"); return;
        } else if(name_len==16 && !strncasecmp(p,"X-Apple2-Control",16)) {
            control=vend-v==1 && *v=='1';
        }
        p=q+2;
    }
    if(c->used<header_bytes+length) return;
    const char *body=c->request+header_bytes;
    if(!strcmp(method,"GET")) {
        if(!strcmp(path,"/")) respond(c,200,"OK","text/html; charset=utf-8",web_page,sizeof(web_page));
        else if(!strcmp(path,"/state")) state_reply(c);
        else if(!strcmp(path,"/mount")) mount_reply(c,200);
        else if(!strcmp(path,"/disks") || !strncmp(path,"/disks?offset=",14)) {
            uint32_t offset=0;
            if(path[6] && !decimal(path+14,strlen(path+14),&offset)) { message(c,400,"Bad Request","Invalid list position."); return; }
            library_reply(c,library_list(offset,c->body,sizeof(c->body)));
        }
        else message(c,404,"Not Found","Not found");
        return;
    }
    if(strcmp(method,"POST")) { message(c,405,"Method Not Allowed","Use GET or POST"); return; }
    if(!control) { message(c,403,"Forbidden","X-Apple2-Control: 1 required"); return; }
    if(!have_length) { message(c,411,"Length Required","Content-Length required"); return; }
    if(!strncmp(path,"/upload/",8) || !strcmp(path,"/disks/mount")) {
        disk_request(c,path,body,length); return;
    }
    if(mount_request.pending) { message(c,409,"Conflict","Wait for the disk to finish loading."); return; }
    if(!strcmp(path,"/type") || !strcmp(path,"/text")) {
        if(disk_ui_is_visible()) { message(c,409,"Conflict","Close the disk menu before typing BASIC"); return; }
        for(size_t i=0;i<length;++i) {
            unsigned char ch=(unsigned char)body[i];
            if((ch<32 && ch!=13 && ch!=10) || ch>126) {
                message(c,400,"Bad Request","Use ASCII text and line breaks; /key sends special keys"); return;
            }
        }
        if(!length) { message(c,400,"Bad Request","Empty input"); return; }
        bool queued = !strcmp(path,"/text") ? typing_try_literal((const uint8_t *)body,length) : typing_try_push((const uint8_t *)body,length);
        if(!queued) {
            message(c,429,"Too Many Requests","Typing queue full; wait before retrying"); return;
        }
        MII_DEBUG_PRINTF("web: queued %u text bytes\n",(unsigned)length);
        message(c,202,"Accepted","Queued");
    } else if(!strcmp(path,"/apple")) {
        if(length != 1 || (unsigned char)body[0] < 32 || (unsigned char)body[0] > 126) {
            message(c,400,"Bad Request","Send one printable Open Apple shortcut key"); return;
        }
        if(!typing_try_apple((uint8_t)body[0])) {
            message(c,409,"Conflict","Close the disk menu and wait for the typing queue"); return;
        }
        message(c,202,"Accepted","Queued");
    } else if(!strcmp(path,"/key")) {
        uint8_t key=key_code(body,length);
        if(!key) { message(c,400,"Bad Request","Unknown key"); return; }
        if(pending_key) { message(c,409,"Conflict","Key action pending; retry"); return; }
        pending_key=key;
        message(c,202,"Accepted","Queued");
    } else message(c,404,"Not Found","Not found");
}

static err_t received(void *arg,struct tcp_pcb *pcb,struct pbuf *p,err_t err) {
    web_client_t *c=arg;
    if(!p) return c->responding ? ERR_OK : release_client(c,false);
    if(err!=ERR_OK) { pbuf_free(p); return release_client(c,true); }
    tcp_recved(pcb,p->tot_len); c->activity=time_us_64();
    if(!c->responding) {
        if(p->tot_len>REQUEST_CAP-c->used) message(c,413,"Payload Too Large","Request too large");
        else {
            pbuf_copy_partial(p,c->request+c->used,p->tot_len,0);
            c->used+=p->tot_len; c->request[c->used]=0;
            parse_request(c);
        }
    }
    pbuf_free(p); return ERR_OK;
}

static err_t sent(void *arg,struct tcp_pcb *pcb,u16_t len) {
    (void)pcb; web_client_t *c=arg;
    c->acked+=len; c->activity=time_us_64();
    if(c->responding && c->acked==c->header_len+c->body_len) return release_client(c,false);
    send_more(c); return ERR_OK;
}
static void failed(void *arg,err_t err) { (void)err; ((web_client_t *)arg)->pcb=NULL; }
static err_t poll_client(void *arg,struct tcp_pcb *pcb) {
    (void)pcb; web_client_t *c=arg;
    if(time_us_64()-c->activity>10000000) return release_client(c,true);
    send_more(c); return ERR_OK;
}
static err_t accepted(void *arg,struct tcp_pcb *pcb,err_t err) {
    (void)arg;
    if(err!=ERR_OK || !pcb) return err;
    for(size_t i=0;i<CLIENTS;++i) if(!clients[i].pcb) {
        web_client_t *c=&clients[i]; memset(c,0,sizeof(*c)); c->pcb=pcb; c->activity=time_us_64();
        tcp_arg(pcb,c); tcp_recv(pcb,received); tcp_sent(pcb,sent); tcp_err(pcb,failed);
        tcp_poll(pcb,poll_client,2); tcp_nagle_disable(pcb); return ERR_OK;
    }
    MII_DEBUG_PRINTF("web: connection capacity reached\n");
    tcp_abort(pcb); return ERR_ABRT;
}

bool web_control_enabled(void) { return enabled; }
void web_control_address(char *out,size_t cap) {
    const char *status = netcard_wifi_status();
    if(status) snprintf(out,cap,"%s",status);
    else if(!netif_default || ip4_addr_isany_val(*netif_ip4_addr(netif_default))) snprintf(out,cap,"Waiting for WiFi...");
    else snprintf(out,cap,"http://%s/",ip4addr_ntoa(netif_ip4_addr(netif_default)));
}
void web_control_toggle(void) {
    if(enabled) {
        enabled=false; pending_key=0; mount_request.pending=false; library_shutdown();
        if(listener) { tcp_accept(listener,NULL); tcp_close(listener); listener=NULL; }
        for(size_t i=0;i<CLIENTS;++i) if(clients[i].pcb) release_client(&clients[i],true);
        MII_DEBUG_PRINTF("web: control OFF\n"); return;
    }
    if(!netif_default) {
        MII_DEBUG_PRINTF("web: WiFi interface is not initialized\n"); return;
    }
    struct tcp_pcb *pcb=tcp_new_ip_type(IPADDR_TYPE_V4);
    if(!pcb) return;
    ip_set_option(pcb, SOF_REUSEADDR);
    err_t bind_error=tcp_bind(pcb,IP_ANY_TYPE,80);
    if(bind_error!=ERR_OK) {
        MII_DEBUG_PRINTF("web: cannot bind port 80 (%d)\n",bind_error);
        tcp_close(pcb); return;
    }
    listener=tcp_listen_with_backlog(pcb,2);
    if(!listener) { tcp_close(pcb); return; }
    tcp_accept(listener,accepted); enabled=true;
    char address[48]; web_control_address(address,sizeof(address));
    MII_DEBUG_PRINTF("web: control ON %s\n",address);
}
void web_control_poll(void) {
    library_poll();
    if(mount_request.pending && time_us_64()>=mount_request.after) {
        mount_request.ok=disk_ui_mount_file(mount_request.name,mount_request.drive,mount_request.boot);
        mount_request.pending=false;
    }
    if(pending_key) {
        uint8_t key=pending_key; pending_key=0;
        remote_control_key(key);
    }
}
