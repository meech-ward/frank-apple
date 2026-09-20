#pragma once

#include <stdbool.h>

void netcard_init(void);
void netcard_poll(void);

#if NETCARD_REALTIME
struct altcp_tls_config;
struct altcp_tls_config *netcard_tls_config(void);
bool netcard_link_up(void);
void netcard_realtime_poll(void);
int netcard_realtime_state(void);
#endif
