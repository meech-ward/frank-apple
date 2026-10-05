/* SPDX-License-Identifier: MIT. POSIX test adapter for the actual embedded engine. */
#include "ssh_transport_internal.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <algorithm>

static int listener=-1,client=-1;
static unsigned port=22222;
static bool slow_input=false;
static bool fast_clock=false;
static void close_client() { if(client>=0) close(client); client=-1; ssh_transport_wire_closed(); }
extern "C" bool ssh_platform_listen(void) {
    listener=socket(AF_INET,SOCK_STREAM,0); int yes=1;
    setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(port); a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(listener,reinterpret_cast<sockaddr*>(&a),sizeof(a)) || listen(listener,4)) return false;
    fcntl(listener,F_SETFL,O_NONBLOCK); return true;
}
extern "C" void ssh_platform_stop(void) { close_client(); if(listener>=0)close(listener); listener=-1; }
extern "C" void ssh_platform_random(uint8_t *out,size_t length) { arc4random_buf(out,length); }
extern "C" uint32_t ssh_platform_milliseconds(void) {
    using namespace std::chrono;
    auto now=duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    static auto epoch=now;
    return uint32_t(now-epoch)*(fast_clock?20:1);
}
extern "C" void ssh_platform_poll(void) {
    if(listener<0) return;
    int c=accept(listener,nullptr,nullptr);
    if(c>=0) {
        if(client>=0 || !ssh_transport_wire_accept()) close(c);
        else { client=c; fcntl(client,F_SETFL,O_NONBLOCK); }
    }
    if(client<0)return;
    uint8_t buf[1024]; size_t cap=std::min(sizeof(buf),ssh_transport_wire_receive_space());
    if(cap) {
        ssize_t n=recv(client,buf,cap,0);
        if(!n || (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK)) {close_client();return;}
        if(n>0 && !ssh_transport_wire_receive(buf,n)) abort();
    }
    size_t n=0; const uint8_t *p=ssh_transport_wire_output(&n);
    if(n) {
        ssize_t wrote=send(client,p,n,0);
        if(wrote>0)ssh_transport_wire_sent(wrote);
        else if(wrote<0 && errno!=EAGAIN && errno!=EWOULDBLOCK) {close_client();return;}
    }
    if(ssh_transport_wire_should_close()) close_client();
}
int main(int argc,char **argv) {
    if(argc>1)port=strtoul(argv[1],nullptr,10);
    if(argc>2)slow_input=true;
    if(argc>3)fast_clock=true;
    signal(SIGPIPE,SIG_IGN);
    uint8_t seed[32]; for(unsigned i=0;i<32;++i)seed[i]=i; /* Test-only identity. */
    if(!ssh_transport_init(seed,"test-pass-123") || !ssh_transport_set_enabled(true)) return 1;
    printf("READY %u %s\n",port,ssh_transport_fingerprint()); fflush(stdout);
    bool greeted=false; uint32_t last_read=0;
    uint8_t pending[1024]; size_t pending_size=0,pending_sent=0;
    for(;;) {
        ssh_transport_poll();
        if(!ssh_transport_connected()) { greeted=false; pending_size=pending_sent=0; }
        else {
            if(!greeted) {
                static const char hello[]="\r\nAPPLE II SSH TEST\r\n]";
                if(ssh_transport_write(reinterpret_cast<const uint8_t*>(hello),strlen(hello))==strlen(hello))greeted=true;
            }
            if(greeted && (!slow_input || ssh_platform_milliseconds()-last_read>2)) {
                if(pending_size==pending_sent) {
                    pending_size=ssh_transport_read(pending,slow_input?7:sizeof(pending)); pending_sent=0;
                    last_read=ssh_platform_milliseconds();
                }
                pending_sent+=ssh_transport_write(pending+pending_sent,pending_size-pending_sent);
                if(pending_sent==pending_size && ssh_transport_input_ended()) ssh_transport_disconnect();
            }
        }
        usleep(1000);
    }
}
