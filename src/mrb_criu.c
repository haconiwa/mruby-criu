/*
** mrb_criu.c - CRIU class
**
** Copyright (c) MATSUMOTO Ryosuke 2014
**
** See Copyright Notice in LICENSE
*/

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <criu/criu.h>

#include "mruby.h"
#include "mruby/data.h"
#include "mrb_criu.h"

#define DONE mrb_gc_arena_restore(mrb, 0);

typedef struct {
  char *socket_file;
  char *images_dir;
  int pid;
  bool leave_running;
  bool evasive_devices;
  bool shell_job;
  bool file_locks;
  int log_level;
  int fd;
  char *log_file;
} mrb_criu_data;

static void mrb_criu_data_free(mrb_state *mrb, void *p)                           
{
  mrb_criu_data *data = (mrb_criu_data *)p;
  close(data->fd);
  mrb_free(mrb, data);
}

static const struct mrb_data_type mrb_criu_data_type = {
  "mrb_criu_data", mrb_criu_data_free,
};

static void mrb_criu_error(mrb_state *mrb, int ret)
{
  switch (ret) {
  case -EBADE:
    mrb_raise(mrb, E_RUNTIME_ERROR, "RPC has returned fail");
  case -ECONNREFUSED:
    mrb_raise(mrb, E_RUNTIME_ERROR, "Unable to connect to CRIU");
  case -ECOMM:
    mrb_raise(mrb, E_RUNTIME_ERROR, "Unable to send/recv msg to/from CRIU");
  case -EINVAL:
    mrb_raise(mrb, E_RUNTIME_ERROR, "CRIU doesn't support this type of request."
        "You should probably update CRIU");
  case -EBADMSG:
    mrb_raise(mrb, E_RUNTIME_ERROR, "Unexpected response from CRIU." 
        "You should probably update CRIU");
  default:
    mrb_raise(mrb, E_RUNTIME_ERROR, "Unknown error type code."
           "You should probably update CRIU");
  }
}

static mrb_value mrb_criu_init(mrb_state *mrb, mrb_value self)
{
  mrb_criu_data *data;

  data = (mrb_criu_data *)DATA_PTR(self);
  if (data) {
    mrb_free(mrb, data);
  }
  DATA_TYPE(self) = &mrb_criu_data_type;
  DATA_PTR(self) = NULL;

  data = (mrb_criu_data *)mrb_malloc(mrb, sizeof(mrb_criu_data));
  data->socket_file = NULL;
  data->images_dir = NULL;
  data->log_level = -1;
  data->log_file = NULL;
  DATA_PTR(self) = data;

  criu_init_opts();

  return self;
}

static mrb_value mrb_criu_dump(mrb_state *mrb, mrb_value self)
{
  int ret;
  ret = criu_dump();
  if (ret < 0) {
    mrb_criu_error(mrb, ret);
  }
  return mrb_fixnum_value(ret);
}

static mrb_value mrb_criu_restore(mrb_state *mrb, mrb_value self)
{
  int ret;
  ret = criu_restore();
  if (ret < 0) {
    mrb_criu_error(mrb, ret);
  }
  return mrb_fixnum_value(ret);
}

static mrb_value mrb_criu_check(mrb_state *mrb, mrb_value self)
{
  int ret;
  ret = criu_check();
  if (ret < 0) {
    mrb_criu_error(mrb, ret);
  }
  return mrb_fixnum_value(ret);
}

static mrb_value mrb_criu_set_service_address(mrb_state *mrb, mrb_value self)
{
  mrb_criu_data *data = DATA_PTR(self);
  char *socket_file;

  mrb_get_args(mrb, "z", &socket_file);

  criu_set_service_address(socket_file);
  data->socket_file = socket_file;

  return mrb_str_new_cstr(mrb, data->socket_file);
}

static mrb_value mrb_criu_set_images_dir(mrb_state *mrb, mrb_value self)
{
  mrb_criu_data *data = DATA_PTR(self);
  char *images_dir;
  int fd;

  mrb_get_args(mrb, "z", &images_dir);
  fd = open(images_dir, O_DIRECTORY);
  if (fd == -1) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "not found image directory.");
  }

  criu_set_images_dir_fd(fd);
  data->images_dir = images_dir;
  data->fd = fd;

  return mrb_fixnum_value(data->fd);
}

static mrb_value mrb_criu_set_pid(mrb_state *mrb, mrb_value self)
{
  mrb_criu_data *data = DATA_PTR(self);
  int pid;

  mrb_get_args(mrb, "i", &pid);
  criu_set_pid(pid);
  data->pid = pid;

  return mrb_fixnum_value(data->pid);
}

static mrb_value mrb_criu_set_shell_job(mrb_state *mrb, mrb_value self)
{
  mrb_criu_data *data = DATA_PTR(self);
  mrb_bool shell_job;

  mrb_get_args(mrb, "b", &shell_job);
  criu_set_shell_job(shell_job);
  data->shell_job = shell_job;

  return mrb_bool_value(data->shell_job);
}

static mrb_value mrb_criu_set_log_file(mrb_state *mrb, mrb_value self)
{
  mrb_criu_data *data = DATA_PTR(self);
  char *log_file;

  mrb_get_args(mrb, "z", &log_file);
  criu_set_log_file(log_file);
  data->log_file = log_file;

  if (data->log_level == -1) {
    data->log_level = 4;
    criu_set_log_level(data->log_level);
  }

  return mrb_str_new_cstr(mrb, data->log_file);
}

void mrb_mruby_criu_gem_init(mrb_state *mrb)
{
    struct RClass *criu;
    criu = mrb_define_class(mrb, "CRIU", mrb->object_class);
    mrb_define_method(mrb, criu, "initialize", mrb_criu_init, MRB_ARGS_NONE());
    mrb_define_method(mrb, criu, "dump", mrb_criu_dump, MRB_ARGS_NONE());
    mrb_define_method(mrb, criu, "restore", mrb_criu_restore, MRB_ARGS_NONE());
    mrb_define_method(mrb, criu, "check", mrb_criu_check, MRB_ARGS_NONE());
    mrb_define_method(mrb, criu, "set_service_address", mrb_criu_set_service_address, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, criu, "set_images_dir", mrb_criu_set_images_dir, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, criu, "set_pid", mrb_criu_set_pid, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, criu, "set_shell_job", mrb_criu_set_shell_job, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, criu, "set_log_file", mrb_criu_set_log_file, MRB_ARGS_REQ(1));
    DONE;
}

void mrb_mruby_criu_gem_final(mrb_state *mrb)
{
}
