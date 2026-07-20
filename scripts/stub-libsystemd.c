/* Stub libsystemd - satisfies linker at cross-compile time.
   Real libsystemd.so is present on target Orange Pi (Debian). */

#include <stddef.h>
#include <stdint.h>

typedef struct sd_bus sd_bus;
typedef struct sd_bus_message sd_bus_message;
typedef struct sd_bus_slot sd_bus_slot;
typedef struct sd_bus_creds sd_bus_creds;
typedef struct sd_event sd_event;
typedef struct sd_event_source sd_event_source;
typedef struct { int a; } sd_id128_t;

typedef struct {
    const char *name;
    const char *message;
    int _need_free;
} sd_bus_error;

typedef struct {
    const char *member;
    const char *result;
    const char *value;
} sd_bus_vtable;

typedef int (*sd_bus_message_handler_t)(sd_bus_message *, void *, sd_bus_error *);
typedef int (*sd_event_handler_t)(void *);
typedef int (*sd_event_io_handler_t)(int, uint32_t, void *);
typedef int (*sd_event_time_handler_t)(uint64_t, void *);
/* sd_event signal handler typedef removed - not needed for stub */

/* sd_bus */
int sd_bus_new(sd_bus **ret) { return 0; }
int sd_bus_open(sd_bus **ret) { return 0; }
int sd_bus_open_user(sd_bus **ret) { return 0; }
int sd_bus_open_system(sd_bus **ret) { return 0; }
int sd_bus_open_system_remote(sd_bus **ret) { return 0; }
sd_bus *sd_bus_close_unref(sd_bus *bus) { return NULL; }
sd_bus *sd_bus_flush_close_unref(sd_bus *bus) { return NULL; }
int sd_bus_flush(sd_bus *bus) { return 0; }
int sd_bus_get_fd(sd_bus *bus) { return -1; }
int sd_bus_get_events(sd_bus *bus) { return 0; }
int sd_bus_get_timeout(sd_bus *bus, uint64_t *ret) { if (ret) *ret = 0; return 0; }
int sd_bus_process(sd_bus *bus, sd_bus_message **ret) { return 0; }
int sd_bus_send(sd_bus *bus, sd_bus_message *msg, uint64_t *cookie) { return 0; }
int sd_bus_call(sd_bus *bus, sd_bus_message *msg, uint64_t usec, sd_bus_error *ret_error, sd_bus_message **reply) { return 0; }
int sd_bus_call_async(sd_bus *bus, sd_bus_slot **slot, sd_bus_message *msg, sd_bus_message_handler_t callback, void *userdata, uint64_t usec) { return 0; }
int sd_bus_get_current_message(sd_bus *bus, sd_bus_message **ret) { return 0; }
int sd_bus_get_n_queued_read(sd_bus *bus, uint64_t *ret) { if (ret) *ret = 0; return 0; }
int sd_bus_get_n_queued_write(sd_bus *bus, uint64_t *ret) { if (ret) *ret = 0; return 0; }
int sd_bus_get_unique_name(sd_bus *bus, const char **name) { if (name) *name = ""; return 0; }
int sd_bus_set_method_call_timeout(sd_bus *bus, uint64_t usec) { return 0; }
int sd_bus_get_method_call_timeout(sd_bus *bus, uint64_t *ret) { if (ret) *ret = 0; return 0; }
int sd_bus_request_name(sd_bus *bus, const char *name, uint64_t flags) { return 0; }
int sd_bus_release_name(sd_bus *bus, const char *name) { return 0; }
int sd_bus_set_bus_client(sd_bus *bus, int b) { return 0; }
int sd_bus_set_server(sd_bus *bus, int b, void *owner) { return 0; }
int sd_bus_set_trusted(sd_bus *bus, int b) { return 0; }
int sd_bus_set_address(sd_bus *bus, const char *address) { return 0; }
int sd_bus_set_fd(sd_bus *bus, int fd, int fd2) { return 0; }
int sd_bus_start(sd_bus *bus) { return 0; }
int sd_bus_add_object_vtable(sd_bus *bus, sd_bus_slot **slot, const char *path, const char *interface, const sd_bus_vtable *vtable, void *userdata) { return 0; }
int sd_bus_add_object_manager(sd_bus *bus, sd_bus_slot **slot, const char *prefix) { return 0; }
int sd_bus_add_match(sd_bus *bus, sd_bus_slot **slot, const char *match, sd_bus_message_handler_t handler, void *userdata) { return 0; }
int sd_bus_add_match_async(sd_bus *bus, sd_bus_slot **slot, const char *match, sd_bus_message_handler_t handler, sd_bus_message_handler_t install_cb, void *userdata) { return 0; }
int sd_bus_match_signal(sd_bus *bus, sd_bus_slot **slot, const char *sender, const char *path, const char *interface, const char *member, sd_bus_message_handler_t handler, void *userdata) { return 0; }
int sd_bus_emit_properties_changed_strv(sd_bus *bus, const char *path, const char *interface, char **names) { return 0; }
int sd_bus_emit_object_added(sd_bus *bus, const char *path) { return 0; }
int sd_bus_emit_object_removed(sd_bus *bus, const char *path) { return 0; }
int sd_bus_emit_interfaces_added_strv(sd_bus *bus, const char *path, char **interfaces) { return 0; }
int sd_bus_emit_interfaces_removed_strv(sd_bus *bus, const char *path, char **interfaces) { return 0; }
int sd_bus_query_sender_creds(sd_bus *bus, uint64_t mask, sd_bus_creds **creds) { return 0; }
int sd_bus_object_path_is_valid(const char *p) { return 1; }
int sd_bus_interface_name_is_valid(const char *p) { return 1; }
int sd_bus_member_name_is_valid(const char *p) { return 1; }
int sd_bus_service_name_is_valid(const char *p) { return 1; }

sd_bus_slot *sd_bus_slot_unref(sd_bus_slot *slot) { return NULL; }

/* sd_bus_message */
sd_bus_message *sd_bus_message_ref(sd_bus_message *msg) { return msg; }
sd_bus_message *sd_bus_message_unref(sd_bus_message *msg) { return NULL; }
int sd_bus_message_new(sd_bus *bus, sd_bus_message **msg, uint8_t type) { return 0; }
int sd_bus_message_new_method_call(sd_bus *bus, sd_bus_message **msg, const char *destination, const char *path, const char *interface, const char *member) { return 0; }
int sd_bus_message_new_method_return(sd_bus_message *call, sd_bus_message **msg) { return 0; }
int sd_bus_message_new_method_error(sd_bus_message *call, sd_bus_message **msg, const sd_bus_error *err) { return 0; }
int sd_bus_message_new_signal(sd_bus *bus, sd_bus_message **msg, const char *path, const char *interface, const char *member) { return 0; }
int sd_bus_message_set_destination(sd_bus_message *msg, const char *destination) { return 0; }
int sd_bus_message_get_interface(sd_bus_message *msg, const char **ret) { if (ret) *ret = ""; return 0; }
int sd_bus_message_get_member(sd_bus_message *msg, const char **ret) { if (ret) *ret = ""; return 0; }
int sd_bus_message_get_sender(sd_bus_message *msg, const char **ret) { if (ret) *ret = ""; return 0; }
int sd_bus_message_get_path(sd_bus_message *msg, const char **ret) { if (ret) *ret = ""; return 0; }
int sd_bus_message_get_destination(sd_bus_message *msg, const char **ret) { if (ret) *ret = ""; return 0; }
int sd_bus_message_get_cookie(sd_bus_message *msg, uint64_t *ret) { if (ret) *ret = 0; return 0; }
int sd_bus_message_get_reply_cookie(sd_bus_message *msg, uint64_t *ret) { if (ret) *ret = 0; return 0; }
int sd_bus_message_get_expect_reply(sd_bus_message *msg) { return 0; }
int sd_bus_message_set_expect_reply(sd_bus_message *msg, int b) { return 0; }
int sd_bus_message_peek_type(sd_bus_message *msg, char *type, const char *contents) { return 0; }
int sd_bus_message_is_empty(sd_bus_message *msg) { return 1; }
int sd_bus_message_at_end(sd_bus_message *msg, int b) { return 0; }
int sd_bus_message_rewind(sd_bus_message *msg, int b) { return 0; }
int sd_bus_message_seal(sd_bus_message *msg) { return 0; }
int sd_bus_message_copy(sd_bus_message *dst, sd_bus_message *src, int all) { return 0; }
int sd_bus_message_dump(sd_bus_message *msg, void *f, int flags) { return 0; }
int sd_bus_message_get_error(sd_bus_message *msg, sd_bus_error **ret) { return 0; }

/* sd_bus_message containers */
int sd_bus_message_enter_container(sd_bus_message *msg, char type, const char *contents) { return 0; }
int sd_bus_message_exit_container(sd_bus_message *msg) { return 0; }
int sd_bus_message_open_container(sd_bus_message *msg, char type, const char *contents) { return 0; }
int sd_bus_message_close_container(sd_bus_message *msg) { return 0; }

/* sd_bus_message read/write */
int sd_bus_message_read_basic(sd_bus_message *msg, char type, void *p) { return 0; }
int sd_bus_message_read_array(sd_bus_message *msg, char type, const void **p, size_t *sz) { return 0; }
int sd_bus_message_append_basic(sd_bus_message *msg, char type, const void *p) { return 0; }
int sd_bus_message_append_string_space(sd_bus_message *msg, const char *s, size_t len) { return 0; }
int sd_bus_message_append_array(sd_bus_message *msg, char type, const void *p, size_t len) { return 0; }

/* sd_bus_error */
int sd_bus_error_set(sd_bus_error *e, const char *name, const char *format, ...) { return 0; }
int sd_bus_error_set_errno(sd_bus_error *e, int err) { return 0; }
int sd_bus_error_is_set(const sd_bus_error *e) { return 0; }
void sd_bus_error_free(sd_bus_error *e) {}

/* sd_bus_creds */
sd_bus_creds *sd_bus_creds_ref(sd_bus_creds *c) { return c; }
sd_bus_creds *sd_bus_creds_unref(sd_bus_creds *c) { return NULL; }
int sd_bus_creds_get_pid(sd_bus_creds *c, uint32_t *pid) { return 0; }
int sd_bus_creds_get_uid(sd_bus_creds *c, uint32_t *uid) { return 0; }
int sd_bus_creds_get_euid(sd_bus_creds *c, uint32_t *euid) { return 0; }
int sd_bus_creds_get_gid(sd_bus_creds *c, uint32_t *gid) { return 0; }
int sd_bus_creds_get_egid(sd_bus_creds *c, uint32_t *egid) { return 0; }
int sd_bus_creds_get_supplementary_gids(sd_bus_creds *c, uint32_t **gids) { return 0; }
int sd_bus_creds_get_selinux_context(sd_bus_creds *c, const char **context) { return 0; }

/* sd_bus_object_vtable_format - global variable */
const sd_bus_vtable *sd_bus_object_vtable_format = NULL;

/* sd_event */
int sd_event_default(sd_event **ret) { return 0; }
sd_event *sd_event_ref(sd_event *e) { return e; }
sd_event *sd_event_unref(sd_event *e) { return NULL; }
int sd_event_add_io(sd_event *e, sd_event_source **s, int fd, uint32_t events, sd_event_io_handler_t handler, void *userdata) { return 0; }
int sd_event_add_time(sd_event *e, sd_event_source **s, int clockid, uint64_t usec, uint64_t accuracy, sd_event_time_handler_t handler, void *userdata) { return 0; }

sd_event_source *sd_event_source_disable_unref(sd_event_source *s) { return NULL; }
int sd_event_source_set_description(sd_event_source *s, const char *description) { return 0; }
int sd_event_source_set_enabled(sd_event_source *s, int enabled) { return 0; }
int sd_event_source_set_io_events(sd_event_source *s, uint32_t events) { return 0; }
int sd_event_source_set_prepare(sd_event_source *s, sd_event_handler_t handler) { return 0; }
int sd_event_source_set_priority(sd_event_source *s, int priority) { return 0; }
int sd_event_source_set_time(sd_event_source *s, uint64_t usec) { return 0; }

/* sd_id128 */
int sd_id128_randomize(sd_id128_t *ret) { return 0; }
