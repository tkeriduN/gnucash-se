/********************************************************************\
 * gnc-se.cpp -- GnuCash SE: lock date and voucher numbering        *
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
#include <glib/gi18n.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string_view>
#include <utility>

#include "gnc-se.hpp"

#include <Account.h>
#include <Account.hpp>
#include <Split.h>
#include <SX-book.h>
#include <gnc-commodity.h>
#include <gnc-date.h>
#include <kvp-frame.hpp>
#include <qofbook.h>
#include <qofinstance-p.h>
#include <qofsession.h>

static QofLogModule log_module = "gnc.se";

/* Version of the "se" slots. */
static const int64_t FORMAT_VERSION = 1;

/* Everything GnuCash SE stores lives under this slot, on the book and
 * on each transaction. Described in docs/design/01-las-och-numrering.md. */
static const char* SE = "se";

static const char* DEFAULT_SERIES = "A";
static const char* PERIOD_FISCAL_YEAR = "fiscal-year";
static const char* PERIOD_CONTINUOUS = "continuous";
static const char* PERIOD_ALL = "all";
static const char* DEFERRAL_MONTH = "month";
static const char* DEFERRAL_QUARTER = "quarter";

/* ------------------------------------------------------------------ */
/* Slot access                                                        */

static QofInstance*
inst (const QofBook* book)
{
    return QOF_INSTANCE (const_cast<QofBook*> (book));
}

static QofInstance*
inst (const Transaction* trans)
{
    return QOF_INSTANCE (const_cast<Transaction*> (trans));
}

static std::string
get_string (QofInstance* instance, const Path& path)
{
    auto value {qof_instance_get_path_kvp<const char*> (instance, path)};
    return value && *value ? *value : "";
}

static int64_t
get_int (QofInstance* instance, const Path& path)
{
    auto value {qof_instance_get_path_kvp<int64_t> (instance, path)};
    return value ? *value : 0;
}

static void
set_string (QofInstance* instance, const Path& path, const std::string& value)
{
    /* The slot takes ownership of the string. */
    qof_instance_set_path_kvp<const char*> (instance,
                                            g_strdup (value.c_str ()), path);
}

static void
set_int (QofInstance* instance, const Path& path, int64_t value)
{
    qof_instance_set_path_kvp<int64_t> (instance, value, path);
}

namespace
{
/* Keeps the book open for editing for as long as it lives. */
struct BookEdit
{
    QofBook* m_book;
    explicit BookEdit (QofBook* book) : m_book {book}
    {
        qof_book_begin_edit (m_book);
    }
    ~BookEdit () { qof_book_commit_edit (m_book); }
    BookEdit (const BookEdit&) = delete;
    BookEdit& operator= (const BookEdit&) = delete;
};
}

/* ------------------------------------------------------------------ */
/* Book settings                                                      */

/* Records that the book is kept in GnuCash SE. */
static void
mark_book (QofBook* book)
{
    if (get_int (inst (book), {SE, "version"}) != FORMAT_VERSION)
        set_int (inst (book), {SE, "version"}, FORMAT_VERSION);
}

bool
gnc_se_book_is_se (const QofBook* book)
{
    g_return_val_if_fail (book, false);
    return get_int (inst (book), {SE, "version"}) > 0;
}

std::optional<GDate>
gnc_se_book_get_lock_date (const QofBook* book)
{
    g_return_val_if_fail (book, std::nullopt);
    auto date {qof_instance_get_path_kvp<GDate> (inst (book), {SE, "lock-date"})};
    if (date && g_date_valid (&*date))
        return date;
    return std::nullopt;
}

bool
gnc_se_date_is_locked (const QofBook* book, const GDate& date)
{
    auto lock_date {gnc_se_book_get_lock_date (book)};
    return lock_date && g_date_compare (&date, &*lock_date) <= 0;
}

gboolean
gnc_se_book_get_lock_gdate (const QofBook* book, GDate* date)
{
    g_return_val_if_fail (book && date, FALSE);
    auto lock_date {gnc_se_book_get_lock_date (book)};
    if (!lock_date)
        return FALSE;
    *date = *lock_date;
    return TRUE;
}

gboolean
gnc_se_book_date_is_locked (const QofBook* book, const GDate* date)
{
    g_return_val_if_fail (book && date, FALSE);
    return g_date_valid (date) && gnc_se_date_is_locked (book, *date);
}

/* 2001 is not a leap year: a fiscal year cannot start on 29 February. */
static bool
fiscal_year_start_is_valid (int month, int day)
{
    return month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
        g_date_valid_dmy (static_cast<GDateDay> (day),
                          static_cast<GDateMonth> (month), 2001);
}

std::pair<int, int>
gnc_se_book_get_fiscal_year_start (const QofBook* book)
{
    g_return_val_if_fail (book, std::make_pair (1, 1));
    auto text {get_string (inst (book), {SE, "fiscal-year-start"})};
    int month, day;
    if (std::sscanf (text.c_str (), "%2d-%2d", &month, &day) == 2 &&
        fiscal_year_start_is_valid (month, day))
        return {month, day};
    return {1, 1};
}

bool
gnc_se_book_set_fiscal_year_start (QofBook* book, int month, int day)
{
    g_return_val_if_fail (book, false);
    if (!fiscal_year_start_is_valid (month, day))
        return false;

    char text[6];
    g_snprintf (text, sizeof text, "%02d-%02d", month, day);
    BookEdit edit {book};
    set_string (inst (book), {SE, "fiscal-year-start"}, text);
    mark_book (book);
    return true;
}

static bool
series_code_is_valid (const std::string& code)
{
    return !code.empty () && code.size () <= 8 &&
        std::all_of (code.begin (), code.end (),
                     [] (unsigned char c) { return g_ascii_isalnum (c); });
}

static std::optional<GncSeSeries>
find_series (const QofBook* book, const std::string& code)
{
    if (!series_code_is_valid (code))
        return std::nullopt;
    auto period {get_string (inst (book), {SE, "series", code, "period"})};
    if (period.empty ())
        return std::nullopt;
    return GncSeSeries {code,
            get_string (inst (book), {SE, "series", code, "description"}),
            period == PERIOD_CONTINUOUS ? GncSePeriodType::CONTINUOUS
                                        : GncSePeriodType::FISCAL_YEAR};
}

/* The names of the slots directly below @a path. */
static std::vector<std::string>
child_keys (QofInstance* instance, const Path& path)
{
    auto slots {qof_instance_get_slots (instance)};
    auto value {slots ? slots->get_slot (path) : nullptr};
    auto frame {value ? value->get<KvpFrame*> () : nullptr};
    return frame ? frame->get_keys () : std::vector<std::string> {};
}

std::vector<GncSeSeries>
gnc_se_book_get_series (const QofBook* book)
{
    std::vector<GncSeSeries> result;
    g_return_val_if_fail (book, result);
    for (const auto& code : child_keys (inst (book), {SE, "series"}))
        if (auto series {find_series (book, code)})
            result.push_back (*series);
    return result;
}

bool
gnc_se_book_add_series (QofBook* book, const std::string& code,
                        const std::string& description,
                        GncSePeriodType period_type)
{
    g_return_val_if_fail (book, false);
    if (!series_code_is_valid (code) || find_series (book, code))
        return false;

    BookEdit edit {book};
    set_string (inst (book), {SE, "series", code, "description"}, description);
    set_string (inst (book), {SE, "series", code, "period"},
                period_type == GncSePeriodType::CONTINUOUS ? PERIOD_CONTINUOUS
                                                           : PERIOD_FISCAL_YEAR);
    mark_book (book);
    return true;
}

std::string
gnc_se_book_get_default_series (const QofBook* book)
{
    g_return_val_if_fail (book, DEFAULT_SERIES);
    auto code {get_string (inst (book), {SE, "default-series"})};
    return code.empty () ? DEFAULT_SERIES : code;
}

bool
gnc_se_book_set_default_series (QofBook* book, const std::string& code)
{
    g_return_val_if_fail (book, false);
    if (!find_series (book, code))
        return false;
    BookEdit edit {book};
    set_string (inst (book), {SE, "default-series"}, code);
    mark_book (book);
    return true;
}

GncSeDeferral
gnc_se_book_get_deferral (const QofBook* book)
{
    g_return_val_if_fail (book, GncSeDeferral::MONTH);
    return get_string (inst (book), {SE, "deferral"}) == DEFERRAL_QUARTER
        ? GncSeDeferral::QUARTER : GncSeDeferral::MONTH;
}

void
gnc_se_book_set_deferral (QofBook* book, GncSeDeferral deferral)
{
    g_return_if_fail (book);
    BookEdit edit {book};
    set_string (inst (book), {SE, "deferral"},
                deferral == GncSeDeferral::QUARTER ? DEFERRAL_QUARTER
                                                   : DEFERRAL_MONTH);
    mark_book (book);
}

std::string
gnc_se_book_get_period (const QofBook* book, const GncSeSeries& series,
                        const GDate& date)
{
    if (series.period_type == GncSePeriodType::CONTINUOUS)
        return PERIOD_ALL;

    auto [start_month, start_day] = gnc_se_book_get_fiscal_year_start (book);
    int year = g_date_get_year (&date);
    int month = g_date_get_month (&date);
    int day = g_date_get_day (&date);
    if (month < start_month || (month == start_month && day < start_day))
        --year;
    return std::to_string (year);
}

/* ------------------------------------------------------------------ */
/* Transactions                                                       */

int64_t
gnc_se_trans_get_number (const Transaction* trans)
{
    g_return_val_if_fail (trans, 0);
    return get_int (inst (trans), {SE, "number"});
}

gboolean
gnc_se_trans_is_locked (const Transaction* trans)
{
    return gnc_se_trans_get_number (trans) > 0;
}

gboolean
gnc_se_account_has_locked (const Account* account)
{
    g_return_val_if_fail (account, FALSE);
    auto has_locked = [] (const Account* acc)
    {
        const auto& splits {xaccAccountGetSplits (acc)};
        return std::any_of (splits.begin (), splits.end (),
                            [] (const Split* split)
                            {
                                auto trans {xaccSplitGetParent (split)};
                                return trans && gnc_se_trans_is_locked (trans);
                            });
    };
    if (has_locked (account))
        return TRUE;
    bool found = false;
    gnc_account_foreach_descendant (account, [&] (Account* child)
                                    {
                                        found = found || has_locked (child);
                                    });
    return found;
}

std::string
gnc_se_trans_get_series (const Transaction* trans)
{
    g_return_val_if_fail (trans, "");
    auto code {get_string (inst (trans), {SE, "series"})};
    return code.empty () ? gnc_se_book_get_default_series (xaccTransGetBook (trans))
                         : code;
}

bool
gnc_se_trans_set_series (Transaction* trans, const std::string& code)
{
    g_return_val_if_fail (trans, false);
    if (gnc_se_trans_is_locked (trans) ||
        !find_series (xaccTransGetBook (trans), code))
        return false;

    xaccTransBeginEdit (trans);
    set_string (inst (trans), {SE, "series"}, code);
    xaccTransCommitEdit (trans);
    return get_string (inst (trans), {SE, "series"}) == code;
}

std::string
gnc_se_trans_get_period (const Transaction* trans)
{
    g_return_val_if_fail (trans, "");
    return get_string (inst (trans), {SE, "period"});
}

static std::string
format_voucher (const std::string& series, int64_t number)
{
    return series + " " + std::to_string (number);
}

std::string
gnc_se_trans_get_voucher (const Transaction* trans)
{
    auto number {gnc_se_trans_get_number (trans)};
    return number > 0 ? format_voucher (gnc_se_trans_get_series (trans), number)
                      : "";
}

std::string
gnc_se_trans_get_former_num (const Transaction* trans)
{
    g_return_val_if_fail (trans, "");
    return get_string (inst (trans), {SE, "former-num"});
}

/* Scheduled-transaction templates live in their own account tree and
 * are not bookkeeping entries. */
static bool
is_template (const Transaction* trans)
{
    auto template_root {gnc_book_get_template_root (xaccTransGetBook (trans))};
    if (!template_root)
        return false;
    for (auto node = xaccTransGetSplitList (trans); node; node = node->next)
    {
        auto account {xaccSplitGetAccount (GNC_SPLIT (node->data))};
        if (account && gnc_account_get_root (account) == template_root)
            return true;
    }
    return false;
}

/* The splits of a transaction. For a transaction that is being edited
 * (@a live) splits that are about to be removed are left out. */
static std::vector<const Split*>
get_splits (const Transaction* trans, bool live)
{
    std::vector<const Split*> splits;
    for (auto node = xaccTransGetSplitList (trans); node; node = node->next)
    {
        auto split {GNC_SPLIT (node->data)};
        if (!live || xaccTransStillHasSplit (trans, split))
            splits.push_back (split);
    }
    return splits;
}

/* Whether the transaction is something to lock and number. */
static bool
is_entry (const Transaction* trans, bool live = true)
{
    return !is_template (trans) && !xaccTransGetVoidStatus (trans) &&
        !get_splits (trans, live).empty ();
}

/* Whether the transaction is an entry dated in the locked period. */
static bool
in_locked_period (const Transaction* trans, const GDate& lock_date, bool live)
{
    auto posted {xaccTransGetDatePostedGDate (trans)};
    return g_date_valid (&posted) &&
        g_date_compare (&posted, &lock_date) <= 0 && is_entry (trans, live);
}

/* ------------------------------------------------------------------ */
/* Canonical form and hash chain                                      */

/* Appends a field so that no two different sequences of fields give
 * the same text. */
static void
put (std::string& out, std::string_view field)
{
    out += std::to_string (field.size ());
    out += ':';
    out += field;
    out += '\n';
}

static std::string_view
view (const char* text)
{
    return text ? text : "";
}

static std::string
guid_string (const GncGUID* guid)
{
    char buffer[GUID_ENCODING_LENGTH + 1];
    guid_to_string_buff (guid, buffer);
    return buffer;
}

static std::string
numeric_string (gnc_numeric value)
{
    auto reduced {gnc_numeric_reduce (value)};
    if (!gnc_numeric_check (reduced))
        value = reduced;
    return std::to_string (value.num) + "/" + std::to_string (value.denom);
}

/* The protected content of a transaction: what must stay the same
 * once it is locked. Reconciliation, lots, notes and links are not
 * part of it. Changing the layout changes every hash, so a new layout
 * needs a new FORMAT_VERSION. */
static std::string
content (const Transaction* trans, bool live)
{
    std::string out;

    auto posted {xaccTransGetDatePostedGDate (trans)};
    char date[16] = "";
    if (g_date_valid (&posted))
        g_snprintf (date, sizeof date, "%04d-%02d-%02d",
                    g_date_get_year (&posted), g_date_get_month (&posted),
                    g_date_get_day (&posted));
    put (out, date);
    /* The date is stored twice, as a date and as a time. */
    put (out, std::to_string (xaccTransGetDate (trans)));
    put (out, std::to_string (xaccTransGetDateEntered (trans)));
    put (out, view (xaccTransGetNum (trans)));
    put (out, view (xaccTransGetDescription (trans)));
    auto currency {xaccTransGetCurrency (trans)};
    put (out, view (currency ? gnc_commodity_get_unique_name (currency) : ""));

    auto splits {get_splits (trans, live)};
    std::sort (splits.begin (), splits.end (),
               [] (const Split* a, const Split* b)
               {
                   return guid_compare (xaccSplitGetGUID (a),
                                        xaccSplitGetGUID (b)) < 0;
               });
    put (out, std::to_string (splits.size ()));
    for (auto split : splits)
    {
        auto account {xaccSplitGetAccount (split)};
        put (out, guid_string (xaccSplitGetGUID (split)));
        put (out, account ? guid_string (xaccAccountGetGUID (account)) : "");
        put (out, numeric_string (xaccSplitGetValue (split)));
        put (out, numeric_string (xaccSplitGetAmount (split)));
        put (out, view (xaccSplitGetMemo (split)));
        put (out, view (xaccSplitGetAction (split)));
    }
    return out;
}

/* The slots GnuCash SE sets when it locks a transaction. */
static std::string
lock_slots (const Transaction* trans)
{
    std::string out;
    put (out, get_string (inst (trans), {SE, "series"}));
    put (out, get_string (inst (trans), {SE, "period"}));
    put (out, std::to_string (get_int (inst (trans), {SE, "number"})));
    auto locked_at {qof_instance_get_path_kvp<Time64> (inst (trans),
                                                       {SE, "locked-at"})};
    put (out, std::to_string (locked_at ? locked_at->t : 0));
    put (out, get_string (inst (trans), {SE, "hash"}));
    put (out, get_string (inst (trans), {SE, "former-num"}));
    return out;
}

static std::string
sha256 (const std::string& data)
{
    auto hex {g_compute_checksum_for_data (
            G_CHECKSUM_SHA256,
            reinterpret_cast<const guchar*> (data.data ()), data.size ())};
    std::string result {hex};
    g_free (hex);
    return result;
}

/* The hash before the first transaction of a series' period. */
static std::string
chain_start (const QofBook* book, const std::string& series,
             const std::string& period)
{
    std::string text;
    put (text, "gnc-se-chain-1");
    put (text, guid_string (qof_book_get_guid (book)));
    put (text, series);
    put (text, period);
    return sha256 (text);
}

/* The hash of a locked transaction given the hash of the one before it. */
static std::string
chain_next (const std::string& previous, const Transaction* trans)
{
    std::string text;
    put (text, "gnc-se-trans-1");
    put (text, previous);
    put (text, guid_string (xaccTransGetGUID (trans)));
    put (text, get_string (inst (trans), {SE, "series"}));
    put (text, get_string (inst (trans), {SE, "period"}));
    put (text, std::to_string (get_int (inst (trans), {SE, "number"})));
    put (text, content (trans, true));
    return sha256 (text);
}

/* ------------------------------------------------------------------ */
/* The commit guard                                                   */

/* True while gnc_se_lock_through() locks transactions. */
static bool lock_in_progress = false;

static std::string last_rejection;

const std::string&
gnc_se_last_rejection ()
{
    return last_rejection;
}

static gboolean
reject (const Transaction* trans, const char* reason)
{
    last_rejection = reason;
    PWARN ("Edit of transaction %s refused: %s",
           guid_string (xaccTransGetGUID (trans)).c_str (), reason);
    return FALSE;
}

/* Called by xaccTransCommitEdit() with the transaction as edited and
 * as it was when the edit began. */
static gboolean
commit_guard (Transaction* trans, const Transaction* orig)
{
    /* A backend that reads a book creates the transactions as stored. */
    if (qof_session_load_in_progress ())
        return TRUE;

    auto destroying {qof_instance_get_destroying (trans)};

    if (gnc_se_trans_is_locked (orig))
    {
        if (destroying)
            return reject (trans, _("A posted transaction cannot be deleted."));
        if (!xaccTransGetReadOnly (trans))
            return reject (trans, _("A posted transaction must stay read-only."));
        if (lock_slots (trans) != lock_slots (orig) ||
            content (trans, true) != content (orig, false))
            return reject (trans, _("A posted transaction cannot be changed. "
                                    "Correct it with a correcting entry."));
        last_rejection.clear ();
        return TRUE;
    }

    if (gnc_se_trans_is_locked (trans))
    {
        if (!lock_in_progress)
            return reject (trans, _("Transactions are posted by locking a "
                                    "period."));
        return TRUE;
    }

    /* A preliminary transaction may be changed and deleted freely, but
     * not moved into the locked period. A transaction that is being
     * created is let through wherever it is dated: an edit that has
     * never been committed cannot be rolled back to anything, and the
     * code that creates it goes on using it. What ends up in the locked
     * period that way is found by gnc_se_verify() and stops the next
     * lock. The user interface refuses such dates beforehand.
     *
     * A transaction is being created if it had no splits when the edit
     * began: one without splits does not survive a commit. (The
     * instance's "infant" flag is only cleared by the SQL backend.) */
    if (!destroying && !get_splits (orig, false).empty ())
    {
        auto lock_date {gnc_se_book_get_lock_date (xaccTransGetBook (trans))};
        if (lock_date && in_locked_period (trans, *lock_date, true) &&
            !in_locked_period (orig, *lock_date, false))
            return reject (trans, _("The date is in a locked period."));
    }
    last_rejection.clear ();
    return TRUE;
}

void
gnc_se_init ()
{
    gnc_trans_set_commit_guard (commit_guard);
}

void
gnc_se_shutdown ()
{
    gnc_trans_set_commit_guard (nullptr);
    last_rejection.clear ();
}

/* ------------------------------------------------------------------ */
/* Locking                                                            */

static std::vector<Transaction*>
all_transactions (QofBook* book)
{
    std::vector<Transaction*> result;
    qof_collection_foreach (
        qof_book_get_collection (book, GNC_ID_TRANS),
        [] (QofInstance* instance, gpointer data)
        {
            static_cast<std::vector<Transaction*>*> (data)->push_back (
                GNC_TRANSACTION (instance));
        },
        &result);
    return result;
}

static bool
entered_before (const Transaction* a, const Transaction* b)
{
    auto entered_a {xaccTransGetDateEntered (a)};
    auto entered_b {xaccTransGetDateEntered (b)};
    if (entered_a != entered_b)
        return entered_a < entered_b;
    return guid_compare (xaccTransGetGUID (a), xaccTransGetGUID (b)) < 0;
}

static std::string
describe (const Transaction* trans)
{
    char date[MAX_DATE_LENGTH + 1] = "";
    auto posted {xaccTransGetDatePostedGDate (trans)};
    qof_print_gdate (date, sizeof date, &posted);
    return std::string {date} + " " + std::string {view (xaccTransGetDescription (trans))};
}

static GncSeLockResult
lock_error (const char* format, ...) G_GNUC_PRINTF (1, 2);

static GncSeLockResult
lock_error (const char* format, ...)
{
    va_list args;
    va_start (args, format);
    auto text {g_strdup_vprintf (format, args)};
    va_end (args);
    GncSeLockResult result {false, 0, text};
    g_free (text);
    return result;
}

namespace
{
/* Where a series' period stands: the next number and the last hash. */
struct PeriodState
{
    int64_t next;
    std::string head;
};

using PeriodKey = std::pair<std::string, std::string>; /* series, period */
}

static PeriodState
read_period_state (const QofBook* book, const PeriodKey& key)
{
    Path base {SE, "series", key.first, "periods", key.second};
    auto next_path {base};
    next_path.push_back ("next");
    auto head_path {base};
    head_path.push_back ("head");

    PeriodState state {get_int (inst (book), next_path),
                       get_string (inst (book), head_path)};
    if (state.next < 1)
        state.next = 1;
    if (state.head.empty ())
        state.head = chain_start (book, key.first, key.second);
    return state;
}

static void
write_period_state (QofBook* book, const PeriodKey& key,
                    const PeriodState& state)
{
    Path base {SE, "series", key.first, "periods", key.second};
    auto next_path {base};
    next_path.push_back ("next");
    auto head_path {base};
    head_path.push_back ("head");
    set_int (inst (book), next_path, state.next);
    set_string (inst (book), head_path, state.head);
}

/* Locks one transaction as number state.next of its series' period. */
static bool
lock_transaction (Transaction* trans, const PeriodKey& key, PeriodState& state)
{
    auto number {state.next};
    auto voucher {format_voucher (key.first, number)};

    xaccTransBeginEdit (trans);
    std::string former_num {view (xaccTransGetNum (trans))};
    if (!former_num.empty ())
        set_string (inst (trans), {SE, "former-num"}, former_num);
    set_string (inst (trans), {SE, "series"}, key.first);
    set_string (inst (trans), {SE, "period"}, key.second);
    set_int (inst (trans), {SE, "number"}, number);
    qof_instance_set_path_kvp<Time64> (inst (trans), Time64 {gnc_time (nullptr)},
                                       {SE, "locked-at"});
    xaccTransSetNum (trans, voucher.c_str ());
    if (!xaccTransGetReadOnly (trans))
    {
        /* Translators: %s is a voucher number such as "A 12". */
        auto reason {g_strdup_printf (_("Posted in GnuCash SE as %s. Correct it "
                                        "with a correcting entry."),
                                      voucher.c_str ())};
        xaccTransSetReadOnly (trans, reason);
        g_free (reason);
    }
    auto hash {chain_next (state.head, trans)};
    set_string (inst (trans), {SE, "hash"}, hash);
    xaccTransCommitEdit (trans);

    if (gnc_se_trans_get_number (trans) != number)
        return false; /* The guard refused, which it should not. */
    state.next = number + 1;
    state.head = hash;
    return true;
}

/* The series as it will be when something is locked: a book without
 * series gets the default one. */
static std::optional<GncSeSeries>
effective_series (const QofBook* book, const std::string& code)
{
    if (auto series {find_series (book, code)})
        return series;
    if (code == DEFAULT_SERIES && gnc_se_book_get_series (book).empty ())
        return GncSeSeries {DEFAULT_SERIES, _("Vouchers"),
                            GncSePeriodType::FISCAL_YEAR};
    return std::nullopt;
}

static GncSeLockPlan
plan_error (const char* format, ...) G_GNUC_PRINTF (1, 2);

static GncSeLockPlan
plan_error (const char* format, ...)
{
    va_list args;
    va_start (args, format);
    auto text {g_strdup_vprintf (format, args)};
    va_end (args);
    GncSeLockPlan plan {false, text, 0, {}};
    g_free (text);
    return plan;
}

/* Finds what locking through @a date would lock, in the order to lock
 * it, and makes sure that all of it can be locked. Changes nothing. */
static GncSeLockPlan
make_plan (QofBook* book, const GDate& date,
           std::vector<Transaction*>& to_lock, std::vector<PeriodKey>& keys)
{
    if (!g_date_valid (&date))
        return plan_error ("%s", _("The date is not valid."));
    GDate today;
    gnc_gdate_set_today (&today);
    if (g_date_compare (&date, &today) > 0)
        return plan_error ("%s", _("A period cannot be locked before it has "
                                   "ended: the date is after today."));
    auto lock_date {gnc_se_book_get_lock_date (book)};
    if (lock_date && g_date_compare (&date, &*lock_date) <= 0)
        return plan_error ("%s", _("The book is already locked through that "
                                   "date. The lock date can only be moved "
                                   "forward."));

    for (auto trans : all_transactions (book))
    {
        if (gnc_se_trans_is_locked (trans) || !is_entry (trans))
            continue;
        auto posted {xaccTransGetDatePostedGDate (trans)};
        if (g_date_compare (&posted, &date) > 0)
            continue;
        if (lock_date && g_date_compare (&posted, &*lock_date) <= 0)
            return plan_error (_("There is a preliminary transaction in the "
                                 "period that is already locked: %s. Move it "
                                 "to an open period or delete it."),
                               describe (trans).c_str ());
        if (!xaccTransIsBalanced (trans))
            return plan_error (_("A transaction is not balanced: %s."),
                               describe (trans).c_str ());
        for (auto split : get_splits (trans, true))
            if (!xaccSplitGetAccount (split))
                return plan_error (_("A transaction has a split without an "
                                     "account: %s."),
                                   describe (trans).c_str ());
        to_lock.push_back (trans);
    }
    std::stable_sort (to_lock.begin (), to_lock.end (), entered_before);

    GncSeLockPlan plan {true, "", static_cast<int> (to_lock.size ()), {}};
    std::map<PeriodKey, size_t> group_index;
    keys.reserve (to_lock.size ());
    for (auto trans : to_lock)
    {
        auto series {effective_series (book, gnc_se_trans_get_series (trans))};
        if (!series)
            return plan_error (_("A transaction is in a voucher series that "
                                 "the book does not have: %s."),
                               describe (trans).c_str ());
        keys.emplace_back (series->code,
                           gnc_se_book_get_period (
                               book, *series,
                               xaccTransGetDatePostedGDate (trans)));
        auto [iter, inserted] = group_index.try_emplace (keys.back (),
                                                         plan.groups.size ());
        if (inserted)
            plan.groups.push_back ({keys.back ().first, keys.back ().second, 0,
                                    read_period_state (book, keys.back ()).next});
        ++plan.groups[iter->second].count;
    }
    return plan;
}

GncSeLockPlan
gnc_se_plan_lock (QofBook* book, const GDate& date)
{
    g_return_val_if_fail (book, plan_error ("No book."));
    std::vector<Transaction*> to_lock;
    std::vector<PeriodKey> keys;
    return make_plan (book, date, to_lock, keys);
}

GncSeLockResult
gnc_se_lock_through (QofBook* book, const GDate& date)
{
    g_return_val_if_fail (book, lock_error ("No book."));

    std::vector<Transaction*> to_lock;
    std::vector<PeriodKey> keys;
    auto plan {make_plan (book, date, to_lock, keys)};
    if (!plan.ok)
        return {false, 0, plan.error};

    BookEdit edit {book};
    if (gnc_se_book_get_series (book).empty ())
        gnc_se_book_add_series (book, DEFAULT_SERIES, _("Vouchers"),
                                GncSePeriodType::FISCAL_YEAR);

    std::map<PeriodKey, PeriodState> states;
    int locked = 0;
    bool failed = false;
    lock_in_progress = true;
    for (size_t i = 0; i < to_lock.size () && !failed; ++i)
    {
        auto [iter, inserted] = states.try_emplace (keys[i]);
        if (inserted)
            iter->second = read_period_state (book, keys[i]);
        if (lock_transaction (to_lock[i], keys[i], iter->second))
            ++locked;
        else
            failed = true;
    }
    lock_in_progress = false;

    /* Record what was numbered even if something went wrong, so that
     * the series stay consistent with their transactions. */
    for (const auto& [key, state] : states)
        write_period_state (book, key, state);
    mark_book (book);
    if (failed)
    {
        auto result {lock_error ("%s", _("A transaction could not be locked. "
                                         "The lock date was not moved."))};
        result.locked = locked;
        return result;
    }

    qof_instance_set_path_kvp<GDate> (inst (book), date, {SE, "lock-date"});
    return {true, locked, ""};
}

/* ------------------------------------------------------------------ */
/* Deadlines                                                          */

/* The last day on which a transaction dated @a posted may be posted. */
static GDate
deadline (const Transaction* trans, GDate posted, GncSeDeferral deferral)
{
    auto splits {get_splits (trans, true)};
    bool cash {std::any_of (splits.begin (), splits.end (),
                            [] (const Split* split)
                            {
                                auto account {xaccSplitGetAccount (split)};
                                return account &&
                                    xaccAccountGetType (account) == ACCT_TYPE_CASH;
                            })};
    GDate due {posted};
    if (cash)
    {
        /* The next working day (5 kap. 2 § BFL, BFNAR 2013:2 point 3.1). */
        do
            g_date_add_days (&due, 1);
        while (g_date_get_weekday (&due) == G_DATE_SATURDAY ||
               g_date_get_weekday (&due) == G_DATE_SUNDAY);
        return due;
    }
    /* 50 days after the end of the month or quarter (points 3.6, 3.7). */
    int month = g_date_get_month (&posted);
    if (deferral == GncSeDeferral::QUARTER)
        month = (month - 1) / 3 * 3 + 3;
    g_date_set_dmy (&due, 1, static_cast<GDateMonth> (month),
                    g_date_get_year (&posted));
    g_date_set_day (&due, g_date_get_days_in_month (g_date_get_month (&due),
                                                    g_date_get_year (&due)));
    g_date_add_days (&due, 50);
    return due;
}

GncSeOverdue
gnc_se_overdue (QofBook* book, const GDate& today)
{
    GncSeOverdue result {0, 0, std::nullopt};
    g_return_val_if_fail (book && g_date_valid (&today), result);

    auto deferral {gnc_se_book_get_deferral (book)};
    for (auto trans : all_transactions (book))
    {
        if (gnc_se_trans_is_locked (trans) || !is_entry (trans))
            continue;
        auto posted {xaccTransGetDatePostedGDate (trans)};
        if (!g_date_valid (&posted))
            continue;
        auto due {deadline (trans, posted, deferral)};
        if (g_date_compare (&due, &today) >= 0)
            continue;
        /* A cash transaction's deadline is at most three days away. */
        if (g_date_days_between (&posted, &due) <= 3)
            ++result.cash;
        else
            ++result.other;
        if (!result.oldest || g_date_compare (&posted, &*result.oldest) < 0)
            result.oldest = posted;
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* Verification                                                       */

static GncSeIssue
issue (GncSeIssueType type, const PeriodKey& key, int64_t number,
       int64_t count, const Transaction* trans)
{
    return {type, key.first, key.second, number, count,
            trans ? *xaccTransGetGUID (trans) : *guid_null ()};
}

std::vector<GncSeIssue>
gnc_se_verify (QofBook* book)
{
    std::vector<GncSeIssue> issues;
    g_return_val_if_fail (book, issues);

    auto lock_date {gnc_se_book_get_lock_date (book)};
    std::map<PeriodKey, std::vector<Transaction*>> groups;

    for (auto trans : all_transactions (book))
    {
        auto posted {xaccTransGetDatePostedGDate (trans)};
        bool in_locked_period {lock_date &&
                               g_date_compare (&posted, &*lock_date) <= 0};
        if (gnc_se_trans_is_locked (trans))
        {
            PeriodKey key {get_string (inst (trans), {SE, "series"}),
                           gnc_se_trans_get_period (trans)};
            groups[key].push_back (trans);
            auto number {gnc_se_trans_get_number (trans)};
            if (!xaccTransGetReadOnly (trans))
                issues.push_back (issue (GncSeIssueType::MISSING_READ_ONLY, key,
                                         number, 0, trans));
            if (!in_locked_period)
                issues.push_back (issue (GncSeIssueType::LOCKED_AFTER_LOCK_DATE,
                                         key, number, 0, trans));
        }
        else if (in_locked_period && is_entry (trans))
            issues.push_back (issue (GncSeIssueType::UNLOCKED_BEFORE_LOCK_DATE,
                                     {}, 0, 0, trans));
    }

    /* Periods that the book says have been numbered, with or without
     * transactions left in them. */
    for (const auto& code : child_keys (inst (book), {SE, "series"}))
        for (const auto& period :
                 child_keys (inst (book), {SE, "series", code, "periods"}))
            groups.try_emplace (PeriodKey {code, period});

    for (auto& [key, transactions] : groups)
    {
        if (!find_series (book, key.first))
            issues.push_back (issue (GncSeIssueType::UNKNOWN_SERIES, key, 0, 0,
                                     nullptr));

        std::stable_sort (transactions.begin (), transactions.end (),
                          [] (const Transaction* a, const Transaction* b)
                          {
                              return gnc_se_trans_get_number (a) <
                                  gnc_se_trans_get_number (b);
                          });

        auto state {read_period_state (book, key)};
        int64_t expected = 1;
        auto previous {chain_start (book, key.first, key.second)};
        bool previous_known = true;
        for (auto trans : transactions)
        {
            auto number {gnc_se_trans_get_number (trans)};
            if (number < expected)
            {
                issues.push_back (issue (GncSeIssueType::DUPLICATE_NUMBER, key,
                                         number, 0, trans));
                continue;
            }
            if (number > expected)
            {
                issues.push_back (issue (GncSeIssueType::GAP, key, expected,
                                         number - expected, nullptr));
                previous_known = false;
            }
            /* Each transaction is checked against the hash stored on
             * the one before it, so that one changed transaction is
             * reported as one. */
            auto stored {get_string (inst (trans), {SE, "hash"})};
            if (previous_known && chain_next (previous, trans) != stored)
                issues.push_back (issue (GncSeIssueType::CHANGED, key, number,
                                         0, trans));
            previous = stored;
            previous_known = true;
            expected = number + 1;
        }

        if (state.next > expected)
            issues.push_back (issue (GncSeIssueType::GAP, key, expected,
                                     state.next - expected, nullptr));
        else if (state.next < expected)
            issues.push_back (issue (GncSeIssueType::COUNTER_MISMATCH, key,
                                     state.next, 0, nullptr));
        else if (previous_known && state.head != previous)
            issues.push_back (issue (GncSeIssueType::HEAD_MISMATCH, key,
                                     expected - 1, 0, nullptr));
    }
    return issues;
}
