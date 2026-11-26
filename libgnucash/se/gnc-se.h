/********************************************************************\
 * gnc-se.h -- GnuCash SE: what C code needs to know about locks    *
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

/** @file gnc-se.h
 *  @brief The part of GnuCash SE's locking that C code can call. The
 *  rest, for C++, is in gnc-se.hpp.
 */

#ifndef GNC_SE_H
#define GNC_SE_H

#include <glib.h>

#include "qof.h"
#include "Account.h"
#include "Transaction.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start enforcing locks: registers the commit guard with the engine.
 *  gnc_engine_init() calls this. */
void gnc_se_init (void);

/** Stop enforcing locks. Locked transactions are then protected only
 *  by their read-only mark, as in plain GnuCash. */
void gnc_se_shutdown (void);

/** The date through which the book is locked.
 *  @return FALSE, leaving @a date alone, if nothing is locked. */
gboolean gnc_se_book_get_lock_gdate (const QofBook *book, GDate *date);

/** Whether a transaction dated @a date would fall in the locked period. */
gboolean gnc_se_book_date_is_locked (const QofBook *book, const GDate *date);

/** Whether the transaction has been locked, i.e. is posted. */
gboolean gnc_se_trans_is_locked (const Transaction *trans);

/** Whether the account or any account below it has a split in a locked
 *  transaction. Such an account cannot be deleted. */
gboolean gnc_se_account_has_locked (const Account *account);

#ifdef __cplusplus
}
#endif

#endif /* GNC_SE_H */
