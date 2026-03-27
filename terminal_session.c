/*
 * Live terminal session bridge for arblink.
 *
 * The job here is to keep the BBS caller side and the remote rlogin socket
 * moving together without blocking either side for long.
 *
 * Important gotchas:
 * - AmiExpress input is polled with GETKEY before Hotkey is called
 * - some Amiga TCP stacks do not support urgent-data handling cleanly
 * - the first NUL byte after connect is the rlogin server acknowledgement
 */
#include "terminal_session.h"
#include "aedoor_bridge.h"
#include "doorlog.h"
#include "door_config.h"
#include "rlogin_client.h"
#include "socket_inline_local.h"

#include <errno.h>
#include <proto/dos.h>
#include <sys/filio.h>
#include <sys/sockio.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>

static const unsigned char session_up_arrow[] = { 0x1b, '[', 'A' };
static const unsigned char session_down_arrow[] = { 0x1b, '[', 'B' };
static const unsigned char session_right_arrow[] = { 0x1b, '[', 'C' };
static const unsigned char session_left_arrow[] = { 0x1b, '[', 'D' };
static const unsigned char session_window_size_reply_prefix[] = { 0xff, 0xff, 's', 's' };

/* Keep session errors short so they can be shown on-screen and in the log. */
static void session_set_error(char *error_text, int error_text_size, const char *message)
{
  if ((error_text == NULL) || (error_text_size <= 0)) {
    return;
  }

  strncpy(error_text, message, (size_t) error_text_size - 1U);
  error_text[error_text_size - 1] = '\0';
}

static int session_send_bytes(struct rlogin_connection *connection, const unsigned char *buffer, long send_length)
{
  if ((connection == NULL) || (buffer == NULL) || (send_length <= 0)) {
    return -1;
  }

  return (SocketLibSend(connection->socket_fd, buffer, send_length, 0) == send_length) ? 0 : -1;
}

static void session_write_u16(unsigned char *buffer, unsigned short value)
{
  buffer[0] = (unsigned char) ((value >> 8) & 0xffU);
  buffer[1] = (unsigned char) (value & 0xffU);
}

/* Reply to the classic rlogin window-size request using the configured size. */
static int session_send_window_size(struct rlogin_connection *connection, const struct door_config *config)
{
  unsigned char reply_text[12];
  unsigned short rows;
  unsigned short columns;

  if ((connection == NULL) || (config == NULL)) {
    return -1;
  }

  rows = config->terminal_rows;
  columns = config->terminal_columns;
  if (rows == 0U) {
    rows = 24U;
  }
  if (columns == 0U) {
    columns = 80U;
  }

  memcpy(reply_text, session_window_size_reply_prefix, sizeof(session_window_size_reply_prefix));
  session_write_u16(reply_text + 4, rows);
  session_write_u16(reply_text + 6, columns);
  session_write_u16(reply_text + 8, 0U);
  session_write_u16(reply_text + 10, 0U);
  return session_send_bytes(connection, reply_text, (long) sizeof(reply_text));
}

static int session_handle_urgent_byte(const struct door_config *config, struct aedoor_context *door, struct rlogin_connection *connection, unsigned char control_byte)
{
  if ((config == NULL) || (door == NULL) || (connection == NULL)) {
    return -1;
  }

  if (control_byte == 0x80U) {
    if (session_send_window_size(connection, config) != 0) {
      return -1;
    }
  }

  return 0;
}

/*
 * Some servers use urgent data for side-band control. If the stack cannot
 * support that path cleanly we disable it and continue with plain traffic.
 */
static int session_poll_urgent_data(const struct door_config *config, struct aedoor_context *door, struct rlogin_connection *connection, struct doorlog *log, char *error_text, int error_text_size)
{
  long at_mark;
  unsigned char control_byte;
  long recv_length;

  if ((config == NULL) || (door == NULL) || (connection == NULL)) {
    return -1;
  }

  if (!connection->urgent_mode_available) {
    return 0;
  }

  at_mark = 0;
  if (SocketLibIoctl(connection->socket_fd, SIOCATMARK, (char *) &at_mark) < 0) {
    if ((connection->socket_errno != EWOULDBLOCK) && (connection->socket_errno != EAGAIN)) {
      connection->urgent_mode_available = 0;
      if (config->debug_enabled) {
        doorlog_writef(log, "Urgent mode disabled after SIOCATMARK failure (%ld).", connection->socket_errno);
      }
    }

    return 0;
  }

  if (!at_mark) {
    return 0;
  }

  recv_length = SocketLibRecv(connection->socket_fd, &control_byte, 1, MSG_OOB);
  if (recv_length == 1) {
    if (config->debug_enabled) {
      if (control_byte == 0x80U) {
        doorlog_write(log, "[rlogin] Received window-size request.");
      } else if (control_byte == 0x02U) {
        doorlog_write(log, "[rlogin] Received flush request.");
      } else if (control_byte == 0x10U) {
        doorlog_write(log, "[rlogin] Remote requested raw flow mode.");
      } else if (control_byte == 0x20U) {
        doorlog_write(log, "[rlogin] Remote requested cooked flow mode.");
      } else {
        doorlog_writef(log, "[rlogin] Ignored unknown urgent control byte: 0x%02x", (unsigned int) control_byte);
      }
    }

    if (session_handle_urgent_byte(config, door, connection, control_byte) != 0) {
      session_set_error(error_text, error_text_size, "socket urgent control handling failed");
      return -1;
    }

    return 1;
  }

  if ((connection->socket_errno != EWOULDBLOCK) && (connection->socket_errno != EAGAIN)) {
    connection->urgent_mode_available = 0;
    if (config->debug_enabled) {
      doorlog_writef(log, "Urgent mode disabled after MSG_OOB read failure (%ld).", connection->socket_errno);
    }
  }

  return 0;
}

static int session_write_remote_data(struct aedoor_context *door, struct rlogin_connection *connection, const unsigned char *recv_buffer, int recv_length)
{
  unsigned char display_buffer[512];
  int read_index;
  int write_index;

  if ((door == NULL) || (connection == NULL) || (recv_buffer == NULL) || (recv_length <= 0)) {
    return 0;
  }

  /* Ignore the first rlogin ack byte and drop later embedded NUL bytes. */
  write_index = 0;
  for (read_index = 0; read_index < recv_length; read_index++) {
    if (connection->waiting_for_server_ack && (recv_buffer[read_index] == 0U)) {
      connection->waiting_for_server_ack = 0;
      continue;
    }

    connection->waiting_for_server_ack = 0;
    if (recv_buffer[read_index] == 0U) {
      continue;
    }

    display_buffer[write_index++] = recv_buffer[read_index];
  }

  if (write_index > 0) {
    aedoor_write_bytes(door, display_buffer, write_index);
  }

  return write_index;
}

static int session_wait_briefly(struct rlogin_connection *connection, char *error_text, int error_text_size)
{
  fd_set read_set;
  fd_set except_set;
  struct timeval timeout_value;
  long wait_result;

  if (connection == NULL) {
    return -1;
  }

  FD_ZERO(&read_set);
  FD_ZERO(&except_set);
  FD_SET((int) connection->socket_fd, &read_set);
  FD_SET((int) connection->socket_fd, &except_set);

  timeout_value.tv_secs = 0;
  timeout_value.tv_micro = 20000;
  wait_result = SocketLibWaitSelect(connection->socket_fd + 1,
                                    &read_set,
                                    NULL,
                                    &except_set,
                                    &timeout_value,
                                    NULL);
  if (wait_result < 0) {
    session_set_error(error_text, error_text_size, "socket wait failed");
    return -1;
  }

  return 0;
}

/* Translate local key values into what the remote ANSI side expects. */
static int session_send_key(const struct door_config *config, const struct aedoor_context *door, struct rlogin_connection *connection, long key_value)
{
  unsigned char out_text[2];
  const char *newline_text;

  if ((config == NULL) || (door == NULL) || (connection == NULL)) {
    return -1;
  }

  if ((key_value == '\r') || (key_value == '\n')) {
    newline_text = "\r\n";
    if (strcmp(config->newline_mode, "cr") == 0) {
      newline_text = "\r";
    } else if (strcmp(config->newline_mode, "lf") == 0) {
      newline_text = "\n";
    }

    return session_send_bytes(connection, (const unsigned char *) newline_text, (long) strlen(newline_text));
  }

  if (door->raw_arrow_enabled) {
    if (key_value == 2) {
      return session_send_bytes(connection, session_left_arrow, (long) sizeof(session_left_arrow));
    }
    if (key_value == 3) {
      return session_send_bytes(connection, session_right_arrow, (long) sizeof(session_right_arrow));
    }
    if (key_value == 4) {
      return session_send_bytes(connection, session_up_arrow, (long) sizeof(session_up_arrow));
    }
    if (key_value == 5) {
      return session_send_bytes(connection, session_down_arrow, (long) sizeof(session_down_arrow));
    }
  }

  out_text[0] = (unsigned char) key_value;
  out_text[1] = 0;
  return session_send_bytes(connection, out_text, 1);
}

int terminal_session_run(const struct door_config *config, struct aedoor_context *door, struct rlogin_connection *connection, struct doorlog *log, char *error_text, int error_text_size)
{
  unsigned char recv_buffer[512];
  char line[160];
  long key_value;
  long recv_length;
  int nonblocking_mode;
  int key_status;
  int got_activity;

  if ((config == NULL) || (door == NULL) || (connection == NULL)) {
    session_set_error(error_text, error_text_size, "invalid terminal session request");
    return -1;
  }

  if (!connection->connected) {
    session_set_error(error_text, error_text_size, "rlogin connection is not active");
    return -1;
  }

  nonblocking_mode = 1;
  SocketLibIoctl(connection->socket_fd, FIONBIO, (char *) &nonblocking_mode);
  aedoor_set_cursor(door, 1);

  /* Notify the caller that the remote connection is up. */
  aedoor_write_line(door, "Connected to remote rlogin service.");

  if (config->debug_enabled) {
    snprintf(line, sizeof(line), "Session started for caller %s", door->username);
    doorlog_write(log, line);

    snprintf(line, sizeof(line), "Node device %s unit %d baud %d ansi %s",
             door->node_device[0] != '\0' ? door->node_device : "(unknown)",
             door->node_unit,
             door->baud_rate,
             door->ansi_capable ? "yes" : "no");
    doorlog_write(log, line);

    snprintf(line, sizeof(line), "Remote user %s terminal %s speed %u size %ux%u newline %s",
             connection->remote_user,
             config->terminal_type,
             (unsigned int) (door->baud_rate > 0 ? (unsigned short) door->baud_rate : config->terminal_speed),
             (unsigned int) config->terminal_columns,
             (unsigned int) config->terminal_rows,
             config->newline_mode);
    doorlog_write(log, line);
  }

  /* Main full-duplex loop: remote read, caller poll, then a short idle wait. */
  for (;;) {
    got_activity = 0;

    key_status = session_poll_urgent_data(config, door, connection, log, error_text, error_text_size);
    if (key_status < 0) {
      doorlog_writef(log, "Session ended with urgent-data error: %s", error_text);
      return -1;
    }
    if (key_status > 0) {
      got_activity = 1;
    }

    recv_length = SocketLibRecv(connection->socket_fd, recv_buffer, (long) sizeof(recv_buffer), 0);
    if (recv_length > 0) {
      session_write_remote_data(door, connection, recv_buffer, (int) recv_length);
      got_activity = 1;
    } else if (recv_length == 0) {
      doorlog_write(log, "Remote host closed the connection.");
      aedoor_write_line(door, "");
      aedoor_write_line(door, "Remote host closed the connection.");
      break;
    } else if ((connection->socket_errno != EWOULDBLOCK) && (connection->socket_errno != EAGAIN)) {
      session_set_error(error_text, error_text_size, "socket receive failed");
      doorlog_writef(log, "Session ended with receive error: %s (%ld)", error_text, connection->socket_errno);
      return -1;
    }

    do {
      key_status = aedoor_poll_key(door, &key_value);
      if (key_status < 0) {
        doorlog_write(log, "Carrier lost.");
        aedoor_write_line(door, "");
        aedoor_write_line(door, "Carrier lost.");
        return 0;
      }
      if (key_status > 0) {
        got_activity = 1;
        if (session_send_key(config, door, connection, key_value) != 0) {
          session_set_error(error_text, error_text_size, "socket send failed");
          doorlog_writef(log, "Session ended with send error: %s (%ld)", error_text, connection->socket_errno);
          return -1;
        }
      }
    } while (key_status > 0);

    if (!got_activity) {
      if (session_wait_briefly(connection, error_text, error_text_size) != 0) {
        doorlog_writef(log, "Session ended with wait error: %s (%ld)", error_text, connection->socket_errno);
        return -1;
      }
    }
  }

  doorlog_write(log, "Session ended normally.");
  error_text[0] = '\0';
  return 0;
}
