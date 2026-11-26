/********************************************************************\
 * gnc-plugin-se.h -- GnuCash SE: the Posting menu                  *
 * Copyright 2026 Isak Adamssvärd                                   *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, see                             *
 * <https://www.gnu.org/licenses/>                                  *
\********************************************************************/

#ifndef GNC_PLUGIN_SE_H
#define GNC_PLUGIN_SE_H

#include <gtk/gtk.h>
#include "gnc-plugin.h"
#include "Transaction.h"

G_BEGIN_DECLS

#define GNC_TYPE_PLUGIN_SE (gnc_plugin_se_get_type ())
G_DECLARE_FINAL_TYPE (GncPluginSe, gnc_plugin_se, GNC, PLUGIN_SE, GncPlugin)

#define GNC_PLUGIN_SE_NAME "gnc-plugin-se"

/** The plugin behind Actions > Posting: lock a period, check the
 *  voucher series, and the settings for both. It also checks a book
 *  when it has been opened. */
GncPlugin *gnc_plugin_se_new (void);

/** Create a correcting entry for a posted transaction: asks for its
 *  date, who makes the correction and why. If the transaction already
 *  has a correcting entry the user is offered to go to it instead.
 *  @return The correcting entry, or NULL if there is none to show. */
Transaction *gnc_plugin_se_correct_transaction (GtkWindow *parent,
                                                Transaction *trans);

G_END_DECLS

#endif /* GNC_PLUGIN_SE_H */
