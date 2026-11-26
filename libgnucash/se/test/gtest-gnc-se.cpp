/********************************************************************\
 * gtest-gnc-se.cpp -- tests of GnuCash SE locking and numbering    *
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
#include <glib/gstdio.h>

#include <gnc-se.hpp>

#include <Account.h>
#include <Split.h>
#include <Transaction.h>
#include <TransLog.h>
#include <gnc-commodity.h>
#include <gnc-date.h>
#include <gnc-engine.h>
#include <qof.h>
#include <qofinstance-p.h>
#include <qofsession.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <gtest/gtest.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <string>
#include <vector>

static GDate
date (int year, int month, int day)
{
    GDate result;
    g_date_set_dmy (&result, static_cast<GDateDay> (day),
                    static_cast<GDateMonth> (month),
                    static_cast<GDateYear> (year));
    return result;
}

static std::string
slot (const Transaction* trans, const char* key)
{
    auto value {qof_instance_get_path_kvp<const char*> (
            QOF_INSTANCE (const_cast<Transaction*> (trans)), {"se", key})};
    return value && *value ? *value : "";
}

static std::string
description (const Transaction* trans)
{
    return xaccTransGetDescription (trans);
}

class SeTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite ()
    {
        qof_init ();
        gnc_engine_init (0, nullptr);
        xaccLogDisable (); /* No transaction logs in the build tree. */
    }

    void SetUp () override
    {
        m_book = qof_book_new ();
        fill_book ();
        gnc_se_init ();
    }

    void TearDown () override
    {
        gnc_se_shutdown ();
        if (m_book)
            qof_book_destroy (m_book);
    }

    void fill_book ()
    {
        m_sek = gnc_commodity_table_lookup (
            gnc_commodity_table_get_table (m_book), GNC_COMMODITY_NS_CURRENCY,
            "SEK");
        ASSERT_NE (m_sek, nullptr);
        auto root {gnc_account_create_root (m_book)};
        m_bank = account (root, "1930", "Företagskonto", ACCT_TYPE_BANK);
        m_sales = account (root, "3001", "Försäljning", ACCT_TYPE_INCOME);
        m_costs = account (root, "5010", "Lokalhyra", ACCT_TYPE_EXPENSE);
    }

    Account* account (Account* root, const char* code, const char* name,
                      GNCAccountType type)
    {
        auto acc {xaccMallocAccount (m_book)};
        xaccAccountBeginEdit (acc);
        xaccAccountSetCode (acc, code);
        xaccAccountSetName (acc, name);
        xaccAccountSetType (acc, type);
        xaccAccountSetCommodity (acc, m_sek);
        gnc_account_append_child (root, acc);
        xaccAccountCommitEdit (acc);
        return acc;
    }

    /* A transaction of @a ore öre from @a credit to @a debit. @a entered
     * is the time it was entered, in seconds. */
    Transaction* add (const GDate& posted, time64 entered, const char* text,
                      int64_t ore, Account* debit, Account* credit)
    {
        auto trans {xaccMallocTransaction (m_book)};
        xaccTransBeginEdit (trans);
        xaccTransSetCurrency (trans, m_sek);
        xaccTransSetDatePostedGDate (trans, posted);
        xaccTransSetDateEnteredSecs (trans, entered);
        xaccTransSetDescription (trans, text);
        for (int sign : {1, -1})
        {
            auto split {xaccMallocSplit (m_book)};
            xaccSplitSetParent (split, trans);
            xaccSplitSetAccount (split, sign > 0 ? debit : credit);
            xaccSplitSetAmount (split, gnc_numeric_create (sign * ore, 100));
            xaccSplitSetValue (split, gnc_numeric_create (sign * ore, 100));
        }
        xaccTransCommitEdit (trans);
        return trans;
    }

    /* Three sales in January 2025, entered in another order than dated. */
    void add_january ()
    {
        m_first = add (date (2025, 1, 20), 1000, "Entered first", 10000, m_bank,
                       m_sales);
        m_second = add (date (2025, 1, 10), 2000, "Entered second", 20000,
                        m_bank, m_sales);
        m_third = add (date (2025, 1, 15), 3000, "Entered third", 30000, m_bank,
                       m_sales);
    }

    GncSeLockResult lock (const GDate& through)
    {
        return gnc_se_lock_through (m_book, through);
    }

    std::vector<GncSeIssueType> issue_types ()
    {
        std::vector<GncSeIssueType> types;
        for (const auto& issue : gnc_se_verify (m_book))
            types.push_back (issue.type);
        return types;
    }

    QofBook* m_book {};
    gnc_commodity* m_sek {};
    Account* m_bank {};
    Account* m_sales {};
    Account* m_costs {};
    Transaction* m_first {};
    Transaction* m_second {};
    Transaction* m_third {};
};

/* ------------------------------------------------------------------ */
/* Book settings                                                      */

TEST_F (SeTest, a_new_book_has_defaults)
{
    EXPECT_FALSE (gnc_se_book_is_se (m_book));
    EXPECT_FALSE (gnc_se_book_get_lock_date (m_book));
    EXPECT_FALSE (gnc_se_date_is_locked (m_book, date (2025, 1, 1)));
    EXPECT_TRUE (gnc_se_book_get_series (m_book).empty ());
    EXPECT_EQ (gnc_se_book_get_default_series (m_book), "A");
    EXPECT_EQ (gnc_se_book_get_fiscal_year_start (m_book),
               std::make_pair (1, 1));
    EXPECT_TRUE (gnc_se_verify (m_book).empty ());
}

TEST_F (SeTest, fiscal_year_start_decides_the_period)
{
    GncSeSeries yearly {"A", "", GncSePeriodType::FISCAL_YEAR};
    GncSeSeries continuous {"B", "", GncSePeriodType::CONTINUOUS};

    EXPECT_EQ (gnc_se_book_get_period (m_book, yearly, date (2025, 6, 30)),
               "2025");

    EXPECT_FALSE (gnc_se_book_set_fiscal_year_start (m_book, 2, 29));
    EXPECT_FALSE (gnc_se_book_set_fiscal_year_start (m_book, 13, 1));
    EXPECT_FALSE (gnc_se_book_set_fiscal_year_start (m_book, 4, 31));
    EXPECT_FALSE (gnc_se_book_set_fiscal_year_start (m_book, 0, 0));
    ASSERT_TRUE (gnc_se_book_set_fiscal_year_start (m_book, 7, 1));
    EXPECT_EQ (gnc_se_book_get_fiscal_year_start (m_book),
               std::make_pair (7, 1));

    EXPECT_EQ (gnc_se_book_get_period (m_book, yearly, date (2025, 6, 30)),
               "2024");
    EXPECT_EQ (gnc_se_book_get_period (m_book, yearly, date (2025, 7, 1)),
               "2025");
    EXPECT_EQ (gnc_se_book_get_period (m_book, continuous, date (2025, 7, 1)),
               "all");
}

TEST_F (SeTest, series_can_be_added_and_chosen)
{
    EXPECT_TRUE (gnc_se_book_add_series (m_book, "L", "Löner",
                                         GncSePeriodType::CONTINUOUS));
    EXPECT_FALSE (gnc_se_book_add_series (m_book, "L", "Again",
                                          GncSePeriodType::FISCAL_YEAR));
    EXPECT_FALSE (gnc_se_book_add_series (m_book, "", "Empty",
                                          GncSePeriodType::FISCAL_YEAR));
    EXPECT_FALSE (gnc_se_book_add_series (m_book, "A B", "Space",
                                          GncSePeriodType::FISCAL_YEAR));
    EXPECT_FALSE (gnc_se_book_add_series (m_book, "A/B", "Slash",
                                          GncSePeriodType::FISCAL_YEAR));
    EXPECT_FALSE (gnc_se_book_add_series (m_book, "TOOLONGCODE", "Long",
                                          GncSePeriodType::FISCAL_YEAR));

    auto series {gnc_se_book_get_series (m_book)};
    ASSERT_EQ (series.size (), 1u);
    EXPECT_EQ (series[0].code, "L");
    EXPECT_EQ (series[0].description, "Löner");
    EXPECT_EQ (series[0].period_type, GncSePeriodType::CONTINUOUS);

    EXPECT_FALSE (gnc_se_book_set_default_series (m_book, "X"));
    EXPECT_TRUE (gnc_se_book_set_default_series (m_book, "L"));
    EXPECT_EQ (gnc_se_book_get_default_series (m_book), "L");
}

/* ------------------------------------------------------------------ */
/* Locking and numbering                                              */

TEST_F (SeTest, locking_numbers_in_the_order_of_entry)
{
    add_january ();
    EXPECT_FALSE (gnc_se_trans_is_locked (m_first));
    EXPECT_EQ (gnc_se_trans_get_voucher (m_first), "");

    auto result {lock (date (2025, 1, 31))};
    ASSERT_TRUE (result.ok) << result.error;
    EXPECT_EQ (result.locked, 3);

    EXPECT_EQ (gnc_se_trans_get_number (m_first), 1);
    EXPECT_EQ (gnc_se_trans_get_number (m_second), 2);
    EXPECT_EQ (gnc_se_trans_get_number (m_third), 3);
    EXPECT_EQ (gnc_se_trans_get_voucher (m_second), "A 2");
    EXPECT_STREQ (xaccTransGetNum (m_second), "A 2");
    EXPECT_EQ (gnc_se_trans_get_series (m_second), "A");
    EXPECT_EQ (gnc_se_trans_get_period (m_second), "2025");
    EXPECT_TRUE (gnc_se_trans_is_locked (m_second));
    EXPECT_NE (xaccTransGetReadOnly (m_second), nullptr);
    EXPECT_EQ (slot (m_second, "hash").size (), 64u);

    EXPECT_TRUE (gnc_se_book_is_se (m_book));
    auto lock_date {gnc_se_book_get_lock_date (m_book)};
    ASSERT_TRUE (lock_date);
    auto expected {date (2025, 1, 31)};
    EXPECT_EQ (g_date_compare (&*lock_date, &expected), 0);
    EXPECT_TRUE (gnc_se_date_is_locked (m_book, date (2025, 1, 31)));
    EXPECT_FALSE (gnc_se_date_is_locked (m_book, date (2025, 2, 1)));

    auto series {gnc_se_book_get_series (m_book)};
    ASSERT_EQ (series.size (), 1u);
    EXPECT_EQ (series[0].code, "A");
    EXPECT_EQ (series[0].period_type, GncSePeriodType::FISCAL_YEAR);

    EXPECT_TRUE (gnc_se_verify (m_book).empty ());
}

TEST_F (SeTest, a_deleted_preliminary_transaction_leaves_no_gap)
{
    add_january ();
    xaccTransDestroy (m_second);

    auto result {lock (date (2025, 1, 31))};
    ASSERT_TRUE (result.ok) << result.error;
    EXPECT_EQ (result.locked, 2);
    EXPECT_EQ (gnc_se_trans_get_number (m_first), 1);
    EXPECT_EQ (gnc_se_trans_get_number (m_third), 2);
    EXPECT_TRUE (gnc_se_verify (m_book).empty ());
}

TEST_F (SeTest, only_transactions_up_to_the_date_are_locked)
{
    add_january ();
    auto result {lock (date (2025, 1, 15))};
    ASSERT_TRUE (result.ok) << result.error;
    EXPECT_EQ (result.locked, 2);
    EXPECT_FALSE (gnc_se_trans_is_locked (m_first)); /* dated 20 January */
    EXPECT_EQ (gnc_se_trans_get_number (m_second), 1);
    EXPECT_EQ (gnc_se_trans_get_number (m_third), 2);

    /* The numbering goes on where it stopped. */
    result = lock (date (2025, 1, 31));
    ASSERT_TRUE (result.ok) << result.error;
    EXPECT_EQ (result.locked, 1);
    EXPECT_EQ (gnc_se_trans_get_number (m_first), 3);
    EXPECT_TRUE (gnc_se_verify (m_book).empty ());
}

TEST_F (SeTest, the_lock_date_only_moves_forward)
{
    add_january ();
    ASSERT_TRUE (lock (date (2025, 1, 31)).ok);

    auto result {lock (date (2025, 1, 15))};
    EXPECT_FALSE (result.ok);
    EXPECT_FALSE (result.error.empty ());
    EXPECT_FALSE (lock (date (2025, 1, 31)).ok);

    auto lock_date {gnc_se_book_get_lock_date (m_book)};
    auto expected {date (2025, 1, 31)};
    EXPECT_EQ (g_date_compare (&*lock_date, &expected), 0);
}

TEST_F (SeTest, a_date_after_today_cannot_be_locked)
{
    add_january ();
    GDate tomorrow;
    gnc_gdate_set_today (&tomorrow);
    g_date_add_days (&tomorrow, 1);

    auto result {lock (tomorrow)};
    EXPECT_FALSE (result.ok);
    EXPECT_FALSE (gnc_se_trans_is_locked (m_first));
    EXPECT_FALSE (gnc_se_book_get_lock_date (m_book));

    GDate invalid;
    g_date_clear (&invalid, 1);
    EXPECT_FALSE (lock (invalid).ok);
}

TEST_F (SeTest, each_fiscal_year_starts_at_one)
{
    auto old_year {add (date (2024, 12, 30), 1000, "2024", 10000, m_bank,
                        m_sales)};
    auto new_year {add (date (2025, 1, 2), 2000, "2025", 10000, m_bank,
                        m_sales)};
    ASSERT_TRUE (lock (date (2025, 1, 31)).ok);

    EXPECT_EQ (gnc_se_trans_get_period (old_year), "2024");
    EXPECT_EQ (gnc_se_trans_get_number (old_year), 1);
    EXPECT_EQ (gnc_se_trans_get_period (new_year), "2025");
    EXPECT_EQ (gnc_se_trans_get_number (new_year), 1);
    EXPECT_NE (slot (old_year, "hash"), slot (new_year, "hash"));
    EXPECT_TRUE (gnc_se_verify (m_book).empty ());
}

TEST_F (SeTest, a_broken_fiscal_year_keeps_its_numbering_over_new_year)
{
    ASSERT_TRUE (gnc_se_book_set_fiscal_year_start (m_book, 7, 1));
    auto december {add (date (2024, 12, 30), 1000, "December", 10000, m_bank,
                        m_sales)};
    auto january {add (date (2025, 1, 2), 2000, "January", 10000, m_bank,
                       m_sales)};
    ASSERT_TRUE (lock (date (2025, 1, 31)).ok);

    EXPECT_EQ (gnc_se_trans_get_period (december), "2024");
    EXPECT_EQ (gnc_se_trans_get_period (january), "2024");
    EXPECT_EQ (gnc_se_trans_get_number (january), 2);
}

TEST_F (SeTest, each_series_has_its_own_numbers)
{
    add_january ();
    ASSERT_TRUE (gnc_se_book_add_series (m_book, "A", "Verifikationer",
                                         GncSePeriodType::FISCAL_YEAR));
    ASSERT_TRUE (gnc_se_book_add_series (m_book, "L", "Löner",
                                         GncSePeriodType::CONTINUOUS));
    EXPECT_FALSE (gnc_se_trans_set_series (m_second, "X"));
    ASSERT_TRUE (gnc_se_trans_set_series (m_second, "L"));
    EXPECT_EQ (gnc_se_trans_get_series (m_second), "L");

    ASSERT_TRUE (lock (date (2025, 1, 31)).ok);
    EXPECT_EQ (gnc_se_trans_get_voucher (m_first), "A 1");
    EXPECT_EQ (gnc_se_trans_get_voucher (m_second), "L 1");
    EXPECT_EQ (gnc_se_trans_get_period (m_second), "all");
    EXPECT_EQ (gnc_se_trans_get_voucher (m_third), "A 2");
    EXPECT_FALSE (gnc_se_trans_set_series (m_first, "L")); /* locked */
    EXPECT_TRUE (gnc_se_verify (m_book).empty ());
}

TEST_F (SeTest, the_former_num_is_kept)
{
    add_january ();
    xaccTransSetNum (m_first, "F-123");
    ASSERT_TRUE (lock (date (2025, 1, 31)).ok);

    EXPECT_STREQ (xaccTransGetNum (m_first), "A 1");
    EXPECT_EQ (gnc_se_trans_get_former_num (m_first), "F-123");
    EXPECT_EQ (gnc_se_trans_get_former_num (m_second), "");
}

TEST_F (SeTest, voided_transactions_are_not_numbered)
{
    add_january ();
    xaccTransVoid (m_second, "Entered by mistake");

    auto result {lock (date (2025, 1, 31))};
    ASSERT_TRUE (result.ok) << result.error;
    EXPECT_EQ (result.locked, 2);
    EXPECT_FALSE (gnc_se_trans_is_locked (m_second));
    EXPECT_EQ (gnc_se_trans_get_number (m_third), 2);
    EXPECT_TRUE (gnc_se_verify (m_book).empty ());

    /* It cannot come back into the locked period. */
    xaccTransUnvoid (m_second);
    EXPECT_TRUE (xaccTransGetVoidStatus (m_second));
    EXPECT_FALSE (gnc_se_last_rejection ().empty ());
}

/* ------------------------------------------------------------------ */
/* The guard                                                          */

TEST_F (SeTest, a_locked_transaction_cannot_be_changed)
{
    add_january ();
    ASSERT_TRUE (lock (date (2025, 1, 31)).ok);
    auto split {xaccTransGetSplit (m_first, 0)};
    auto value {xaccSplitGetValue (split)};

    xaccTransSetDescription (m_first, "Changed");
    EXPECT_EQ (description (m_first), "Entered first");
    EXPECT_FALSE (gnc_se_last_rejection ().empty ());

    xaccTransSetDatePostedGDate (m_first, date (2025, 3, 1));
    auto posted {xaccTransGetDatePostedGDate (m_first)};
    EXPECT_EQ (g_date_get_month (&posted), G_DATE_JANUARY);

    xaccTransSetNum (m_first, "X 99");
    EXPECT_STREQ (xaccTransGetNum (m_first), "A 1");

    xaccSplitSetValue (split, gnc_numeric_create (1, 100));
    EXPECT_TRUE (gnc_numeric_equal (xaccSplitGetValue (split), value));

    xaccSplitSetMemo (split, "Changed");
    EXPECT_STREQ (xaccSplitGetMemo (split), "");

    xaccSplitSetAccount (split, m_costs);
    EXPECT_NE (xaccSplitGetAccount (split), m_costs);

    xaccTransBeginEdit (m_first);
    auto extra {xaccMallocSplit (m_book)};
    xaccSplitSetParent (extra, m_first);
    xaccSplitSetAccount (extra, m_costs);
    xaccTransCommitEdit (m_first);
    EXPECT_EQ (xaccTransCountSplits (m_first), 2);

    EXPECT_TRUE (gnc_se_verify (m_book).empty ());
}

TEST_F (SeTest, a_locked_transaction_cannot_be_deleted_or_unlocked)
{
    add_january ();
    ASSERT_TRUE (lock (date (2025, 1, 31)).ok);
    auto guid {*xaccTransGetGUID (m_first)};

    xaccTransDestroy (m_first);
    ASSERT_EQ (xaccTransLookup (&guid, m_book), m_first);

    /* What "Check & Repair" and unposting of invoices do. */
    xaccTransClearReadOnly (m_first);
    EXPECT_NE (xaccTransGetReadOnly (m_first), nullptr);
    EXPECT_FALSE (gnc_se_last_rejection ().empty ());

    xaccTransVoid (m_first, "Void");
    EXPECT_FALSE (xaccTransGetVoidStatus (m_first));

    xaccSplitDestroy (xaccTransGetSplit (m_first, 0));
    EXPECT_EQ (xaccTransCountSplits (m_first), 2);

    /* The lock itself. */
    xaccTransBeginEdit (m_first);
    qof_instance_set_path_kvp<int64_t> (QOF_INSTANCE (m_first), 7,
                                        {"se", "number"});
    xaccTransCommitEdit (m_first);
    EXPECT_EQ (gnc_se_trans_get_number (m_first), 1);

    EXPECT_TRUE (gnc_se_verify (m_book).empty ());
}

TEST_F (SeTest, reconciling_and_notes_are_allowed_on_a_locked_transaction)
{
    add_january ();
    ASSERT_TRUE (lock (date (2025, 1, 31)).ok);
    auto split {xaccTransGetSplit (m_first, 0)};

    xaccSplitSetReconcile (split, YREC);
    EXPECT_EQ (xaccSplitGetReconcile (split), YREC);
    xaccTransSetNotes (m_first, "Checked against the bank statement");
    EXPECT_STREQ (xaccTransGetNotes (m_first),
                  "Checked against the bank statement");
    xaccTransSetDocLink (m_first, "file:///verifikationer/a1.pdf");
    EXPECT_STREQ (xaccTransGetDocLink (m_first),
                  "file:///verifikationer/a1.pdf");

    EXPECT_TRUE (gnc_se_last_rejection ().empty ());
    EXPECT_TRUE (gnc_se_verify (m_book).empty ());
}

TEST_F (SeTest, nothing_new_gets_into_a_locked_period)
{
    add_january ();
    ASSERT_TRUE (lock (date (2025, 1, 31)).ok);

    auto late {add (date (2025, 1, 31), 4000, "Too late", 5000, m_bank,
                    m_sales)};
    EXPECT_EQ (xaccTransCountSplits (late), 0);
    EXPECT_FALSE (gnc_se_last_rejection ().empty ());

    auto february {add (date (2025, 2, 1), 5000, "February", 5000, m_bank,
                        m_sales)};
    EXPECT_EQ (xaccTransCountSplits (february), 2);
    EXPECT_TRUE (gnc_se_last_rejection ().empty ());

    xaccTransSetDatePostedGDate (february, date (2025, 1, 15));
    auto posted {xaccTransGetDatePostedGDate (february)};
    EXPECT_EQ (g_date_get_month (&posted), G_DATE_FEBRUARY);

    /* A preliminary transaction can still be changed and deleted. */
    xaccTransSetDescription (february, "February, changed");
    EXPECT_EQ (description (february), "February, changed");
    auto guid {*xaccTransGetGUID (february)};
    xaccTransDestroy (february);
    EXPECT_EQ (xaccTransLookup (&guid, m_book), nullptr);
}

TEST_F (SeTest, a_number_cannot_be_set_from_outside)
{
    add_january ();
    xaccTransBeginEdit (m_first);
    qof_instance_set_path_kvp<int64_t> (QOF_INSTANCE (m_first), 1,
                                        {"se", "number"});
    xaccTransCommitEdit (m_first);

    EXPECT_FALSE (gnc_se_trans_is_locked (m_first));
    EXPECT_FALSE (gnc_se_last_rejection ().empty ());
}

TEST_F (SeTest, without_the_guard_nothing_is_refused)
{
    add_january ();
    ASSERT_TRUE (lock (date (2025, 1, 31)).ok);
    gnc_se_shutdown ();

    xaccTransSetDescription (m_first, "Changed");
    EXPECT_EQ (description (m_first), "Changed");
}

/* ------------------------------------------------------------------ */
/* Verification: what is done to the book without GnuCash SE          */

class SeVerifyTest : public SeTest
{
protected:
    void SetUp () override
    {
        SeTest::SetUp ();
        add_january ();
        ASSERT_TRUE (lock (date (2025, 1, 31)).ok);
        ASSERT_TRUE (gnc_se_verify (m_book).empty ());
        gnc_se_shutdown (); /* As in plain GnuCash. */
    }
};

TEST_F (SeVerifyTest, a_changed_transaction_is_found)
{
    xaccTransSetDescription (m_second, "Changed");

    auto issues {gnc_se_verify (m_book)};
    ASSERT_EQ (issues.size (), 1u);
    EXPECT_EQ (issues[0].type, GncSeIssueType::CHANGED);
    EXPECT_EQ (issues[0].series, "A");
    EXPECT_EQ (issues[0].period, "2025");
    EXPECT_EQ (issues[0].number, 2);
    EXPECT_TRUE (guid_equal (&issues[0].guid, xaccTransGetGUID (m_second)));
}

TEST_F (SeVerifyTest, a_changed_amount_is_found)
{
    for (auto node = xaccTransGetSplitList (m_third); node; node = node->next)
    {
        auto split {GNC_SPLIT (node->data)};
        auto value {gnc_numeric_neg (xaccSplitGetValue (split))};
        xaccSplitSetValue (split, value);
        xaccSplitSetAmount (split, value);
    }
    auto issues {gnc_se_verify (m_book)};
    ASSERT_EQ (issues.size (), 1u);
    EXPECT_EQ (issues[0].type, GncSeIssueType::CHANGED);
    EXPECT_EQ (issues[0].number, 3);
}

TEST_F (SeVerifyTest, a_deleted_transaction_is_found)
{
    xaccTransClearReadOnly (m_second);
    xaccTransDestroy (m_second);

    auto issues {gnc_se_verify (m_book)};
    ASSERT_EQ (issues.size (), 1u);
    EXPECT_EQ (issues[0].type, GncSeIssueType::GAP);
    EXPECT_EQ (issues[0].number, 2);
    EXPECT_EQ (issues[0].count, 1);
}

TEST_F (SeVerifyTest, a_deleted_last_transaction_is_found)
{
    xaccTransClearReadOnly (m_third);
    xaccTransDestroy (m_third);

    auto issues {gnc_se_verify (m_book)};
    ASSERT_EQ (issues.size (), 1u);
    EXPECT_EQ (issues[0].type, GncSeIssueType::GAP);
    EXPECT_EQ (issues[0].number, 3);
    EXPECT_EQ (issues[0].count, 1);
}

TEST_F (SeVerifyTest, all_transactions_deleted_is_found)
{
    for (auto trans : {m_first, m_second, m_third})
    {
        xaccTransClearReadOnly (trans);
        xaccTransDestroy (trans);
    }
    auto issues {gnc_se_verify (m_book)};
    ASSERT_EQ (issues.size (), 1u);
    EXPECT_EQ (issues[0].type, GncSeIssueType::GAP);
    EXPECT_EQ (issues[0].number, 1);
    EXPECT_EQ (issues[0].count, 3);
}

TEST_F (SeVerifyTest, a_removed_read_only_mark_is_found)
{
    xaccTransClearReadOnly (m_first);
    EXPECT_EQ (issue_types (), std::vector<GncSeIssueType> {
            GncSeIssueType::MISSING_READ_ONLY});
}

TEST_F (SeVerifyTest, a_new_transaction_in_the_locked_period_is_found)
{
    auto late {add (date (2025, 1, 5), 9000, "Added in plain GnuCash", 100,
                    m_bank, m_sales)};
    auto issues {gnc_se_verify (m_book)};
    ASSERT_EQ (issues.size (), 1u);
    EXPECT_EQ (issues[0].type, GncSeIssueType::UNLOCKED_BEFORE_LOCK_DATE);
    EXPECT_TRUE (guid_equal (&issues[0].guid, xaccTransGetGUID (late)));

    /* GnuCash SE does not take it in: the next lock is refused... */
    gnc_se_init ();
    auto result {lock (date (2025, 2, 28))};
    EXPECT_FALSE (result.ok);
    EXPECT_NE (result.error.find ("Added in plain GnuCash"), std::string::npos);
    EXPECT_FALSE (gnc_se_trans_is_locked (late));

    /* ...until it has been deleted. */
    xaccTransDestroy (late);
    EXPECT_TRUE (lock (date (2025, 2, 28)).ok);
    EXPECT_TRUE (gnc_se_verify (m_book).empty ());
}

TEST_F (SeVerifyTest, a_copied_number_is_found)
{
    qof_instance_set_path_kvp<int64_t> (QOF_INSTANCE (m_third), 2,
                                        {"se", "number"});
    auto types {issue_types ()};
    EXPECT_NE (std::find (types.begin (), types.end (),
                          GncSeIssueType::DUPLICATE_NUMBER), types.end ());
    EXPECT_NE (std::find (types.begin (), types.end (), GncSeIssueType::GAP),
               types.end ());
}

TEST_F (SeVerifyTest, a_lock_date_moved_back_is_found)
{
    qof_instance_set_path_kvp<GDate> (QOF_INSTANCE (m_book), date (2025, 1, 12),
                                      {"se", "lock-date"});
    auto issues {gnc_se_verify (m_book)};
    ASSERT_EQ (issues.size (), 2u); /* dated 15 and 20 January */
    EXPECT_EQ (issues[0].type, GncSeIssueType::LOCKED_AFTER_LOCK_DATE);
    EXPECT_EQ (issues[1].type, GncSeIssueType::LOCKED_AFTER_LOCK_DATE);
}

/* ------------------------------------------------------------------ */
/* The hash must not change between versions: it is stored in books.  */

TEST_F (SeTest, the_hash_of_a_transaction_is_stable)
{
    auto set_guid = [] (gpointer instance, const char* text)
    {
        GncGUID guid;
        ASSERT_TRUE (string_to_guid (text, &guid));
        qof_instance_set_guid (instance, &guid);
    };
    set_guid (m_book, "00000000000000000000000000000001");
    set_guid (m_bank, "00000000000000000000000000000002");
    set_guid (m_sales, "00000000000000000000000000000003");
    auto trans {add (date (2025, 1, 20), 1737369000, "Försäljning, faktura 17",
                     123456, m_bank, m_sales)};
    set_guid (trans, "00000000000000000000000000000010");
    set_guid (xaccTransGetSplit (trans, 0), "00000000000000000000000000000011");
    set_guid (xaccTransGetSplit (trans, 1), "00000000000000000000000000000012");
    xaccSplitSetMemo (xaccTransGetSplit (trans, 1), "Tjänster");

    ASSERT_TRUE (lock (date (2025, 1, 31)).ok);
    /* Worked out independently from the format described in
     * docs/design/01-las-och-numrering.md, section 6.3. */
    EXPECT_EQ (slot (trans, "hash"),
               "75fa10fea9c39a141128a8e0565d7e13135240d701dc648adbbe202ba2f5e730");
    EXPECT_TRUE (gnc_se_verify (m_book).empty ());
}

/* ------------------------------------------------------------------ */
/* Saved and read again                                               */

class SeFileTest : public SeTest, public ::testing::WithParamInterface<const char*>
{
protected:
    void SetUp () override
    {
        m_dir = g_dir_make_tmp ("gnc-se-XXXXXX", nullptr);
        ASSERT_NE (m_dir, nullptr);
        auto path {g_build_filename (m_dir, "book.gnucash", nullptr)};
        m_path = path;
        g_free (path);
        m_url = std::string {GetParam ()} + "://" + m_path;

        /* The session owns the book. */
        m_session = qof_session_new (qof_book_new ());
        m_book = qof_session_get_book (m_session);
        fill_book ();
        gnc_se_init ();
    }

    void TearDown () override
    {
        gnc_se_shutdown ();
        if (m_session)
        {
            qof_session_end (m_session);
            qof_session_destroy (m_session);
        }
        m_book = nullptr;
        if (m_dir)
        {
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
    }

    void save_and_reopen ()
    {
        qof_session_begin (m_session, m_url.c_str (), SESSION_NEW_STORE);
        ASSERT_EQ (qof_session_get_error (m_session), ERR_BACKEND_NO_ERR);
        qof_session_save (m_session, nullptr);
        ASSERT_EQ (qof_session_get_error (m_session), ERR_BACKEND_NO_ERR);
        qof_session_end (m_session);
        qof_session_destroy (m_session);

        m_session = qof_session_new (qof_book_new ());
        qof_session_begin (m_session, m_url.c_str (), SESSION_NORMAL_OPEN);
        ASSERT_EQ (qof_session_get_error (m_session), ERR_BACKEND_NO_ERR);
        qof_session_load (m_session, nullptr);
        ASSERT_EQ (qof_session_get_error (m_session), ERR_BACKEND_NO_ERR);
        m_book = qof_session_get_book (m_session);
    }

    QofSession* m_session {};
    char* m_dir {};
    std::string m_path;
    std::string m_url;
};

TEST_P (SeFileTest, a_locked_book_survives_save_and_load)
{
    add_january ();
    xaccTransSetNum (m_first, "F-123");
    auto february {add (date (2025, 2, 3), 4000, "Preliminary", 5000, m_bank,
                        m_sales)};
    ASSERT_TRUE (gnc_se_book_set_fiscal_year_start (m_book, 7, 1));
    ASSERT_TRUE (lock (date (2025, 1, 31)).ok);
    auto first_guid {*xaccTransGetGUID (m_first)};
    auto february_guid {*xaccTransGetGUID (february)};
    auto hash {slot (m_first, "hash")};

    ASSERT_NO_FATAL_FAILURE (save_and_reopen ());
    EXPECT_FALSE (qof_session_load_in_progress ());

    auto first {xaccTransLookup (&first_guid, m_book)};
    ASSERT_NE (first, nullptr);
    EXPECT_EQ (gnc_se_trans_get_voucher (first), "A 1");
    EXPECT_EQ (gnc_se_trans_get_period (first), "2024");
    EXPECT_EQ (gnc_se_trans_get_former_num (first), "F-123");
    EXPECT_EQ (slot (first, "hash"), hash);
    EXPECT_NE (xaccTransGetReadOnly (first), nullptr);

    EXPECT_TRUE (gnc_se_book_is_se (m_book));
    EXPECT_TRUE (gnc_se_date_is_locked (m_book, date (2025, 1, 31)));
    EXPECT_FALSE (gnc_se_date_is_locked (m_book, date (2025, 2, 1)));
    EXPECT_EQ (gnc_se_book_get_fiscal_year_start (m_book),
               std::make_pair (7, 1));
    EXPECT_TRUE (gnc_se_verify (m_book).empty ());

    /* The guard is in force for the book that was read. */
    xaccTransSetDescription (first, "Changed");
    EXPECT_EQ (description (first), "Entered first");

    /* And the numbering goes on. */
    auto preliminary {xaccTransLookup (&february_guid, m_book)};
    ASSERT_NE (preliminary, nullptr);
    EXPECT_FALSE (gnc_se_trans_is_locked (preliminary));
    ASSERT_TRUE (lock (date (2025, 2, 28)).ok);
    EXPECT_EQ (gnc_se_trans_get_voucher (preliminary), "A 4");
    EXPECT_TRUE (gnc_se_verify (m_book).empty ());
}

INSTANTIATE_TEST_SUITE_P (Backends, SeFileTest,
                          ::testing::Values ("xml", "sqlite3"));
