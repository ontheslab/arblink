/*
 * arblink main door entry point.
 *
 * This file owns the top-level control flow only:
 * - load configuration with safe fallbacks
 * - open AEDoor and fetch the caller identity
 * - connect to the remote rlogin service
 * - hand off the live session to the terminal bridge loop
 * - restore local state on every exit path
 *
 * Important gotcha:
 * AmiExpress XIM launches pass the AEDoor node argument in argv[1].
 * An explicit config path, when used, is expected in argv[2].
 */
#include "aedoor_bridge.h"
#include "doorlog.h"
#include "door_config.h"
#include "door_version.h"
#include "rlogin_client.h"
#include "terminal_session.h"

#include <stdio.h>
#include <string.h>

/* Try the requested path first, then the standard in-drawer paths. */
static int load_config_with_fallbacks(const char **used_path, const char *requested_path, struct door_config *config, char *error_text, int error_text_size)
{
  static const char *default_paths[] = {
    "PROGDIR:arblink.cfg",
    "arblink.cfg",
    "PROGDIR:rlogindoor.cfg",  /* legacy name -- accepted for backward compatibility */
    "rlogindoor.cfg"           /* legacy name -- accepted for backward compatibility */
  };
  int status;
  int index;

  if ((used_path == NULL) || (config == NULL)) {
    return -1;
  }

  if ((requested_path != NULL) && (*requested_path != '\0')) {
    status = config_load_file(requested_path, config, error_text, error_text_size);
    if (status == 0) {
      *used_path = requested_path;
      return 0;
    }
  }

  for (index = 0; index < (int) (sizeof(default_paths) / sizeof(default_paths[0])); index++) {
    status = config_load_file(default_paths[index], config, error_text, error_text_size);
    if (status == 0) {
      *used_path = default_paths[index];
      return 0;
    }
  }

  *used_path = requested_path;
  config_set_defaults(config);
  return -1;
}

static int path_looks_like_config(const char *text)
{
  size_t text_length;

  if ((text == NULL) || (*text == '\0')) {
    return 0;
  }

  if ((strchr(text, ':') != NULL) || (strchr(text, '/') != NULL) || (strchr(text, '\\') != NULL)) {
    return 1;
  }

  text_length = strlen(text);
  if ((text_length >= 4U) && (strcmp(text + text_length - 4U, ".cfg") == 0)) {
    return 1;
  }

  return 0;
}

int main(int argc, char **argv)
{
  const char *config_path;
  const char *requested_config_path;
  struct door_config config;
  struct aedoor_context door;
  struct doorlog log;
  struct rlogin_connection connection;
  char error_text[160];
  int status;

  requested_config_path = NULL;
  if ((argc > 2) && (argv[2] != NULL) && (argv[2][0] != '\0')) {
    requested_config_path = argv[2];
  } else if ((argc > 1) && path_looks_like_config(argv[1])) {
    requested_config_path = argv[1];
  }
  config_path = requested_config_path != NULL ? requested_config_path : "PROGDIR:arblink.cfg";

  memset(&door, 0, sizeof(door));
  memset(&log, 0, sizeof(log));
  memset(&connection, 0, sizeof(connection));
  error_text[0] = '\0';

  /* Configuration and logging come first so early startup failures are visible. */
  status = load_config_with_fallbacks(&config_path, requested_config_path, &config, error_text, (int) sizeof(error_text));
  if (status != 0) {
    printf("Config warning: %s (%s)\n", error_text, config_path != NULL ? config_path : "(default search)");
    printf("Continuing with defaults so the skeleton remains runnable.\n");
  }

  if (doorlog_open(&log, config.debug_log, config.debug_enabled) != 0) {
    printf("Log warning: could not open %s\n", config.debug_log);
  } else if (config.debug_enabled) {
    doorlog_writef(&log, "Door version %s", ARBLINK_VERSION);
    doorlog_writef(&log, "Door start with config %s", config_path);
    if ((argc > 1) && (argv[1] != NULL)) {
      doorlog_writef(&log, "argv[1]=%s", argv[1]);
    }
    if ((argc > 2) && (argv[2] != NULL)) {
      doorlog_writef(&log, "argv[2]=%s", argv[2]);
    }
    if (status != 0) {
      doorlog_writef(&log, "Config load warning: %s", error_text);
      doorlog_write(&log, "Continuing with default configuration values.");
    }
  }

  /* Open the BBS-side door session before any remote work is attempted. */
  status = aedoor_open(&door, argc, argv, error_text, (int) sizeof(error_text));
  if (status != 0) {
    doorlog_writef(&log, "AEDoor open failed: %s", error_text);
    printf("AEDoor open failed: %s\n", error_text);
    doorlog_close(&log);
    return 10;
  }
  aedoor_write_line(&door, "RLogin Door " ARBLINK_VERSION);
  doorlog_write(&log, "AEDoor opened.");

  status = aedoor_fetch_username(&door, &config, error_text, (int) sizeof(error_text));
  if (status != 0) {
    doorlog_writef(&log, "AEDoor username fetch failed: %s", error_text);
    aedoor_write_line(&door, "Door start failed while fetching the caller name.");
    aedoor_write_line(&door, error_text);
    printf("AEDoor username fetch failed: %s\n", error_text);
    aedoor_close(&door);
    doorlog_close(&log);
    return 20;
  }
  doorlog_writef(&log, "Caller name resolved to %s", door.username);

  /* Prepare AmiExpress state for full-screen remote output. */
  status = aedoor_prepare_session(&door, &config, error_text, (int) sizeof(error_text));
  if (status != 0) {
    doorlog_writef(&log, "AEDoor session preparation failed: %s", error_text);
    aedoor_write_line(&door, "Door start failed while preparing the session.");
    aedoor_write_line(&door, error_text);
    printf("AEDoor session prep failed: %s\n", error_text);
    aedoor_close(&door);
    doorlog_close(&log);
    return 30;
  }
  doorlog_writef(&log, "Session prepared for node device %s unit %d baud %d", door.node_device, door.node_unit, door.baud_rate);

  /*
   * By default the remote user name follows the local caller name after any
   * configured prefix has been added. The config can still override it.
   */
  status = rlogin_connect(&connection, &config, &door, error_text, (int) sizeof(error_text));
  if (status != 0) {
    doorlog_writef(&log, "rlogin connect failed: %s", error_text);
    aedoor_write_line(&door, "Remote connection failed.");
    aedoor_write_line(&door, error_text);
    printf("rlogin connect failed: %s\n", error_text);
    aedoor_restore_session(&door);
    aedoor_close(&door);
    doorlog_close(&log);
    return 40;
  }
  doorlog_writef(&log, "rlogin connected to %s:%u as local %s remote %s",
                 config.host,
                 (unsigned int) config.port,
                 door.username,
                 connection.remote_user);

  /* The terminal bridge loop handles all live caller and remote traffic from here. */
  status = terminal_session_run(&config, &door, &connection, &log, error_text, (int) sizeof(error_text));
  if (status != 0) {
    doorlog_writef(&log, "Terminal session failed: %s", error_text);
    aedoor_write_line(&door, "Terminal session failed.");
    aedoor_write_line(&door, error_text);
    printf("terminal session failed: %s\n", error_text);
  }

  /* Always restore AEDoor state, even after a transport failure. */
  doorlog_write(&log, "Closing transport and restoring AEDoor state.");
  rlogin_disconnect(&connection);
  aedoor_restore_session(&door);
  aedoor_close(&door);
  doorlog_close(&log);

  return status == 0 ? 0 : 50;
}
