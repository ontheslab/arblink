/*
 * rlogin -- standalone rlogin CLI client.
 *
 * Intentionally separate from the door so the transport can be tested
 * outside AmiExpress. Shares the rlogin_client transport layer with arblink.
 */
#include "rlogin_client.h"
#include "doorlog.h"
#include "door_version.h"
#include "socket_inline_local.h"

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <errno.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/filio.h>

/* Thin output wrappers keep the CLI code readable on classic Amiga DOS. */
static void cli_write_text_to(BPTR file_handle, const char *text)
{
  if ((file_handle != 0) && (text != NULL)) {
    Write(file_handle, (APTR) text, (LONG) strlen(text));
  }
}

static void cli_write_bytes_to(BPTR file_handle, const unsigned char *data, int length)
{
  if ((file_handle != 0) && (data != NULL) && (length > 0)) {
    Write(file_handle, (APTR) data, (LONG) length);
  }
}

static BPTR cli_open_console_handle(struct doorlog *log)
{
  struct Task *task;
  struct Process *process;

  task = FindTask(NULL);
  if ((task == NULL) || (task->tc_Node.ln_Type != NT_PROCESS)) {
    doorlog_write(log, "No current process was available for console lookup.");
    return 0;
  }

  process = (struct Process *) task;
  if (process->pr_ConsoleTask == NULL) {
    doorlog_write(log, "Current process has no console task.");
    return 0;
  }

  doorlog_write(log, "Current process console task located.");
  return (BPTR) process->pr_ConsoleTask;
}

/* Raw mode is required if we want immediate keypress behaviour from the console. */
static int cli_set_console_raw(BPTR file_handle, struct MsgPort *console_port, struct doorlog *log)
{
  long packet_result;

  if ((file_handle == 0) && (console_port == NULL)) {
    return -1;
  }

  if (console_port != NULL) {
    packet_result = DoPkt1(console_port, ACTION_SCREEN_MODE, -1L);
    doorlog_writef(log, "ACTION_SCREEN_MODE raw request returned %ld.", packet_result);
  }

  if ((file_handle != 0) && !SetMode(file_handle, -1L)) {
    doorlog_write(log, "SetMode raw request failed.");
    return -1;
  }

  return 0;
}

static void cli_restore_console_mode(BPTR file_handle, struct MsgPort *console_port, struct doorlog *log)
{
  long packet_result;

  if (console_port != NULL) {
    packet_result = DoPkt1(console_port, ACTION_SCREEN_MODE, 0L);
    doorlog_writef(log, "ACTION_SCREEN_MODE cooked request returned %ld.", packet_result);
  }

  if (file_handle != 0) {
    SetMode(file_handle, 0);
  }
}

static void cli_log_key(struct doorlog *log, const char *source_name, unsigned char input_char)
{
  if (log == NULL) {
    return;
  }

  if ((input_char >= 32U) && (input_char <= 126U)) {
    doorlog_writef(log, "%s key 0x%02x '%c'", source_name, (unsigned int) input_char, (char) input_char);
  } else {
    doorlog_writef(log, "%s key 0x%02x", source_name, (unsigned int) input_char);
  }
}

static int cli_send_key(struct rlogin_connection *connection, int key_value)
{
  unsigned char out_text[2];

  out_text[0] = (unsigned char) key_value;
  out_text[1] = 0;
  return (SocketLibSend(connection->socket_fd, out_text, 1, 0) == 1) ? 0 : -1;
}

static int cli_text_equal_folded(const char *left_text, const char *right_text)
{
  unsigned char left_char;
  unsigned char right_char;

  if ((left_text == NULL) || (right_text == NULL)) {
    return 0;
  }

  while ((*left_text != '\0') && (*right_text != '\0')) {
    left_char = (unsigned char) tolower((unsigned char) *left_text);
    right_char = (unsigned char) tolower((unsigned char) *right_text);
    if (left_char != right_char) {
      return 0;
    }

    left_text++;
    right_text++;
  }

  return (*left_text == '\0') && (*right_text == '\0');
}

/* Accept the conventional "login" alias as well as numeric ports. */
static int cli_parse_port_text(const char *port_text, unsigned short *port_number)
{
  long parsed_value;

  if ((port_text == NULL) || (port_number == NULL)) {
    return -1;
  }

  if (cli_text_equal_folded(port_text, "login")) {
    *port_number = 513;
    return 0;
  }

  parsed_value = atol(port_text);
  if ((parsed_value <= 0) || (parsed_value > 65535)) {
    return -1;
  }

  *port_number = (unsigned short) parsed_value;
  return 0;
}

int main(int argc, char **argv)
{
  struct rlogin_connection connection;
  struct doorlog log;
  unsigned char recv_buffer[512];
  char error_text[160];
  const char *host_name;
  const char *user_name;
  const char *terminal_name;
  BPTR input_handle;
  BPTR output_handle;
  struct MsgPort *console_port;
  unsigned short port_number;
  long recv_length;
  long chars_waiting;
  int nonblocking_mode;
  unsigned char input_char;
  int shell_input_raw_enabled;
  int log_opened;

  input_handle = Input();
  output_handle = Output();
  console_port = NULL;
  shell_input_raw_enabled = 0;
  log_opened = 0;
  memset(&log, 0, sizeof(log));

  if (argc < 4) {
    cli_write_text_to(output_handle, "Usage: rlogin <host> <port|login> <user> [terminal]\n");
    return 10;
  }

  cli_write_text_to(output_handle, "RLogin CLI " RLOGIN_VERSION "\r\n");

  host_name = argv[1];
  if (cli_parse_port_text(argv[2], &port_number) != 0) {
    cli_write_text_to(output_handle, "Invalid port. Use a number from 1 to 65535, or 'login' for 513.\n");
    return 10;
  }
  user_name = argv[3];
  terminal_name = (argc > 4) ? argv[4] : "ansi";

  if (doorlog_open(&log, "RAM:rlogin.log", 1) == 0) {
    log_opened = 1;
    doorlog_writef(&log, "CLI version %s", RLOGIN_VERSION);
    doorlog_writef(&log, "CLI start host %s port %u user %s terminal %s",
                   host_name,
                   (unsigned int) port_number,
                   user_name,
                   terminal_name);
  }

  console_port = (struct MsgPort *) cli_open_console_handle(log_opened ? &log : NULL);
  if (log_opened) {
    doorlog_writef(&log, "Input handle %ld output handle %ld console port %ld",
                   (long) input_handle,
                   (long) output_handle,
                   (long) console_port);
    doorlog_writef(&log, "Input handle interactive: %s",
                   IsInteractive(input_handle) ? "yes" : "no");
  }

  error_text[0] = '\0';
  if (rlogin_connect_named(&connection, host_name, port_number, user_name, user_name, terminal_name, 19200U, error_text, (int) sizeof(error_text)) != 0) {
    if (log_opened) {
      doorlog_writef(&log, "rlogin connect failed: %s", error_text);
      doorlog_close(&log);
    }
    cli_write_text_to(output_handle, "rlogin connect failed: ");
    cli_write_text_to(output_handle, error_text);
    cli_write_text_to(output_handle, "\n");
    return 20;
  }
  if (log_opened) {
    doorlog_write(&log, "rlogin connection opened.");
  }

  if (cli_set_console_raw(input_handle, console_port, log_opened ? &log : NULL) == 0) {
    shell_input_raw_enabled = 1;
    if (log_opened) {
      doorlog_write(&log, "Live shell console input switched to raw mode.");
    }
  } else if (log_opened) {
    doorlog_write(&log, "Could not switch the active input handle to raw mode.");
  }

  nonblocking_mode = 1;
  SocketLibIoctl(connection.socket_fd, FIONBIO, (char *) &nonblocking_mode);

  cli_write_text_to(output_handle, "Connected. Press Ctrl-C to quit.\r\n");
  cli_write_text_to(output_handle, "Shell input active.\r\n");

  /* Simple loop: show remote bytes, then drain any waiting local keypresses. */
  for (;;) {
    recv_length = SocketLibRecv(connection.socket_fd, recv_buffer, (long) sizeof(recv_buffer), 0);
    if (recv_length > 0) {
      cli_write_bytes_to(output_handle, recv_buffer, (int) recv_length);
    } else if (recv_length == 0) {
      cli_write_text_to(output_handle, "\nRemote host closed the connection.\n");
      break;
    } else if ((connection.socket_errno != EWOULDBLOCK) && (connection.socket_errno != EAGAIN)) {
      cli_write_text_to(output_handle, "\nSocket receive failed.\n");
      break;
    }

    chars_waiting = WaitForChar(input_handle, 0);
    while (chars_waiting != 0) {
      if (Read(input_handle, &input_char, 1) != 1) {
        if (log_opened) {
          doorlog_write(&log, "Active input handle read returned no byte.");
        }
        chars_waiting = 0;
        break;
      }

      if (log_opened) {
        cli_log_key(&log, "Shell", input_char);
      }
      if (input_char == 3U) {
        if (log_opened) {
          doorlog_write(&log, "Local Ctrl-C received.");
        }
        cli_write_text_to(output_handle, "\r\nLocal session ended.\r\n");
        chars_waiting = 0;
        goto cli_finish;
      }

      if (cli_send_key(&connection, input_char) != 0) {
        if (log_opened) {
          doorlog_writef(&log, "Socket send failed after local key (%ld).", connection.socket_errno);
        }
        cli_write_text_to(output_handle, "\nSocket send failed.\n");
        if (shell_input_raw_enabled) {
          cli_restore_console_mode(input_handle, console_port, &log);
        }
        if (log_opened) {
          doorlog_close(&log);
        }
        rlogin_disconnect(&connection);
        return 30;
      }

      chars_waiting = WaitForChar(input_handle, 0);
    }

    Delay(1);
  }

cli_finish:
  /* Always restore console modes before returning to the shell. */
  if (shell_input_raw_enabled) {
    cli_restore_console_mode(input_handle, console_port, log_opened ? &log : NULL);
  }
  if (log_opened) {
    doorlog_write(&log, "CLI session ended locally.");
    doorlog_close(&log);
  }
  rlogin_disconnect(&connection);
  return 0;
}
