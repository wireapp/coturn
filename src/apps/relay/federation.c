/*
 * Copyright (C) 2022 Wire Swiss GmbH
 */

#include "mainrelay.h"

#include "federation.h"
#include "hostcheck.h"
#include "ns_ioalib_impl.h"
#include "ns_turn_utils.h"

#include <openssl/x509v3.h>
#include <pthread.h>

///////////////////////////////////////////////////

// Whitelist data
static char **whitelist_hostnames = NULL;
static char **whitelist_issuers = NULL;
static size_t whitelist_count = 0;

///////////// federation singleton data ////////
typedef struct _federation_data {
  ur_addr_map federation_client_tuple_to_fed_connection;
  lm_map federation_cid_to_fed_connection;
  bool federation_initialized;
  bool use_mutex;   // only needed when relay threads and the federation thread differ
  turn_mutex mutex; // only initialized if use_mutex is set
  struct bufferevent *in_buf;
  struct bufferevent *out_buf;
  dtls_listener_relay_server_type *listener;
  // Certificate reload: contexts are built on the signal thread and swapped in on the federation thread.
  turn_mutex ctx_mutex;
  struct event *ctx_update_ev;
  SSL_CTX *pending_client_ctx;
  SSL_CTX *pending_server_ctx;
} federation_data;

static federation_data federation_data_singleton = {0};
#define FED_LOCK()                                                                                                     \
  if (federation_data_singleton.use_mutex)                                                                             \
  turn_mutex_lock(&federation_data_singleton.mutex)
#define FED_UNLOCK()                                                                                                   \
  if (federation_data_singleton.use_mutex)                                                                             \
  turn_mutex_unlock(&federation_data_singleton.mutex)

typedef struct _federation_connection {
  uint16_t connection_id; // channel number
  ioa_addr client_addr;
  turn_turnserver *server;
  turnsession_id sid;
} federation_connection;

#define FEDERATION_HANDSHAKE_TIMEOUT_SECS 10

#define FEDERATION_HEARTBEAT_TIME_SECS 5 // Send a client ping (\r\n\r\n) every 5 seconds after DTLS handshake
#define FEDERATION_CLIENT_HEARTBEAT_MAX_OUTSTANDING                                                                    \
  1 // Client side: We are allowed to have up to 1 ping that doesn't get ponged - max detection time is 15s
#define FEDERATION_SERVER_MAX_MISSING_HEARTBEATS                                                                       \
  2 // Server side: We are allowed to have up to 2 5 second intervals with no pings - max detection time is 15s

static int federation_send_data_imp(dtls_listener_relay_server_type *server, ioa_addr *dest_addr,
                                    ioa_network_buffer_handle nbh, int ttl, int tos, int *skip);

// WARNING:  make sure this is always called under lock
static federation_connection *federation_get_connection_by_connection_id(uint16_t connection_id) {
  federation_connection *fed_con = NULL;
  ur_map_value_type mvt = 0;
  if (lm_map_get(&federation_data_singleton.federation_cid_to_fed_connection, connection_id, &mvt)) {
    fed_con = (federation_connection *)mvt;
  }
  return fed_con;
}

// WARNING:  make sure this is always called under lock
static federation_connection *federation_get_connection_by_allocation(allocation *client_allocation) {
  federation_connection *fed_con = NULL;
  ur_addr_map_value_type mvt = 0;
  ioa_addr *client_addr = &(((ts_ur_super_session *)client_allocation->owner)->client_socket->remote_addr);
  if (ur_addr_map_get(&federation_data_singleton.federation_client_tuple_to_fed_connection, client_addr, &mvt)) {
    fed_con = (federation_connection *)mvt;
  }
  return fed_con;
}

// returns true on success
static bool federation_get_connection_info_by_connection_id(uint16_t connection_id, turn_turnserver **turn_server,
                                                            turnsession_id *turn_session_id) {
  bool ret = false;

  FED_LOCK();

  federation_connection *fed_con = federation_get_connection_by_connection_id(connection_id);
  if (fed_con) {
    ret = true;
    if (turn_server) {
      *turn_server = fed_con->server;
    }
    if (turn_session_id) {
      *turn_session_id = fed_con->sid;
    }
  }

  FED_UNLOCK();

  return ret;
}

uint16_t federation_add_connection(allocation *client_allocation) {

  uint16_t connection_id = 0;

  if (!client_allocation) {
    TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "%s: allocation was NULL\n", __FUNCTION__);
    return 0;
  }
  ts_ur_super_session *ss = (ts_ur_super_session *)client_allocation->owner;
  if (!ss || !ss->client_socket) {
    TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "%s: allocation has no owner\n", __FUNCTION__);
    return 0;
  }

  FED_LOCK();

  // Check if connection for client_addr already exists, if so return it
  federation_connection *fed_con = federation_get_connection_by_allocation(client_allocation);
  if (fed_con) {
    connection_id = fed_con->connection_id;
    goto done;
  }

  // The cid is sent as a channel number, which makes it easy for the client to demux and for wireshark to display.
  do {
    connection_id = (uint16_t)((unsigned long)turn_random_number() % 0x3FFF + 0x4000);
    if (federation_get_connection_by_connection_id(connection_id)) {
      connection_id = 0;
    }
  } while (connection_id == 0);

  fed_con = (federation_connection *)turn_malloc(sizeof(federation_connection));

  fed_con->connection_id = connection_id;
  addr_cpy(&fed_con->client_addr, &(ss->client_socket->remote_addr));
  fed_con->sid = ss->id;
  fed_con->server = (turn_turnserver *)ss->server;

  ur_addr_map_put(&federation_data_singleton.federation_client_tuple_to_fed_connection, &fed_con->client_addr,
                  (ur_addr_map_value_type)fed_con);
  lm_map_put(&federation_data_singleton.federation_cid_to_fed_connection, fed_con->connection_id,
             (ur_map_value_type)fed_con);

  if (fed_con->server->verbose) {
    char log_buf[128] = {0};
    snprintf(log_buf, sizeof(log_buf), "%s: added cid=%u, sid=%018llu, client_addr", __FUNCTION__,
             fed_con->connection_id, (unsigned long long)fed_con->sid);
    addr_debug_print(1, &fed_con->client_addr, log_buf);
  }

done:

  FED_UNLOCK();

  return connection_id;
}

int federation_remove_connection(uint16_t connection_id) {
  int ret = EINVAL;

  FED_LOCK();

  federation_connection *fed_con = federation_get_connection_by_connection_id(connection_id);
  if (fed_con) {
    ur_addr_map_del(&federation_data_singleton.federation_client_tuple_to_fed_connection, &fed_con->client_addr, NULL);
    lm_map_del(&federation_data_singleton.federation_cid_to_fed_connection, connection_id, NULL);

    free(fed_con);

    TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "%s: removed cid=%u\n", __FUNCTION__, connection_id);

    ret = 0;
  }

  FED_UNLOCK();

  return ret;
}

// returns -1 if not found, otherwise position in whitelist
static int hostname_whitelist_match(const char *cert_hostname) {
  for (size_t i = 0; i < whitelist_count; i++) {
    if (wildcard_hostcheck(whitelist_hostnames[i], cert_hostname) == 1) {
      TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "%s: config_hostname=%s, cert_hostname=%s - MATCHED!\n", __FUNCTION__,
                    whitelist_hostnames[i], cert_hostname);
      return (int)i;
    }
  }
  return -1;
}

static int federation_cert_verify(X509_STORE_CTX *x509_ctx, void *arg) {
  UNUSED_ARG(arg);

  char cSubjectNameString[257] = {0};
  char cIssuerNameString[257] = {0};

  // Do the normal builtin certification validation by openssl
  int preverify_ok = X509_verify_cert(x509_ctx);
  int postverify_ok = 0;

  X509 *pCurCert = X509_STORE_CTX_get_current_cert(x509_ctx);

  if (NULL != pCurCert) {
    X509_NAME_oneline(X509_get_subject_name(pCurCert), cSubjectNameString, 256);
    X509_NAME_oneline(X509_get_issuer_name(pCurCert), cIssuerNameString, 256);
  }

  if (preverify_ok <= 0) {
    int iErr = X509_STORE_CTX_get_error(x509_ctx);
    int iDepth = X509_STORE_CTX_get_error_depth(x509_ctx);

    TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "%s: Error when verifying peer's chain of certificates: %s, depth=%d %s %s\n",
                  __FUNCTION__, X509_verify_cert_error_string(iErr), iDepth, cSubjectNameString, cIssuerNameString);
  } else {
    TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "%s: SSL preverify OK: %s %s\n", __FUNCTION__, cSubjectNameString,
                  cIssuerNameString);

    if (whitelist_count > 0) {
      int bSubjectAltNamePresent = 0;
      int whitelistMatchPosition = -1;

      // Subject Alt Name Check
      STACK_OF(GENERAL_NAME) *pSubjectAltNames = X509_get_ext_d2i(pCurCert, NID_subject_alt_name, NULL, NULL);
      if (pSubjectAltNames != NULL) {
        bSubjectAltNamePresent = 1;
        int numNames = sk_GENERAL_NAME_num(pSubjectAltNames);
        for (int i = 0; i < numNames; i++) {
          const GENERAL_NAME *pSubjectAltName = sk_GENERAL_NAME_value(pSubjectAltNames, i);
          if (pSubjectAltName->type == GEN_DNS) {
            const char *cDNSName = (const char *)ASN1_STRING_get0_data(pSubjectAltName->d.dNSName);
            whitelistMatchPosition = hostname_whitelist_match(cDNSName);
            if (whitelistMatchPosition == -1) {
              TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "%s: Found non-matching Subject Alt Name: %s\n", __FUNCTION__,
                            cDNSName);
            } else {
              TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "%s: Found matching Subject Alt Name: %s\n", __FUNCTION__, cDNSName);
              break;
            }
          }
        }
      }
      sk_GENERAL_NAME_pop_free(pSubjectAltNames, GENERAL_NAME_free);

      // RFC 6125 Section 6.4.4: the Common Name is only checked when there are no Subject Alt Names.
      if (!bSubjectAltNamePresent) {
        int commonNameIndex = X509_NAME_get_index_by_NID(X509_get_subject_name(pCurCert), NID_commonName, -1);
        if (commonNameIndex >= 0) {
          const X509_NAME_ENTRY *pCommonNameEntry =
              X509_NAME_get_entry(X509_get_subject_name(pCurCert), commonNameIndex);
          if (pCommonNameEntry != NULL) {
            const ASN1_STRING *pCommonNameASN1 = X509_NAME_ENTRY_get_data(pCommonNameEntry);
            if (pCommonNameASN1 != NULL) {
              const char *cCommonName = (const char *)ASN1_STRING_get0_data(pCommonNameASN1);
              whitelistMatchPosition = hostname_whitelist_match(cCommonName);
              if (whitelistMatchPosition == -1) {
                TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "%s: Found non-matching Common Name: %s\n", __FUNCTION__,
                              cCommonName);
              } else {
                TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "%s: Found matching Common Name: %s\n", __FUNCTION__, cCommonName);
              }
            }
          }
        }
      }

      // Check Issuer, if we found a hostname match, and issuer is specified
      if (whitelistMatchPosition != -1) {
        if (whitelist_issuers[whitelistMatchPosition][0] != 0) {
          STACK_OF(X509) *pChain = X509_STORE_CTX_get1_chain(x509_ctx);
          int numCertsInChain = sk_X509_num(pChain);
          TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "%s: %d certs in chain\n", __FUNCTION__, numCertsInChain);
          for (int i = 0; i < numCertsInChain; i++) {
            X509 *pCert = sk_X509_value(pChain, i);

            int issuerNameIndex = X509_NAME_get_index_by_NID(X509_get_issuer_name(pCert), NID_commonName, -1);
            if (issuerNameIndex >= 0) {
              const X509_NAME_ENTRY *pIssuerNameEntry =
                  X509_NAME_get_entry(X509_get_issuer_name(pCert), issuerNameIndex);
              if (pIssuerNameEntry != NULL) {
                const ASN1_STRING *pIssuerNameASN1 = X509_NAME_ENTRY_get_data(pIssuerNameEntry);
                if (pIssuerNameASN1 != NULL) {
                  const char *cIssuerName = (const char *)ASN1_STRING_get0_data(pIssuerNameASN1);
                  if (strcmp(whitelist_issuers[whitelistMatchPosition], cIssuerName) == 0) {
                    TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "%s: Found matching Issuer Name: %s\n", __FUNCTION__,
                                  cIssuerName);
                    postverify_ok = 1;
                    break;
                  } else {
                    TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "%s: Found non-matching Issuer Name: %s\n", __FUNCTION__,
                                  cIssuerName);
                  }
                }
              }
            }
          }
          sk_X509_pop_free(pChain, X509_free);
        } else {
          // No issuer specified, accept all
          postverify_ok = 1;
        }
      }
    } else {
      // No whitelist provided, accept all
      postverify_ok = 1;
    }
  }

  if (preverify_ok > 0 && postverify_ok) {
    return 1;
  }
  return 0;
}

#if ALPN_SUPPORTED
static const unsigned char kALPNProtos[] = "\x08http/1.1\x09stun.turn\x12stun.nat-discovery";
static const size_t kALPNProtosLen = sizeof(kALPNProtos) - 1;
#endif

static const char *cipherlist = "ECDHE-RSA-AES128-GCM-SHA256:"
                                "ECDHE-ECDSA-AES128-GCM-SHA256:"
                                "ECDHE-RSA-AES256-GCM-SHA384:"
                                "ECDHE-ECDSA-AES256-GCM-SHA384:"
                                "DHE-RSA-AES128-GCM-SHA256:"
                                "ECDHE-RSA-AES128-SHA256:"
                                "ECDHE-ECDSA-AES128-SHA256:"
                                "ECDHE-RSA-AES128-SHA:"
                                "ECDHE-ECDSA-AES128-SHA:"
                                "ECDHE-RSA-AES256-SHA384:"
                                "ECDHE-ECDSA-AES256-SHA384:"
                                "ECDHE-RSA-AES256-SHA:"
                                "ECDHE-ECDSA-AES256-SHA:"
                                "DHE-RSA-AES128-SHA256:"
                                "DHE-RSA-AES128-SHA:"
                                "DHE-RSA-AES256-SHA256:"
                                "DHE-RSA-AES256-SHA";

static SSL_CTX *federation_setup_dtls_ctx(bool is_client) {
  SSL_CTX *ssl_ctx = NULL;

  turn_set_ssl_ctx(&ssl_ctx, "Federation-DTLS", is_client ? DTLS_client_method() : DTLS_server_method(),
                   turn_params.federation_cert_file, turn_params.federation_pkey_file, turn_params.federation_pkey_pwd);
  SSL_CTX_set_min_proto_version(ssl_ctx, DTLS1_2_VERSION);

  SSL_CTX_set_cipher_list(ssl_ctx, cipherlist);

  if (turn_params.ca_cert_file[0]) {
    // Overrides the verify mode set by turn_set_ssl_ctx. The cert verify callback runs once per handshake rather
    // than once per chain element, so the hostname and issuer checks see the whole chain in one place.
    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE, NULL);
    SSL_CTX_set_cert_verify_callback(ssl_ctx, federation_cert_verify, NULL);
  }

  setup_dtls_callbacks(ssl_ctx);

  return ssl_ctx;
}

static void federation_swap_ctx(evutil_socket_t sock, short events, void *args) {
  UNUSED_ARG(sock);
  UNUSED_ARG(events);
  UNUSED_ARG(args);

  turn_mutex_lock(&federation_data_singleton.ctx_mutex);
  SSL_CTX *client_ctx = federation_data_singleton.pending_client_ctx;
  SSL_CTX *server_ctx = federation_data_singleton.pending_server_ctx;
  federation_data_singleton.pending_client_ctx = NULL;
  federation_data_singleton.pending_server_ctx = NULL;
  turn_mutex_unlock(&federation_data_singleton.ctx_mutex);

  // Runs on the federation thread, the only user of these contexts. Live SSL objects hold their own reference.
  if (client_ctx) {
    SSL_CTX_free(turn_params.federation_dtls_client_ctx);
    turn_params.federation_dtls_client_ctx = client_ctx;
  }
  if (server_ctx) {
    SSL_CTX_free(turn_params.federation_dtls_server_ctx);
    turn_params.federation_dtls_server_ctx = server_ctx;
  }
}

void federation_load_certificates(void) {
  if (turn_params.federation_no_dtls || !federation_data_singleton.federation_initialized) {
    return;
  }

  if (turn_params.ca_cert_file[0]) {
    print_abs_file_name("", "Federation CA", turn_params.ca_cert_file);
  } else {
    TURN_LOG_FUNC(TURN_LOG_LEVEL_WARNING, "No CA cert file, federation certificate validation is disabled.\n");
  }
  print_abs_file_name("", "Federation Certificate", turn_params.federation_cert_file);
  print_abs_file_name("", "Federation Private key", turn_params.federation_pkey_file);

  SSL_CTX *client_ctx = federation_setup_dtls_ctx(true);
  SSL_CTX *server_ctx = federation_setup_dtls_ctx(false);

  turn_mutex_lock(&federation_data_singleton.ctx_mutex);
  SSL_CTX_free(federation_data_singleton.pending_client_ctx);
  SSL_CTX_free(federation_data_singleton.pending_server_ctx);
  federation_data_singleton.pending_client_ctx = client_ctx;
  federation_data_singleton.pending_server_ctx = server_ctx;
  turn_mutex_unlock(&federation_data_singleton.ctx_mutex);

  event_active(federation_data_singleton.ctx_update_ev, EV_READ, 0);
}

enum _MESSAGE_TO_FEDERATION_TYPE { FMT_UNKNOWN = 0, FMT_SEND_DATA };
typedef enum _MESSAGE_TO_FEDERATION_TYPE MESSAGE_TO_FEDERATION_TYPE;

struct federation_send_data_message {
  dtls_listener_relay_server_type *listen_server;
  ioa_addr dest_addr;
  int ttl;
  int tos;
  ioa_network_buffer_handle nbh;
};

struct message_to_federation {
  MESSAGE_TO_FEDERATION_TYPE t;
  union {
    struct federation_send_data_message send_data;
  } m;
};

static void federation_receive_message(struct bufferevent *bev, void *ptr) {
  struct message_to_federation msg;
  int n = 0;
  struct evbuffer *input = bufferevent_get_input(bev);
  UNUSED_ARG(ptr);

  while ((n = evbuffer_remove(input, &msg, sizeof(struct message_to_federation))) > 0) {

    if (n != sizeof(struct message_to_federation)) {
      TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "%s: Weird buffer error\n", __FUNCTION__);
      continue;
    }

    switch (msg.t) {
    case FMT_SEND_DATA:
      federation_send_data_imp(msg.m.send_data.listen_server, &msg.m.send_data.dest_addr, msg.m.send_data.nbh,
                               msg.m.send_data.ttl, msg.m.send_data.tos, NULL);
      break;

    default:
      break;
    }
  }
}

void federation_init(dtls_listener_relay_server_type *listener) {
  ioa_engine_handle e = get_engine(listener);

  federation_data_singleton.listener = listener;

  ur_addr_map_init(&federation_data_singleton.federation_client_tuple_to_fed_connection);
  lm_map_init(&federation_data_singleton.federation_cid_to_fed_connection);

  // Connections are added and looked up from every relay thread when they differ from the federation thread.
  if (turn_params.general_relay_servers_number > 1) {
    turn_mutex_init(&federation_data_singleton.mutex);
    federation_data_singleton.use_mutex = true;
  }

  // Senders on relay threads write to out_buf; the data arrives on in_buf on the federation thread.
  struct bufferevent *pair[2];
  bufferevent_pair_new(e->event_base, TURN_BUFFEREVENTS_OPTIONS, pair);
  federation_data_singleton.in_buf = pair[0];
  federation_data_singleton.out_buf = pair[1];
  bufferevent_setcb(federation_data_singleton.in_buf, federation_receive_message, NULL, NULL, NULL);
  bufferevent_enable(federation_data_singleton.in_buf, EV_READ);

  turn_mutex_init(&federation_data_singleton.ctx_mutex);
  federation_data_singleton.ctx_update_ev = event_new(e->event_base, -1, EV_PERSIST, federation_swap_ctx, NULL);

  federation_data_singleton.federation_initialized = true;

  if (!turn_params.federation_no_dtls) {
    // The federation thread is not running yet, so install the contexts directly.
    print_abs_file_name("", "Federation Certificate", turn_params.federation_cert_file);
    print_abs_file_name("", "Federation Private key", turn_params.federation_pkey_file);
    if (!turn_params.ca_cert_file[0]) {
      TURN_LOG_FUNC(TURN_LOG_LEVEL_WARNING, "No CA cert file, federation certificate validation is disabled.\n");
    }
    turn_params.federation_dtls_client_ctx = federation_setup_dtls_ctx(true);
    turn_params.federation_dtls_server_ctx = federation_setup_dtls_ctx(false);
  }
}

typedef struct {
  ioa_socket_handle *sockets;
  size_t count;
} federated_sockets;

static bool collect_federated_socket(ur_addr_map_value_type value, void *arg) {
  federated_sockets *fs = (federated_sockets *)arg;
  fs->sockets = (ioa_socket_handle *)turn_realloc(fs->sockets, (fs->count + 1) * sizeof(ioa_socket_handle));
  fs->sockets[fs->count++] = (ioa_socket_handle)value;
  return true;
}

// Call only once the federation thread has stopped.
void federation_close_connections(void) {
  if (!federation_data_singleton.federation_initialized) {
    return;
  }
  ur_addr_map *children = turn_dtls_listener_get_children(federation_data_singleton.listener);
  federated_sockets fs = {0};
  // Closing a socket removes it from the map, so collect them before closing.
  ur_addr_map_foreach_arg(children, collect_federated_socket, &fs);
  TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "Ending %zu federated connection(s)\n", fs.count);
  for (size_t i = 0; i < fs.count; ++i) {
    close_ioa_socket(fs.sockets[i]);
  }
  free(fs.sockets);
}

static int federation_send_data_imp(dtls_listener_relay_server_type *server, ioa_addr *dest_addr,
                                    ioa_network_buffer_handle nbh, int ttl, int tos, int *skip) {
  ioa_socket_handle listen_s = turn_dtls_listener_get_socket(server);
  ioa_engine_handle e = get_engine(server);
  const int verbose = e->verbose;

  if (turn_params.federation_no_dtls) {
    addr_debug_print(eve(verbose), get_local_addr_from_ioa_socket(listen_s),
                     "Federation: relaying data from SendInd from");
    addr_debug_print(eve(verbose), dest_addr, "Federation: relaying data from SendInd to");
    return send_data_from_ioa_socket_nbh(listen_s, dest_addr, nbh, ttl, tos, skip);
  }

  ur_addr_map *amap = turn_dtls_listener_get_children(server);

  ioa_socket_handle chs = NULL;
  ur_addr_map_value_type mvt = 0;
  if ((ur_addr_map_get(amap, dest_addr, &mvt) > 0) && mvt) {
    chs = (ioa_socket_handle)mvt;
  }

  if (!chs) {
    // No connection yet, DTLS connect to dest_addr
    SSL *ssl = SSL_new(turn_params.federation_dtls_client_ctx);
    SSL_set_options(ssl, SSL_OP_COOKIE_EXCHANGE);

#if ALPN_SUPPORTED
    SSL_set_alpn_protos(ssl, kALPNProtos, kALPNProtosLen);
#endif

    BIO *bio = BIO_new_dgram(listen_s->fd, BIO_NOCLOSE);
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_PEER, 0, &dest_addr->ss);

    SSL_set_bio(ssl, bio, bio);

    struct timeval timeout;
    timeout.tv_sec = DTLS_MAX_RECV_TIMEOUT;
    timeout.tv_usec = 0;
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);

    SSL_set_max_cert_list(ssl, 655350);

    SSL_set_connect_state(ssl);

    addr_debug_print(verbose, dest_addr, "Starting DTLS handshake to ");

    chs = create_ioa_socket_from_ssl(e, listen_s, ssl, DTLS_SOCKET, CLIENT_SOCKET, dest_addr,
                                     turn_dtls_listener_get_addr(server));
    if (!chs) {
      TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "Cannot create ioa_socket from SSL\n");
      SSL_free(ssl);
      ioa_network_buffer_delete(e, nbh);
      return -1;
    }

    federation_start_handshake_timer(chs);

    add_socket_to_map(chs, amap);

    if (register_callback_on_ioa_socket(e, chs, IOA_EV_READ, federation_input_handler, server, 0) < 0) {
      IOA_CLOSE_SOCKET(chs);
      ioa_network_buffer_delete(e, nbh);
      return -1;
    }
  }

  addr_debug_print(eve(verbose), get_local_addr_from_ioa_socket(chs), "Federation: relaying data from SendInd from");
  addr_debug_print(eve(verbose), dest_addr, "Federation: relaying data from SendInd to");

  return send_data_from_ioa_socket_nbh(chs, dest_addr, nbh, ttl, tos, skip);
}

void federation_send_data(dtls_listener_relay_server_type *server, ioa_addr *dest_addr, ioa_network_buffer_handle nbh,
                          int ttl, int tos, int *skip) {

  if (turn_params.general_relay_servers_number > 1) {
    // Multi-threaded: hand the data to the federation thread, which owns the federation sockets.
    struct message_to_federation msg;
    memset(&msg, 0, sizeof(msg));
    msg.t = FMT_SEND_DATA;
    msg.m.send_data.listen_server = server;
    addr_cpy(&msg.m.send_data.dest_addr, dest_addr);
    msg.m.send_data.ttl = ttl;
    msg.m.send_data.tos = tos;
    msg.m.send_data.nbh = nbh;

    struct evbuffer *output = bufferevent_get_output(federation_data_singleton.out_buf);
    if (evbuffer_add(output, &msg, sizeof(struct message_to_federation)) < 0) {
      TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "%s: Weird buffer error\n", __FUNCTION__);
      ioa_network_buffer_delete(NULL, nbh);
    }

    return;
  }

  federation_send_data_imp(server, dest_addr, nbh, ttl, tos, skip);
}

// The caller deletes in_buffer->nbh unless it is set to NULL here.
void federation_input_handler(ioa_socket_handle s, int event_type, ioa_net_data *in_buffer, void *arg, int can_resume) {

  UNUSED_ARG(can_resume);

  if (!(event_type & IOA_EV_READ) || !arg) {
    return;
  }

  if (in_buffer->recv_ttl == 0) {
    return;
  }

  if (!s || ioa_socket_tobeclosed(s)) {
    return;
  }

  dtls_listener_relay_server_type *dtls_listener = (dtls_listener_relay_server_type *)arg;
  ioa_socket_handle listen_s = turn_dtls_listener_get_socket(dtls_listener);

  if (!listen_s || ioa_socket_tobeclosed(listen_s)) {
    return;
  }

  const int verbose = get_engine(dtls_listener)->verbose;
  uint8_t *data = ioa_network_buffer_data(in_buffer->nbh);
  const size_t size = ioa_network_buffer_get_size(in_buffer->nbh);

  // Heartbeat ping (double CRLF): answer with a pong (single CRLF), reusing the inbound buffer.
  if (size == 4 && memcmp("\r\n\r\n", data, 4) == 0) {
    ioa_network_buffer_handle nbh = in_buffer->nbh;
    in_buffer->nbh = NULL;
    ioa_network_buffer_set_size(nbh, 2);
    addr_debug_print(eve(verbose), get_local_addr_from_ioa_socket(s), "Federation: PONG from");
    send_data_from_ioa_socket_nbh(s, &in_buffer->src_addr, nbh, TTL_IGNORE, TOS_IGNORE, NULL);

    s->federation_heartbeat_pings_outstanding = 0;
    return;
  }

  // Heartbeat pong
  if (size == 2 && memcmp("\r\n", data, 2) == 0) {
    s->federation_heartbeat_pings_outstanding = 0;
    return;
  }

  if (size < 4) {
    return;
  }

  // The first two bytes carry the federation cid, which maps the data to the allocation of the client.
  const uint16_t cid = turn_read_u16(data);
  if (!STUN_VALID_CHANNEL(cid)) {
    TURN_LOG_FUNC(TURN_LOG_LEVEL_WARNING,
                  "%s: Federation listener received data for invalid channel range cid=%hu ignoring\n", __FUNCTION__,
                  cid);
    return;
  }

  turn_turnserver *turn_server = NULL;
  turnsession_id sid = 0;
  if (!federation_get_connection_info_by_connection_id(cid, &turn_server, &sid)) {
    TURN_LOG_FUNC(TURN_LOG_LEVEL_WARNING, "%s: Federation listener received data for invalid cid=%hu ignoring\n",
                  __FUNCTION__, cid);
    return;
  }

  ioa_network_buffer_handle nbh = in_buffer->nbh;
  in_buffer->nbh = NULL;

  if (turn_params.general_relay_servers_number > 1) {
    if (eve(turn_server->verbose)) {
      TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO,
                    "%s: Federation listener received data cid=%hu, relaying data via relay thread...\n", __FUNCTION__,
                    cid);
    }
    send_federation_data_message_to_relay(sid, nbh, in_buffer->recv_ttl - 1, in_buffer->recv_tos);
  } else {
    if (eve(turn_server->verbose)) {
      TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "%s: Federation listener received data cid=%hu, relaying data directly...\n",
                    __FUNCTION__, cid);
    }
    turn_send_federation_data(turn_server, sid, nbh, in_buffer->recv_ttl - 1, in_buffer->recv_tos);
  }
}

void federation_whitelist_add(const char *hostname, const char *issuer) {
  size_t new_size = (whitelist_count + 1) * sizeof(char *);
  whitelist_hostnames = (char **)turn_realloc(whitelist_hostnames, new_size);
  whitelist_issuers = (char **)turn_realloc(whitelist_issuers, new_size);
  whitelist_hostnames[whitelist_count] = turn_strdup(hostname);
  whitelist_issuers[whitelist_count] = turn_strdup(issuer);
  whitelist_count++;
}

static void federation_handshake_timeout_handler(ioa_engine_handle e, void *arg) {

  UNUSED_ARG(e);

  if (!arg) {
    TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "!!! %s: empty federation_client_connection to be cleaned\n", __FUNCTION__);
    return;
  }

  ioa_socket_handle s = (ioa_socket_handle)arg;

  if (s->ssl && !SSL_is_init_finished(s->ssl)) {
    addr_debug_print(1, &s->remote_addr, "DTLS handshake timedout with ");
    // Closing the socket also removes it from its containing map.
    IOA_CLOSE_SOCKET(s);
  }
}

void federation_start_handshake_timer(ioa_socket_handle s) {
  IOA_EVENT_DEL(s->federation_handshake_tmr);
  s->federation_handshake_tmr =
      set_ioa_timer(s->e, FEDERATION_HANDSHAKE_TIMEOUT_SECS, 0, federation_handshake_timeout_handler, s, 0,
                    "federation_client_connect_timeout_handler");
}

static void federation_client_heartbeat_timeout_handler(ioa_engine_handle e, void *arg) {

  UNUSED_ARG(e);

  if (!arg) {
    TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "!!! %s: empty socket handle to heartbeat\n", __FUNCTION__);
    return;
  }

  ioa_socket_handle s = (ioa_socket_handle)arg;

  if (ioa_socket_tobeclosed(s)) {
    return;
  }

  if (s->federation_heartbeat_pings_outstanding <= FEDERATION_CLIENT_HEARTBEAT_MAX_OUTSTANDING) {
    // Double CRLF as heartbeat ping, expecting a single CRLF pong back
    ioa_network_buffer_handle nbh = ioa_network_buffer_allocate(s->e);
    uint8_t *data = ioa_network_buffer_data(nbh);
    memcpy(data, "\r\n\r\n", 4);
    ioa_network_buffer_set_size(nbh, 4);
    addr_debug_print(eve(s->e->verbose), get_local_addr_from_ioa_socket(s), "Federation: PING from");
    send_data_from_ioa_socket_nbh(s, NULL, nbh, TTL_IGNORE, TOS_IGNORE, NULL);

    s->federation_heartbeat_pings_outstanding++;

    federation_start_client_heartbeat_timer(s);
  } else {
    addr_debug_print(1, &s->remote_addr,
                     "Federation: keepalive pong not received after pings, terminating connection to");
    // The mapping is likely dead.
    close_ioa_socket(s);
  }
}

void federation_start_client_heartbeat_timer(ioa_socket_handle s) {
  IOA_EVENT_DEL(s->federation_heartbeat_tmr);
  s->federation_heartbeat_tmr =
      set_ioa_timer(s->e, FEDERATION_HEARTBEAT_TIME_SECS, 0, federation_client_heartbeat_timeout_handler, s, 0,
                    "federation_client_heartbeat_timeout_handler");
}

static void federation_server_heartbeat_timeout_handler(ioa_engine_handle e, void *arg) {

  UNUSED_ARG(e);

  if (!arg) {
    TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "!!! %s: empty socket handle to heartbeat\n", __FUNCTION__);
    return;
  }

  ioa_socket_handle s = (ioa_socket_handle)arg;

  if (ioa_socket_tobeclosed(s)) {
    return;
  }

  if (s->federation_heartbeat_pings_outstanding <= FEDERATION_SERVER_MAX_MISSING_HEARTBEATS) {
    s->federation_heartbeat_pings_outstanding++;
    federation_start_server_heartbeat_timer(s);
  } else {
    addr_debug_print(1, &s->remote_addr, "Federation: keepalive pings not received, terminating connection to");
    close_ioa_socket(s);
  }
}

void federation_start_server_heartbeat_timer(ioa_socket_handle s) {
  IOA_EVENT_DEL(s->federation_heartbeat_tmr);
  s->federation_heartbeat_tmr =
      set_ioa_timer(s->e, FEDERATION_HEARTBEAT_TIME_SECS, 0, federation_server_heartbeat_timeout_handler, s, 0,
                    "federation_server_heartbeat_timeout_handler");
}
