// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Isak Adamssvärd

/** @file test-gcse-core.cpp
 *  @brief The command layer, through gcse::Company alone: every command
 *  with a request it takes and the requests it must refuse.
 */

#include <glib.h>
#include <glib/gstdio.h>

#include <gcse-core.hpp>

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

using namespace gcse;

class CoreTest : public ::testing::TestWithParam<const char*>
{
protected:
    void SetUp () override
    {
        m_dir = g_dir_make_tmp ("gcse-core-XXXXXX", nullptr);
        ASSERT_NE (m_dir, nullptr);
        m_uri = std::string {GetParam ()} + "://" + m_dir + "/book.gnucash";
        if (std::string {GetParam ()} == "postgres")
        {
            /* A new, empty database for each test, in the server that
             * util/se/pg-env has started. */
            m_database = "gcse_" + std::to_string (g_get_monotonic_time ());
            ASSERT_TRUE (psql ("CREATE DATABASE " + m_database + " OWNER gnc"));
            m_uri = std::string {g_getenv ("GCSE_PG_URL")} + "/" + m_database;
        }
        auto created {Company::create (
            m_uri, {"Testbolaget AB", "556000-0000", "SEK", 1, 1})};
        ASSERT_TRUE (created.has_value ()) << created.error ().code;
        m_company = std::move (*created);
        m_company->set_actor ("anna");
        add_account ("1930", "Företagskonto", "bank");
        add_account ("1940", "Eurokonto", "bank", "EUR");
        add_account ("2099", "Årets resultat", "equity");
        add_account ("3001", "Försäljning", "income");
        add_account ("5010", "Lokalhyra", "expense");
        add_account ("8999", "Årets resultat", "expense");
    }

    void TearDown () override
    {
        m_company.reset ();
        if (!m_database.empty ())
        {
            /* Closing the company closes its connections; the server
             * waits some seconds for them before it refuses to drop the
             * database. A connection that stays is a leak. */
            bool dropped {psql ("DROP DATABASE IF EXISTS " + m_database, true)};
            if (!dropped)
            {
                ADD_FAILURE () << "the company left a connection open";
                psql ("DROP DATABASE IF EXISTS " + m_database + " WITH (FORCE)");
            }
        }
        if (!m_dir)
            return;
        auto dir {g_dir_open (m_dir, 0, nullptr)};
        while (auto name = dir ? g_dir_read_name (dir) : nullptr)
        {
            auto file {g_build_filename (m_dir, name, nullptr)};
            g_unlink (file);
            g_free (file);
        }
        if (dir)
            g_dir_close (dir);
        g_rmdir (m_dir);
        g_free (m_dir);
    }

    static bool psql (const std::string& statement, bool quiet = false)
    {
        const char* argv[] {"psql", "-q", "-h", "127.0.0.1", "-U", "postgres",
                            "-c", statement.c_str (), nullptr};
        int status = 1;
        auto flags {static_cast<GSpawnFlags> (
            G_SPAWN_SEARCH_PATH | (quiet ? G_SPAWN_STDERR_TO_DEV_NULL : 0))};
        return g_spawn_sync (nullptr, const_cast<char**> (argv), nullptr, flags,
                             nullptr, nullptr, nullptr, nullptr, &status,
                             nullptr) &&
            g_spawn_check_wait_status (status, nullptr);
    }

    void add_account (const std::string& number, const std::string& name,
                      const std::string& type, const std::string& currency = "")
    {
        auto made {m_company->create_account (
            {number, name, type, currency, "", "", false})};
        ASSERT_TRUE (made.has_value ()) << made.error ().code;
        m_accounts[number] = made->id;
    }

    /* A voucher of @a amount from the account @a credit to @a debit. */
    Result<Voucher> enter (const std::string& date, const std::string& text,
                           const std::string& amount, const std::string& debit,
                           const std::string& credit)
    {
        return m_company->create_voucher (
            {date, text, "",
             {{m_accounts[debit], amount, "", "", "", ""},
              {m_accounts[credit], "-" + amount, "", "", "", ""}}});
    }

    void reopen ()
    {
        m_company.reset ();
        auto opened {Company::open (m_uri)};
        ASSERT_TRUE (opened.has_value ()) << opened.error ().code;
        m_company = std::move (*opened);
        m_company->set_actor ("anna");
    }

    template <typename T> static std::string code (const Result<T>& result)
    {
        return result ? "" : result.error ().code;
    }

    char* m_dir {};
    std::string m_database; /* Only with PostgreSQL. */
    std::string m_uri;
    std::unique_ptr<Company> m_company;
    std::map<std::string, std::string> m_accounts;
};

TEST_P (CoreTest, a_company_is_created_and_opened_again)
{
    auto info {m_company->info ()};
    EXPECT_EQ (info.name, "Testbolaget AB");
    EXPECT_EQ (info.organisation_number, "556000-0000");
    EXPECT_EQ (info.currency, "SEK");
    EXPECT_EQ (info.lock_date, "");
    EXPECT_EQ (info.fiscal_year_start_month, 1);
    EXPECT_FALSE (info.id.empty ());

    /* One company per process. */
    EXPECT_EQ (code (Company::open (m_uri)), "process.busy");
    EXPECT_EQ (code (Company::create (m_uri, {"X", "", "SEK", 1, 1})),
               "process.busy");

    ASSERT_NO_FATAL_FAILURE (reopen ());
    EXPECT_EQ (m_company->info ().name, "Testbolaget AB");
    EXPECT_EQ (m_company->info ().id, info.id);
    EXPECT_EQ (m_company->accounts ().size (), 6u);
    EXPECT_TRUE (m_company->verify ().empty ());

    /* The settings that made the company are in its history, and there
     * is already a book where it is. */
    EXPECT_FALSE (m_company->history ().items.empty ());
    m_company.reset ();
    EXPECT_EQ (code (Company::create (m_uri, {"Annat AB", "", "SEK", 1, 1})),
               "storage.exists");
    auto other {"sqlite3://" + std::string {m_dir} + "/other.gnucash"};
    EXPECT_EQ (code (Company::create (other, {"", "", "SEK", 1, 1})),
               "company.name-missing");
    EXPECT_EQ (code (Company::create (other, {"X", "", "XXQ", 1, 1})),
               "currency.unknown");
    EXPECT_EQ (code (Company::create (other, {"X", "", "SEK", 13, 1})),
               "fiscal-year.start-invalid");
    EXPECT_EQ (code (Company::open (other)), "storage.not-found");
    ASSERT_NO_FATAL_FAILURE (reopen ());
}

TEST_P (CoreTest, accounts_are_added_changed_and_deleted)
{
    using Make = NewAccount;
    EXPECT_EQ (code (m_company->create_account (Make {"1930", "Dubblett", "bank"})),
               "account.number-taken");
    EXPECT_EQ (code (m_company->create_account (Make {"1931", "", "bank"})),
               "account.name-missing");
    EXPECT_EQ (code (m_company->create_account (Make {"1931", "X", "stock"})),
               "account.type-unknown");
    EXPECT_EQ (code (m_company->create_account (Make {"1931", "X", "bank", "XXQ"})),
               "currency.unknown");
    EXPECT_EQ (code (m_company->create_account (
                         Make {"1931", "X", "bank", "", std::string (32, '0')})),
               "account.parent-not-found");

    auto heading {m_company->create_account (
        Make {"19", "Kassa och bank", "asset", "", "", "", true})};
    ASSERT_TRUE (heading.has_value ());
    EXPECT_TRUE (heading->placeholder);
    auto savings {m_company->create_account (
        Make {"1950", "Sparkonto", "bank", "", heading->id, "Ränta 2 %"})};
    ASSERT_TRUE (savings.has_value ());
    EXPECT_EQ (savings->parent, heading->id);
    EXPECT_EQ (savings->currency, "SEK");
    EXPECT_EQ (savings->description, "Ränta 2 %");

    AccountChange rename;
    rename.name = "Placeringskonto";
    EXPECT_EQ (code (m_company->update_account (savings->id, rename, "stale")),
               "conflict");
    auto renamed {m_company->update_account (savings->id, rename, savings->etag)};
    ASSERT_TRUE (renamed.has_value ());
    EXPECT_EQ (renamed->name, "Placeringskonto");
    EXPECT_NE (renamed->etag, savings->etag);
    /* The etag it started from is now stale. */
    EXPECT_EQ (code (m_company->update_account (savings->id, rename,
                                                savings->etag)), "conflict");

    AccountChange move;
    move.parent = savings->id;
    auto top {m_company->account (heading->id)};
    ASSERT_TRUE (top.has_value ());
    EXPECT_EQ (code (m_company->update_account (heading->id, move, top->etag)),
               "account.parent-invalid");
    AccountChange number;
    number.number = "1930";
    EXPECT_EQ (code (m_company->update_account (savings->id, number,
                                                renamed->etag)),
               "account.number-taken");

    /* An account with vouchers keeps its type and cannot be deleted. */
    ASSERT_TRUE (enter ("2025-01-10", "Hyra", "100.00", "5010", "1930"));
    auto rent {m_company->account (m_accounts["5010"])};
    ASSERT_TRUE (rent.has_value ());
    AccountChange type;
    type.type = "income";
    EXPECT_EQ (code (m_company->update_account (rent->id, type, rent->etag)),
               "account.in-use");
    EXPECT_EQ (code (m_company->delete_account (rent->id, rent->etag)),
               "account.in-use");
    EXPECT_EQ (code (m_company->delete_account (heading->id, top->etag)),
               "account.has-children");

    EXPECT_EQ (code (m_company->delete_account (savings->id, "stale")),
               "conflict");
    EXPECT_TRUE (m_company->delete_account (savings->id, renamed->etag));
    EXPECT_EQ (code (m_company->account (savings->id)), "account.not-found");
    EXPECT_EQ (m_company->accounts ().size (), 7u);

    /* The changes are in the history, signed by who made them. */
    auto history {m_company->history (1000).items};
    ASSERT_FALSE (history.empty ());
    EXPECT_EQ (history.back ().type, "account");
    EXPECT_EQ (history.back ().signature, "anna");
}

TEST_P (CoreTest, a_voucher_is_checked_before_it_is_entered)
{
    auto bank {m_accounts["1930"]};
    auto sales {m_accounts["3001"]};
    auto make {[&] (std::vector<VoucherLine> lines,
                    std::string date = "2025-01-15", std::string series = "")
    {
        return m_company->create_voucher ({date, "Test", series, lines});
    }};
    using L = VoucherLine;
    EXPECT_EQ (code (make ({L {bank, "100.00"}, L {sales, "-99.00"}})),
               "voucher.unbalanced");
    EXPECT_EQ (code (make ({L {bank, "100.00"}})), "voucher.too-few-lines");
    EXPECT_EQ (code (make ({L {bank, "0"}, L {sales, "0"}})),
               "voucher.zero-line");
    EXPECT_EQ (code (make ({L {bank, "1e2"}, L {sales, "-100"}})),
               "amount.invalid");
    EXPECT_EQ (code (make ({L {bank, "1,50"}, L {sales, "-1.50"}})),
               "amount.invalid");
    EXPECT_EQ (code (make ({L {bank, "1.005"}, L {sales, "-1.005"}})),
               "amount.too-many-decimals");
    EXPECT_EQ (code (make ({L {"nope", "1"}, L {sales, "-1"}})),
               "account.not-found");
    EXPECT_EQ (code (make ({L {bank, "1"}, L {sales, "-1"}}, "2025-02-30")),
               "date.invalid");
    EXPECT_EQ (code (make ({L {bank, "1"}, L {sales, "-1"}}, "20250115")),
               "date.invalid");
    EXPECT_EQ (code (make ({L {bank, "1"}, L {sales, "-1"}}, "2025-01-15", "Q")),
               "voucher.unknown-series");
    EXPECT_EQ (code (make ({L {bank, "1", "1"}, L {sales, "-1"}})),
               "voucher.foreign-amount-unexpected");
    auto heading {m_company->create_account (
        NewAccount {"30", "Intäkter", "income", "", "", "", true})};
    ASSERT_TRUE (heading.has_value ());
    EXPECT_EQ (code (make ({L {bank, "1"}, L {heading->id, "-1"}})),
               "voucher.heading-account");

    /* Nothing was entered by any of them. */
    auto none {m_company->vouchers ({})};
    ASSERT_TRUE (none.has_value ());
    EXPECT_TRUE (none->items.empty ());

    auto made {make ({L {bank, "1250.00", "", "Betalning"},
                      L {sales, "-1000.00"}, L {sales, "-250"}})};
    ASSERT_TRUE (made.has_value ()) << code (made);
    EXPECT_EQ (made->date, "2025-01-15");
    EXPECT_EQ (made->series, "A");
    EXPECT_FALSE (made->posted);
    EXPECT_EQ (made->number, 0);
    EXPECT_GT (made->sequence, 0);
    ASSERT_EQ (made->lines.size (), 3u);
    EXPECT_EQ (made->lines[0].amount, "1250.00");
    EXPECT_EQ (made->lines[0].text, "Betalning");
    EXPECT_EQ (made->lines[0].account_number, "1930");
    EXPECT_EQ (made->lines[2].amount, "-250.00");

    auto read {m_company->voucher (made->id)};
    ASSERT_TRUE (read.has_value ());
    EXPECT_EQ (read->etag, made->etag);
    EXPECT_EQ (code (m_company->voucher ("nope")), "voucher.not-found");

    /* The lines come back in the order they were entered, also from
     * storage that does not keep the order by itself. */
    std::vector<VoucherLine> many;
    for (int i = 1; i <= 12; ++i)
        many.push_back (L {i % 2 ? bank : sales, std::to_string (i) + ".00", "",
                           "Rad " + std::to_string (i)});
    many.push_back (L {sales, "-78.00", "", "Rad 13"});
    auto long_one {make (many)};
    ASSERT_TRUE (long_one.has_value ()) << code (long_one);
    ASSERT_NO_FATAL_FAILURE (reopen ());
    auto again {m_company->voucher (long_one->id)};
    ASSERT_TRUE (again.has_value ());
    ASSERT_EQ (again->lines.size (), 13u);
    for (size_t i = 0; i < again->lines.size (); ++i)
        EXPECT_EQ (again->lines[i].text, "Rad " + std::to_string (i + 1));
    EXPECT_EQ (again->etag, long_one->etag);
}

TEST_P (CoreTest, a_preliminary_voucher_is_changed_and_deleted)
{
    auto first {enter ("2025-01-10", "Hyra januari", "5000.00", "5010", "1930")};
    ASSERT_TRUE (first.has_value ());
    NewVoucher changed {"2025-01-11", "Hyra januari, rättad", "",
                        {{m_accounts["5010"], "5500.00"},
                         {m_accounts["1930"], "-5500.00"}}};
    EXPECT_EQ (code (m_company->update_voucher (first->id, changed, "stale")),
               "conflict");
    changed.lines[1].amount = "-5000.00";
    EXPECT_EQ (code (m_company->update_voucher (first->id, changed, first->etag)),
               "voucher.unbalanced");
    changed.lines[1].amount = "-5500.00";
    auto updated {m_company->update_voucher (first->id, changed, first->etag)};
    ASSERT_TRUE (updated.has_value ()) << code (updated);
    EXPECT_EQ (updated->id, first->id);
    EXPECT_EQ (updated->date, "2025-01-11");
    EXPECT_EQ (updated->text, "Hyra januari, rättad");
    ASSERT_EQ (updated->lines.size (), 2u);
    EXPECT_EQ (updated->lines[0].amount, "5500.00");
    EXPECT_EQ (updated->sequence, first->sequence);

    EXPECT_EQ (code (m_company->delete_voucher (first->id, first->etag)),
               "conflict");
    EXPECT_TRUE (m_company->delete_voucher (first->id, updated->etag));
    EXPECT_EQ (code (m_company->voucher (first->id)), "voucher.not-found");
    EXPECT_TRUE (m_company->verify ().empty ());
}

TEST_P (CoreTest, vouchers_are_listed_in_pages)
{
    for (int day = 1; day <= 5; ++day)
        ASSERT_TRUE (enter ("2025-01-0" + std::to_string (day),
                            "Dag " + std::to_string (day), "10.00", "5010",
                            "1930"));
    ASSERT_TRUE (enter ("2025-02-01", "Februari", "20.00", "1930", "3001"));

    VoucherQuery query;
    query.limit = 2;
    std::vector<std::string> texts;
    for (int pages = 0; pages < 10; ++pages)
    {
        auto page {m_company->vouchers (query)};
        ASSERT_TRUE (page.has_value ());
        EXPECT_LE (page->items.size (), 2u);
        for (const auto& item : page->items)
            texts.push_back (item.text);
        if (page->next.empty ())
            break;
        query.after = page->next;
    }
    EXPECT_EQ (texts, (std::vector<std::string> {"Dag 1", "Dag 2", "Dag 3",
                                                 "Dag 4", "Dag 5", "Februari"}));

    VoucherQuery january;
    january.from = "2025-01-02";
    january.to = "2025-01-31";
    auto some {m_company->vouchers (january)};
    ASSERT_TRUE (some.has_value ());
    EXPECT_EQ (some->items.size (), 4u);
    EXPECT_TRUE (some->next.empty ());

    VoucherQuery sales;
    sales.account = m_accounts["3001"];
    auto on_sales {m_company->vouchers (sales)};
    ASSERT_TRUE (on_sales.has_value ());
    ASSERT_EQ (on_sales->items.size (), 1u);
    EXPECT_EQ (on_sales->items[0].text, "Februari");

    VoucherQuery bad;
    bad.from = "januari";
    EXPECT_EQ (code (m_company->vouchers (bad)), "date.invalid");
    bad.from = "";
    bad.after = "x";
    EXPECT_EQ (code (m_company->vouchers (bad)), "cursor.invalid");
}

TEST_P (CoreTest, a_line_in_another_currency_carries_both_amounts)
{
    auto euro {m_accounts["1940"]};
    auto bank {m_accounts["1930"]};
    using L = VoucherLine;
    auto make {[&] (std::vector<VoucherLine> lines)
    {
        return m_company->create_voucher ({"2025-03-01", "Växling", "", lines});
    }};
    EXPECT_EQ (code (make ({L {euro, "1150.00"}, L {bank, "-1150.00"}})),
               "voucher.foreign-amount-missing");
    EXPECT_EQ (code (make ({L {euro, "1150.00", "-100.00"},
                            L {bank, "-1150.00"}})),
               "voucher.foreign-amount-sign");

    auto made {make ({L {euro, "1150.00", "100.00"}, L {bank, "-1150.00"}})};
    ASSERT_TRUE (made.has_value ()) << code (made);
    EXPECT_EQ (made->lines[0].amount, "1150.00");
    EXPECT_EQ (made->lines[0].foreign_amount, "100.00");
    EXPECT_EQ (made->lines[1].foreign_amount, "");

    /* The reports are in the company's currency. */
    auto balance {m_company->trial_balance ("2025-01-01", "2025-12-31")};
    ASSERT_TRUE (balance.has_value ());
    EXPECT_EQ (balance->debit, "1150.00");
    EXPECT_EQ (balance->credit, "1150.00");

    ASSERT_NO_FATAL_FAILURE (reopen ());
    auto read {m_company->voucher (made->id)};
    ASSERT_TRUE (read.has_value ());
    EXPECT_EQ (read->lines[0].foreign_amount, "100.00");
    EXPECT_EQ (read->etag, made->etag);
}

TEST_P (CoreTest, posted_vouchers_are_fixed_and_corrected)
{
    auto rent {enter ("2025-01-10", "Hyra", "5000.00", "5010", "1930")};
    auto sale {enter ("2025-01-20", "Försäljning", "8000.00", "1930", "3001")};
    auto later {enter ("2025-02-05", "Februari", "100.00", "5010", "1930")};
    ASSERT_TRUE (rent && sale && later);

    EXPECT_EQ (code (m_company->plan_lock ("i går")), "date.invalid");
    EXPECT_EQ (code (m_company->lock_through ("2999-01-01")), "lock.refused");
    auto plan {m_company->plan_lock ("2025-01-31")};
    ASSERT_TRUE (plan.has_value ());
    EXPECT_EQ (plan->total, 2);
    ASSERT_EQ (plan->groups.size (), 1u);
    EXPECT_EQ (plan->groups[0].series, "A");
    EXPECT_EQ (plan->groups[0].first_number, 1);

    auto locked {m_company->lock_through ("2025-01-31")};
    ASSERT_TRUE (locked.has_value ()) << code (locked);
    EXPECT_EQ (locked->posted, 2);
    EXPECT_EQ (m_company->info ().lock_date, "2025-01-31");
    EXPECT_EQ (code (m_company->lock_through ("2025-01-31")), "lock.refused");

    auto posted {m_company->voucher (rent->id)};
    ASSERT_TRUE (posted.has_value ());
    EXPECT_TRUE (posted->posted);
    EXPECT_EQ (posted->voucher, "A 1");
    EXPECT_EQ (posted->period, "2025");
    EXPECT_EQ (m_company->voucher (sale->id)->voucher, "A 2");

    NewVoucher change {"2025-02-10", "Ändrad", "",
                       {{m_accounts["5010"], "1.00"},
                        {m_accounts["1930"], "-1.00"}}};
    EXPECT_EQ (code (m_company->update_voucher (rent->id, change, posted->etag)),
               "voucher.posted");
    EXPECT_EQ (code (m_company->delete_voucher (rent->id, posted->etag)),
               "voucher.posted");
    EXPECT_EQ (code (enter ("2025-01-15", "För sent", "1.00", "5010", "1930")),
               "voucher.date-locked");
    change.date = "2025-01-15";
    EXPECT_EQ (code (m_company->update_voucher (later->id, change, later->etag)),
               "voucher.date-locked");

    VoucherQuery only_posted;
    only_posted.posted = true;
    EXPECT_EQ (m_company->vouchers (only_posted)->items.size (), 2u);

    /* Correcting. */
    EXPECT_EQ (code (m_company->correct (later->id, {"2025-02-10", "Fel"})),
               "voucher.not-posted");
    EXPECT_EQ (code (m_company->correct (rent->id, {"2025-01-20", "Fel"})),
               "voucher.date-locked");
    auto fixed {m_company->correct (rent->id,
                                    {"2025-02-10", "Fel belopp", true})};
    ASSERT_TRUE (fixed.has_value ()) << code (fixed);
    EXPECT_EQ (fixed->correction.corrects, rent->id);
    EXPECT_EQ (fixed->correction.signature, "anna");
    EXPECT_EQ (fixed->correction.reason, "Fel belopp");
    ASSERT_EQ (fixed->correction.lines.size (), 2u);
    for (const auto& line : fixed->correction.lines)
        EXPECT_EQ (line.amount,
                   line.account_number == "5010" ? "-5000.00" : "5000.00");
    ASSERT_TRUE (fixed->replacement.has_value ());
    EXPECT_EQ (fixed->replacement->replaces, rent->id);
    EXPECT_EQ (m_company->voucher (rent->id)->corrected_by,
               fixed->correction.id);
    EXPECT_EQ (code (m_company->correct (rent->id, {"2025-02-10", "Igen"})),
               "voucher.already-corrected");

    auto history {m_company->history (1000).items};
    ASSERT_GE (history.size (), 2u);
    EXPECT_EQ (history.back ().type, "correction");
    EXPECT_EQ (history.back ().signature, "anna");
    EXPECT_TRUE (m_company->verify ().empty ());

    ASSERT_NO_FATAL_FAILURE (reopen ());
    EXPECT_EQ (m_company->voucher (rent->id)->voucher, "A 1");
    EXPECT_EQ (m_company->voucher (rent->id)->corrected_by,
               fixed->correction.id);
    EXPECT_EQ (m_company->info ().lock_date, "2025-01-31");
    EXPECT_TRUE (m_company->verify ().empty ());
    auto next {m_company->lock_through ("2025-02-28")};
    ASSERT_TRUE (next.has_value ()) << code (next);
    EXPECT_EQ (next->posted, 3);
    EXPECT_TRUE (m_company->verify ().empty ());
}

TEST_P (CoreTest, the_history_is_read_in_pages)
{
    ASSERT_TRUE (enter ("2025-01-10", "Hyra", "5000.00", "5010", "1930"));
    ASSERT_TRUE (m_company->lock_through ("2025-01-31"));
    auto all {m_company->history (1000)};
    ASSERT_GE (all.items.size (), 3u);
    EXPECT_TRUE (all.next.empty ());
    EXPECT_EQ (all.items.back ().type, "lock");

    std::vector<int64_t> numbers;
    std::string after;
    for (int pages = 0; pages < 100; ++pages)
    {
        auto page {m_company->history (2, after)};
        EXPECT_LE (page.items.size (), 2u);
        for (const auto& event : page.items)
            numbers.push_back (event.number);
        if (page.next.empty ())
            break;
        after = page.next;
    }
    ASSERT_EQ (numbers.size (), all.items.size ());
    for (size_t i = 0; i < numbers.size (); ++i)
        EXPECT_EQ (numbers[i], all.items[i].number);
}

TEST_P (CoreTest, the_reports_follow_the_fiscal_year)
{
    ASSERT_TRUE (enter ("2024-12-20", "Försäljning 2024", "3000.00", "1930",
                        "3001"));
    ASSERT_TRUE (enter ("2025-01-10", "Hyra", "400.00", "5010", "1930"));
    ASSERT_TRUE (enter ("2025-01-20", "Försäljning", "1000.00", "1930", "3001"));
    ASSERT_TRUE (enter ("2025-02-03", "Hyra", "400.00", "5010", "1930"));

    EXPECT_EQ (code (m_company->trial_balance ("2025-02-01", "2025-01-01")),
               "date.invalid");
    auto january {m_company->trial_balance ("2025-01-01", "2025-01-31")};
    ASSERT_TRUE (january.has_value ());
    EXPECT_EQ (january->currency, "SEK");
    EXPECT_EQ (january->debit, "1400.00");
    EXPECT_EQ (january->credit, "1400.00");
    std::map<std::string, TrialBalanceRow> rows;
    for (const auto& row : january->rows)
        rows[row.number] = row;
    ASSERT_EQ (rows.count ("1930"), 1u);
    /* The bank account carries its balance over the year end ... */
    EXPECT_EQ (rows["1930"].opening, "3000.00");
    EXPECT_EQ (rows["1930"].debit, "1000.00");
    EXPECT_EQ (rows["1930"].credit, "400.00");
    EXPECT_EQ (rows["1930"].closing, "3600.00");
    /* ... and the income account starts the year at zero. */
    EXPECT_EQ (rows["3001"].opening, "0.00");
    EXPECT_EQ (rows["3001"].credit, "1000.00");
    EXPECT_EQ (rows["3001"].closing, "-1000.00");
    EXPECT_EQ (rows["5010"].closing, "400.00");
    EXPECT_EQ (rows.count ("1940"), 0u);

    auto february {m_company->trial_balance ("2025-02-01", "2025-02-28")};
    ASSERT_TRUE (february.has_value ());
    for (const auto& row : february->rows)
        if (row.number == "3001")
        {
            EXPECT_EQ (row.opening, "-1000.00");
            EXPECT_EQ (row.closing, "-1000.00");
        }

    auto ledger {m_company->ledger (m_accounts["1930"], "2025-01-01",
                                    "2025-12-31")};
    ASSERT_TRUE (ledger.has_value ());
    EXPECT_EQ (ledger->account.number, "1930");
    EXPECT_EQ (ledger->opening, "3000.00");
    ASSERT_EQ (ledger->lines.size (), 3u);
    EXPECT_EQ (ledger->lines[0].date, "2025-01-10");
    EXPECT_EQ (ledger->lines[0].amount, "-400.00");
    EXPECT_EQ (ledger->lines[0].balance, "2600.00");
    EXPECT_EQ (ledger->lines[2].balance, "3200.00");
    EXPECT_EQ (ledger->closing, "3200.00");
    EXPECT_EQ (code (m_company->ledger ("nope", "2025-01-01", "2025-12-31")),
               "account.not-found");
}

TEST_P (CoreTest, a_fiscal_year_gets_its_result_and_is_closed)
{
    ASSERT_TRUE (enter ("2025-03-10", "Hyra", "400.00", "5010", "1930"));
    ASSERT_TRUE (enter ("2025-06-20", "Försäljning", "1000.00", "1930", "3001"));

    EXPECT_EQ (code (m_company->define_fiscal_year ("2025-01-01", "2027-12-31")),
               "fiscal-year.refused");
    auto year {m_company->define_fiscal_year ("2025-01-01", "2025-12-31")};
    ASSERT_TRUE (year.has_value ()) << code (year);
    EXPECT_EQ (year->key, "2025");
    EXPECT_FALSE (year->closed);
    ASSERT_EQ (m_company->fiscal_years ().size (), 1u);

    EXPECT_EQ (code (m_company->result_voucher ("2025-06-30", m_accounts["1930"],
                                                m_accounts["2099"])),
               "result.account-invalid");
    EXPECT_EQ (code (m_company->result_voucher ("2025-06-30", m_accounts["8999"],
                                                m_accounts["1930"])),
               "result.account-invalid");
    auto result {m_company->result_voucher ("2025-06-30", m_accounts["8999"],
                                            m_accounts["2099"])};
    ASSERT_TRUE (result.has_value ()) << code (result);
    EXPECT_EQ (result->date, "2025-12-31");
    ASSERT_EQ (result->lines.size (), 2u);
    /* A profit of 600: debit on the result account, credit to equity. */
    EXPECT_EQ (result->lines[0].account_number, "8999");
    EXPECT_EQ (result->lines[0].amount, "600.00");
    EXPECT_EQ (result->lines[1].account_number, "2099");
    EXPECT_EQ (result->lines[1].amount, "-600.00");
    EXPECT_EQ (code (m_company->result_voucher ("2025-06-30", m_accounts["8999"],
                                                m_accounts["2099"])),
               "result.nothing-to-enter");

    auto closed {m_company->close_fiscal_year ("2025-06-30")};
    ASSERT_TRUE (closed.has_value ()) << code (closed);
    EXPECT_EQ (closed->date, "2025-12-31");
    EXPECT_EQ (closed->posted, 3);
    auto years {m_company->fiscal_years ()};
    ASSERT_GE (years.size (), 1u);
    EXPECT_TRUE (years[0].closed);
    EXPECT_EQ (years[0].closed_by, "anna");
    EXPECT_EQ (m_company->info ().lock_date, "2025-12-31");
    EXPECT_TRUE (m_company->verify ().empty ());
}

/* SQLite and XML always; PostgreSQL too when a server is at hand
 * (GCSE_PG_URL, set by util/se/pg-env). */
static std::vector<const char*>
storages ()
{
    std::vector<const char*> result {"sqlite3", "xml"};
    if (g_getenv ("GCSE_PG_URL"))
        result.push_back ("postgres");
    return result;
}

INSTANTIATE_TEST_SUITE_P (Storage, CoreTest, ::testing::ValuesIn (storages ()));
