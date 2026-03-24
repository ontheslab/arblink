#ifndef RLOGIN_CLIENT_H
#define RLOGIN_CLIENT_H

struct door_config;
struct aedoor_context;

/* Shared transport state used by both the door and the standalone CLI. */
struct Library;
struct door_config;
struct aedoor_context;

struct rlogin_connection {
  int connected;
  int waiting_for_server_ack;
  int urgent_mode_available;
  long socket_fd;
  long socket_errno;
  struct Library *socket_base;
  char remote_user[64];
};

/* Low-level transport entry points. */
int rlogin_connect_named(struct rlogin_connection *connection,
                          const char *host_name,
                          unsigned short port_number,
                          const char *local_user_name,
                          const char *remote_user_name,
                          const char *terminal_name,
                          unsigned short terminal_speed,
                          char *error_text,
                          int error_text_size);
int rlogin_connect(struct rlogin_connection *connection, const struct door_config *config, const struct aedoor_context *door, char *error_text, int error_text_size);
void rlogin_disconnect(struct rlogin_connection *connection);

#endif
