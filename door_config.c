/*
 * Flat text configuration loader for arblink.
 *
 * The config is intentionally simple so it can be edited easily on classic
 * Amiga systems and copied around without extra tooling.
 *
 * Important gotcha:
 * remote_user should normally be left blank for the shared multi-BBS service,
 * because the prefixed caller name is the value that service expects.
 */
#include "door_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shared helper for short parse and validation errors. */
static void config_set_error(char *error_text, int error_text_size, const char *message)
{
  if ((error_text == NULL) || (error_text_size <= 0)) {
    return;
  }

  strncpy(error_text, message, (size_t) error_text_size - 1U);
  error_text[error_text_size - 1] = '\0';
}

static int config_text_equal_folded(const char *left_text, const char *right_text)
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

static char *trim_whitespace(char *text)
{
  char *end;

  while ((*text != '\0') && isspace((unsigned char) *text)) {
    text++;
  }

  end = text + strlen(text);
  while ((end > text) && isspace((unsigned char) end[-1])) {
    end--;
  }
  *end = '\0';

  return text;
}

/* Accept either a numeric port or the traditional rlogin alias. */
static unsigned short config_parse_port_value(const char *value)
{
  long parsed_value;

  if (value == NULL) {
    return 513U;
  }

  if (config_text_equal_folded(value, "login")) {
    return 513U;
  }

  parsed_value = atol(value);
  if ((parsed_value <= 0) || (parsed_value > 65535)) {
    return 513U;
  }

  return (unsigned short) parsed_value;
}

/* Clamp empty or invalid values back to sensible door defaults. */
static void config_apply_bounds(struct door_config *config)
{
  if (config == NULL) {
    return;
  }

  if (config->host[0] == '\0') {
    strcpy(config->host, "127.0.0.1");
  }
  if ((config->port == 0U) || (config->port > 65535U)) {
    config->port = 513U;
  }
  if (config->terminal_type[0] == '\0') {
    strcpy(config->terminal_type, "ansi");
  }
  if (config->terminal_speed == 0U) {
    config->terminal_speed = 19200U;
  }
  if (config->terminal_columns == 0U) {
    config->terminal_columns = 80U;
  }
  if (config->terminal_rows == 0U) {
    config->terminal_rows = 24U;
  }
  if ((strcmp(config->newline_mode, "cr") != 0) &&
      (strcmp(config->newline_mode, "lf") != 0) &&
      (strcmp(config->newline_mode, "crlf") != 0)) {
    strcpy(config->newline_mode, "crlf");
  }
  if (config->debug_log[0] == '\0') {
    strcpy(config->debug_log, "rlogindoor.log");
  }
}

void config_set_defaults(struct door_config *config)
{
  if (config == NULL) {
    return;
  }

  /* These defaults keep the door runnable even when config loading fails. */
  strcpy(config->host, "127.0.0.1");
  config->port = 513;
  config->username_prefix[0] = '\0';
  config->remote_user[0] = '\0';
  strcpy(config->terminal_type, "ansi");
  config->terminal_speed = 19200;
  config->terminal_columns = 80;
  config->terminal_rows = 24;
  strcpy(config->newline_mode, "crlf");
  strcpy(config->debug_log, "RAM:rlogindoor.log");
  config->debug_enabled = 1;
  config->disable_paging = 1;
}

int config_load_file(const char *path, struct door_config *config, char *error_text, int error_text_size)
{
  FILE *handle;
  char line[256];
  int line_number;

  config_set_defaults(config);

  if ((path == NULL) || (*path == '\0')) {
    return 0;
  }

  handle = fopen(path, "r");
  if (handle == NULL) {
    config_set_error(error_text, error_text_size, "could not open config file");
    return -1;
  }

  line_number = 0;
  while (fgets(line, sizeof(line), handle) != NULL) {
    char *key;
    char *value;
    char *equals;

    line_number++;

    key = trim_whitespace(line);
    if ((*key == '\0') || (*key == '#') || (*key == ';')) {
      continue;
    }

    equals = strchr(key, '=');
    if (equals == NULL) {
      fclose(handle);
      config_set_error(error_text, error_text_size, "invalid config line");
      return -1;
    }

    *equals = '\0';
    value = trim_whitespace(equals + 1);
    key = trim_whitespace(key);

    /* Keep the accepted key set small and explicit for predictable behaviour. */
    if (strcmp(key, "host") == 0) {
      strncpy(config->host, value, sizeof(config->host) - 1U);
      config->host[sizeof(config->host) - 1U] = '\0';
    } else if (strcmp(key, "port") == 0) {
      config->port = config_parse_port_value(value);
    } else if (strcmp(key, "username_prefix") == 0) {
      strncpy(config->username_prefix, value, sizeof(config->username_prefix) - 1U);
      config->username_prefix[sizeof(config->username_prefix) - 1U] = '\0';
    } else if (strcmp(key, "remote_user") == 0) {
      strncpy(config->remote_user, value, sizeof(config->remote_user) - 1U);
      config->remote_user[sizeof(config->remote_user) - 1U] = '\0';
    } else if (strcmp(key, "terminal_type") == 0) {
      strncpy(config->terminal_type, value, sizeof(config->terminal_type) - 1U);
      config->terminal_type[sizeof(config->terminal_type) - 1U] = '\0';
    } else if (strcmp(key, "terminal_speed") == 0) {
      config->terminal_speed = (unsigned short) atoi(value);
    } else if (strcmp(key, "terminal_columns") == 0) {
      config->terminal_columns = (unsigned short) atoi(value);
    } else if (strcmp(key, "terminal_rows") == 0) {
      config->terminal_rows = (unsigned short) atoi(value);
    } else if (strcmp(key, "newline_mode") == 0) {
      strncpy(config->newline_mode, value, sizeof(config->newline_mode) - 1U);
      config->newline_mode[sizeof(config->newline_mode) - 1U] = '\0';
    } else if (strcmp(key, "debug_log") == 0) {
      strncpy(config->debug_log, value, sizeof(config->debug_log) - 1U);
      config->debug_log[sizeof(config->debug_log) - 1U] = '\0';
    } else if (strcmp(key, "debug_enabled") == 0) {
      config->debug_enabled = atoi(value) != 0;
    } else if (strcmp(key, "disable_paging") == 0) {
      config->disable_paging = atoi(value) != 0;
    } else {
      fclose(handle);
      config_set_error(error_text, error_text_size, "unknown config key");
      return -1;
    }
  }

  fclose(handle);
  config_apply_bounds(config);
  error_text[0] = '\0';
  return 0;
}
