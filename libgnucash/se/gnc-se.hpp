/********************************************************************\
 * gnc-se.hpp -- GnuCash SE: lock date and voucher numbering        *
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

/** @file gnc-se.hpp
 *  @brief Posting ("bokföring") of transactions as Swedish law requires it.
 *
 * A transaction is preliminary until it is locked. Locking is done for
 * all transactions up to and including a date, the book's lock date,
 * which can only move forward. When a transaction is locked it gets
 * the next number in its voucher series, in the order the transactions
 * were entered, and from then on it cannot be changed or deleted. A
 * hash chain per series makes changes made outside GnuCash SE
 * detectable.
 *
 * Design: docs/design/01-las-och-numrering.md.
 */

#ifndef GNC_SE_HPP
#define GNC_SE_HPP

#include <glib.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <qof.h>
#include <Transaction.h>

#include "gnc-se.h"

/** How the numbers of a voucher series run. */
enum class GncSePeriodType
{
    FISCAL_YEAR, ///< The numbers start again at 1 every fiscal year.
    CONTINUOUS,  ///< One sequence for as long as the book is used.
};

struct GncSeSeries
{
    std::string code;        ///< E.g. "A". Letters and digits, at most 8.
    std::string description;
    GncSePeriodType period_type;
};

/** When transactions other than cash payments have to be posted at
 *  the latest (BFNAR 2013:2, points 3.6 and 3.7). */
enum class GncSeDeferral
{
    MONTH,   ///< 50 days after the end of the month.
    QUARTER, ///< 50 days after the end of the quarter (turnover at most 3 MSEK).
};

/** Preliminary transactions that should have been posted by now. */
struct GncSeOverdue
{
    int cash;  ///< With a cash account: due the next working day.
    int other; ///< Others: due according to the book's deferral.
    std::optional<GDate> oldest; ///< The earliest date among them.
};

/** What locking through a date would number in one series' period. */
struct GncSeLockGroup
{
    std::string series;
    std::string period;
    int count;            ///< Transactions to lock.
    int64_t first_number; ///< The number the first of them gets.
};

/** What gnc_se_lock_through() would do. */
struct GncSeLockPlan
{
    bool ok;           ///< False if the date cannot be locked; see error.
    std::string error;
    int total;         ///< Transactions to lock in all.
    std::vector<GncSeLockGroup> groups;
};

/** An entry in the processing history ("behandlingshistorik"). */
struct GncSeEvent
{
    int64_t number;        ///< 1, 2, 3, ...
    time64 time;           ///< When it was recorded.
    std::string type;      ///< "start", "lock", "correction", "settings",
                           ///< "account", "tax-table", "program" or "check".
    std::string signature; ///< Who did it, where that was asked for.
    std::string text;      ///< What happened, in the language of the user.
};

/** The outcome of gnc_se_correct(). */
struct GncSeCorrection
{
    bool ok;                  ///< False if nothing was created; see error.
    std::string error;
    Transaction* correction;  ///< The correcting entry, preliminary.
    Transaction* replacement; ///< A copy of the original to put right, or null.
};

/** The outcome of gnc_se_lock_through(). */
struct GncSeLockResult
{
    bool ok;           ///< False if nothing was locked; see error.
    int locked;        ///< Number of transactions that were locked.
    std::string error; ///< Why nothing was locked.
};

enum class GncSeIssueType
{
    GAP,                   ///< Numbers are missing in a series.
    DUPLICATE_NUMBER,      ///< Two transactions carry the same number.
    CHANGED,               ///< A locked transaction no longer matches its hash.
    HEAD_MISMATCH,         ///< The series' recorded last hash does not match.
    COUNTER_MISMATCH,      ///< The series' next number is lower than a used number.
    UNKNOWN_SERIES,        ///< Locked transactions in a series the book lacks.
    MISSING_READ_ONLY,     ///< A locked transaction has lost its read-only mark.
    LOCKED_AFTER_LOCK_DATE,///< A locked transaction is dated after the lock date.
    UNLOCKED_BEFORE_LOCK_DATE, ///< A preliminary transaction on or before the lock date.
    CORRECTION_WITHOUT_ORIGINAL, ///< A posted correcting entry whose original is gone or not posted.
    HISTORY_CHANGED,       ///< An entry in the processing history is missing or changed.
};

/** Something gnc_se_verify() found. */
struct GncSeIssue
{
    GncSeIssueType type;
    std::string series;  ///< Empty when the issue is not about a series.
    std::string period;  ///< The period within the series, e.g. "2026".
    int64_t number;      ///< The (first) number concerned, or 0. For
                         ///< HISTORY_CHANGED: the number of the entry.
    int64_t count;       ///< For GAP: how many numbers are missing.
    GncGUID guid;        ///< The transaction concerned, or the null GUID.
};

/** Why the guard last refused an edit, or empty. Cleared by the next
 *  accepted edit. */
const std::string& gnc_se_last_rejection ();

/** @name Book settings
 @{ */

/** Whether the book is kept in GnuCash SE: something has been locked
 *  or one of the settings below has been made. */
bool gnc_se_book_is_se (const QofBook* book);

/** The date through which the book is locked, if any. */
std::optional<GDate> gnc_se_book_get_lock_date (const QofBook* book);

/** Whether a transaction dated @a date would fall in the locked period. */
bool gnc_se_date_is_locked (const QofBook* book, const GDate& date);

/** First day of the fiscal year as {month, day}; {1, 1} by default. */
std::pair<int, int> gnc_se_book_get_fiscal_year_start (const QofBook* book);

/** Set the first day of the fiscal year. It decides in which period
 *  of a FISCAL_YEAR series a transaction is numbered when it is
 *  locked; transactions already locked keep their period.
 *  @return false if the date is not a valid day of a non-leap year. */
bool gnc_se_book_set_fiscal_year_start (QofBook* book, int month, int day);

/** The book's voucher series. Empty until a series has been added or
 *  something has been locked. */
std::vector<GncSeSeries> gnc_se_book_get_series (const QofBook* book);

/** Add a voucher series.
 *  @return false if the code is invalid or already used. */
bool gnc_se_book_add_series (QofBook* book, const std::string& code,
                             const std::string& description,
                             GncSePeriodType period_type);

/** The series of transactions that have not been given one; "A" by
 *  default. */
std::string gnc_se_book_get_default_series (const QofBook* book);

/** @return false if the book has no such series. */
bool gnc_se_book_set_default_series (QofBook* book, const std::string& code);

/** How long posting may be put off; MONTH by default. */
GncSeDeferral gnc_se_book_get_deferral (const QofBook* book);
void gnc_se_book_set_deferral (QofBook* book, GncSeDeferral deferral);

/** The period of @a series in which a transaction dated @a date is
 *  numbered: the year in which the fiscal year starts, or "all" for a
 *  CONTINUOUS series. */
std::string gnc_se_book_get_period (const QofBook* book,
                                    const GncSeSeries& series,
                                    const GDate& date);
/** @} */

/** @name Transactions
 @{ */

/** The transaction's voucher series: its own, or the book's default. */
std::string gnc_se_trans_get_series (const Transaction* trans);

/** Choose the series of a preliminary transaction.
 *  @return false if the transaction is locked or the book has no such
 *  series. */
bool gnc_se_trans_set_series (Transaction* trans, const std::string& code);

/** The period of the series in which the transaction was numbered, or
 *  empty for a preliminary transaction. */
std::string gnc_se_trans_get_period (const Transaction* trans);

/** The transaction's number in its series, or 0 for a preliminary
 *  transaction. */
int64_t gnc_se_trans_get_number (const Transaction* trans);

/** Series and number as shown to the user, e.g. "A 12", or empty for a
 *  preliminary transaction. */
std::string gnc_se_trans_get_voucher (const Transaction* trans);

/** What the transaction's num field held before it was locked. */
std::string gnc_se_trans_get_former_num (const Transaction* trans);
/** @} */

/** @name Corrections
 *
 * A posted transaction is corrected with a correcting entry, a new
 * transaction with its own voucher (BFNAR 2013:2 points 2.17, 2.18).
 * The two refer to each other, and the correcting entry says who made
 * it and why.
 @{ */

/** Create a correcting entry for a posted transaction: a preliminary
 *  transaction dated @a date that takes back the original, split by
 *  split. With @a with_replacement a preliminary copy of the original
 *  is created as well, to be changed into what should have been
 *  posted.
 *
 *  Nothing is created if the original is not posted or already has a
 *  correcting entry, if @a date is in the locked period, or if
 *  @a signature is empty.
 *
 *  @param signature Who makes the correction.
 *  @param reason Why; may be empty. */
GncSeCorrection gnc_se_correct (Transaction* original, const GDate& date,
                                const std::string& signature,
                                const std::string& reason,
                                bool with_replacement);

/** The correcting entry of a transaction, or null. A correcting entry
 *  that has been deleted while it was preliminary no longer counts. */
Transaction* gnc_se_trans_get_correction (const Transaction* original);

/** The transaction that a correcting entry corrects, or null. */
Transaction* gnc_se_trans_get_corrected (const Transaction* correction);

/** The transaction that this one was created to replace, or null. */
Transaction* gnc_se_trans_get_replaced (const Transaction* replacement);

/** Who made a correcting entry, and why. */
std::string gnc_se_trans_get_signature (const Transaction* trans);
std::string gnc_se_trans_get_reason (const Transaction* trans);
/** @} */

/** Work out what gnc_se_lock_through() would lock, without changing
 *  the book. */
GncSeLockPlan gnc_se_plan_lock (QofBook* book, const GDate& date);

/** Lock every preliminary transaction dated on or before @a date and
 *  move the book's lock date to @a date.
 *
 *  The transactions are numbered per series and period in the order
 *  they were entered. Scheduled-transaction templates, voided
 *  transactions and transactions without splits are left alone.
 *
 *  Nothing is locked if @a date is not after the current lock date or
 *  is after today, if any transaction to lock is unbalanced, has a
 *  split without an account or is in a series the book lacks, or if
 *  there is a preliminary transaction in the period that is already
 *  locked.
 *
 *  @param signature Who locks; recorded in the processing history. */
GncSeLockResult gnc_se_lock_through (QofBook* book, const GDate& date,
                                     const std::string& signature = "");

/** Count the preliminary transactions that should have been posted
 *  before @a today. Weekends are not working days; holidays are not
 *  known. */
GncSeOverdue gnc_se_overdue (QofBook* book, const GDate& today);

/** @name Processing history
 *
 * A record, kept in the book, of what has been done to the system
 * (BFNAR 2013:2 point 9.16): periods locked, corrections made, settings
 * changed, accounts and tax tables added, changed or removed, and the
 * versions of the program that have been used. The entries are chained
 * with hashes like the voucher series. When each transaction was
 * entered and posted is recorded on the transaction itself.
 *
 * The history starts the first time something is recorded, with the
 * accounts and tax tables as they are then.
 @{ */

/** The entries, oldest first. */
std::vector<GncSeEvent> gnc_se_book_get_history (const QofBook* book);

/** Record what has changed without being noticed as it happened: a new
 *  program version, and accounts and tax tables changed while events
 *  were off or in another program. Called when a book has been opened
 *  and before a period is locked. Does nothing in a book that is not
 *  kept in GnuCash SE or is read-only. */
void gnc_se_history_sync (QofBook* book);

/** Record what gnc_se_verify() found, unless it is what was recorded
 *  the last time. */
void gnc_se_history_record_issues (QofBook* book,
                                   const std::vector<GncSeIssue>& issues);

/** The processing history as text: the entries, and every posted
 *  transaction with its voucher number and when it was entered and
 *  posted. */
std::string gnc_se_history_text (QofBook* book);
/** @} */

/** Check the locked transactions against their series: unbroken
 *  numbering and unchanged content, and the processing history.
 *  @return what is wrong; empty if nothing is. */
std::vector<GncSeIssue> gnc_se_verify (QofBook* book);

#endif /* GNC_SE_HPP */
