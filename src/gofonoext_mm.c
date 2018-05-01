/*
 * Copyright (C) 2015-2018 Jolla Ltd.
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
 *   3. Neither the name of Jolla Ltd nor the names of its contributors may
 *      be used to endorse or promote products derived from this software
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
#include "gofonoext_call_p.h"
#include "gofonoext_log.h"

#include <gofono_modem.h>
#include <gofono_names.h>

#include <gutil_strv.h>
#include <gutil_misc.h>

/* Generated headers */
#include "org.nemomobile.ofono.ModemManager.h"

/* Log module */
GLOG_MODULE_DEFINE("ofonoext");

/* Retry delay */
#define MM_RETRY_SEC (2)

/* Object definition */
enum proxy_handler_id {
    PROXY_SIGNAL_ENABLED_MODEMS_CHANGED,
    PROXY_SIGNAL_DATA_IMSI_CHANGED,
    PROXY_SIGNAL_DATA_MODEM_CHANGED,
    PROXY_SIGNAL_VOICE_IMSI_CHANGED,
    PROXY_SIGNAL_VOICE_MODEM_CHANGED,
    PROXY_SIGNAL_PRESENT_SIMS_CHANGED,
    PROXY_SIGNAL_MMS_IMSI_CHANGED,
    PROXY_SIGNAL_MMS_MODEM_CHANGED,
    PROXY_SIGNAL_READY_CHANGED,
    PROXY_SIGNAL_COUNT
};

struct ofonoext_mm_priv {
    GDBusConnection* bus;
    OrgNemomobileOfonoModemManager* proxy;
    gulong proxy_signal_id[PROXY_SIGNAL_COUNT];
    guint ofono_watch_id;
    guint retry_timer_id;
    int version;
    GCancellable* cancel;
    GStrV* available;
    GStrV* enabled;
    char* data_imsi;
    char* voice_imsi;
    char* mms_imsi;
    gboolean* present_sims;
    GStrV* imei;
};

typedef GObjectClass OfonoExtModemManagerClass;
G_DEFINE_TYPE(OfonoExtModemManager, ofonoext_mm, G_TYPE_OBJECT)

enum ofonoext_mm_signal {
    SIGNAL_VALID_CHANGED,
    SIGNAL_ENABLED_MODEMS_CHANGED,
    SIGNAL_DATA_IMSI_CHANGED,
    SIGNAL_DATA_MODEM_CHANGED,
    SIGNAL_VOICE_IMSI_CHANGED,
    SIGNAL_VOICE_MODEM_CHANGED,
    SIGNAL_MMS_IMSI_CHANGED,
    SIGNAL_MMS_MODEM_CHANGED,
    SIGNAL_PRESENT_SIMS_CHANGED,
    SIGNAL_SIM_COUNT_CHANGED,
    SIGNAL_ACTIVE_SIM_COUNT_CHANGED,
    SIGNAL_READY_CHANGED,
    SIGNAL_COUNT
};

#define SIGNAL_VALID_CHANGED_NAME               "valid-changed"
#define SIGNAL_ENABLED_MODEMS_CHANGED_NAME      "enabled-modems-changed"
#define SIGNAL_DATA_IMSI_CHANGED_NAME           "data-imsi-changed"
#define SIGNAL_DATA_MODEM_CHANGED_NAME          "data-modem-changed"
#define SIGNAL_VOICE_IMSI_CHANGED_NAME          "voice-imsi-changed"
#define SIGNAL_VOICE_MODEM_CHANGED_NAME         "voice-modem-changed"
#define SIGNAL_PRESENT_SIMS_CHANGED_NAME        "present-sims-changed"
#define SIGNAL_SIM_COUNT_CHANGED_NAME           "sim-count-changed"
#define SIGNAL_ACTIVE_SIM_COUNT_CHANGED_NAME    "active-sim-count-changed"
#define SIGNAL_MMS_IMSI_CHANGED_NAME            "mms-imsi-changed"
#define SIGNAL_MMS_MODEM_CHANGED_NAME           "mms-modem-changed"
#define SIGNAL_READY_CHANGED_NAME               "ready-changed"

static guint ofonoext_mm_signals[SIGNAL_COUNT] = { 0 };

#define OFONOEXT_SIGNAL_NEW(NAME) \
    ofonoext_mm_signals[SIGNAL_##NAME##_CHANGED] = \
        g_signal_new(SIGNAL_##NAME##_CHANGED_NAME, \
            G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST, 0, \
            NULL, NULL, NULL, G_TYPE_NONE, 0)

/* Forward declarations */
static
void
ofonoext_mm_schedule_retry(
    OfonoExtModemManager* self);

/* Weak reference to the single instance of OfonoExtModemManager */
static OfonoExtModemManager* ofonoext_mm_instance = NULL;

/* Async call context */
typedef struct ofonoext_mm_set_mms_sim_call {
    OfonoExtCall common;
    OfonoExtModemManagerSetMmsSimHandler fn;
    void* arg;
} OfonoExtModemManagerSetMmsSimCall;

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
ofonoext_mm_destroyed(
    gpointer arg,
    GObject* object)
{
    GASSERT(object == (GObject*)ofonoext_mm_instance);
    ofonoext_mm_instance = NULL;
    GVERBOSE_("%p", object);
}

static
void
ofonoext_mm_set_mms_sim_done(
    GObject* proxy,
    GAsyncResult* result,
    gpointer data)
{
    OfonoExtModemManagerSetMmsSimCall* call = data;
    char* path = NULL;
    GError* error = NULL;
    if (!org_nemomobile_ofono_modem_manager_call_set_mms_sim_finish(
        ORG_NEMOMOBILE_OFONO_MODEM_MANAGER(proxy), &path, result, &error)) {
        GERR("%s", GERRMSG(error));
    }
    if (call->fn && !g_cancellable_is_cancelled(call->common.cancel)) {
        OfonoExtModemManager* mm = OFONOEXT_MODEM_MANAGER(call->common.owner);
        call->fn(mm, path, error, call->arg);
    }
    if (error) {
        g_error_free(error);
    }
    ofonoext_call_destroy(&call->common);
    g_free(call);
    g_free(path);
}

static
void
ofonoext_mm_set_valid(
    OfonoExtModemManager* self,
    gboolean valid)
{
    if (self->valid != valid) {
        self->valid = valid;
        g_signal_emit(self, ofonoext_mm_signals[SIGNAL_VALID_CHANGED], 0);
    }
}

static
gboolean
ofonoext_mm_is_timeout(
    const GError* error)
{
    if (error) {
        if (error->domain == G_IO_ERROR) {
            switch (error->code) {
            case G_IO_ERROR_TIMED_OUT:
                return TRUE;
            default:
                return FALSE;
            }
        } else if (error->domain == G_DBUS_ERROR) {
            switch (error->code) {
            case G_DBUS_ERROR_TIMEOUT:
            case G_DBUS_ERROR_TIMED_OUT:
                return TRUE;
            default:
                return FALSE;
            }
        }
    }
    return FALSE;
}

static
void
ofonoext_mm_cancel_retry(
    OfonoExtModemManager* self)
{
    OfonoExtModemManagerPriv* priv = self->priv;
    if (priv->retry_timer_id) {
        g_source_remove(priv->retry_timer_id);
        priv->retry_timer_id = 0;
        GDEBUG("Retry cancelled");
    }
}

static
void
ofonoext_mm_reset(
    OfonoExtModemManager* self)
{
    OfonoExtModemManagerPriv* priv = self->priv;
    ofonoext_mm_cancel_retry(self);
    if (priv->proxy) {
        gutil_disconnect_handlers(priv->proxy, priv->proxy_signal_id,
            G_N_ELEMENTS(priv->proxy_signal_id));
        g_object_unref(priv->proxy);
        priv->proxy = NULL;
    }
    if (self->available) {
        g_strfreev(priv->available);
        self->available = priv->available = NULL;
    }
    if (self->enabled) {
        g_strfreev(priv->enabled);
        self->enabled = priv->enabled = NULL;
    }
    if (self->data_imsi) {
        g_free(priv->data_imsi);
        self->data_imsi = priv->data_imsi = NULL;
    }
    if (self->voice_imsi) {
        g_free(priv->voice_imsi);
        self->voice_imsi = priv->voice_imsi = NULL;
    }
    if (self->mms_imsi) {
        g_free(priv->mms_imsi);
        self->mms_imsi = priv->mms_imsi = NULL;
    }
    if (self->data_modem) {
        ofono_modem_unref(self->data_modem);
        self->data_modem = NULL;
    }
    if (self->voice_modem) {
        ofono_modem_unref(self->voice_modem);
        self->voice_modem = NULL;
    }
    if (self->present_sims) {
        g_free(priv->present_sims);
        self->present_sims = priv->present_sims = NULL;
    }
    if (self->mms_modem) {
        ofono_modem_unref(self->mms_modem);
        self->mms_modem = NULL;
    }
    if (self->imei) {
        g_strfreev(priv->imei);
        self->imei = priv->imei = NULL;
    }
}

static
void
ofonoext_mm_update_sim_counts(
    OfonoExtModemManager* self,
    gboolean emit_signals)
{
    guint i;
    OfonoExtModemManagerPriv* priv = self->priv;
    const guint old_sim_count = self->sim_count;
    const guint old_active_sim_count = self->active_sim_count;

    self->sim_count = 0;
    self->active_sim_count = 0;
    for (i=0; i<self->modem_count; i++) {
        if (priv->present_sims[i]) {
            self->sim_count++;
            if (ofonoext_mm_modem_enabled_at(self, i)) {
                self->active_sim_count++;
            }
        }
    }

    if (emit_signals) {
        if (old_sim_count != self->sim_count) {
            g_signal_emit(self, ofonoext_mm_signals[
                SIGNAL_SIM_COUNT_CHANGED], 0);
        }
        if (old_active_sim_count != self->active_sim_count) {
            g_signal_emit(self, ofonoext_mm_signals[
                SIGNAL_ACTIVE_SIM_COUNT_CHANGED], 0);
        }
    }
}

static
void
ofonoext_mm_enabled_modems_changed(
    OrgNemomobileOfonoModemManager* proxy,
    char** modems,
    gpointer data)
{
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(data);
    OfonoExtModemManagerPriv* priv = self->priv;
    g_strfreev(priv->enabled);
    self->enabled = priv->enabled = g_strdupv(modems);
    ofonoext_mm_update_sim_counts(self, TRUE);
    g_signal_emit(self, ofonoext_mm_signals[SIGNAL_ENABLED_MODEMS_CHANGED], 0);
}

static
void
ofonoext_mm_default_data_sim_changed(
    OrgNemomobileOfonoModemManager* proxy,
    const char* imsi,
    gpointer data)
{
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(data);
    OfonoExtModemManagerPriv* priv = self->priv;
    g_free(priv->data_imsi);
    self->data_imsi = priv->data_imsi = g_strdup(imsi);
    g_signal_emit(self, ofonoext_mm_signals[
        SIGNAL_DATA_IMSI_CHANGED], 0);
}

static
void
ofonoext_mm_default_data_modem_changed(
    OrgNemomobileOfonoModemManager* proxy,
    const char* path,
    gpointer data)
{
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(data);
    ofono_modem_unref(self->data_modem);
    self->data_modem = (path && path[0]) ? ofono_modem_new(path) : NULL;
    g_signal_emit(self, ofonoext_mm_signals[
        SIGNAL_DATA_MODEM_CHANGED], 0);
}

static
void
ofonoext_mm_default_voice_sim_changed(
    OrgNemomobileOfonoModemManager* proxy,
    const char* imsi,
    gpointer data)
{
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(data);
    OfonoExtModemManagerPriv* priv = self->priv;
    g_free(priv->voice_imsi);
    self->voice_imsi = priv->voice_imsi = g_strdup(imsi);
    g_signal_emit(self, ofonoext_mm_signals[
        SIGNAL_VOICE_IMSI_CHANGED], 0);
}

static
void
ofonoext_mm_default_voice_modem_changed(
    OrgNemomobileOfonoModemManager* proxy,
    const char* path,
    gpointer data)
{
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(data);
    ofono_modem_unref(self->voice_modem);
    self->voice_modem = (path && path[0]) ? ofono_modem_new(path) : NULL;
    g_signal_emit(self, ofonoext_mm_signals[
        SIGNAL_VOICE_MODEM_CHANGED], 0);
}

static
void
ofonoext_mm_present_sims_changed(
    OrgNemomobileOfonoModemManager* proxy,
    int index,
    gboolean present,
    gpointer data)
{
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(data);
    OfonoExtModemManagerPriv* priv = self->priv;
    GASSERT(index >= 0 && index < self->modem_count);
    if (index >= 0 && index < self->modem_count) {
        priv->present_sims[index] = (present != FALSE);
        g_signal_emit(self, ofonoext_mm_signals[
            SIGNAL_PRESENT_SIMS_CHANGED], 0);
        ofonoext_mm_update_sim_counts(self, TRUE);
    }
}

static
void
ofonoext_mm_mms_sim_changed(
    OrgNemomobileOfonoModemManager* proxy,
    const char* imsi,
    gpointer data)
{
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(data);
    OfonoExtModemManagerPriv* priv = self->priv;
    g_free(priv->mms_imsi);
    self->mms_imsi = priv->mms_imsi = g_strdup(imsi);
    g_signal_emit(self, ofonoext_mm_signals[SIGNAL_MMS_IMSI_CHANGED], 0);
}

static
void
ofonoext_mm_mms_modem_changed(
    OrgNemomobileOfonoModemManager* proxy,
    const char* path,
    gpointer data)
{
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(data);
    ofono_modem_unref(self->mms_modem);
    self->mms_modem = (path && path[0]) ? ofono_modem_new(path) : NULL;
    g_signal_emit(self, ofonoext_mm_signals[SIGNAL_MMS_MODEM_CHANGED], 0);
}

static
void
ofonoext_mm_ready_changed(
    OrgNemomobileOfonoModemManager* proxy,
    gboolean ready,
    gpointer data)
{
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(data);
    self->ready = ready;
    g_signal_emit(self, ofonoext_mm_signals[SIGNAL_READY_CHANGED], 0);
}

static
void
ofonoext_mm_init_done(
    OfonoExtModemManager* self,
    GStrV* available,
    GStrV* enabled,
    char* data_imsi,
    char* voice_imsi,
    const char* data_path,
    const char* voice_path,
    GVariant* present_sims,
    GStrV* imei,
    char* mms_imsi,
    const char* mms_path,
    gboolean ready)
{
    OfonoExtModemManagerPriv* priv = self->priv;
    OfonoModem* voice_modem;
    OfonoModem* data_modem;
    OfonoModem* mms_modem;

    g_strfreev(priv->available);
    g_strfreev(priv->enabled);
    g_strfreev(priv->imei);
    g_free(priv->data_imsi);
    g_free(priv->voice_imsi);
    g_free(priv->mms_imsi);

    self->available = priv->available = available;
    self->enabled = priv->enabled = enabled;
    self->imei = priv->imei = imei;
    self->data_imsi = priv->data_imsi = data_imsi;
    self->voice_imsi = priv->voice_imsi = voice_imsi;
    self->mms_imsi = priv->mms_imsi = mms_imsi;
    self->modem_count = gutil_strv_length(available);
    self->ready = ready;

    /* The modem could be the same, so unref the current one after selecting
     * the new one, to avoid unnecessary deallocations */
    voice_modem = self->voice_modem;
    self->voice_modem = (voice_path && voice_path[0]) ?
        ofono_modem_new(voice_path) : NULL;

    data_modem = self->data_modem;
    self->data_modem = (data_path && data_path[0]) ?
        ofono_modem_new(data_path) : NULL;

    mms_modem = self->mms_modem;
    self->mms_modem = (mms_path && mms_path[0]) ?
        ofono_modem_new(mms_path) : NULL;

    ofono_modem_unref(voice_modem);
    ofono_modem_unref(data_modem);
    ofono_modem_unref(mms_modem);

    if (present_sims) {
        guint i;
        GASSERT(self->modem_count == g_variant_n_children(present_sims));
        g_free(priv->present_sims);
        priv->present_sims = g_new0(gboolean, self->modem_count);
        self->present_sims = priv->present_sims;
        for (i=0; i<self->modem_count; i++) {
            GVariant* v = g_variant_get_child_value(present_sims, i);
            priv->present_sims[i] = g_variant_get_boolean(v);
            g_variant_unref(v);
        }
    }

    /* Subscribe for notifications */
    priv->proxy_signal_id[PROXY_SIGNAL_ENABLED_MODEMS_CHANGED] =
        g_signal_connect(priv->proxy, "enabled-modems-changed",
            G_CALLBACK(ofonoext_mm_enabled_modems_changed), self);
    priv->proxy_signal_id[PROXY_SIGNAL_DATA_IMSI_CHANGED] =
        g_signal_connect(priv->proxy, "default-data-sim-changed",
            G_CALLBACK(ofonoext_mm_default_data_sim_changed), self);
    priv->proxy_signal_id[PROXY_SIGNAL_DATA_MODEM_CHANGED] =
        g_signal_connect(priv->proxy, "default-data-modem-changed",
            G_CALLBACK(ofonoext_mm_default_data_modem_changed), self);
    priv->proxy_signal_id[PROXY_SIGNAL_VOICE_IMSI_CHANGED] =
        g_signal_connect(priv->proxy, "default-voice-sim-changed",
            G_CALLBACK(ofonoext_mm_default_voice_sim_changed), self);
    priv->proxy_signal_id[PROXY_SIGNAL_VOICE_MODEM_CHANGED] =
        g_signal_connect(priv->proxy, "default-voice-modem-changed",
            G_CALLBACK(ofonoext_mm_default_voice_modem_changed), self);
    priv->proxy_signal_id[PROXY_SIGNAL_PRESENT_SIMS_CHANGED] =
        g_signal_connect(priv->proxy, "present-sims-changed",
            G_CALLBACK(ofonoext_mm_present_sims_changed), self);
    priv->proxy_signal_id[PROXY_SIGNAL_MMS_IMSI_CHANGED] =
        g_signal_connect(priv->proxy, "mms-sim-changed",
            G_CALLBACK(ofonoext_mm_mms_sim_changed), self);
    priv->proxy_signal_id[PROXY_SIGNAL_MMS_MODEM_CHANGED] =
        g_signal_connect(priv->proxy, "mms-modem-changed",
            G_CALLBACK(ofonoext_mm_mms_modem_changed), self);
    priv->proxy_signal_id[PROXY_SIGNAL_READY_CHANGED] =
        g_signal_connect(priv->proxy, "ready-changed",
            G_CALLBACK(ofonoext_mm_ready_changed), self);

    ofonoext_mm_update_sim_counts(self, FALSE);
    ofonoext_mm_set_valid(self, TRUE);
}

static
void
ofonoext_mm_get_all_finish(
    GObject* proxy,
    GAsyncResult* result,
    gpointer data,
    gboolean (*finish_call)(
        OrgNemomobileOfonoModemManager* proxy,
        gint* version,
        gchar*** available,
        gchar*** enabled,
        gchar** data_imsi,
        gchar** voice_imsi,
        gchar** data_modem,
        gchar** voice_modem,
        GVariant** present_sims,
        gchar*** imei,
        gchar** mms_imsi,
        gchar** mms_modem,
        gboolean* ready,
        GAsyncResult* res,
        GError **error))
{
    GError* error = NULL;
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(data);
    OfonoExtModemManagerPriv* priv = self->priv;
    int version = 0;
    char** available = NULL;
    char** enabled = NULL;
    char* data_imsi = NULL;
    char* voice_imsi = NULL;
    char* mms_imsi = NULL;
    char* data_path = NULL;
    char* voice_path = NULL;
    char* mms_path = NULL;
    GVariant* present_sims = NULL;
    char** imei = NULL;
    gboolean ready = TRUE;

    GASSERT(!self->valid);
    GASSERT(priv->cancel);
    g_object_unref(priv->cancel);
    priv->cancel = NULL;
    if (finish_call(ORG_NEMOMOBILE_OFONO_MODEM_MANAGER(proxy), &version,
        &available, &enabled, &data_imsi, &voice_imsi, &data_path,
        &voice_path, &present_sims, &imei, &mms_imsi, &mms_path, &ready,
        result, &error)) {
        /* This call is only made for interface versions 2 and later */
        GASSERT(version > 1);
        /* ofonoext_mm_init_done takes ownership of all the pointers except
         * data_path, voice_path, mms_path and present_sims */
        ofonoext_mm_init_done(self, available, enabled, data_imsi, voice_imsi,
            data_path, voice_path, present_sims, imei, mms_imsi, mms_path,
            ready);
    } else {
        g_strfreev(available);
        g_strfreev(enabled);
        g_free(data_imsi);
        g_free(voice_imsi);
        g_strfreev(imei);
#if GUTIL_LOG_ERR
        if (error->code == G_IO_ERROR_CANCELLED) {
            GDEBUG("%s", GERRMSG(error));
        } else {
            GERR("%s", GERRMSG(error));
        }
#endif
        /* Retry the call */
        if (ofonoext_mm_is_timeout(error)) {
            ofonoext_mm_schedule_retry(self);
        }
    }
    g_free(data_path);
    g_free(voice_path);
    g_free(mms_path);
    ofonoext_mm_unref(self);
    if (present_sims) g_variant_unref(present_sims);
    if (error) g_error_free(error);
}

static
void
ofonoext_mm_get_all5_done(
    GObject* proxy,
    GAsyncResult* res,
    gpointer data)
{
    ofonoext_mm_get_all_finish(proxy, res, data,
        org_nemomobile_ofono_modem_manager_call_get_all5_finish);
}

static
gboolean
ofonoext_mm_get_all4_finish(
    OrgNemomobileOfonoModemManager* proxy,
    gint* version,
    gchar*** available,
    gchar*** enabled,
    gchar** data_imsi,
    gchar** voice_imsi,
    gchar** data_path,
    gchar** voice_path,
    GVariant** present_sims,
    gchar*** imei,
    gchar** mms_imsi,
    gchar** mms_path,
    gboolean* ready,
    GAsyncResult* res,
    GError **error)
{
    *ready = TRUE;
    return org_nemomobile_ofono_modem_manager_call_get_all4_finish(proxy,
        version, available, enabled, data_imsi, voice_imsi, data_path,
        voice_path, present_sims, imei, mms_imsi, mms_path, res, error);
}

static
void
ofonoext_mm_get_all4_done(
    GObject* proxy,
    GAsyncResult* res,
    gpointer data)
{
    ofonoext_mm_get_all_finish(proxy, res, data, ofonoext_mm_get_all4_finish);
}

static
gboolean
ofonoext_mm_get_all3_finish(
    OrgNemomobileOfonoModemManager* proxy,
    gint* version,
    gchar*** available,
    gchar*** enabled,
    gchar** data_imsi,
    gchar** voice_imsi,
    gchar** data_path,
    gchar** voice_path,
    GVariant** present_sims,
    gchar*** imei,
    gchar** mms_imsi,
    gchar** mms_path,
    gboolean* ready,
    GAsyncResult* res,
    GError **error)
{
    *mms_imsi = NULL;
    *mms_path = NULL;
    *ready = TRUE;
    return org_nemomobile_ofono_modem_manager_call_get_all3_finish(proxy,
        version, available, enabled, data_imsi, voice_imsi, data_path,
        voice_path, present_sims, imei, res, error);
}

static
void
ofonoext_mm_get_all3_done(
    GObject* proxy,
    GAsyncResult* res,
    gpointer data)
{
    ofonoext_mm_get_all_finish(proxy, res, data, ofonoext_mm_get_all3_finish);
}

static
gboolean
ofonoext_mm_get_all2_finish(
    OrgNemomobileOfonoModemManager* proxy,
    gint* version,
    gchar*** available,
    gchar*** enabled,
    gchar** data_imsi,
    gchar** voice_imsi,
    gchar** data_path,
    gchar** voice_path,
    GVariant** present_sims,
    gchar*** imei,
    gchar** mms_imsi,
    gchar** mms_path,
    gboolean* ready,
    GAsyncResult* res,
    GError **error)
{
    *imei = NULL;
    *mms_imsi = NULL;
    *mms_path = NULL;
    *ready = TRUE;
    return org_nemomobile_ofono_modem_manager_call_get_all2_finish(proxy,
        version, available, enabled, data_imsi, voice_imsi, data_path,
        voice_path, present_sims, res, error);
}

static
void
ofonoext_mm_get_all2_done(
    GObject* proxy,
    GAsyncResult* res,
    gpointer data)
{
    ofonoext_mm_get_all_finish(proxy, res, data, ofonoext_mm_get_all2_finish);
}

static
void
ofonoext_mm_get_allx(
    OfonoExtModemManager* self)
{
    OfonoExtModemManagerPriv* priv = self->priv;

    GASSERT(!self->valid);
    GASSERT(!priv->cancel);
    GASSERT(priv->version > 1);
   
    priv->cancel = g_cancellable_new();
    ofonoext_mm_ref(self);
    switch (priv->version) {
    case 2:
        /* Request version 2 settings */
        org_nemomobile_ofono_modem_manager_call_get_all2(priv->proxy,
            priv->cancel, ofonoext_mm_get_all2_done, self);
        break;
    case 3:
        /* Request version 3 settings */
        org_nemomobile_ofono_modem_manager_call_get_all3(priv->proxy,
            priv->cancel, ofonoext_mm_get_all3_done, self);
        break;
    case 4:
        /* Request version 4 settings */
        org_nemomobile_ofono_modem_manager_call_get_all4(priv->proxy,
            priv->cancel, ofonoext_mm_get_all4_done, self);
        break;
    case 5:
    default:
        /* Request version 5 settings */
        org_nemomobile_ofono_modem_manager_call_get_all5(priv->proxy,
            priv->cancel, ofonoext_mm_get_all5_done, self);
        break;
    }
}

static
void
ofonoext_mm_get_all_done(
    GObject* proxy,
    GAsyncResult* result,
    gpointer data)
{
    GError* error = NULL;
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(data);
    OfonoExtModemManagerPriv* priv = self->priv;
    int version = 0;
    char** available = NULL;
    char** enabled = NULL;
    char* data_imsi = NULL;
    char* voice_imsi = NULL;
    char* data_path = NULL;
    char* voice_path = NULL;

    GASSERT(!self->valid);
    GASSERT(priv->cancel);
    g_object_unref(priv->cancel);
    priv->cancel = NULL;
    if (org_nemomobile_ofono_modem_manager_call_get_all_finish(
        ORG_NEMOMOBILE_OFONO_MODEM_MANAGER(proxy), &version, &available,
        &enabled, &data_imsi, &voice_imsi, &data_path, &voice_path,
        result, &error)) {
        GDEBUG("Interface version %d", version);
        priv->version = version;
        if (version == 1) {
            /* ofonoext_mm_init_done takes the ownership of the arguments
             * except for data_path and voice_path */
            ofonoext_mm_init_done(self, available, enabled, data_imsi,
                voice_imsi, data_path, voice_path, NULL, NULL, NULL, NULL,
                TRUE);
        } else {
            g_strfreev(available);
            g_strfreev(enabled);
            g_free(data_imsi);
            g_free(voice_imsi);
            ofonoext_mm_get_allx(self);
        }
    } else {
        g_strfreev(available);
        g_strfreev(enabled);
        g_free(data_imsi);
        g_free(voice_imsi);
#if GUTIL_LOG_ERR
        if (error->code == G_IO_ERROR_CANCELLED) {
            GDEBUG("%s", GERRMSG(error));
        } else {
            GERR("%s", GERRMSG(error));
        }
#endif
        /* Retry the call */
        if (ofonoext_mm_is_timeout(error)) {
            ofonoext_mm_schedule_retry(self);
        }
    }
    g_free(data_path);
    g_free(voice_path);
    ofonoext_mm_unref(self);
    if (error) g_error_free(error);
}

static
gboolean
ofonoext_mm_retry_cb(
    gpointer data)
{
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(data);
    OfonoExtModemManagerPriv* priv = self->priv;

    GASSERT(!self->valid);
    GASSERT(!priv->cancel);
    GASSERT(priv->retry_timer_id);
    priv->retry_timer_id = 0;

    /* Bump the reference count for the duration of the D-Bus call */
    ofonoext_mm_ref(self);
    if (priv->version) {
        ofonoext_mm_get_allx(self);
    } else {
        priv->cancel = g_cancellable_new();
        org_nemomobile_ofono_modem_manager_call_get_all(priv->proxy,
            priv->cancel, ofonoext_mm_get_all_done, self);
    }
    return G_SOURCE_REMOVE;
}

static
void
ofonoext_mm_schedule_retry(
    OfonoExtModemManager* self)
{
    OfonoExtModemManagerPriv* priv = self->priv;

    GASSERT(!priv->cancel);
    GASSERT(!self->valid);
    if (!priv->retry_timer_id) {
        priv->retry_timer_id = g_timeout_add_seconds(MM_RETRY_SEC,
            ofonoext_mm_retry_cb, self);
    }
}

static
void
ofonoext_mm_proxy_created(
    GObject* proxy,
    GAsyncResult* result,
    gpointer data)
{
    GError* error = NULL;
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(data);
    OfonoExtModemManagerPriv* priv = self->priv;

    GASSERT(priv->cancel);
    GASSERT(!self->valid);
    GASSERT(!priv->proxy);
    g_object_unref(priv->cancel);
    priv->cancel = NULL;
    priv->proxy =
        org_nemomobile_ofono_modem_manager_proxy_new_finish(result, &error);
    
    if (priv->proxy) {
        /* Request current settings */
        priv->cancel = g_cancellable_new();
        org_nemomobile_ofono_modem_manager_call_get_all(priv->proxy,
            priv->cancel, ofonoext_mm_get_all_done, self);
    } else {
#if GUTIL_LOG_ERR
        if (error->code == G_IO_ERROR_CANCELLED) {
            GDEBUG("%s", GERRMSG(error));
        } else {
            GERR("%s", GERRMSG(error));
        }
#endif
        ofonoext_mm_reset(self);
        ofonoext_mm_unref(self);
    }
    if (error) g_error_free(error);
}

static
void
ofonoext_mm_name_appeared(
    GDBusConnection* bus,
    const gchar* name,
    const gchar* owner,
    gpointer arg)
{
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(arg);
    OfonoExtModemManagerPriv* priv = self->priv;
    GDEBUG("Name '%s' is owned by %s", name, owner);

    /* Start the initialization sequence */
    GASSERT(!priv->cancel);
    priv->cancel = g_cancellable_new();
    org_nemomobile_ofono_modem_manager_proxy_new(bus,
        G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, OFONO_SERVICE, "/",
        priv->cancel, ofonoext_mm_proxy_created, ofonoext_mm_ref(self));
}

static
void
ofonoext_mm_name_vanished(
    GDBusConnection* bus,
    const gchar* name,
    gpointer arg)
{
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(arg);
    GDEBUG("Name '%s' has disappeared", name);
    ofonoext_mm_reset(self);
    ofonoext_mm_set_valid(self, FALSE);
}

/*==========================================================================*
 * API
 *==========================================================================*/

static
OfonoExtModemManager*
ofonoext_mm_create()
{
    GError* error = NULL;
    GDBusConnection* bus = g_bus_get_sync(OFONO_BUS_TYPE, NULL, &error);
    if (bus) {
        OfonoExtModemManager* self =
            g_object_new(OFONOEXT_TYPE_MODEM_MANAGER, NULL);
        OfonoExtModemManagerPriv* priv = self->priv;
        priv->bus = bus;
        priv->ofono_watch_id = g_bus_watch_name_on_connection(bus,
            OFONO_SERVICE, G_BUS_NAME_WATCHER_FLAGS_NONE,
            ofonoext_mm_name_appeared,
            ofonoext_mm_name_vanished,
            self, NULL);
        return self;
    } else {
        GERR("%s", GERRMSG(error));
        g_error_free(error);
    }
    return NULL;
}

OfonoExtModemManager*
ofonoext_mm_new()
{
    OfonoExtModemManager* mm;
    if (ofonoext_mm_instance) {
        g_object_ref(mm = ofonoext_mm_instance);
    } else {
        mm = ofonoext_mm_create();
        ofonoext_mm_instance = mm;
        g_object_weak_ref(G_OBJECT(mm), ofonoext_mm_destroyed, mm);
    }
    return mm;
}

OfonoExtModemManager*
ofonoext_mm_ref(
    OfonoExtModemManager* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(OFONOEXT_MODEM_MANAGER(self));
        return self;
    } else {
        return NULL;
    }
}

void
ofonoext_mm_unref(
    OfonoExtModemManager* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(OFONOEXT_MODEM_MANAGER(self));
    }
}

void
ofonoext_mm_set_mms_imsi(
    OfonoExtModemManager* self,
    const char* imsi)
{
    ofonoext_mm_set_mms_imsi_full(self, imsi, NULL, NULL);
}

OfonoExtCall*
ofonoext_mm_set_mms_imsi_full(
    OfonoExtModemManager* self,
    const char* imsi,
    OfonoExtModemManagerSetMmsSimHandler fn,
    void* arg)
{
    if (G_LIKELY(self)) {
        GASSERT(self->valid);
        if (G_LIKELY(self->valid)) {
            OfonoExtModemManagerPriv* priv = self->priv;
            OfonoExtModemManagerSetMmsSimCall* call =
                g_new0(OfonoExtModemManagerSetMmsSimCall,1);
            ofonoext_call_init(&call->common, G_OBJECT(self));
            call->fn = fn;
            call->arg = arg;
            org_nemomobile_ofono_modem_manager_call_set_mms_sim(priv->proxy,
                imsi, call->common.cancel, ofonoext_mm_set_mms_sim_done, call);
            return &call->common;
        }
    }
    return NULL;
}

gboolean
ofonoext_mm_modem_enabled_at(
    OfonoExtModemManager* self,
    gint index)
{
    if (G_LIKELY(self)) {
        OfonoExtModemManagerPriv* priv = self->priv;
        const char* path = gutil_strv_at(priv->available, index);
        if (path) {
            return gutil_strv_contains(priv->enabled, path);
        }
    }
    return FALSE;
}

gulong
ofonoext_mm_add_valid_changed_handler(
    OfonoExtModemManager* self,
    OfonoExtModemManagerHandler fn,
    void* data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_VALID_CHANGED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
ofonoext_mm_add_enabled_modems_changed_handler(
    OfonoExtModemManager* self,
    OfonoExtModemManagerHandler fn,
    void* data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_ENABLED_MODEMS_CHANGED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
ofonoext_mm_add_data_imsi_changed_handler(
    OfonoExtModemManager* self,
    OfonoExtModemManagerHandler fn,
    void* data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_DATA_IMSI_CHANGED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
ofonoext_mm_add_data_modem_changed_handler(
    OfonoExtModemManager* self,
    OfonoExtModemManagerHandler fn,
    void* data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_DATA_MODEM_CHANGED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
ofonoext_mm_add_voice_imsi_changed_handler(
    OfonoExtModemManager* self,
    OfonoExtModemManagerHandler fn,
    void* data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_VOICE_IMSI_CHANGED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
ofonoext_mm_add_voice_modem_changed_handler(
    OfonoExtModemManager* self,
    OfonoExtModemManagerHandler fn,
    void* data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_VOICE_MODEM_CHANGED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
ofonoext_mm_add_present_sims_changed_handler(
    OfonoExtModemManager* self,
    OfonoExtModemManagerHandler fn,
    void* data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_PRESENT_SIMS_CHANGED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
ofonoext_mm_add_sim_count_changed_handler(
    OfonoExtModemManager* self,
    OfonoExtModemManagerHandler fn,
    void* data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_SIM_COUNT_CHANGED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
ofonoext_mm_add_active_sim_count_changed_handler(
    OfonoExtModemManager* self,
    OfonoExtModemManagerHandler fn,
    void* data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_ACTIVE_SIM_COUNT_CHANGED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
ofonoext_mm_add_mms_imsi_changed_handler(
    OfonoExtModemManager* self,
    OfonoExtModemManagerHandler fn,
    void* data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_MMS_IMSI_CHANGED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
ofonoext_mm_add_mms_modem_changed_handler(
    OfonoExtModemManager* self,
    OfonoExtModemManagerHandler fn,
    void* data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_MMS_MODEM_CHANGED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
ofonoext_mm_add_ready_changed_handler(
    OfonoExtModemManager* self,
    OfonoExtModemManagerHandler fn,
    void* data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_READY_CHANGED_NAME, G_CALLBACK(fn), data) : 0;
}

void
ofonoext_mm_remove_handler(
    OfonoExtModemManager* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
ofonoext_mm_remove_handlers(
    OfonoExtModemManager* self,
    gulong* ids,
    unsigned int count)
{
    gutil_disconnect_handlers(self, ids, count);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

/**
 * Per instance initializer
 */
static
void
ofonoext_mm_init(
    OfonoExtModemManager* self)
{
    OfonoExtModemManagerPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        OFONOEXT_TYPE_MODEM_MANAGER, OfonoExtModemManagerPriv);
    self->priv = priv;
}

/**
 * Final stage of deinitialization
 */
static
void
ofonoext_mm_finalize(
    GObject* object)
{
    OfonoExtModemManager* self = OFONOEXT_MODEM_MANAGER(object);
    OfonoExtModemManagerPriv* priv = self->priv;
    GASSERT(!priv->cancel);
    ofonoext_mm_reset(self);
    if (priv->ofono_watch_id) {
        g_bus_unwatch_name(priv->ofono_watch_id);
    }
    if (priv->bus) {
        g_object_unref(priv->bus);
    }
    G_OBJECT_CLASS(ofonoext_mm_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
ofonoext_mm_class_init(
    OfonoExtModemManagerClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = ofonoext_mm_finalize;
    g_type_class_add_private(klass, sizeof(OfonoExtModemManagerPriv));
    OFONOEXT_SIGNAL_NEW(VALID);
    OFONOEXT_SIGNAL_NEW(ENABLED_MODEMS);
    OFONOEXT_SIGNAL_NEW(DATA_IMSI);
    OFONOEXT_SIGNAL_NEW(DATA_MODEM);
    OFONOEXT_SIGNAL_NEW(VOICE_IMSI);
    OFONOEXT_SIGNAL_NEW(VOICE_MODEM);
    OFONOEXT_SIGNAL_NEW(PRESENT_SIMS);
    OFONOEXT_SIGNAL_NEW(SIM_COUNT);
    OFONOEXT_SIGNAL_NEW(ACTIVE_SIM_COUNT);
    OFONOEXT_SIGNAL_NEW(MMS_IMSI);
    OFONOEXT_SIGNAL_NEW(MMS_MODEM);
    OFONOEXT_SIGNAL_NEW(READY);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
