/*
 * Copyright (C) 2015-2020 Jolla Ltd.
 * Copyright (C) 2015-2020 Slava Monich <slava.monich@jolla.com>
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
 *   3. Neither the names of the copyright holders nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
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

#ifndef GOFONOEXT_MM_H
#define GOFONOEXT_MM_H

#include "gofonoext_types.h"

G_BEGIN_DECLS

typedef struct ofonoext_mm_priv OfonoExtModemManagerPriv;

struct ofonoext_modem_manager {
    GObject object;
    OfonoExtModemManagerPriv* priv;
    gboolean valid;
    const GStrV* available;
    const GStrV* enabled;
    const char* data_imsi;
    const char* voice_imsi;
    OfonoModem* data_modem;
    OfonoModem* voice_modem;
    const gboolean* present_sims;
    guint modem_count;
    guint sim_count;
    guint active_sim_count;
    const GStrV* imei;              /* Since 1.0.3 */
    const char* mms_imsi;           /* Since 1.0.4 */
    OfonoModem* mms_modem;
    gboolean ready;                 /* Since 1.0.7 */
};

GType ofonoext_mm_get_type(void);
#define OFONOEXT_TYPE_MODEM_MANAGER (ofonoext_mm_get_type())
#define OFONOEXT_MODEM_MANAGER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        OFONOEXT_TYPE_MODEM_MANAGER, OfonoExtModemManager))

typedef
void
(*OfonoExtModemManagerHandler)(
    OfonoExtModemManager* mm,
    void* data);

typedef
void
(*OfonoExtModemManagerSetMmsSimHandler)(
    OfonoExtModemManager* mm,
    const char* path,
    const GError* error,
    void* data);

OfonoExtModemManager*
ofonoext_mm_new(void);

OfonoExtModemManager*
ofonoext_mm_ref(
    OfonoExtModemManager* mm);

void
ofonoext_mm_unref(
    OfonoExtModemManager* mm);

gboolean
ofonoext_mm_modem_enabled_at(
    OfonoExtModemManager* mm,
    gint index);

void
ofonoext_mm_set_mms_imsi(
    OfonoExtModemManager* mm,
    const char* imsi);

OfonoExtCall*
ofonoext_mm_set_mms_imsi_full(
    OfonoExtModemManager* mm,
    const char* imsi,
    OfonoExtModemManagerSetMmsSimHandler fn,
    void* arg);

gulong
ofonoext_mm_add_valid_changed_handler(
    OfonoExtModemManager* mm,
    OfonoExtModemManagerHandler fn,
    void* data);

gulong
ofonoext_mm_add_enabled_modems_changed_handler(
    OfonoExtModemManager* mm,
    OfonoExtModemManagerHandler fn,
    void* data);

gulong
ofonoext_mm_add_data_imsi_changed_handler(
    OfonoExtModemManager* mm,
    OfonoExtModemManagerHandler fn,
    void* data);

gulong
ofonoext_mm_add_data_modem_changed_handler(
    OfonoExtModemManager* mm,
    OfonoExtModemManagerHandler fn,
    void* data);

gulong
ofonoext_mm_add_voice_imsi_changed_handler(
    OfonoExtModemManager* mm,
    OfonoExtModemManagerHandler fn,
    void* data);

gulong
ofonoext_mm_add_voice_modem_changed_handler(
    OfonoExtModemManager* mm,
    OfonoExtModemManagerHandler fn,
    void* data);

gulong
ofonoext_mm_add_present_sims_changed_handler(
    OfonoExtModemManager* mm,
    OfonoExtModemManagerHandler fn,
    void* data);

gulong
ofonoext_mm_add_sim_count_changed_handler(
    OfonoExtModemManager* mm,
    OfonoExtModemManagerHandler fn,
    void* data);

gulong
ofonoext_mm_add_active_sim_count_changed_handler(
    OfonoExtModemManager* mm,
    OfonoExtModemManagerHandler fn,
    void* data);

gulong
ofonoext_mm_add_mms_imsi_changed_handler(
    OfonoExtModemManager* mm,
    OfonoExtModemManagerHandler fn,
    void* data);

gulong
ofonoext_mm_add_mms_modem_changed_handler(
    OfonoExtModemManager* mm,
    OfonoExtModemManagerHandler fn,
    void* data);

gulong
ofonoext_mm_add_ready_changed_handler(
    OfonoExtModemManager* mm,
    OfonoExtModemManagerHandler fn,
    void* data);

void
ofonoext_mm_remove_handler(
    OfonoExtModemManager* mm,
    gulong id);

void
ofonoext_mm_remove_handlers(
    OfonoExtModemManager* mm,
    gulong* ids,
    unsigned int count);

G_END_DECLS

#endif /* GOFONOEXT_MM_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
