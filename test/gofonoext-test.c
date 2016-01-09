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

#include <gutil_log.h>

#define RET_OK          (0)
#define RET_NOTFOUND    (1)
#define RET_ERR         (2)
#define RET_TIMEOUT     (3)

typedef struct app {
    gint timeout;
    GMainLoop* loop;
    OfonoExtModemManager* mm;
    gulong mm_valid_id;
    int ret;
} App;

static
void
mm_dump_bool(
    const char* title,
    const gboolean* values,
    guint count)
{
    guint i;
    GString* buf = g_string_new("[");
    for (i=0; i<count; i++) {
        if (i > 0) g_string_append(buf, ", ");
        g_string_append(buf, values[i] ? "true" : "false");
    }
    g_string_append(buf, "]");
    printf("%s: %s\n", title, buf->str);
    g_string_free(buf, TRUE);
}

void
mm_dump_strv(
    const char* title,
    const GStrV* sv)
{
    const GStrV* ptr;
    GString* buf = g_string_new("[");
    for (ptr = sv; *ptr; ptr++) {
        if (ptr > sv) g_string_append(buf, ", ");
        g_string_append(buf, *ptr);
    }
    g_string_append(buf, "]");
    printf("%s: %s\n", title, buf->str);
    g_string_free(buf, TRUE);
}

static
void
mm_valid(
    App* app)
{
    GDEBUG("ofono is running");
    mm_dump_strv("Available modems", app->mm->available);
    mm_dump_strv("Enabled modems", app->mm->enabled);
    if (ofonoext_version() >= GOFONOEXT_API_VERSION(1,0,3)) {
        mm_dump_strv("IMEI", app->mm->imei);
    }
    mm_dump_bool("Present SIMs", app->mm->present_sims, app->mm->sim_count);
    printf("Data SIM: %s\n", app->mm->data_imsi);
    printf("Voice SIM: %s\n", app->mm->voice_imsi);
    printf("Modem count: %u\n", app->mm->modem_count);
    printf("SIM count: %u\n", app->mm->sim_count);
    printf("Active SIM count: %u\n", app->mm->active_sim_count);
    if (app->loop) {
        g_main_loop_quit(app->loop);
    }
}

static
void
mm_valid_handler(
    OfonoExtModemManager* mm,
    void* arg)
{
    if (mm->valid) {
        App* app = arg;
        ofonoext_mm_remove_handler(mm, app->mm_valid_id);
        app->mm_valid_id = 0;
        mm_valid(app);
    }
}

static
int
app_run(
    App* app)
{
    app->mm = ofonoext_mm_new();
    if (app->mm->valid) {
        mm_valid(app);
    } else {
        app->ret = RET_ERR;
        app->loop = g_main_loop_new(NULL, FALSE);
        if (app->timeout > 0) GDEBUG("Timeout %d sec", app->timeout);
        app->mm_valid_id = ofonoext_mm_add_valid_changed_handler(app->mm,
            mm_valid_handler, app);
        g_main_loop_run(app->loop);
        g_main_loop_unref(app->loop);
        ofonoext_mm_remove_handler(app->mm, app->mm_valid_id);
    }
    ofonoext_mm_unref(app->mm);
    return app->ret;
}

static
gboolean
app_log_verbose(
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
app_init(
    App* app,
    int argc,
    char* argv[])
{
    gboolean ok = FALSE;
    GOptionEntry entries[] = {
        { "verbose", 'v', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          app_log_verbose, "Enable verbose output", NULL },
        { "timeout", 't', 0, G_OPTION_ARG_INT,
          &app->timeout, "Timeout in seconds", "SECONDS" },
        { NULL }
    };
    GError* error = NULL;
    GOptionContext* options = g_option_context_new("[INTERFACE]");
    g_option_context_add_main_entries(options, entries, NULL);
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
