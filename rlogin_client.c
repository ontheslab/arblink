/*
 * rlogin transport layer for arblink.
 *
 * This module owns the raw TCP socket, hostname lookup, and the initial
 * rlogin handshake. The door and CLI both build on these entry points.
 *
 * Important gotcha:
 * if remote_user is blank in the door config, the already-prefixed caller
 * name is used for both the local and remote rlogin user fields.
 */
#include "rlogin_client.h"
#include "aedoor_bridge.h"
#include "door_config.h"
#include "socket_inline_local.h"

#include <exec/types.h>
#include <proto/exec.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <libraries/bsdsocket.h>

#include <stdio.h>
#include <string.h>

struct Library *SocketBase = NULL;

/* Keep socket-layer errors short and caller-friendly. */
static void client_set_error(char *error_text, int error_text_size, const char *message)
{
  if ((error_text == NULL) || (error_text_size <= 0)) {
    return;
  }

  strncpy(error_text, message, (size_t) error_text_size - 1U);
  error_text[error_text_size - 1] = '\0';
}

static void client_set_errorf(char *error_text, int error_text_size, const char *prefix, long code)
{
  char buffer[160];

  snprintf(buffer, sizeof(buffer), "%s (%ld)", prefix, code);
  client_set_error(error_text, error_text_size, buffer);
}

/* Append one NUL-terminated field to the rlogin startup packet. */
static int client_append_field(unsigned char *buffer, size_t buffer_size, size_t *position, const char *text)
{
  size_t text_length;

  text_length = strlen(text);
  if ((*position + text_length + 1U) > buffer_size) {
    return -1;
  }

  memcpy(buffer + *position, text, text_length);
  *position += text_length;
  buffer[*position] = 0;
  *position += 1U;

  return 0;
}

/* rlogin wants terminal type and speed in the form "name/speed". */
static void client_build_terminal_text(const char *terminal_name, unsigned short terminal_speed, char *terminal_text, size_t terminal_text_size)
{
  unsigned short effective_speed;

  if ((terminal_name == NULL) || (terminal_text == NULL) || (terminal_text_size == 0U)) {
    return;
  }

  effective_speed = terminal_speed;
  if (effective_speed == 0U) {
    effective_speed = 38400U;
  }

  snprintf(terminal_text, terminal_text_size, "%s/%u", terminal_name, (unsigned int) effective_speed);
}

/* Build the standard rlogin handshake: NUL, local user, remote user, terminal. */
static int client_build_handshake_named(const char *local_user_name, const char *remote_user_name, const char *terminal_name, unsigned short terminal_speed, unsigned char *buffer, size_t buffer_size, size_t *handshake_size)
{
  size_t position;
  char terminal_text[64];

  if ((local_user_name == NULL) || (remote_user_name == NULL) || (terminal_name == NULL) || (buffer == NULL) || (handshake_size == NULL)) {
    return -1;
  }

  if (buffer_size < 8U) {
    return -1;
  }

  position = 0U;
  buffer[position++] = 0;
  client_build_terminal_text(terminal_name, terminal_speed, terminal_text, sizeof(terminal_text));
  if (client_append_field(buffer, buffer_size, &position, local_user_name) != 0) {
    return -1;
  }
  if (client_append_field(buffer, buffer_size, &position, remote_user_name) != 0) {
    return -1;
  }
  if (client_append_field(buffer, buffer_size, &position, terminal_text) != 0) {
    return -1;
  }

  *handshake_size = position;
  return 0;
}

/* Accept either a literal IP or a DNS-style host name. */
static int client_resolve_address_named(const char *host_name, unsigned short port_number, struct sockaddr_in *address)
{
  struct hostent *host_entry;
  ULONG host_value;

  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_port = htons(port_number);

  host_value = SocketLibInetAddr((const UBYTE *) host_name);
  if (host_value != INADDR_NONE) {
    address->sin_addr.s_addr = host_value;
    return 0;
  }

  host_entry = SocketLibGetHostByName((const UBYTE *) host_name);
  if ((host_entry == NULL) || (host_entry->h_addr == NULL)) {
    return -1;
  }

  memcpy(&address->sin_addr, host_entry->h_addr, sizeof(address->sin_addr));
  return 0;
}

int rlogin_connect_named(struct rlogin_connection *connection,
                         const char *host_name,
                         unsigned short port_number,
                         const char *local_user_name,
                         const char *remote_user_name,
                         const char *terminal_name,
                         unsigned short terminal_speed,
                         char *error_text,
                         int error_text_size)
{
  struct sockaddr_in server_address;
  unsigned char handshake[256];
  size_t handshake_size;
  int tcp_nodelay;
  long send_result;

  if ((connection == NULL) || (host_name == NULL) || (local_user_name == NULL) || (remote_user_name == NULL) || (terminal_name == NULL)) {
    client_set_error(error_text, error_text_size, "invalid rlogin connect request");
    return -1;
  }

  memset(connection, 0, sizeof(*connection));
  connection->socket_fd = -1;
  connection->urgent_mode_available = 1;
  strncpy(connection->remote_user, remote_user_name, sizeof(connection->remote_user) - 1U);
  connection->remote_user[sizeof(connection->remote_user) - 1U] = '\0';

  SocketBase = OpenLibrary("bsdsocket.library", 0);
  if (SocketBase == NULL) {
    client_set_error(error_text, error_text_size, "could not open bsdsocket.library");
    return -1;
  }

  connection->socket_base = SocketBase;
  connection->socket_errno = 0;
  SocketLibSetErrnoPtr(&connection->socket_errno, sizeof(connection->socket_errno));

  /* Host resolution and handshake building are split out for easier reuse. */
  if (client_resolve_address_named(host_name, port_number, &server_address) != 0) {
    client_set_error(error_text, error_text_size, "could not resolve host");
    rlogin_disconnect(connection);
    return -1;
  }

  if (client_build_handshake_named(local_user_name, remote_user_name, terminal_name, terminal_speed, handshake, sizeof(handshake), &handshake_size) != 0) {
    client_set_error(error_text, error_text_size, "could not build rlogin handshake");
    rlogin_disconnect(connection);
    return -1;
  }

  connection->socket_fd = SocketLibSocket(AF_INET, SOCK_STREAM, 0);
  if (connection->socket_fd < 0) {
    client_set_errorf(error_text, error_text_size, "socket creation failed", connection->socket_errno);
    rlogin_disconnect(connection);
    return -1;
  }

  tcp_nodelay = 1;
  SocketLibSetSockOpt(connection->socket_fd, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(tcp_nodelay));

  if (SocketLibConnect(connection->socket_fd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
    client_set_errorf(error_text, error_text_size, "connect failed", connection->socket_errno);
    rlogin_disconnect(connection);
    return -1;
  }

  send_result = SocketLibSend(connection->socket_fd, handshake, (long) handshake_size, 0);
  if (send_result != (long) handshake_size) {
    client_set_errorf(error_text, error_text_size, "rlogin handshake send failed", connection->socket_errno);
    rlogin_disconnect(connection);
    return -1;
  }

  connection->connected = 1;
  /* The first zero byte received after connect is the server-side ack. */
  connection->waiting_for_server_ack = 1;
  error_text[0] = '\0';
  return 0;
}

int rlogin_connect(struct rlogin_connection *connection, const struct door_config *config, const struct aedoor_context *door, char *error_text, int error_text_size)
{
  if ((connection == NULL) || (config == NULL) || (door == NULL)) {
    client_set_error(error_text, error_text_size, "invalid rlogin connect request");
    return -1;
  }

  /* remote_user overrides the prefixed caller name only when explicitly set. */
  return rlogin_connect_named(connection,
                              config->host,
                              config->port,
                              door->username,
                              config->remote_user[0] != '\0' ? config->remote_user : door->username,
                              config->terminal_type,
                              (door->baud_rate > 0) ? (unsigned short) door->baud_rate : config->terminal_speed,
                              error_text,
                              error_text_size);
}

void rlogin_disconnect(struct rlogin_connection *connection)
{
  if (connection == NULL) {
    return;
  }

  /* Socket close comes first, then the library handle. */
  if (connection->socket_fd >= 0) {
    SocketLibClose(connection->socket_fd);
    connection->socket_fd = -1;
  }
  if (connection->socket_base != NULL) {
    CloseLibrary(connection->socket_base);
    connection->socket_base = NULL;
    SocketBase = NULL;
  }

  connection->connected = 0;
  connection->waiting_for_server_ack = 0;
  connection->urgent_mode_available = 0;
}
