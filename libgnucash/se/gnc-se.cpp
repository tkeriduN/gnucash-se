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
#include <gnc-version.h>
#include <gncTaxTable.h>
#include <kvp-frame.hpp>
#include <qofbook.h>
#include <qofinstance-p.h>
#include <qofsession.h>

static QofLogModule log_module = "gnc.se";

/* Version of GnuCash SE's slots. Version 2 has flat keys (design 06). */
static const int64_t FORMAT_VERSION = 2;

/* The first element of every path that GnuCash SE stores under, on the
 * book and on each transaction. The paths are described in
 * docs/design/01-las-och-numrering.md; how they are stored, in
 * docs/design/06-motorn-for-server.md. */
static const char* SE = "se";

static const char* DEFAULT_SERIES = "A";
static const char* PERIOD_FISCAL_YEAR = "fiscal-year";
static const char* PERIOD_CONTINUOUS = "continuous";
static const char* PERIOD_ALL = "all";
static const char* DEFERRAL_MONTH = "month";
static const char* DEFERRAL_QUARTER = "quarter";
static const char* FISCAL_YEARS = "fiscal-years";

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

/* The SQL backend stores every nested frame as a group of rows of its
 * own, read with one query per frame and deleted with two. GnuCash SE
 * therefore has no nested frames: a path {"se", "a", "b"} is the single
 * key "se-a:b" at the top level of the instance. The elements after the
 * first are series codes, period names, GUIDs and fixed words; none of
 * them contains a colon or a slash. */
static Path
flat (const Path& path)
{
    std::string key {path.empty () ? "" : path.front ()};
    for (size_t i = 1; i < path.size (); ++i)
    {
        key += i == 1 ? '-' : ':';
        key += path[i];
    }
    return {key};
}

template <typename T> static std::optional<T>
get_kvp (QofInstance* instance, const Path& path)
{
    return qof_instance_get_path_kvp<T> (instance, flat (path));
}

template <typename T> static void
set_kvp (QofInstance* instance, T value, const Path& path)
{
    qof_instance_set_path_kvp<T> (instance, value, flat (path));
}

static void
delete_kvp (QofInstance* instance, const Path& path)
{
    qof_instance_slot_path_delete (instance, flat (path));
}

static std::string
get_string (QofInstance* instance, const Path& path)
{
    auto value {get_kvp<const char*> (instance, path)};
    return value && *value ? *value : "";
}

static int64_t
get_int (QofInstance* instance, const Path& path)
{
    auto value {get_kvp<int64_t> (instance, path)};
    return value ? *value : 0;
}

static void
set_string (QofInstance* instance, const Path& path, const std::string& value)
{
    /* The slot takes ownership of the string. */
    set_kvp<const char*> (instance,
                                            g_strdup (value.c_str ()), path);
}

static void
set_int (QofInstance* instance, const Path& path, int64_t value)
{
    set_kvp<int64_t> (instance, value, path);
}

static const GncGUID*
get_guid (QofInstance* instance, const Path& path)
{
    auto value {get_kvp<GncGUID*> (instance, path)};
    return value ? *value : nullptr;
}

static void
set_guid (QofInstance* instance, const Path& path, const GncGUID* guid)
{
    /* The slot takes ownership of the copy. */
    set_kvp<GncGUID*> (instance, guid_copy (guid), path);
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

/* Gives a book that was locked before it had fiscal years the years of
 * what is locked; defined further down. */
static void migrate_fiscal_years (QofBook* book);

/* Adds an entry to the processing history; defined further down. */
static void history_append (QofBook* book, const char* type,
                            const std::string& signature,
                            const std::string& text);

static std::string
text_printf (const char* format, ...) G_GNUC_PRINTF (1, 2);

static std::string
text_printf (const char* format, ...)
{
    va_list args;
    va_start (args, format);
    auto text {g_strdup_vprintf (format, args)};
    va_end (args);
    std::string result {text};
    g_free (text);
    return result;
}

/* ------------------------------------------------------------------ */
/* Book settings                                                      */

/* The names of the slots directly below a path; defined further down. */
static std::vector<std::string> child_keys (QofInstance* instance,
                                            const Path& path);

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
    auto date {get_kvp<GDate> (inst (book), {SE, "lock-date"})};
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
    auto [old_month, old_day] = gnc_se_book_get_fiscal_year_start (book);
    if (old_month == month && old_day == day &&
        !get_string (inst (book), {SE, "fiscal-year-start"}).empty ())
        return true;
    /* Once the book has fiscal years they follow each other. */
    if (!child_keys (inst (book), {SE, FISCAL_YEARS}).empty ())
        return false;

    BookEdit edit {book};
    set_string (inst (book), {SE, "fiscal-year-start"}, text);
    mark_book (book);
    /* Translators: month and day as numbers, e.g. "07-01". */
    history_append (book, "settings", "",
                    text_printf (_("The first day of the fiscal year was set "
                                   "to %s (month-day); it was %02d-%02d."),
                                 text, old_month, old_day));
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

/* The names directly below @a path: the distinct next elements of the
 * instance's keys that start with the path. In order. */
static std::vector<std::string>
child_keys (QofInstance* instance, const Path& path)
{
    std::vector<std::string> children;
    auto slots {qof_instance_get_slots (instance)};
    if (!slots)
        return children;
    auto prefix {flat (path).front () + ":"};
    slots->for_each_slot_prefix (
        prefix,
        [] (const char* rest, KvpValue*, std::vector<std::string>& found)
        {
            std::string_view name {rest};
            auto child {name.substr (0, name.find (':'))};
            /* The keys come in order, so the same child comes in a row. */
            if (found.empty () || found.back () != child)
                found.emplace_back (child);
        },
        children);
    std::sort (children.begin (), children.end ());
    return children;
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
    history_append (book, "settings", "",
                    period_type == GncSePeriodType::CONTINUOUS
                    ? text_printf (_("Voucher series %s was added: %s, with "
                                     "continuous numbering."),
                                   code.c_str (), description.c_str ())
                    : text_printf (_("Voucher series %s was added: %s, with "
                                     "new numbers every fiscal year."),
                                   code.c_str (), description.c_str ()));
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
    auto old_code {gnc_se_book_get_default_series (book)};
    if (old_code == code)
        return true;
    BookEdit edit {book};
    set_string (inst (book), {SE, "default-series"}, code);
    mark_book (book);
    history_append (book, "settings", "",
                    text_printf (_("The voucher series of new transactions was "
                                   "set to %s; it was %s."),
                                 code.c_str (), old_code.c_str ()));
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
    if (deferral == gnc_se_book_get_deferral (book) &&
        !get_string (inst (book), {SE, "deferral"}).empty ())
        return;
    BookEdit edit {book};
    set_string (inst (book), {SE, "deferral"},
                deferral == GncSeDeferral::QUARTER ? DEFERRAL_QUARTER
                                                   : DEFERRAL_MONTH);
    mark_book (book);
    history_append (book, "settings", "",
                    deferral == GncSeDeferral::QUARTER
                    ? _("Posting may be put off until 50 days after the end of "
                        "the quarter.")
                    : _("Posting may be put off until 50 days after the end of "
                        "the month."));
}

/* ------------------------------------------------------------------ */
/* Fiscal years                                                       */

static GDate
days_after (GDate date, int days)
{
    if (days >= 0)
        g_date_add_days (&date, static_cast<guint> (days));
    else
        g_date_subtract_days (&date, static_cast<guint> (-days));
    return date;
}

static GDate
months_after (GDate date, int months)
{
    if (months >= 0)
        g_date_add_months (&date, static_cast<guint> (months));
    else
        g_date_subtract_months (&date, static_cast<guint> (-months));
    return date;
}

static std::string
iso_date (const GDate& date)
{
    char text[16];
    g_snprintf (text, sizeof text, "%04d-%02d-%02d", g_date_get_year (&date),
                g_date_get_month (&date), g_date_get_day (&date));
    return text;
}

static std::string
shown_date (const GDate& date)
{
    char text[MAX_DATE_LENGTH + 1] = "";
    qof_print_gdate (text, sizeof text, &date);
    return text;
}

static bool
year_contains (const GncSeFiscalYear& year, const GDate& date)
{
    return g_date_compare (&year.start, &date) <= 0 &&
        g_date_compare (&date, &year.end) <= 0;
}

/* A year gets the calendar year it starts in as its key, which is what
 * the voucher series of the first version were numbered by. A second
 * year starting in the same calendar year gets its first day. */
static std::string
year_key (const GDate& start, const std::vector<std::string>& taken)
{
    auto key {std::to_string (g_date_get_year (&start))};
    if (std::find (taken.begin (), taken.end (), key) == taken.end ())
        return key;
    return iso_date (start);
}

std::vector<GncSeFiscalYear>
gnc_se_book_get_fiscal_years (const QofBook* book)
{
    std::vector<GncSeFiscalYear> years;
    g_return_val_if_fail (book, years);
    for (const auto& key : child_keys (inst (book), {SE, FISCAL_YEARS}))
    {
        auto start {get_kvp<GDate> (
                inst (book), {SE, FISCAL_YEARS, key, "start"})};
        auto end {get_kvp<GDate> (
                inst (book), {SE, FISCAL_YEARS, key, "end"})};
        if (!start || !end || !g_date_valid (&*start) || !g_date_valid (&*end))
            continue;
        auto closed_at {get_kvp<Time64> (
                inst (book), {SE, FISCAL_YEARS, key, "closed-at"})};
        years.push_back ({key, *start, *end, true,
                          closed_at && closed_at->t != 0,
                          closed_at ? closed_at->t : 0,
                          get_string (inst (book),
                                      {SE, FISCAL_YEARS, key, "closed-by"})});
    }
    std::sort (years.begin (), years.end (),
               [] (const GncSeFiscalYear& a, const GncSeFiscalYear& b)
               {
                   return g_date_compare (&a.start, &b.start) < 0;
               });
    return years;
}

static GncSeFiscalYear
new_year (const GDate& start, const GDate& end, std::vector<std::string>& taken)
{
    GncSeFiscalYear year {year_key (start, taken), start, end, false, false, 0,
                          ""};
    taken.push_back (year.key);
    return year;
}

/* The years the book lacks to have one that @a date lies in, in the
 * order to add them; the last one is the year of the date. Empty if
 * the book has that year. */
static std::vector<GncSeFiscalYear>
years_to_reach (const QofBook* book, const GDate& date)
{
    std::vector<GncSeFiscalYear> chain;
    auto years {gnc_se_book_get_fiscal_years (book)};
    std::vector<std::string> taken;
    for (const auto& year : years)
    {
        if (year_contains (year, date))
            return chain;
        taken.push_back (year.key);
    }

    if (years.empty ())
    {
        /* The twelve months from the book's first day of the year. */
        auto [month, day] = gnc_se_book_get_fiscal_year_start (book);
        GDate start;
        g_date_set_dmy (&start, static_cast<GDateDay> (day),
                        static_cast<GDateMonth> (month),
                        g_date_get_year (&date));
        if (g_date_compare (&date, &start) < 0)
            start = months_after (start, -12);
        chain.push_back (new_year (start, days_after (months_after (start, 12),
                                                      -1), taken));
        return chain;
    }

    /* Twelve-month years onwards from the latest year, or backwards
     * from the earliest. */
    const int limit = 2000;
    if (g_date_compare (&date, &years.back ().end) > 0)
    {
        auto start {days_after (years.back ().end, 1)};
        for (int i = 0; i < limit; ++i)
        {
            auto end {days_after (months_after (start, 12), -1)};
            chain.push_back (new_year (start, end, taken));
            if (g_date_compare (&date, &end) <= 0)
                break;
            start = days_after (end, 1);
        }
    }
    else
    {
        auto end {days_after (years.front ().start, -1)};
        for (int i = 0; i < limit; ++i)
        {
            auto start {months_after (days_after (end, 1), -12)};
            chain.push_back (new_year (start, end, taken));
            if (g_date_compare (&start, &date) <= 0)
                break;
            end = days_after (start, -1);
        }
    }
    return chain;
}

GncSeFiscalYear
gnc_se_book_get_fiscal_year (const QofBook* book, const GDate& date)
{
    GncSeFiscalYear none {"", date, date, false, false, 0, ""};
    g_return_val_if_fail (book && g_date_valid (&date), none);
    for (const auto& year : gnc_se_book_get_fiscal_years (book))
        if (year_contains (year, date))
            return year;
    auto chain {years_to_reach (book, date)};
    return chain.empty () ? none : chain.back ();
}

static void
store_year (QofBook* book, const GncSeFiscalYear& year)
{
    set_kvp<GDate> (inst (book), year.start,
                                      {SE, FISCAL_YEARS, year.key, "start"});
    set_kvp<GDate> (inst (book), year.end,
                                      {SE, FISCAL_YEARS, year.key, "end"});
}

/* Gives the book the fiscal year that @a date lies in, and those
 * between it and the years the book has. */
static void
ensure_fiscal_year (QofBook* book, const GDate& date)
{
    for (const auto& year : years_to_reach (book, date))
    {
        store_year (book, year);
        history_append (book, "fiscal-year", "",
                        text_printf (_("Fiscal year %s was set up: %s to %s."),
                                     year.key.c_str (),
                                     shown_date (year.start).c_str (),
                                     shown_date (year.end).c_str ()));
    }
}

/* Whether @a end is a last day that a year starting on @a start can
 * have: not before the first, and at most 18 months (3 kap. 3 § BFL). */
static bool
year_length_is_valid (const GDate& start, const GDate& end)
{
    auto limit {months_after (start, 18)};
    return g_date_compare (&start, &end) <= 0 &&
        g_date_compare (&end, &limit) < 0;
}

GncSeResult
gnc_se_book_define_fiscal_year (QofBook* book, const GDate& start,
                                const GDate& end)
{
    g_return_val_if_fail (book, (GncSeResult {false, "No book."}));
    if (!g_date_valid (&start) || !g_date_valid (&end))
        return {false, _("The date is not valid.")};
    if (!year_length_is_valid (start, end))
        return {false, _("A fiscal year is at most 18 months, and ends after "
                         "it starts.")};

    BookEdit edit {book};
    migrate_fiscal_years (book);
    auto years {gnc_se_book_get_fiscal_years (book)};
    if (years.empty ())
    {
        std::vector<std::string> taken;
        auto year {new_year (start, end, taken)};
        store_year (book, year);
        mark_book (book);
        history_append (book, "fiscal-year", "",
                        text_printf (_("Fiscal year %s was set up: %s to %s."),
                                     year.key.c_str (),
                                     shown_date (start).c_str (),
                                     shown_date (end).c_str ()));
        return {true, ""};
    }

    auto& last {years.back ()};
    if (g_date_compare (&start, &last.start) == 0)
    {
        if (g_date_compare (&end, &last.end) == 0)
            return {true, ""};
        if (last.closed)
            return {false, _("The fiscal year is closed and cannot be "
                             "changed.")};
        auto lock_date {gnc_se_book_get_lock_date (book)};
        if (lock_date && g_date_compare (&end, &*lock_date) < 0)
            return {false, _("The book is already posted past that day. The "
                             "fiscal year cannot end before the lock date.")};
        set_kvp<GDate> (inst (book), end,
                                          {SE, FISCAL_YEARS, last.key, "end"});
        history_append (book, "fiscal-year", "",
                        text_printf (_("The last day of fiscal year %s was "
                                       "moved from %s to %s."),
                                     last.key.c_str (),
                                     shown_date (last.end).c_str (),
                                     shown_date (end).c_str ()));
        return {true, ""};
    }

    auto next_start {days_after (last.end, 1)};
    if (g_date_compare (&start, &next_start) != 0)
        return {false, text_printf (_("Fiscal years follow each other. The "
                                      "next one starts on %s."),
                                    shown_date (next_start).c_str ())};
    std::vector<std::string> taken;
    for (const auto& year : years)
        taken.push_back (year.key);
    auto year {new_year (start, end, taken)};
    store_year (book, year);
    history_append (book, "fiscal-year", "",
                    text_printf (_("Fiscal year %s was set up: %s to %s."),
                                 year.key.c_str (), shown_date (start).c_str (),
                                 shown_date (end).c_str ()));
    return {true, ""};
}

std::string
gnc_se_book_get_period (const QofBook* book, const GncSeSeries& series,
                        const GDate& date)
{
    if (series.period_type == GncSePeriodType::CONTINUOUS)
        return PERIOD_ALL;
    return gnc_se_book_get_fiscal_year (book, date).key;
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

/* A slot that refers to another transaction, as text; empty if unset. */
static std::string
guid_slot_string (const Transaction* trans, const char* key)
{
    auto guid {get_guid (inst (trans), {SE, key})};
    return guid ? guid_string (guid) : "";
}

/* The slots that must stay the same once a transaction is locked:
 * those GnuCash SE sets when it locks, and what a correcting entry
 * says about itself. */
static std::string
lock_slots (const Transaction* trans)
{
    std::string out;
    put (out, get_string (inst (trans), {SE, "series"}));
    put (out, get_string (inst (trans), {SE, "period"}));
    put (out, std::to_string (get_int (inst (trans), {SE, "number"})));
    auto locked_at {get_kvp<Time64> (inst (trans),
                                                       {SE, "locked-at"})};
    put (out, std::to_string (locked_at ? locked_at->t : 0));
    put (out, get_string (inst (trans), {SE, "hash"}));
    put (out, get_string (inst (trans), {SE, "former-num"}));
    put (out, guid_slot_string (trans, "corrects"));
    put (out, guid_slot_string (trans, "replaces"));
    put (out, get_string (inst (trans), {SE, "signature"}));
    put (out, get_string (inst (trans), {SE, "reason"}));
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
    /* Added after the first format: hashed only when present, so that
     * transactions without them keep their hash. */
    if (auto corrects {guid_slot_string (trans, "corrects")}; !corrects.empty ())
    {
        put (text, "corrects");
        put (text, corrects);
        put (text, get_string (inst (trans), {SE, "signature"}));
        put (text, get_string (inst (trans), {SE, "reason"}));
    }
    if (auto replaces {guid_slot_string (trans, "replaces")}; !replaces.empty ())
    {
        put (text, "replaces");
        put (text, replaces);
    }
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

/* Starts and stops recording changes of accounts and tax tables in the
 * processing history; defined further down. */
static void history_watch (bool on);

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
    history_watch (true);
}

void
gnc_se_shutdown ()
{
    gnc_trans_set_commit_guard (nullptr);
    history_watch (false);
    last_rejection.clear ();
}

/* ------------------------------------------------------------------ */
/* Corrections                                                        */

static Transaction*
linked_transaction (const Transaction* trans, const char* key)
{
    auto guid {get_guid (inst (trans), {SE, key})};
    return guid ? xaccTransLookup (guid, xaccTransGetBook (trans)) : nullptr;
}

Transaction*
gnc_se_trans_get_correction (const Transaction* original)
{
    g_return_val_if_fail (original, nullptr);
    auto correction {linked_transaction (original, "corrected-by")};
    /* The link back tells a correcting entry from whatever else might
     * have got the GUID, and one that has been deleted is not found. */
    if (correction && gnc_se_trans_get_corrected (correction) == original)
        return correction;
    return nullptr;
}

Transaction*
gnc_se_trans_get_corrected (const Transaction* correction)
{
    g_return_val_if_fail (correction, nullptr);
    return linked_transaction (correction, "corrects");
}

Transaction*
gnc_se_trans_get_replaced (const Transaction* replacement)
{
    g_return_val_if_fail (replacement, nullptr);
    return linked_transaction (replacement, "replaces");
}

std::string
gnc_se_trans_get_signature (const Transaction* trans)
{
    g_return_val_if_fail (trans, "");
    return get_string (inst (trans), {SE, "signature"});
}

std::string
gnc_se_trans_get_reason (const Transaction* trans)
{
    g_return_val_if_fail (trans, "");
    return get_string (inst (trans), {SE, "reason"});
}

static std::string
trimmed (const std::string& text)
{
    auto copy {g_strdup (text.c_str ())};
    std::string result {g_strstrip (copy)};
    g_free (copy);
    return result;
}

/* A preliminary copy of a posted transaction, dated @a date and linked
 * to it through the slot @a link. With @a negate the copy takes the
 * original back. */
static Transaction*
copy_for_correction (const Transaction* original, const GDate& date,
                     time64 entered, bool negate, const char* link,
                     const std::string& description)
{
    auto copy {xaccTransCloneNoKvp (original)};
    g_return_val_if_fail (copy, nullptr);

    xaccTransBeginEdit (copy);
    for (auto node = xaccTransGetSplitList (copy); node; node = node->next)
    {
        auto split {GNC_SPLIT (node->data)};
        if (negate)
        {
            xaccSplitSetAmount (split, gnc_numeric_neg (xaccSplitGetAmount (split)));
            xaccSplitSetValue (split, gnc_numeric_neg (xaccSplitGetValue (split)));
        }
        xaccSplitSetReconcile (split, NREC);
    }
    xaccTransSetDatePostedGDate (copy, date);
    xaccTransSetDateEnteredSecs (copy, entered);
    xaccTransSetNum (copy, "");
    xaccTransSetDescription (copy, description.c_str ());
    set_string (inst (copy), {SE, "series"},
                get_string (inst (original), {SE, "series"}));
    set_guid (inst (copy), {SE, link}, xaccTransGetGUID (original));
    xaccTransCommitEdit (copy);
    return copy;
}

GncSeCorrection
gnc_se_correct (Transaction* original, const GDate& date,
                const std::string& signature, const std::string& reason,
                bool with_replacement)
{
    auto fail = [] (const char* error)
    {
        return GncSeCorrection {false, error, nullptr, nullptr};
    };
    g_return_val_if_fail (original, fail ("No transaction."));

    if (!gnc_se_trans_is_locked (original))
        return fail (_("Only a posted transaction is corrected with a "
                       "correcting entry. A preliminary transaction can be "
                       "changed as it is."));
    if (!g_date_valid (&date))
        return fail (_("The date is not valid."));
    if (gnc_se_date_is_locked (xaccTransGetBook (original), date))
        return fail (_("A correcting entry is dated in an open period."));
    auto who {trimmed (signature)};
    if (who.empty ())
        return fail (_("A correcting entry says who made it."));
    if (gnc_se_trans_get_correction (original))
        return fail (_("The transaction already has a correcting entry."));

    std::string old_description {view (xaccTransGetDescription (original))};
    /* Translators: a voucher number such as "A 12", then the
     * description of the transaction that is corrected. */
    auto text {g_strdup_printf (_("Correction of %s: %s"),
                                gnc_se_trans_get_voucher (original).c_str (),
                                old_description.c_str ())};
    std::string description {text};
    g_free (text);

    auto now {gnc_time (nullptr)};
    auto correction {copy_for_correction (original, date, now, true, "corrects",
                                          description)};
    if (!correction)
        return fail (_("The correcting entry could not be created."));
    xaccTransBeginEdit (correction);
    set_string (inst (correction), {SE, "signature"}, who);
    set_string (inst (correction), {SE, "reason"}, trimmed (reason));
    xaccTransCommitEdit (correction);

    /* The original points to its correcting entry. "reversed-by" is
     * the inherited slot, which the register uses to jump to it. */
    xaccTransBeginEdit (original);
    set_guid (inst (original), {SE, "corrected-by"},
              xaccTransGetGUID (correction));
    set_guid (inst (original), {"reversed-by"}, xaccTransGetGUID (correction));
    xaccTransCommitEdit (original);
    if (gnc_se_trans_get_correction (original) != correction)
    {
        xaccTransDestroy (correction);
        return fail (_("The correcting entry could not be created."));
    }

    {
        char date_text[MAX_DATE_LENGTH + 1] = "";
        qof_print_gdate (date_text, sizeof date_text, &date);
        auto event {text_printf (_("A correcting entry dated %s was created "
                                   "for %s (%s)."),
                                 date_text,
                                 gnc_se_trans_get_voucher (original).c_str (),
                                 old_description.c_str ())};
        if (auto why {trimmed (reason)}; !why.empty ())
            event += " " + text_printf (_("Reason: %s"), why.c_str ());
        history_append (xaccTransGetBook (original), "correction", who, event);
    }

    Transaction* replacement {nullptr};
    if (with_replacement)
        /* Entered a second later, so that the correcting entry gets the
         * lower number when the two are posted together. */
        replacement = copy_for_correction (original, date, now + 1, false,
                                           "replaces", old_description);
    return {true, "", correction, replacement};
}

/* When a correcting entry has been posted, says so in the notes of the
 * transaction it corrects, where the register and the reports show it. */
static void
note_correction (const Transaction* correction)
{
    auto original {gnc_se_trans_get_corrected (correction)};
    if (!original)
        return;

    char date[MAX_DATE_LENGTH + 1] = "";
    auto posted {xaccTransGetDatePostedGDate (correction)};
    qof_print_gdate (date, sizeof date, &posted);
    /* Translators: a voucher number such as "A 15", a date, and the
     * signature of who made the correction. */
    auto line {g_strdup_printf (_("Corrected by %s, dated %s, signed %s."),
                                gnc_se_trans_get_voucher (correction).c_str (),
                                date,
                                gnc_se_trans_get_signature (correction).c_str ())};
    std::string notes {view (xaccTransGetNotes (original))};
    if (!notes.empty ())
        notes += "\n";
    notes += line;
    g_free (line);
    xaccTransSetNotes (original, notes.c_str ());
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
    set_kvp<Time64> (inst (trans), Time64 {gnc_time (nullptr)},
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
    note_correction (trans);
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
gnc_se_lock_through (QofBook* book, const GDate& date,
                     const std::string& signature)
{
    g_return_val_if_fail (book, lock_error ("No book."));

    /* What has happened to the accounts is recorded before the lock is. */
    gnc_se_history_sync (book);

    std::vector<Transaction*> to_lock;
    std::vector<PeriodKey> keys;
    auto plan {make_plan (book, date, to_lock, keys)};
    if (!plan.ok)
        return {false, 0, plan.error};

    BookEdit edit {book};
    if (gnc_se_book_get_series (book).empty ())
        gnc_se_book_add_series (book, DEFAULT_SERIES, _("Vouchers"),
                                GncSePeriodType::FISCAL_YEAR);

    /* The book gets the fiscal years of what is locked. They are the
     * years the plan worked out, so the periods stay the same. */
    migrate_fiscal_years (book);
    if (!to_lock.empty ())
    {
        auto earliest {date};
        for (auto trans : to_lock)
        {
            auto posted {xaccTransGetDatePostedGDate (trans)};
            if (g_date_compare (&posted, &earliest) < 0)
                earliest = posted;
        }
        ensure_fiscal_year (book, earliest);
    }
    ensure_fiscal_year (book, date);

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

    set_kvp<GDate> (inst (book), date, {SE, "lock-date"});

    char date_text[MAX_DATE_LENGTH + 1] = "";
    qof_print_gdate (date_text, sizeof date_text, &date);
    std::string numbered;
    for (const auto& group : plan.groups)
    {
        PeriodKey key {group.series, group.period};
        auto last {group.first_number + group.count - 1};
        if (!numbered.empty ())
            numbered += "; ";
        /* Series and period, first and last number, and the start of
         * the hash of the last one. */
        numbered += group.series + " " + group.period + ": " +
            std::to_string (group.first_number) + "-" + std::to_string (last) +
            " [" + states[key].head.substr (0, 16) + "]";
    }
    history_append (book, "lock", trimmed (signature),
                    numbered.empty ()
                    ? text_printf (_("The book was posted through %s. No "
                                     "transactions were numbered."), date_text)
                    : text_printf (_("The book was posted through %s. "
                                     "Numbered: %s."),
                                   date_text, numbered.c_str ()));
    return {true, locked, ""};
}

static void
migrate_fiscal_years (QofBook* book)
{
    auto lock_date {gnc_se_book_get_lock_date (book)};
    if (!lock_date || !child_keys (inst (book), {SE, FISCAL_YEARS}).empty ())
        return;

    auto earliest {*lock_date};
    for (auto trans : all_transactions (book))
    {
        if (!gnc_se_trans_is_locked (trans))
            continue;
        auto posted {xaccTransGetDatePostedGDate (trans)};
        if (g_date_valid (&posted) && g_date_compare (&posted, &earliest) < 0)
            earliest = posted;
    }
    ensure_fiscal_year (book, earliest);
    ensure_fiscal_year (book, *lock_date);
}

GncSeLockResult
gnc_se_close_fiscal_year (QofBook* book, const GDate& date,
                          const std::string& signature)
{
    g_return_val_if_fail (book, lock_error ("No book."));
    if (!g_date_valid (&date))
        return lock_error ("%s", _("The date is not valid."));
    auto who {trimmed (signature)};
    if (who.empty ())
        return lock_error ("%s", _("Closing a fiscal year takes a signature."));

    BookEdit edit {book};
    migrate_fiscal_years (book);
    auto year {gnc_se_book_get_fiscal_year (book, date)};
    if (year.closed)
        return lock_error (_("Fiscal year %s is already closed."),
                           year.key.c_str ());
    GDate today;
    gnc_gdate_set_today (&today);
    if (g_date_compare (&year.end, &today) > 0)
        return lock_error (_("Fiscal year %s has not ended yet."),
                           year.key.c_str ());

    ensure_fiscal_year (book, date);
    for (const auto& other : gnc_se_book_get_fiscal_years (book))
        if (!other.closed && g_date_compare (&other.start, &year.start) < 0)
            return lock_error (_("Fiscal year %s is still open. Close it "
                                 "first."), other.key.c_str ());

    int locked = 0;
    auto lock_date {gnc_se_book_get_lock_date (book)};
    if (!lock_date || g_date_compare (&*lock_date, &year.end) < 0)
    {
        auto result {gnc_se_lock_through (book, year.end, who)};
        if (!result.ok)
            return result;
        locked = result.locked;
    }

    set_kvp<Time64> (
        inst (book), Time64 {gnc_time (nullptr)},
        {SE, FISCAL_YEARS, year.key, "closed-at"});
    set_string (inst (book), {SE, FISCAL_YEARS, year.key, "closed-by"}, who);
    history_append (book, "fiscal-year", who,
                    text_printf (_("Fiscal year %s, %s to %s, was closed."),
                                 year.key.c_str (),
                                 shown_date (year.start).c_str (),
                                 shown_date (year.end).c_str ()));
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
/* Processing history                                                 */

static const char* HISTORY = "history";

/* True while the history is being written, so that what the writing
 * itself sets off is not recorded. */
static bool history_busy = false;

static std::string
history_chain_start (const QofBook* book)
{
    std::string text;
    put (text, "gnc-se-history-1");
    put (text, guid_string (qof_book_get_guid (book)));
    return sha256 (text);
}

static std::string
history_chain_next (const std::string& previous, const GncSeEvent& event)
{
    std::string text;
    put (text, "gnc-se-event-1");
    put (text, previous);
    put (text, std::to_string (event.number));
    put (text, std::to_string (event.time));
    put (text, event.type);
    put (text, event.signature);
    put (text, event.text);
    return sha256 (text);
}

static Path
event_path (int64_t number, const char* field)
{
    return {SE, HISTORY, "events", std::to_string (number), field};
}

static bool
history_started (const QofBook* book)
{
    return get_int (inst (book), {SE, HISTORY, "next"}) > 0;
}

/* --- Where the history is kept ---------------------------------------
 *
 * In the book's slots, or in a store that the backend has given the
 * book (design 06). The functions below are the only ones that know. */

static const char* STORE_KEY = "gnc-se-history-store";

static GncSeHistoryStore*
store_of (const QofBook* book)
{
    auto holder {static_cast<std::shared_ptr<GncSeHistoryStore>*> (
        qof_book_get_data (book, STORE_KEY))};
    return holder ? holder->get () : nullptr;
}

static std::optional<GncSeStoredEvent>
slot_event (const QofBook* book, int64_t number)
{
    auto hash {get_string (inst (book), event_path (number, "hash"))};
    if (hash.empty ())
        return std::nullopt;
    auto time {get_kvp<Time64> (inst (book), event_path (number, "time"))};
    return GncSeStoredEvent {
        {number, time ? time->t : 0,
         get_string (inst (book), event_path (number, "type")),
         get_string (inst (book), event_path (number, "signature")),
         get_string (inst (book), event_path (number, "text"))},
        hash};
}

static void
set_slot_event (QofBook* book, const GncSeStoredEvent& stored)
{
    auto number {stored.event.number};
    set_kvp<Time64> (inst (book), Time64 {stored.event.time},
                     event_path (number, "time"));
    set_string (inst (book), event_path (number, "type"), stored.event.type);
    set_string (inst (book), event_path (number, "signature"),
                stored.event.signature);
    set_string (inst (book), event_path (number, "text"), stored.event.text);
    set_string (inst (book), event_path (number, "hash"), stored.hash);
}

static void
delete_slot_event (QofBook* book, int64_t number)
{
    for (auto field : {"time", "type", "signature", "text", "hash"})
        delete_kvp (inst (book), event_path (number, field));
}

/* The event with that number, if it is there. */
static std::optional<GncSeStoredEvent>
history_event (const QofBook* book, int64_t number)
{
    if (auto store {store_of (book)})
        return store->event (number);
    return slot_event (book, number);
}

static Path
watched_path (const std::string& kind, const std::string& key,
              const char* field)
{
    return {SE, HISTORY, kind, key, field};
}

static std::optional<GncSeWatched>
slot_watched (const QofBook* book, const std::string& kind,
              const std::string& key)
{
    auto state {get_string (inst (book), watched_path (kind, key, "state"))};
    if (state.empty ())
        return std::nullopt;
    return GncSeWatched {
        state, get_string (inst (book), watched_path (kind, key, "text"))};
}

static void
set_slot_watched (QofBook* book, const std::string& kind,
                  const std::string& key, const GncSeWatched& watched)
{
    set_string (inst (book), watched_path (kind, key, "state"), watched.state);
    set_string (inst (book), watched_path (kind, key, "text"), watched.text);
}

static void
delete_slot_watched (QofBook* book, const std::string& kind,
                     const std::string& key)
{
    delete_kvp (inst (book), watched_path (kind, key, "state"));
    delete_kvp (inst (book), watched_path (kind, key, "text"));
}

static std::optional<GncSeWatched>
history_watched (const QofBook* book, const std::string& kind,
                 const std::string& key)
{
    if (auto store {store_of (book)})
        return store->watched (kind, key);
    return slot_watched (book, kind, key);
}

static void
history_set_watched (QofBook* book, const std::string& kind,
                     const std::string& key, const GncSeWatched& watched)
{
    if (auto store {store_of (book)})
        store->set_watched (kind, key, watched);
    else
        set_slot_watched (book, kind, key, watched);
}

static void
history_erase_watched (QofBook* book, const std::string& kind,
                       const std::string& key)
{
    if (auto store {store_of (book)})
        store->erase_watched (kind, key);
    else
        delete_slot_watched (book, kind, key);
}

static std::vector<std::string>
history_watched_keys (const QofBook* book, const std::string& kind)
{
    if (auto store {store_of (book)})
        return store->watched_keys (kind);
    return child_keys (inst (book), {SE, HISTORY, kind});
}

static void
write_event (QofBook* book, const char* type, const std::string& signature,
             const std::string& text)
{
    auto number {std::max<int64_t> (get_int (inst (book),
                                             {SE, HISTORY, "next"}), 1)};
    auto previous {get_string (inst (book), {SE, HISTORY, "head"})};
    if (previous.empty ())
        previous = history_chain_start (book);

    GncSeEvent event {number, gnc_time (nullptr), type, signature, text};
    GncSeStoredEvent stored {event, history_chain_next (previous, event)};
    if (auto store {store_of (book)})
        store->append (stored);
    else
        set_slot_event (book, stored);
    /* The position in the chain is always in the book. */
    set_int (inst (book), {SE, HISTORY, "next"}, number + 1);
    set_string (inst (book), {SE, HISTORY, "head"}, stored.hash);
}

static std::string
account_name (const Account* account)
{
    std::string code {view (xaccAccountGetCode (account))};
    std::string name {view (xaccAccountGetName (account))};
    return code.empty () ? name : code + " " + name;
}

/* Whether the account is one of the book's own: not the root, and not
 * in the tree of scheduled-transaction templates or in a loose tree. */
static bool
account_is_watched (QofBook* book, const Account* account)
{
    if (gnc_account_is_root (account))
        return false;
    auto top {gnc_account_get_root (const_cast<Account*> (account))};
    return top == gnc_book_get_root_account (book);
}

static GncSeWatched
watched_account (const Account* account)
{
    auto parent {gnc_account_get_parent (account)};
    auto type {xaccAccountGetType (account)};

    std::string state;
    put (state, view (xaccAccountTypeEnumAsString (type)));
    put (state, view (xaccAccountGetCode (account)));
    put (state, view (xaccAccountGetName (account)));
    put (state, parent ? guid_string (xaccAccountGetGUID (parent)) : "");

    auto name {account_name (account)};
    auto text {parent && !gnc_account_is_root (parent)
               /* Translators: an account, its type, and the account it
                * lies under: "1930 Bank (Bank), under 19 Cash". */
               ? text_printf (_("%s (%s), under %s"), name.c_str (),
                              xaccAccountGetTypeStr (type),
                              account_name (parent).c_str ())
               : text_printf ("%s (%s)", name.c_str (),
                              xaccAccountGetTypeStr (type))};
    return {state, text};
}

static GncSeWatched
watched_tax_table (const GncTaxTable* table)
{
    std::vector<std::pair<std::string, std::string>> entries; /* state, text */
    for (auto node = gncTaxTableGetEntries (table); node; node = node->next)
    {
        auto entry {static_cast<GncTaxTableEntry*> (node->data)};
        auto account {gncTaxTableEntryGetAccount (entry)};
        auto percent {gncTaxTableEntryGetType (entry) == GNC_AMT_TYPE_PERCENT};
        auto amount {gncTaxTableEntryGetAmount (entry)};

        std::string state;
        put (state, account ? guid_string (xaccAccountGetGUID (account)) : "");
        put (state, percent ? "percent" : "value");
        put (state, numeric_string (amount));
        auto text {text_printf (percent ? "%s %g %%" : "%s %g",
                                account ? account_name (account).c_str () : "",
                                gnc_numeric_to_double (amount))};
        entries.emplace_back (state, text);
    }
    std::sort (entries.begin (), entries.end ());

    GncSeWatched result;
    std::string name {view (gncTaxTableGetName (table))};
    put (result.state, name);
    result.text = name + ":";
    for (const auto& [state, text] : entries)
    {
        put (result.state, state);
        result.text += " " + text + ";";
    }
    return result;
}

/* Compares one account or tax table with what the history knows about
 * it, records the difference and remembers the new state. With @a log
 * false the state is only remembered. */
static void
sync_watched (QofBook* book, const char* kind, const GncGUID* guid,
              const GncSeWatched* now, bool log, const char* added,
              const char* changed, const char* removed)
{
    auto key {guid_string (guid)};
    auto old {history_watched (book, kind, key)};

    if (!now)
    {
        if (!old)
            return;
        history_erase_watched (book, kind, key);
        if (log)
            write_event (book, kind, "", text_printf (removed, old->text.c_str ()));
        return;
    }
    if (old && now->state == old->state)
        return;
    history_set_watched (book, kind, key, *now);
    if (!log)
        return;
    write_event (book, kind, "",
                 !old
                 ? text_printf (added, now->text.c_str ())
                 : text_printf (changed, old->text.c_str (), now->text.c_str ()));
}

static void
sync_account (QofBook* book, const Account* account, bool gone, bool log)
{
    GncSeWatched now;
    /* An account that is being set up counts once it has a name. */
    bool present {!gone && account_is_watched (book, account) &&
                  !view (xaccAccountGetName (account)).empty ()};
    if (present)
        now = watched_account (account);
    sync_watched (book, "account", xaccAccountGetGUID (account),
                  present ? &now : nullptr, log,
                  _("Account added: %s."),
                  _("Account changed: %s is now %s."),
                  _("Account removed: %s."));
}

/* A tax table that is a copy made for a posted invoice is not one the
 * user changes. */
static void
sync_tax_table (QofBook* book, const GncTaxTable* table, bool gone, bool log)
{
    if (gncTaxTableGetParent (table))
        return;
    /* A table that is being set up counts once it has a name. */
    if (!gone && view (gncTaxTableGetName (table)).empty ())
        return;
    GncSeWatched now;
    if (!gone)
        now = watched_tax_table (table);
    sync_watched (book, "tax-table", qof_instance_get_guid (table),
                  gone ? nullptr : &now, log,
                  _("Tax table added: %s"),
                  _("Tax table changed: %s is now %s"),
                  _("Tax table removed: %s"));
}

static std::string
program_text ()
{
    return std::string {"GnuCash SE "} + gnc_version () + " (" +
        gnc_build_id () + ")";
}

/* Brings what the history knows up to date with the whole book. */
static void
sync_everything (QofBook* book, bool log)
{
    auto program {program_text ()};
    if (get_string (inst (book), {SE, HISTORY, "program"}) != program)
    {
        set_string (inst (book), {SE, HISTORY, "program"}, program);
        if (log)
            write_event (book, "program", "",
                         text_printf (_("The book is now kept with %s."),
                                      program.c_str ()));
    }

    std::vector<std::string> seen;
    gnc_account_foreach_descendant (
        gnc_book_get_root_account (book), [&] (Account* account)
        {
            seen.push_back (guid_string (xaccAccountGetGUID (account)));
            sync_account (book, account, false, log);
        });
    std::sort (seen.begin (), seen.end ());
    for (const auto& key : history_watched_keys (book, "account"))
        if (!std::binary_search (seen.begin (), seen.end (), key))
        {
            GncGUID guid;
            if (string_to_guid (key.c_str (), &guid))
                sync_watched (book, "account", &guid, nullptr, log, "", "",
                              _("Account removed: %s."));
        }

    seen.clear ();
    for (auto node = gncTaxTableGetTables (book); node; node = node->next)
    {
        auto table {static_cast<GncTaxTable*> (node->data)};
        if (gncTaxTableGetParent (table))
            continue;
        seen.push_back (guid_string (qof_instance_get_guid (table)));
        sync_tax_table (book, table, false, log);
    }
    std::sort (seen.begin (), seen.end ());
    for (const auto& key : history_watched_keys (book, "tax-table"))
        if (!std::binary_search (seen.begin (), seen.end (), key))
        {
            GncGUID guid;
            if (string_to_guid (key.c_str (), &guid))
                sync_watched (book, "tax-table", &guid, nullptr, log, "", "",
                              _("Tax table removed: %s"));
        }
}

/* The first entry: from here on changes are recorded. */
static void
history_start (QofBook* book)
{
    sync_everything (book, false);
    auto accounts {history_watched_keys (book, "account").size ()};
    auto tables {history_watched_keys (book, "tax-table").size ()};
    write_event (book, "start", "",
                 text_printf (_("The processing history starts. The book is "
                                "kept with %s and has %d accounts and %d tax "
                                "tables."),
                              program_text ().c_str (),
                              static_cast<int> (accounts),
                              static_cast<int> (tables)));
}

static void
history_append (QofBook* book, const char* type, const std::string& signature,
                const std::string& text)
{
    auto was_busy {history_busy};
    history_busy = true;
    {
        BookEdit edit {book};
        if (!history_started (book))
            history_start (book);
        write_event (book, type, signature, text);
    }
    history_busy = was_busy;
}

void
gnc_se_history_sync (QofBook* book)
{
    g_return_if_fail (book);
    if (history_busy || !gnc_se_book_is_se (book) ||
        qof_book_is_readonly (book) || qof_book_shutting_down (book))
        return;

    history_busy = true;
    {
        BookEdit edit {book};
        if (history_started (book))
            sync_everything (book, true);
        else
            history_start (book);
    }
    history_busy = false;
}

/* Records a change of an account or a tax table as it happens. */
static void
history_event_handler (QofInstance* entity, QofEventId event_type,
                       gpointer handler_data, gpointer event_data)
{
    if (history_busy || !(event_type & (QOF_EVENT_MODIFY | QOF_EVENT_DESTROY)))
        return;
    bool is_account = GNC_IS_ACCOUNT (entity) != 0;
    if (!is_account && !GNC_IS_TAXTABLE (entity))
        return;
    auto book {qof_instance_get_book (entity)};
    if (!book || qof_book_shutting_down (book) || qof_book_is_readonly (book) ||
        qof_session_load_in_progress () || !history_started (book))
        return;

    bool gone {(event_type & QOF_EVENT_DESTROY) != 0};
    history_busy = true;
    {
        BookEdit edit {book};
        if (is_account)
            sync_account (book, GNC_ACCOUNT (entity), gone, true);
        else
            sync_tax_table (book, GNC_TAXTABLE (entity), gone, true);
    }
    history_busy = false;
}

static void
history_watch (bool on)
{
    static gint handler_id = 0;
    if (on && !handler_id)
        handler_id = qof_event_register_handler (history_event_handler, nullptr);
    else if (!on && handler_id)
    {
        qof_event_unregister_handler (handler_id);
        handler_id = 0;
    }
}

std::shared_ptr<GncSeHistoryStore>
gnc_se_book_get_history_store (const QofBook* book)
{
    g_return_val_if_fail (book, nullptr);
    auto holder {static_cast<std::shared_ptr<GncSeHistoryStore>*> (
        qof_book_get_data (book, STORE_KEY))};
    return holder ? *holder : nullptr;
}

/* Replaces the book's store; nothing is moved. */
static void
attach_store (QofBook* book, std::shared_ptr<GncSeHistoryStore> store)
{
    delete static_cast<std::shared_ptr<GncSeHistoryStore>*> (
        qof_book_get_data (book, STORE_KEY));
    qof_book_set_data_fin (
        book, STORE_KEY,
        store ? new std::shared_ptr<GncSeHistoryStore> {std::move (store)}
              : nullptr,
        [] (QofBook*, gpointer, gpointer data)
        {
            delete static_cast<std::shared_ptr<GncSeHistoryStore>*> (data);
        });
}

void
gnc_se_book_set_history_store (QofBook* book,
                               std::shared_ptr<GncSeHistoryStore> store)
{
    g_return_if_fail (book && store);

    /* Move what the slots hold. With nothing there, the book is left
     * untouched: it may be one that is being loaded. */
    auto next {get_int (inst (book), {SE, HISTORY, "next"})};
    std::vector<GncSeStoredEvent> events;
    for (int64_t number = 1; number < next; ++number)
        if (auto stored {slot_event (book, number)})
            events.push_back (*stored);
    auto accounts {child_keys (inst (book), {SE, HISTORY, "account"})};
    auto tables {child_keys (inst (book), {SE, HISTORY, "tax-table"})};
    if (!events.empty () || !accounts.empty () || !tables.empty ())
    {
        /* The book is not saved here: this is called while a backend
         * saves or loads the book, and it writes the book itself. */
        for (const auto& stored : events)
        {
            store->append (stored);
            delete_slot_event (book, stored.event.number);
        }
        for (auto kind : {"account", "tax-table"})
            for (const auto& key :
                 std::string_view {kind} == "account" ? accounts : tables)
            {
                if (auto watched {slot_watched (book, kind, key)})
                    store->set_watched (kind, key, *watched);
                delete_slot_watched (book, kind, key);
            }
        qof_instance_set_dirty (inst (book));
    }
    attach_store (book, std::move (store));
}

void
gnc_se_history_to_book (QofBook* book)
{
    g_return_if_fail (book);
    auto store {gnc_se_book_get_history_store (book)};
    if (!store)
        return;
    /* The book is not saved here; whoever asked for this saves it. */
    for (const auto& stored : store->events ())
        set_slot_event (book, stored);
    for (auto kind : {"account", "tax-table"})
        for (const auto& key : store->watched_keys (kind))
            if (auto watched {store->watched (kind, key)})
                set_slot_watched (book, kind, key, *watched);
    qof_instance_set_dirty (inst (book));
    attach_store (book, nullptr);
}

bool
gnc_se_command_begin (QofBook* book)
{
    g_return_val_if_fail (book, false);
    auto store {store_of (book)};
    return store ? store->begin () : true;
}

bool
gnc_se_command_commit (QofBook* book)
{
    g_return_val_if_fail (book, false);
    auto store {store_of (book)};
    return store ? store->commit () : true;
}

bool
gnc_se_command_rollback (QofBook* book)
{
    g_return_val_if_fail (book, false);
    auto store {store_of (book)};
    return store ? store->rollback () : true;
}

std::vector<GncSeEvent>
gnc_se_book_get_history (const QofBook* book)
{
    std::vector<GncSeEvent> events;
    g_return_val_if_fail (book, events);
    if (auto store {store_of (book)})
    {
        for (const auto& stored : store->events ())
            events.push_back (stored.event);
        return events;
    }
    auto next {get_int (inst (book), {SE, HISTORY, "next"})};
    for (int64_t number = 1; number < next; ++number)
        if (auto stored {slot_event (book, number)})
            events.push_back (stored->event);
    return events;
}

static const char*
issue_code (GncSeIssueType type)
{
    switch (type)
    {
    case GncSeIssueType::GAP: return "gap";
    case GncSeIssueType::DUPLICATE_NUMBER: return "duplicate-number";
    case GncSeIssueType::CHANGED: return "changed";
    case GncSeIssueType::HEAD_MISMATCH: return "head-mismatch";
    case GncSeIssueType::COUNTER_MISMATCH: return "counter-mismatch";
    case GncSeIssueType::UNKNOWN_SERIES: return "unknown-series";
    case GncSeIssueType::MISSING_READ_ONLY: return "missing-read-only";
    case GncSeIssueType::LOCKED_AFTER_LOCK_DATE: return "posted-after-lock-date";
    case GncSeIssueType::UNLOCKED_BEFORE_LOCK_DATE:
        return "preliminary-in-locked-period";
    case GncSeIssueType::CORRECTION_WITHOUT_ORIGINAL:
        return "correction-without-original";
    case GncSeIssueType::HISTORY_CHANGED: return "history-changed";
    case GncSeIssueType::PERIOD_MISMATCH: return "period-mismatch";
    }
    return "unknown";
}

void
gnc_se_history_record_issues (QofBook* book,
                              const std::vector<GncSeIssue>& issues)
{
    g_return_if_fail (book);
    if (!gnc_se_book_is_se (book) || qof_book_is_readonly (book))
        return;

    std::string list;
    for (const auto& found : issues)
    {
        if (!list.empty ())
            list += "; ";
        list += issue_code (found.type);
        if (!found.series.empty ())
            list += " " + found.series + " " + found.period;
        if (found.number > 0)
            list += " " + std::to_string (found.number);
        if (!guid_equal (&found.guid, guid_null ()))
            list += " " + guid_string (&found.guid);
    }
    /* The same findings are recorded once, not every time they are
     * found again. */
    auto digest {issues.empty () ? std::string {} : sha256 (list)};
    if (digest == get_string (inst (book), {SE, HISTORY, "last-check"}))
        return;

    BookEdit edit {book};
    history_append (book, "check", "",
                    issues.empty ()
                    ? std::string {_("The check of the voucher series finds "
                                     "nothing wrong any more.")}
                    : text_printf (_("The check of the voucher series found "
                                     "%d things wrong: %s"),
                                   static_cast<int> (issues.size ()),
                                   list.c_str ()));
    set_string (inst (book), {SE, HISTORY, "last-check"}, digest);
}

static std::string
time_text (time64 time)
{
    if (!time)
        return "-";
    auto text {gnc_print_time64 (time, "%Y-%m-%d %H:%M:%S")};
    std::string result {text ? text : ""};
    g_free (text);
    return result;
}

std::string
gnc_se_history_text (QofBook* book)
{
    g_return_val_if_fail (book, "");
    gnc_se_history_sync (book);

    std::string out;
    out += _("Processing history");
    out += "\n";
    out += text_printf (_("Book %s. Produced %s with %s."),
                        guid_string (qof_book_get_guid (book)).c_str (),
                        time_text (gnc_time (nullptr)).c_str (),
                        program_text ().c_str ());
    out += "\n\n";
    out += _("Entries: number, time, kind, signature, what happened");
    out += "\n";
    for (const auto& event : gnc_se_book_get_history (book))
        out += std::to_string (event.number) + "\t" + time_text (event.time) +
            "\t" + event.type + "\t" +
            (event.signature.empty () ? "-" : event.signature) + "\t" +
            event.text + "\n";

    out += "\n";
    out += _("Fiscal years: key, first day, last day, closed");
    out += "\n";
    for (const auto& year : gnc_se_book_get_fiscal_years (book))
        out += year.key + "\t" + shown_date (year.start) + "\t" +
            shown_date (year.end) + "\t" +
            (year.closed ? time_text (year.closed_at) + " " + year.closed_by
                         : std::string {"-"}) + "\n";

    std::map<PeriodKey, std::vector<Transaction*>> groups;
    for (auto trans : all_transactions (book))
        if (gnc_se_trans_is_locked (trans))
            groups[{get_string (inst (trans), {SE, "series"}),
                    gnc_se_trans_get_period (trans)}].push_back (trans);

    out += "\n";
    out += _("Posted transactions: voucher, date, entered, posted, "
             "description, correction");
    out += "\n";
    for (auto& [key, transactions] : groups)
    {
        std::sort (transactions.begin (), transactions.end (),
                   [] (const Transaction* a, const Transaction* b)
                   {
                       return gnc_se_trans_get_number (a) <
                           gnc_se_trans_get_number (b);
                   });
        out += text_printf (_("Series %s, period %s"), key.first.c_str (),
                            key.second.c_str ());
        out += "\n";
        for (auto trans : transactions)
        {
            char date[MAX_DATE_LENGTH + 1] = "";
            auto posted {xaccTransGetDatePostedGDate (trans)};
            qof_print_gdate (date, sizeof date, &posted);
            auto locked_at {get_kvp<Time64> (
                    inst (trans), {SE, "locked-at"})};

            std::string correction;
            if (auto original {gnc_se_trans_get_corrected (trans)})
            {
                correction = text_printf (
                    _("corrects %s, signed %s"),
                    gnc_se_trans_get_voucher (original).c_str (),
                    gnc_se_trans_get_signature (trans).c_str ());
                if (auto why {gnc_se_trans_get_reason (trans)}; !why.empty ())
                    correction += ": " + why;
            }
            if (auto by {gnc_se_trans_get_correction (trans)})
            {
                if (!correction.empty ())
                    correction += "; ";
                auto voucher {gnc_se_trans_get_voucher (by)};
                correction += voucher.empty ()
                    ? std::string {_("has a correcting entry that is not "
                                     "posted yet")}
                    : text_printf (_("corrected by %s"), voucher.c_str ());
            }

            out += gnc_se_trans_get_voucher (trans) + "\t" + date + "\t" +
                time_text (xaccTransGetDateEntered (trans)) + "\t" +
                time_text (locked_at ? locked_at->t : 0) + "\t" +
                std::string {view (xaccTransGetDescription (trans))} + "\t" +
                correction + "\n";
        }
    }
    return out;
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
    auto fiscal_years {gnc_se_book_get_fiscal_years (book)};

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
            /* Numbered in the fiscal year that the date lies in. */
            auto series {find_series (book, key.first)};
            if (!fiscal_years.empty () && series &&
                series->period_type == GncSePeriodType::FISCAL_YEAR)
            {
                auto year {std::find_if (fiscal_years.begin (),
                                         fiscal_years.end (),
                                         [&] (const GncSeFiscalYear& candidate)
                                         {
                                             return year_contains (candidate,
                                                                   posted);
                                         })};
                if (year == fiscal_years.end () || year->key != key.second)
                    issues.push_back (issue (GncSeIssueType::PERIOD_MISMATCH,
                                             key, number, 0, trans));
            }
            if (get_guid (inst (trans), {SE, "corrects"}))
            {
                auto original {gnc_se_trans_get_corrected (trans)};
                if (!original || !gnc_se_trans_is_locked (original))
                    issues.push_back (
                        issue (GncSeIssueType::CORRECTION_WITHOUT_ORIGINAL, key,
                               number, 0, trans));
            }
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
    /* The processing history. */
    auto next_event {get_int (inst (book), {SE, HISTORY, "next"})};
    auto previous_hash {history_chain_start (book)};
    bool previous_hash_known = true;
    for (int64_t number = 1; number < next_event; ++number)
    {
        auto stored {history_event (book, number)};
        if (!stored ||
            (previous_hash_known &&
             history_chain_next (previous_hash, stored->event) != stored->hash))
            issues.push_back (issue (GncSeIssueType::HISTORY_CHANGED, {}, number,
                                     0, nullptr));
        previous_hash_known = stored.has_value ();
        previous_hash = stored ? stored->hash : "";
    }
    if (next_event > 1 && previous_hash_known &&
        get_string (inst (book), {SE, HISTORY, "head"}) != previous_hash)
        issues.push_back (issue (GncSeIssueType::HISTORY_CHANGED, {},
                                 next_event - 1, 0, nullptr));
    return issues;
}
