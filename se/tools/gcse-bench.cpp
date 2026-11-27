// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Isak Adamssvärd

/** @file gcse-bench.cpp
 *  @brief Generate a book of a given size and measure what the engine
 *  costs with it: load time, memory, the time of single writes and of
 *  the reads that the reports need.
 *
 *  The numbers decide whether the engine stays behind the API (design
 *  05). The same program is run against XML, SQLite and PostgreSQL.
 *
 *      gcse-bench generate URI [--accounts=N] [--years=N] [--per-year=N]
 *                               [--no-lock]
 *      gcse-bench measure URI [--ops=N] [--load-only]
 *      gcse-bench selftest
 *
 *  Run from the build tree with GNC_UNINSTALLED=YES and
 *  GNC_BUILDDIR=<build directory>, so that the engine finds its backends.
 */

#include <config.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <gnc-se.hpp>

#include <Account.h>
#include <Account.hpp>
#include <Split.h>
#include <Transaction.h>
#include <TransLog.h>
#include <gnc-commodity.h>
#include <gnc-date.h>
#include <gnc-engine.h>
#include <qof.h>
#include <qofbook.h>
#include <qofsession.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

double
seconds_since (Clock::time_point start)
{
    return std::chrono::duration<double> (Clock::now () - start).count ();
}

/* Time one call, in milliseconds. */
double
time_ms (const std::function<void ()>& work)
{
    auto start {Clock::now ()};
    work ();
    return seconds_since (start) * 1000.0;
}

struct Options
{
    std::string command;
    std::string uri;
    int accounts = 1300;   /* About the size of a BAS chart of accounts. */
    int years = 5;
    int per_year = 10000;  /* Transactions per fiscal year. */
    int ops = 50;          /* Repetitions of each timed write. */
    bool lock = true;      /* Lock each month as GnuCash SE would. */
    bool load_only = false;
};

GDate
make_date (int year, int month, int day)
{
    GDate date;
    g_date_clear (&date, 1);
    g_date_set_dmy (&date, static_cast<GDateDay> (day),
                    static_cast<GDateMonth> (month),
                    static_cast<GDateYear> (year));
    return date;
}

int
this_year ()
{
    GDate today;
    g_date_clear (&today, 1);
    g_date_set_time_t (&today, time (nullptr));
    return g_date_get_year (&today);
}

/* The process's memory in megabytes, as the kernel reports it: what it
 * holds now and the most it has held. Zero where /proc is missing. */
struct Memory
{
    double now_mb = 0;
    double peak_mb = 0;
};

Memory
memory ()
{
    Memory result;
    std::ifstream status {"/proc/self/status"};
    std::string line;
    while (std::getline (status, line))
    {
        long kb = 0;
        if (std::sscanf (line.c_str (), "VmRSS: %ld kB", &kb) == 1)
            result.now_mb = static_cast<double> (kb) / 1024.0;
        else if (std::sscanf (line.c_str (), "VmHWM: %ld kB", &kb) == 1)
            result.peak_mb = static_cast<double> (kb) / 1024.0;
    }
    return result;
}

bool
session_ok (QofSession* session, const char* what)
{
    if (qof_session_get_error (session) == ERR_BACKEND_NO_ERR)
        return true;
    auto message {qof_session_get_error_message (session)};
    std::fprintf (stderr, "gcse-bench: %s failed: error %d %s\n", what,
                  static_cast<int> (qof_session_get_error (session)),
                  message ? message : "");
    return false;
}

void
start_engine ()
{
    qof_init ();
    gnc_engine_init (0, nullptr);
    xaccLogDisable (); /* No transaction log beside the book. */
}

/* --- generate --------------------------------------------------------- */

GNCAccountType
type_for_code (int code)
{
    switch (code / 1000)
    {
    case 1:
        return ACCT_TYPE_ASSET;
    case 2:
        return ACCT_TYPE_LIABILITY;
    case 3:
        return ACCT_TYPE_INCOME;
    default:
        return ACCT_TYPE_EXPENSE;
    }
}

/* A flat chart of @a count accounts with four-digit numbers spread over
 * 1000-8999. The names are made up: no data from BAS. */
std::vector<Account*>
make_accounts (QofBook* book, gnc_commodity* currency, int count)
{
    std::vector<Account*> accounts;
    auto root {gnc_account_create_root (book)};
    for (int i = 0; i < count; ++i)
    {
        auto code {1000 + static_cast<int> (8000LL * i / count)};
        auto code_text {std::to_string (code)};
        auto name {"Konto " + code_text};
        auto account {xaccMallocAccount (book)};
        xaccAccountBeginEdit (account);
        xaccAccountSetCode (account, code_text.c_str ());
        xaccAccountSetName (account, name.c_str ());
        xaccAccountSetType (account, type_for_code (code));
        xaccAccountSetCommodity (account, currency);
        gnc_account_append_child (root, account);
        xaccAccountCommitEdit (account);
        accounts.push_back (account);
    }
    return accounts;
}

/* A balanced transaction with two to four splits on random accounts. */
Transaction*
add_transaction (QofBook* book, gnc_commodity* currency,
                 const std::vector<Account*>& accounts, std::mt19937_64& random,
                 const GDate& posted, time64 entered, const std::string& text)
{
    std::uniform_int_distribution<size_t> pick_account {0, accounts.size () - 1};
    std::uniform_int_distribution<int> pick_splits {2, 4};
    std::uniform_int_distribution<int64_t> pick_ore {100, 5000000};

    auto trans {xaccMallocTransaction (book)};
    xaccTransBeginEdit (trans);
    xaccTransSetCurrency (trans, currency);
    xaccTransSetDatePostedGDate (trans, posted);
    xaccTransSetDateEnteredSecs (trans, entered);
    xaccTransSetDescription (trans, text.c_str ());
    auto splits {pick_splits (random)};
    int64_t sum = 0;
    for (int i = 0; i < splits; ++i)
    {
        auto ore {i + 1 < splits ? pick_ore (random) : -sum};
        sum += ore;
        auto split {xaccMallocSplit (book)};
        xaccSplitSetParent (split, trans);
        xaccSplitSetAccount (split, accounts[pick_account (random)]);
        xaccSplitSetAmount (split, gnc_numeric_create (ore, 100));
        xaccSplitSetValue (split, gnc_numeric_create (ore, 100));
    }
    xaccTransCommitEdit (trans);
    return trans;
}

int
generate (const Options& options)
{
    start_engine ();
    auto session {qof_session_new (qof_book_new ())};
    auto book {qof_session_get_book (session)};
    auto currency {gnc_commodity_table_lookup (
        gnc_commodity_table_get_table (book), GNC_COMMODITY_NS_CURRENCY, "SEK")};
    if (!currency)
    {
        std::fprintf (stderr, "gcse-bench: no SEK in the commodity table\n");
        return 1;
    }

    auto start {Clock::now ()};
    std::mt19937_64 random {20261127};
    auto accounts {make_accounts (book, currency, options.accounts)};

    /* Whole calendar years, the last one being last year, so that every
     * date is in the past. The last two months are left open: that is
     * where "measure" adds its transactions. */
    auto last_year {this_year () - 1};
    auto first_year {last_year - options.years + 1};
    auto per_month {std::max (1, options.per_year / 12)};
    time64 sequence = 0;
    int locks = 0;
    for (int year = first_year; year <= last_year; ++year)
    {
        auto defined {gnc_se_book_define_fiscal_year (
            book, make_date (year, 1, 1), make_date (year, 12, 31))};
        if (!defined.ok)
        {
            std::fprintf (stderr, "gcse-bench: fiscal year %d: %s\n", year,
                          defined.error.c_str ());
            return 1;
        }
        for (int month = 1; month <= 12; ++month)
        {
            auto days {static_cast<int> (g_date_get_days_in_month (
                static_cast<GDateMonth> (month),
                static_cast<GDateYear> (year)))};
            std::uniform_int_distribution<int> pick_day {1, days};
            for (int i = 0; i < per_month; ++i)
            {
                auto day {pick_day (random)};
                /* Entered at noon on the day it is dated, plus a running
                 * second so that the order of entry is unambiguous. */
                auto entered {gnc_dmy2time64_neutral (day, month, year) +
                              (++sequence % 43000)};
                add_transaction (book, currency, accounts, random,
                                 make_date (year, month, day), entered,
                                 "Verifikation " + std::to_string (sequence));
            }
            if (!options.lock || (year == last_year && month > 10))
                continue;
            auto locked {gnc_se_lock_through (
                book, make_date (year, month, days), "bench")};
            if (!locked.ok)
            {
                std::fprintf (stderr, "gcse-bench: lock %d-%02d: %s\n", year,
                              month, locked.error.c_str ());
                return 1;
            }
            ++locks;
        }
        if (options.lock && year < last_year)
        {
            auto closed {gnc_se_close_fiscal_year (
                book, make_date (year, 12, 31), "bench")};
            if (!closed.ok)
            {
                std::fprintf (stderr, "gcse-bench: close %d: %s\n", year,
                              closed.error.c_str ());
                return 1;
            }
        }
    }
    auto built {seconds_since (start)};

    start = Clock::now ();
    qof_session_begin (session, options.uri.c_str (), SESSION_NEW_OVERWRITE);
    if (!session_ok (session, "creating the store"))
        return 1;
    qof_session_save (session, nullptr);
    if (!session_ok (session, "saving"))
        return 1;
    auto saved {seconds_since (start)};

    auto transactions {qof_collection_count (
        qof_book_get_collection (book, GNC_ID_TRANS))};
    auto splits {
        qof_collection_count (qof_book_get_collection (book, GNC_ID_SPLIT))};
    std::printf ("generated: %d accounts, %u transactions, %u splits, "
                 "%d fiscal years, %d locks, %zu history events\n",
                 options.accounts, transactions, splits, options.years, locks,
                 gnc_se_book_get_history (book).size ());
    std::printf ("built in memory in %.1f s, saved in %.1f s, peak memory "
                 "%.0f MB\n",
                 built, saved, memory ().peak_mb);

    qof_session_end (session);
    qof_session_destroy (session);
    return 0;
}

/* --- measure ---------------------------------------------------------- */

struct Timing
{
    std::string name;
    std::vector<double> ms;
};

void
print_timing (const Timing& timing)
{
    auto sorted {timing.ms};
    std::sort (sorted.begin (), sorted.end ());
    if (sorted.empty ())
        return;
    auto at {[&sorted] (double share) {
        auto index {static_cast<size_t> (
            share * static_cast<double> (sorted.size () - 1) + 0.5)};
        return sorted[std::min (index, sorted.size () - 1)];
    }};
    std::printf ("| %-34s | %4zu | %10.2f | %10.2f | %10.2f |\n",
                 timing.name.c_str (), sorted.size (), at (0.5), at (0.95),
                 sorted.back ());
}

struct VoucherRow
{
    std::string series;
    std::string period;
    int64_t number;
};

int
measure (const Options& options)
{
    start_engine ();
    auto before {memory ()};

    auto start {Clock::now ()};
    auto session {qof_session_new (qof_book_new ())};
    qof_session_begin (session, options.uri.c_str (), SESSION_NORMAL_OPEN);
    if (!session_ok (session, "opening"))
        return 1;
    qof_session_load (session, nullptr);
    if (!session_ok (session, "loading"))
        return 1;
    auto loaded {seconds_since (start)};
    auto after {memory ()};

    auto book {qof_session_get_book (session)};
    auto root {gnc_book_get_root_account (book)};
    std::vector<Account*> accounts;
    auto descendants {gnc_account_get_descendants (root)};
    for (auto node = descendants; node; node = node->next)
        accounts.push_back (static_cast<Account*> (node->data));
    g_list_free (descendants);
    if (accounts.size () < 2)
    {
        std::fprintf (stderr, "gcse-bench: the book has no accounts\n");
        return 1;
    }
    auto currency {xaccAccountGetCommodity (accounts.front ())};
    auto lock_date {gnc_se_book_get_lock_date (book)};
    if (!lock_date && !options.load_only)
    {
        std::fprintf (stderr, "gcse-bench: the book has no lock date; "
                              "make it with \"generate\"\n");
        return 1;
    }

    std::printf ("book: %zu accounts, %u transactions, %u splits, "
                 "%zu history events\n",
                 accounts.size (),
                 qof_collection_count (
                     qof_book_get_collection (book, GNC_ID_TRANS)),
                 qof_collection_count (
                     qof_book_get_collection (book, GNC_ID_SPLIT)),
                 gnc_se_book_get_history (book).size ());
    std::printf ("load: %.2f s; memory %.0f MB after load (%.0f MB before), "
                 "peak %.0f MB\n\n",
                 loaded, after.now_mb, before.now_mb, after.peak_mb);
    if (options.load_only)
    {
        qof_session_end (session);
        qof_session_destroy (session);
        return 0;
    }

    std::mt19937_64 random {4711};
    std::vector<Timing> timings;

    /* New transactions in the open period, the day after the lock date
     * and four weeks on. */
    Timing add {"new transaction", {}};
    std::vector<Transaction*> added;
    auto entered {gnc_time (nullptr)};
    for (int i = 0; i < options.ops; ++i)
    {
        auto posted {*lock_date};
        g_date_add_days (&posted, 1 + static_cast<guint> (i % 28));
        add.ms.push_back (time_ms ([&] {
            added.push_back (add_transaction (
                book, currency, accounts, random, posted, entered + i,
                "Mätning " + std::to_string (i)));
        }));
    }
    timings.push_back (add);

    Timing edit {"change a transaction's text", {}};
    for (size_t i = 0; i < added.size (); ++i)
    {
        auto text {"Mätning, ändrad " + std::to_string (i)};
        edit.ms.push_back (time_ms ([&] {
            xaccTransBeginEdit (added[i]);
            xaccTransSetDescription (added[i], text.c_str ());
            xaccTransCommitEdit (added[i]);
        }));
    }
    timings.push_back (edit);

    /* Changes to the book itself: with the SQL backend each one writes
     * all of the book's key-value pairs again. */
    Timing counter {"next invoice number (book counter)", {}};
    for (int i = 0; i < options.ops; ++i)
        counter.ms.push_back (time_ms ([&] {
            g_free (qof_book_increment_and_format_counter (book, "gncInvoice"));
        }));
    timings.push_back (counter);

    Timing rename {"rename an account (history event)", {}};
    for (int i = 0; i < options.ops; ++i)
    {
        auto account {accounts[static_cast<size_t> (i) % accounts.size ()]};
        auto name {std::string {xaccAccountGetName (account)} + " *"};
        rename.ms.push_back (time_ms ([&] {
            xaccAccountBeginEdit (account);
            xaccAccountSetName (account, name.c_str ());
            xaccAccountCommitEdit (account);
        }));
    }
    timings.push_back (rename);

    Timing lock {"lock four weeks (" + std::to_string (added.size ()) +
                     " transactions)",
                 {}};
    {
        auto through {*lock_date};
        g_date_add_days (&through, 28);
        GncSeLockResult result {};
        lock.ms.push_back (time_ms (
            [&] { result = gnc_se_lock_through (book, through, "bench"); }));
        if (!result.ok)
            std::fprintf (stderr, "gcse-bench: lock: %s\n",
                          result.error.c_str ());
    }
    timings.push_back (lock);

    /* The reads behind the reports, for the latest fiscal year in the
     * book: the one the lock date is in. */
    auto year {gnc_se_book_get_fiscal_year (book, *lock_date)};
    auto from {gnc_time64_get_day_start_gdate (&year.start)};
    auto to {gnc_time64_get_day_end_gdate (&year.end)};
    const int reads = 5;

    Timing balances {"balance of every account", {}};
    for (int i = 0; i < reads; ++i)
        balances.ms.push_back (time_ms ([&] {
            for (auto account : accounts)
                (void)xaccAccountGetBalanceAsOfDate (account, to);
        }));
    timings.push_back (balances);

    Timing ledger {"general ledger, one fiscal year", {}};
    size_t ledger_rows = 0;
    for (int i = 0; i < reads; ++i)
        ledger.ms.push_back (time_ms ([&] {
            ledger_rows = 0;
            for (auto account : accounts)
            {
                auto running {gnc_numeric_zero ()};
                for (auto split : xaccAccountGetSplits (account))
                {
                    auto posted {
                        xaccTransGetDate (xaccSplitGetParent (split))};
                    if (posted < from || posted > to)
                        continue;
                    running = gnc_numeric_add_fixed (
                        running, xaccSplitGetValue (split));
                    ++ledger_rows;
                }
            }
        }));
    timings.push_back (ledger);

    Timing vouchers {"voucher list, one fiscal year", {}};
    size_t voucher_rows = 0;
    for (int i = 0; i < reads; ++i)
        vouchers.ms.push_back (time_ms ([&] {
            struct Collect
            {
                time64 from;
                time64 to;
                std::vector<VoucherRow> rows;
            } collect {from, to, {}};
            qof_collection_foreach (
                qof_book_get_collection (book, GNC_ID_TRANS),
                [] (QofInstance* instance, gpointer data) {
                    auto into {static_cast<Collect*> (data)};
                    auto trans {GNC_TRANSACTION (instance)};
                    auto posted {xaccTransGetDate (trans)};
                    if (posted < into->from || posted > into->to)
                        return;
                    into->rows.push_back ({gnc_se_trans_get_series (trans),
                                           gnc_se_trans_get_period (trans),
                                           gnc_se_trans_get_number (trans)});
                },
                &collect);
            std::sort (collect.rows.begin (), collect.rows.end (),
                       [] (const VoucherRow& a, const VoucherRow& b) {
                           if (a.series != b.series)
                               return a.series < b.series;
                           if (a.period != b.period)
                               return a.period < b.period;
                           return a.number < b.number;
                       });
            voucher_rows = collect.rows.size ();
        }));
    timings.push_back (vouchers);

    Timing verify {"verify series and history", {}};
    size_t issues = 0;
    verify.ms.push_back (
        time_ms ([&] { issues = gnc_se_verify (book).size (); }));
    timings.push_back (verify);

    Timing save {"save (writes the whole file for XML)", {}};
    save.ms.push_back (time_ms ([&] { qof_session_save (session, nullptr); }));
    if (!session_ok (session, "saving"))
        return 1;
    timings.push_back (save);

    std::printf ("| %-34s | %4s | %10s | %10s | %10s |\n", "operation", "n",
                 "median ms", "p95 ms", "max ms");
    std::printf ("|---|---|---|---|---|\n");
    for (const auto& timing : timings)
        print_timing (timing);
    std::printf ("\nledger rows %zu, voucher rows %zu, issues from verify %zu, "
                 "memory now %.0f MB, peak %.0f MB\n",
                 ledger_rows, voucher_rows, issues, memory ().now_mb,
                 memory ().peak_mb);

    qof_session_end (session);
    qof_session_destroy (session);
    return issues == 0 ? 0 : 1;
}

/* --- selftest --------------------------------------------------------- */

/* A small book in SQLite in a temporary directory: generated in a child
 * process and measured in another, as the two commands are used. */
int
selftest (const char* program)
{
    auto dir {g_dir_make_tmp ("gcse-bench-XXXXXX", nullptr)};
    if (!dir)
        return 1;
    auto path {g_build_filename (dir, "book.gnucash", nullptr)};
    auto uri {std::string {"sqlite3://"} + path};

    int status = 1;
    auto run {[&] (std::vector<std::string> arguments) {
        std::vector<char*> argv;
        argv.push_back (const_cast<char*> (program));
        for (auto& argument : arguments)
            argv.push_back (argument.data ());
        argv.push_back (nullptr);
        int wait_status = 0;
        GError* error = nullptr;
        if (!g_spawn_sync (nullptr, argv.data (), nullptr,
                           G_SPAWN_CHILD_INHERITS_STDIN, nullptr, nullptr,
                           nullptr, nullptr, &wait_status, &error))
        {
            std::fprintf (stderr, "gcse-bench: %s\n", error->message);
            g_error_free (error);
            return false;
        }
        return g_spawn_check_wait_status (wait_status, nullptr) != FALSE;
    }};
    if (run ({"generate", uri, "--accounts=40", "--years=2",
              "--per-year=240"}) &&
        run ({"measure", uri, "--ops=5"}))
        status = 0;

    g_unlink (path);
    g_rmdir (dir);
    g_free (path);
    g_free (dir);
    return status;
}

bool
parse_number (const char* argument, const char* name, int& value)
{
    auto length {std::strlen (name)};
    if (std::strncmp (argument, name, length) != 0 || argument[length] != '=')
        return false;
    value = std::atoi (argument + length + 1);
    return value > 0;
}

int
usage ()
{
    std::fprintf (
        stderr,
        "usage: gcse-bench generate URI [--accounts=N] [--years=N] "
        "[--per-year=N] [--no-lock]\n"
        "       gcse-bench measure URI [--ops=N] [--load-only]\n"
        "       gcse-bench selftest\n"
        "URI is e.g. xml:///tmp/b.gnucash, sqlite3:///tmp/b.gnucash or\n"
        "postgres://user:password@host/database. \"generate\" replaces what\n"
        "is there; \"measure\" adds transactions to the book and locks them.\n"
        "--no-lock makes a book as plain GnuCash would, without locks, series\n"
        "and history; such a book can only be measured with --load-only.\n");
    return 2;
}

} // namespace

int
main (int argc, char** argv)
{
    if (argc < 2)
        return usage ();
    Options options;
    options.command = argv[1];
    if (options.command == "selftest")
        return selftest (argv[0]);
    if (argc < 3)
        return usage ();
    options.uri = argv[2];
    for (int i = 3; i < argc; ++i)
        if (std::strcmp (argv[i], "--no-lock") == 0)
            options.lock = false;
        else if (std::strcmp (argv[i], "--load-only") == 0)
            options.load_only = true;
        else if (!parse_number (argv[i], "--accounts", options.accounts) &&
            !parse_number (argv[i], "--years", options.years) &&
            !parse_number (argv[i], "--per-year", options.per_year) &&
            !parse_number (argv[i], "--ops", options.ops))
            return usage ();
    if (options.command == "generate")
        return generate (options);
    if (options.command == "measure")
        return measure (options);
    return usage ();
}
