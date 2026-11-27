/********************************************************************\
 * gnc-se-sql.cpp -- GnuCash SE: the processing history in SQL      *
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

#include <config.h>

#include <glib.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gnc-se.hpp>
#include <qof.h>

#include "gnc-se-sql.h"
#include "gnc-sql-backend.hpp"
#include "gnc-sql-connection.hpp"
#include "gnc-sql-result.hpp"

static QofLogModule log_module = "gnc.backend.sql.se";

/* The type the object backend is registered under. It is not the type
 * of any QofInstance, so the backend is never asked to commit one. */
static const char* SE_HISTORY_TYPE = "gnc-se-history";
static const char* EVENTS_TABLE = "se_events";
static const char* WATCHED_TABLE = "se_watched";
static const int TABLE_VERSION = 1;

/* The tables are created with plain SQL: they need a column without a
 * length limit for the texts, which the column table entries of the
 * backend do not offer. The column names carry a prefix so that none of
 * them is a keyword in SQLite, PostgreSQL or MySQL. */
static const char* CREATE_EVENTS =
    "CREATE TABLE IF NOT EXISTS se_events ("
    "ev_number BIGINT PRIMARY KEY NOT NULL, "
    "ev_time BIGINT NOT NULL, "
    "ev_type VARCHAR(32) NOT NULL, "
    "ev_signature TEXT, "
    "ev_text TEXT, "
    "ev_hash VARCHAR(64) NOT NULL)";
static const char* CREATE_WATCHED =
    "CREATE TABLE IF NOT EXISTS se_watched ("
    "w_kind VARCHAR(32) NOT NULL, "
    "w_key VARCHAR(32) NOT NULL, "
    "w_state TEXT, "
    "w_text TEXT, "
    "PRIMARY KEY (w_kind, w_key))";

static const EntryVec no_columns;

namespace
{

/* The history in memory, written to the tables as it changes. Reading
 * never goes to the database: the history is small next to the book,
 * and the whole book is in memory too. */
class SqlHistoryStore : public GncSeHistoryStore
{
public:
    explicit SqlHistoryStore (GncSqlBackend* sql_be) : m_sql_be {sql_be} {}

    std::vector<GncSeStoredEvent> events () const override { return m_events; }

    std::optional<GncSeStoredEvent> event (int64_t number) const override
    {
        /* The numbers are normally 1, 2, 3, ... without gaps. */
        if (number >= 1 && static_cast<size_t> (number) <= m_events.size () &&
            m_events[static_cast<size_t> (number) - 1].event.number == number)
            return m_events[static_cast<size_t> (number) - 1];
        auto found {std::find_if (m_events.begin (), m_events.end (),
                                  [number] (const GncSeStoredEvent& stored)
                                  { return stored.event.number == number; })};
        if (found == m_events.end ())
            return std::nullopt;
        return *found;
    }

    void append (const GncSeStoredEvent& stored) override
    {
        m_events.push_back (stored);
        if (m_writing)
            insert_event (stored);
    }

    std::optional<GncSeWatched> watched (const std::string& kind,
                                         const std::string& key) const override
    {
        auto found {m_watched.find ({kind, key})};
        if (found == m_watched.end ())
            return std::nullopt;
        return found->second;
    }

    void set_watched (const std::string& kind, const std::string& key,
                      const GncSeWatched& watched) override
    {
        auto [iter, inserted] = m_watched.insert_or_assign ({kind, key}, watched);
        if (!m_writing)
            return;
        if (!inserted)
            delete_watched (kind, key);
        insert_watched (kind, key, watched);
    }

    void erase_watched (const std::string& kind,
                        const std::string& key) override
    {
        if (m_watched.erase ({kind, key}) && m_writing)
            delete_watched (kind, key);
    }

    std::vector<std::string> watched_keys (const std::string& kind) const override
    {
        std::vector<std::string> keys;
        for (const auto& [id, watched] : m_watched)
            if (id.first == kind)
                keys.push_back (id.second);
        return keys; /* The map is ordered, so the keys are. */
    }

    bool begin () override
    {
        return m_sql_be && m_sql_be->begin_transaction ();
    }
    bool commit () override
    {
        return m_sql_be && m_sql_be->commit_transaction ();
    }
    bool rollback () override
    {
        return m_sql_be && m_sql_be->rollback_transaction ();
    }

    /* Whether changes go to the tables as they happen. They do not while
     * a book is being saved to a new database: the tables do not exist
     * until write_all() has made them. */
    void set_writing (bool writing) { m_writing = writing; }
    GncSqlBackend* backend () const { return m_sql_be; }
    void detach ()
    {
        m_sql_be = nullptr;
        m_writing = false;
    }

    /* Fills the store from the tables. */
    bool read_all ()
    {
        m_events.clear ();
        m_watched.clear ();
        auto events {select (std::string {"SELECT * FROM "} + EVENTS_TABLE +
                             " ORDER BY ev_number")};
        if (!events)
            return false;
        for (auto row : *events)
        {
            auto number {row.get_int_at_col ("ev_number")};
            auto time {row.get_int_at_col ("ev_time")};
            if (!number || !time)
                continue;
            m_events.push_back (
                {{*number, *time, row.get_string_at_col ("ev_type").value_or (""),
                  row.get_string_at_col ("ev_signature").value_or (""),
                  row.get_string_at_col ("ev_text").value_or ("")},
                 row.get_string_at_col ("ev_hash").value_or ("")});
        }
        auto watched {select (std::string {"SELECT * FROM "} + WATCHED_TABLE)};
        if (!watched)
            return false;
        for (auto row : *watched)
        {
            auto kind {row.get_string_at_col ("w_kind")};
            auto key {row.get_string_at_col ("w_key")};
            if (!kind || !key)
                continue;
            m_watched[{*kind, *key}] = {
                row.get_string_at_col ("w_state").value_or (""),
                row.get_string_at_col ("w_text").value_or ("")};
        }
        return true;
    }

    /* Replaces what the tables hold with what the store holds. */
    bool write_all ()
    {
        bool ok {execute (std::string {"DELETE FROM "} + EVENTS_TABLE) &&
                 execute (std::string {"DELETE FROM "} + WATCHED_TABLE)};
        for (const auto& stored : m_events)
            ok = ok && insert_event (stored);
        for (const auto& [id, watched] : m_watched)
            ok = ok && insert_watched (id.first, id.second, watched);
        return ok;
    }

private:
    GncSqlResultPtr select (const std::string& sql) const
    {
        if (!m_sql_be)
            return nullptr;
        auto statement {m_sql_be->create_statement_from_sql (sql)};
        return statement ? m_sql_be->execute_select_statement (statement)
                         : nullptr;
    }

    bool execute (const std::string& sql) const
    {
        if (!m_sql_be)
            return false;
        auto statement {m_sql_be->create_statement_from_sql (sql)};
        if (!statement || m_sql_be->execute_nonselect_statement (statement) < 0)
        {
            PERR ("Could not store the processing history: %s", sql.c_str ());
            return false;
        }
        return true;
    }

    std::string quote (const std::string& text) const
    {
        return m_sql_be->quote_string (text);
    }

    bool insert_event (const GncSeStoredEvent& stored) const
    {
        if (!m_sql_be)
            return false;
        return execute (
            std::string {"INSERT INTO "} + EVENTS_TABLE +
            " (ev_number, ev_time, ev_type, ev_signature, ev_text, ev_hash) "
            "VALUES (" +
            std::to_string (stored.event.number) + ", " +
            std::to_string (stored.event.time) + ", " +
            quote (stored.event.type) + ", " + quote (stored.event.signature) +
            ", " + quote (stored.event.text) + ", " + quote (stored.hash) + ")");
    }

    bool insert_watched (const std::string& kind, const std::string& key,
                         const GncSeWatched& watched) const
    {
        if (!m_sql_be)
            return false;
        return execute (std::string {"INSERT INTO "} + WATCHED_TABLE +
                        " (w_kind, w_key, w_state, w_text) VALUES (" +
                        quote (kind) + ", " + quote (key) + ", " +
                        quote (watched.state) + ", " + quote (watched.text) +
                        ")");
    }

    bool delete_watched (const std::string& kind, const std::string& key) const
    {
        if (!m_sql_be)
            return false;
        return execute (std::string {"DELETE FROM "} + WATCHED_TABLE +
                        " WHERE w_kind = " + quote (kind) + " AND w_key = " +
                        quote (key));
    }

    GncSqlBackend* m_sql_be;
    bool m_writing {false};
    std::vector<GncSeStoredEvent> m_events;
    std::map<std::pair<std::string, std::string>, GncSeWatched> m_watched;
};

/* The book's store, if it is one of ours and belongs to this backend. */
std::shared_ptr<SqlHistoryStore>
our_store (GncSqlBackend* sql_be, QofBook* book)
{
    if (!book)
        return nullptr;
    auto store {std::dynamic_pointer_cast<SqlHistoryStore> (
        gnc_se_book_get_history_store (book))};
    return store && store->backend () == sql_be ? store : nullptr;
}

/* Lets the backend's object backend know the store, so that the store
 * stops writing when the backend goes away. The book may outlive it. */
void
remember (GncSqlBackend* sql_be, const std::shared_ptr<SqlHistoryStore>& store)
{
    if (auto backend {std::dynamic_pointer_cast<GncSqlSeHistoryBackend> (
            sql_be->get_object_backend (SE_HISTORY_TYPE))})
        backend->set_store (store);
}

} // namespace

GncSqlSeHistoryBackend::GncSqlSeHistoryBackend ()
    : GncSqlObjectBackend (TABLE_VERSION, SE_HISTORY_TYPE, EVENTS_TABLE,
                           no_columns)
{
}

GncSqlSeHistoryBackend::~GncSqlSeHistoryBackend ()
{
    if (auto store {std::dynamic_pointer_cast<SqlHistoryStore> (m_store.lock ())})
        store->detach ();
}

void
GncSqlSeHistoryBackend::create_tables (GncSqlBackend* sql_be)
{
    g_return_if_fail (sql_be != nullptr);
    for (auto table : {EVENTS_TABLE, WATCHED_TABLE})
    {
        if (sql_be->get_table_version (table) != 0)
            continue;
        auto statement {sql_be->create_statement_from_sql (
            table == EVENTS_TABLE ? CREATE_EVENTS : CREATE_WATCHED)};
        if (!statement || sql_be->execute_nonselect_statement (statement) < 0)
        {
            PERR ("Could not create the table %s", table);
            continue;
        }
        sql_be->set_table_version (table, TABLE_VERSION);
    }
}

void
GncSqlSeHistoryBackend::load_all (GncSqlBackend* sql_be)
{
    g_return_if_fail (sql_be != nullptr);
    auto book {sql_be->book ()};
    if (!book)
        return;
    /* A database that GnuCash SE has not saved to has no tables, and its
     * book keeps whatever history it has in its slots. */
    if (sql_be->get_table_version (EVENTS_TABLE) == 0 ||
        sql_be->get_table_version (WATCHED_TABLE) == 0)
        return;
    auto store {std::make_shared<SqlHistoryStore> (sql_be)};
    if (!store->read_all ())
        return;
    store->set_writing (true);
    remember (sql_be, store);
    gnc_se_book_set_history_store (book, store);
}

bool
GncSqlSeHistoryBackend::write (GncSqlBackend* sql_be)
{
    g_return_val_if_fail (sql_be != nullptr, false);
    auto store {our_store (sql_be, sql_be->book ())};
    if (!store)
        return true;
    auto ok {store->write_all ()};
    store->set_writing (ok);
    return ok;
}

void
gnc_sql_se_before_sync (GncSqlBackend* sql_be, QofBook* book)
{
    g_return_if_fail (sql_be != nullptr && book != nullptr);
    auto store {our_store (sql_be, book)};
    if (!store)
    {
        /* The history moves from the book's slots (or from the store of
         * another backend, by way of the slots) into a store of ours. */
        gnc_se_history_to_book (book);
        store = std::make_shared<SqlHistoryStore> (sql_be);
        remember (sql_be, store);
        gnc_se_book_set_history_store (book, store);
    }
    /* Nothing is written until write() has the new tables to write to. */
    store->set_writing (false);
}
