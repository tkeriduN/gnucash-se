/********************************************************************\
 * gnc-se-sql.h -- GnuCash SE: the processing history in SQL tables *
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

/** @file gnc-se-sql.h
 *  @brief Keeps GnuCash SE's processing history in two tables of its
 *  own instead of in the book's slots, where every entry would be
 *  written again each time the book is saved.
 *  Design: docs/design/06-motorn-for-server.md.
 */

#ifndef GNC_SE_SQL_H
#define GNC_SE_SQL_H

#include <memory>

#include "gnc-sql-object-backend.hpp"

class GncSeHistoryStore;

class GncSqlSeHistoryBackend : public GncSqlObjectBackend
{
public:
    GncSqlSeHistoryBackend ();
    /** The store stops writing: the connection is about to go away. */
    ~GncSqlSeHistoryBackend () override;
    /** Reads the tables and gives the book a store that writes to them. */
    void load_all (GncSqlBackend* sql_be) override;
    void create_tables (GncSqlBackend* sql_be) override;
    /** There is no object to save: the store writes as things happen. */
    bool commit (GncSqlBackend*, QofInstance*) override { return true; }
    /** Writes the whole history, when a book is saved to a new database. */
    bool write (GncSqlBackend* sql_be) override;

    /** The store that this backend has given its book. */
    void set_store (const std::shared_ptr<GncSeHistoryStore>& store)
    {
        m_store = store;
    }

private:
    std::weak_ptr<GncSeHistoryStore> m_store;
};

/** Called first in GncSqlBackend::sync(): takes the processing history
 *  out of the book's slots before the book is written. */
void gnc_sql_se_before_sync (GncSqlBackend* sql_be, QofBook* book);

#endif /* GNC_SE_SQL_H */
