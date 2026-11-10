/*
 * Copyright (C) 2022 Wire Swiss GmbH
 */

#ifndef __FEDERATION__
#define __FEDERATION__

#include "dtls_listener.h"

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////

void federation_init(dtls_listener_relay_server_type *listener);
void federation_load_certificates(void);
void federation_close_connections(void);
uint16_t federation_add_connection(allocation *fed_allocation);
int federation_remove_connection(uint16_t connection_id);
void federation_send_data(dtls_listener_relay_server_type *server, ioa_addr *dest_addr, ioa_network_buffer_handle nbh,
                          int ttl, int tos, int *skip);
void federation_input_handler(ioa_socket_handle s, int event_type, ioa_net_data *in_buffer, void *arg, int can_resume);
void federation_whitelist_add(const char *hostname, const char *issuer);
void federation_start_handshake_timer(ioa_socket_handle s);
void federation_start_client_heartbeat_timer(ioa_socket_handle s);
void federation_start_server_heartbeat_timer(ioa_socket_handle s);

///////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif //__FEDERATION__
