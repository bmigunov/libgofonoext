/*
 * Copyright (C) 2016 Jolla Ltd.
 * Contact: Slava Monich <slava.monich@jolla.com>
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the name of the Jolla Ltd nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gofonoext_mm.h"
#include "gofonoext_version.h"

#include "gofono_modem.h"

#include <gutil_log.h>

#define RET_OK          (0)
#define RET_NOTFOUND    (1)
#define RET_ERR         (2)
#define RET_TIMEOUT     (3)

typedef struct action Action;

enum event_id {
    EVENT_VALID, /* Must be first */
    EVENT_ENABLED_MODEMS,
    EVENT_PRESENT_SIMS,
    EVENT_VOICE_IMSI,
    EVENT_VOICE_MODEM,
    EVENT_DATA_IMSI,
    EVENT_DATA_MODEM,
    EVENT_MMS_IMSI,
    EVENT_MMS_MODEM,
    EVENT_SIM_COUNT,
    EVENT_ACTIVE_SIM_COUNT,
    EVENT_READY,
    EVENT_COUNT
};

typedef struct app {
    gint timeout;
    gint active;
    GMainLoop* loop;
    OfonoExtModemManager* mm;
    gulong event_id[EVENT_COUNT];
    Action* actions;
    gboolean monitor;
    int ret;
} App;

typedef void (*ActionFunc)(App* app, const char* param);

typedef struct action {
    Action* next;
    ActionFunc fn;
    char* param;
} Action;

static
void
app_run_actions(
    App* app);

static
void
app_add_action(
    App* app,
    ActionFunc fn,
    const char* param)
{
    Action* action = g_new0(Action,1);
    action->fn = fn;
    action->param = g_strdup(param);
    if (app->actions) {
        Action* ptr = app->actions;
        while (ptr->next) ptr = ptr->next;
        ptr->next = action;
    } else {
        app->actions = action;
    }
}

static
GString*
mm_format_bools(
    GString* buf,
    const gboolean* values,
    guint count)
{
    guint i;
    if (buf) {
        g_string_truncate(buf, 0);
    } else {
        buf = g_string_new(NULL);
    }
    g_string_append(buf, "[");
    for (i=0; i<count; i++) {
        if (i > 0) g_string_append(buf, ", ");
        g_string_append(buf, values[i] ? "true" : "false");
    }
    g_string_append(buf, "]");
    return buf;
}

static
GString*
mm_format_strv(
    GString* buf,
    const GStrV* sv)
{
    const GStrV* ptr;
    if (buf) {
        g_string_truncate(buf, 0);
    } else {
        buf = g_string_new(NULL);
    }
    g_string_append(buf, "[");
    for (ptr = sv; *ptr; ptr++) {
        if (ptr > sv) g_string_append(buf, ", ");
        g_string_append(buf, *ptr);
    }
    g_string_append(buf, "]");
    return buf;
}

static
void
mm_enabled_modems_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    GString* buf = NULL;
    buf = mm_format_strv(buf, mm->enabled);
    GDEBUG("Enabled modems: %s", buf->str);
    g_string_free(buf, TRUE);
}

static
void
mm_present_sims_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    GString* buf = NULL;
    buf = mm_format_bools(buf, mm->present_sims, mm->sim_count);
    GDEBUG("Present SIMs: %s", buf->str);
    g_string_free(buf, TRUE);
}

static
void
mm_data_imsi_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    GDEBUG("Data SIM: %s", mm->data_imsi);
}

static
void
mm_data_modem_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    GDEBUG("Data modem: %s", ofono_modem_path(mm->data_modem));
}

static
void
mm_voice_imsi_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    GDEBUG("Voice SIM: %s", mm->voice_imsi);
}

static
void
mm_voice_modem_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    GDEBUG("Voice modem: %s", ofono_modem_path(mm->voice_modem));
}

static
void
mm_mms_imsi_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    GDEBUG("MMS SIM: %s", mm->mms_imsi);
}

static
void
mm_mms_modem_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    GDEBUG("MMS modem: %s", ofono_modem_path(mm->mms_modem));
}

static
void
mm_sim_count_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    GDEBUG("SIM count: %u", mm->sim_count);
}

static
void
mm_active_sim_count_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    GDEBUG("Active SIM count: %u", mm->active_sim_count);
}

static
void
mm_ready_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    GDEBUG("Ready: %s", mm->ready ? "yes" : "no");
}

static
void
mm_valid(
    App* app)
{
    GString* buf = NULL;
    GDEBUG("ofono is running");
    buf = mm_format_strv(buf, app->mm->available);
    printf("Ready: %s\n", app->mm->ready ? "yes" : "no");
    printf("Available modems: %s\n", buf->str);
    buf = mm_format_strv(buf, app->mm->enabled);
    printf("Enabled modems: %s\n", buf->str);
    buf = mm_format_strv(buf, app->mm->imei);
    printf("IMEI: %s\n", buf->str);
    buf = mm_format_bools(buf, app->mm->present_sims, app->mm->sim_count);
    printf("Present SIMs: %s\n", buf->str);
    printf("Voice SIM: %s\n", app->mm->voice_imsi);
    printf("Data SIM: %s\n", app->mm->data_imsi);
    printf("MMS SIM: %s\n", app->mm->mms_imsi);
    printf("Voice modem: %s\n", ofono_modem_path(app->mm->voice_modem));
    printf("Data modem: %s\n", ofono_modem_path(app->mm->data_modem));
    printf("MMS modem: %s\n", ofono_modem_path(app->mm->mms_modem));
    printf("Modem count: %u\n", app->mm->modem_count);
    printf("SIM count: %u\n", app->mm->sim_count);
    printf("Active SIM count: %u\n", app->mm->active_sim_count);
    g_string_free(buf, TRUE);
    app_run_actions(app);
    if (app->monitor) {
        ofonoext_mm_remove_handlers(app->mm, app->event_id+1, EVENT_COUNT-1);
        app->event_id[EVENT_ENABLED_MODEMS] =
            ofonoext_mm_add_enabled_modems_changed_handler(app->mm,
                mm_enabled_modems_changed, app);
        app->event_id[EVENT_PRESENT_SIMS] =
            ofonoext_mm_add_present_sims_changed_handler(app->mm,
                mm_present_sims_changed, app);
        app->event_id[EVENT_VOICE_IMSI] =
            ofonoext_mm_add_voice_imsi_changed_handler(app->mm,
                mm_voice_imsi_changed, app);
        app->event_id[EVENT_VOICE_MODEM] =
            ofonoext_mm_add_voice_modem_changed_handler(app->mm,
                mm_voice_modem_changed, app);
        app->event_id[EVENT_DATA_IMSI] =
            ofonoext_mm_add_data_imsi_changed_handler(app->mm,
                mm_data_imsi_changed, app);
        app->event_id[EVENT_DATA_MODEM] =
            ofonoext_mm_add_data_modem_changed_handler(app->mm,
                mm_data_modem_changed, app);
        app->event_id[EVENT_MMS_IMSI] =
            ofonoext_mm_add_mms_imsi_changed_handler(app->mm,
                mm_mms_imsi_changed, app);
        app->event_id[EVENT_MMS_MODEM] =
            ofonoext_mm_add_mms_modem_changed_handler(app->mm,
                mm_mms_modem_changed, app);
        app->event_id[EVENT_SIM_COUNT] =
            ofonoext_mm_add_sim_count_changed_handler(app->mm,
                mm_sim_count_changed, app);
        app->event_id[EVENT_ACTIVE_SIM_COUNT] =
            ofonoext_mm_add_active_sim_count_changed_handler(app->mm,
                mm_active_sim_count_changed, app);
        app->event_id[EVENT_READY] =
            ofonoext_mm_add_ready_changed_handler(app->mm,
                mm_ready_changed, app);
    } else if (!app->active) {
        g_main_loop_quit(app->loop);
    }
}

static
void
mm_valid_changed(
    OfonoExtModemManager* mm,
    void* arg)
{
    App* app = arg;
    if (mm->valid) {
        mm_valid(app);
    } else {
        /* Detach all handlers except EVENT_VALID which is the first one */
        ofonoext_mm_remove_handlers(app->mm, app->event_id+1, EVENT_COUNT-1);
        GDEBUG("ofono has disappeared");
    }
}

static
void
app_action_done(
    App* app)
{
    app->active--;
    app_run_actions(app);
    if (!app->active && !app->monitor) {
        g_main_loop_quit(app->loop);
    }
}

static
void
app_action_mms_sim_done(
    OfonoExtModemManager* mm,
    const char* path,
    const GError* error,
    void* data)
{
    App* app = data;
    if (error) {
        GVERBOSE("failed");
    } else {
        GVERBOSE("MMS path %s", path);
    }
    app_action_done(app);
}

static
void
action_mms_sim(
    App* app,
    const char* imsi)
{
    app->active++;
    ofonoext_mm_set_mms_imsi_full(app->mm, imsi, app_action_mms_sim_done, app);
}

static
void
app_run_actions(
    App* app)
{
    while (app->actions && !app->active) {
        Action* action = app->actions;
        app->actions = action->next;
        action->fn(app, action->param);
        g_free(action->param);
        g_free(action);
    }
}

static
int
app_run(
    App* app)
{
    app->mm = ofonoext_mm_new();
    app->ret = RET_ERR;
    app->loop = g_main_loop_new(NULL, FALSE);
    if (app->timeout > 0) GDEBUG("Timeout %d sec", app->timeout);
    if (app->mm->valid) {
        mm_valid(app);
        if (app->active) {
            g_main_loop_run(app->loop);
        }
    } else {
        app->event_id[EVENT_VALID] =
            ofonoext_mm_add_valid_changed_handler(app->mm,
                mm_valid_changed, app);
        g_main_loop_run(app->loop);
        ofonoext_mm_remove_handlers(app->mm, app->event_id, EVENT_COUNT);
    }
    g_main_loop_unref(app->loop);
    ofonoext_mm_unref(app->mm);
    return app->ret;
}

static
gboolean
app_opt_verbose(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    gutil_log_default.level = GLOG_LEVEL_VERBOSE;
    return TRUE;
}

static
gboolean
app_opt_mms_sim(
    const gchar* name,
    const gchar* value,
    gpointer app,
    GError** error)
{
    app_add_action(app, action_mms_sim, value);
    return TRUE;
}

static
gboolean
app_init(
    App* app,
    int argc,
    char* argv[])
{
    gboolean ok = FALSE;
    GOptionEntry entries[] = {
        { "verbose", 'v', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          app_opt_verbose, "Enable verbose output", NULL },
        { "timeout", 't', 0, G_OPTION_ARG_INT,
          &app->timeout, "Timeout in seconds", "SECONDS" },
        { "monitor", 'm', 0, G_OPTION_ARG_NONE,
          &app->monitor, "Monitor events", NULL },
        { NULL }
    };
    GOptionEntry action_entries[] = {
        { "mms-sim", 0, 0, G_OPTION_ARG_CALLBACK,
          &app_opt_mms_sim, "Select SIM for MMS", "IMSI" },
        { NULL }
    };
    GError* error = NULL;
    GOptionContext* options = g_option_context_new("[INTERFACE]");
    GOptionGroup* actions = g_option_group_new("actions",
        "Actions:", "Show all actions", app, NULL);
    g_option_context_add_main_entries(options, entries, NULL);
    g_option_group_add_entries(actions, action_entries);
    g_option_context_add_group(options, actions);
    if (g_option_context_parse(options, &argc, &argv, &error)) {
        if (argc == 1) {
            ok = TRUE;
        } else {
            char* help = g_option_context_get_help(options, TRUE, NULL);
            fprintf(stderr, "%s", help);
            g_free(help);
        }
    } else {
        GERR("%s", error->message);
        g_error_free(error);
    }
    g_option_context_free(options);
    return ok;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    App app;
    memset(&app, 0, sizeof(app));
    app.timeout = -1;
    gutil_log_timestamp = FALSE;
    gutil_log_set_type(GLOG_TYPE_STDERR, "test");
    gutil_log_default.level = GLOG_LEVEL_DEFAULT;
    if (app_init(&app, argc, argv)) {
        ret = app_run(&app);
    }
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
