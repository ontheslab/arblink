#ifndef TERMINAL_SESSION_H
#define TERMINAL_SESSION_H

/* Forward declarations keep the session header small and cheap to include. */
struct door_config;
struct aedoor_context;
struct doorlog;
struct rlogin_connection;

/* Run the live caller-to-remote session until one side disconnects. */
int terminal_session_run(const struct door_config *config, struct aedoor_context *door, struct rlogin_connection *connection, struct doorlog *log, char *error_text, int error_text_size);

#endif
