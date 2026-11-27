// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Isak Adamssvärd

/** @file gcse-core.hpp
 *  @brief The command layer of GnuCash SE: one company's books, as
 *  commands that change them and queries that read them.
 *
 *  Nothing from the accounting engine shows here, so that the engine can
 *  be replaced without the API changing. Design:
 *  docs/design/07-kommandolager.md.
 *
 *  Conventions: identities are text that the caller does not interpret.
 *  Amounts are decimal numbers as text ("1234.50"), never floating
 *  point. Dates are "YYYY-MM-DD". Points in time are seconds since 1970.
 *  A resource that can be changed has an etag; a change names the etag
 *  it started from and is refused with the error "conflict" if the
 *  resource has changed since.
 *
 *  Not thread-safe, and one company per process: the engine holds one
 *  book per process.
 */

#ifndef GCSE_CORE_HPP
#define GCSE_CORE_HPP

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gcse
{

/** Why something was refused. @a code is stable and is what clients
 *  translate and act on; @a message is English, for logs, or the
 *  engine's own (translated) message where the refusal came from there;
 *  @a field names the part of the request that is wrong, if one is. */
struct Error
{
    std::string code;
    std::string message;
    std::string field;
};

template <typename T> using Result = std::expected<T, Error>;

/** One page of a list. @a next is the cursor for the page after this
 *  one, empty when there is none. */
template <typename T> struct Page
{
    std::vector<T> items;
    std::string next;
};

/* --- The company ------------------------------------------------------ */

struct NewCompany
{
    std::string name;
    std::string organisation_number;
    std::string currency = "SEK"; ///< The currency the books are kept in.
    int fiscal_year_start_month = 1;
    int fiscal_year_start_day = 1;
};

struct CompanyInfo
{
    std::string id;
    std::string name;
    std::string organisation_number;
    std::string currency;
    std::string lock_date;       ///< Posted through this date; empty if nothing is.
    int fiscal_year_start_month;
    int fiscal_year_start_day;
};

/* --- Accounts --------------------------------------------------------- */

/** @a type is one of "asset", "bank", "cash", "receivable", "payable",
 *  "liability", "equity", "income" and "expense"; an account of a kind
 *  that the API does not offer reads as "other". */
struct Account
{
    std::string id;
    std::string number;
    std::string name;
    std::string type;
    std::string currency;
    std::string parent;      ///< Empty for an account at the top.
    std::string description;
    bool placeholder;        ///< A heading: no vouchers on it.
    bool hidden;
    std::string etag;
};

struct NewAccount
{
    std::string number;
    std::string name;
    std::string type;
    std::string currency;    ///< Empty: the company's currency.
    std::string parent;      ///< Empty: at the top.
    std::string description;
    bool placeholder = false;
};

/** What to change; what is not set stays as it is. */
struct AccountChange
{
    std::optional<std::string> number;
    std::optional<std::string> name;
    std::optional<std::string> type;
    std::optional<std::string> parent;
    std::optional<std::string> description;
    std::optional<bool> placeholder;
    std::optional<bool> hidden;
};

/* --- Vouchers --------------------------------------------------------- */

/** One line of a voucher. @a amount is in the company's currency,
 *  positive for debit and negative for credit. On an account in another
 *  currency @a foreign_amount is the amount in the account's currency,
 *  with the same sign; otherwise it is empty. */
struct VoucherLine
{
    std::string account;
    std::string amount;
    std::string foreign_amount;
    std::string text;
    /* Filled in when read, ignored when written: */
    std::string account_number;
    std::string account_name;
};

struct NewVoucher
{
    std::string date;
    std::string text;
    std::string series;      ///< Empty: the company's default series.
    std::vector<VoucherLine> lines;
};

struct Voucher
{
    std::string id;
    std::string date;
    std::string text;
    std::string series;
    bool posted;             ///< Locked and numbered; cannot be changed.
    std::string period;      ///< The period of the series, once posted.
    int64_t number;          ///< The number in the series; 0 until posted.
    std::string voucher;     ///< "A 12"; empty until posted.
    int64_t entered;         ///< When it was first saved.
    int64_t sequence;        ///< Its place in the order of entry.
    std::vector<VoucherLine> lines;
    std::string corrects;      ///< The voucher this one corrects.
    std::string corrected_by;  ///< The voucher that corrects this one.
    std::string replaces;      ///< The voucher this one replaces.
    std::string signature;     ///< Who made the correction.
    std::string reason;        ///< Why the correction was made.
    std::string etag;
};

struct VoucherQuery
{
    std::string from;        ///< First date; empty for no limit.
    std::string to;          ///< Last date; empty for no limit.
    std::string series;      ///< Empty for all.
    std::optional<bool> posted;
    std::string account;     ///< Only vouchers with a line on this account.
    int limit = 100;
    std::string after;       ///< The cursor from the page before.
};

/* --- Posting, correcting, fiscal years -------------------------------- */

struct LockGroup
{
    std::string series;
    std::string period;
    int count;
    int64_t first_number;
};

struct LockPlan
{
    std::string date;
    int total;               ///< Vouchers that would be posted.
    std::vector<LockGroup> groups;
};

struct LockOutcome
{
    std::string date;
    int posted;
};

struct CorrectionRequest
{
    std::string date;
    std::string reason;
    bool with_replacement = false; ///< Also make a copy to put right.
};

struct Correction
{
    Voucher correction;
    std::optional<Voucher> replacement;
};

struct FiscalYear
{
    std::string key;         ///< Its name in the voucher series, e.g. "2026".
    std::string start;
    std::string end;
    bool stored;             ///< False for a year that is only worked out.
    bool closed;
    std::string closed_by;
};

/** What is waiting to be posted past its time limit. */
struct Overdue
{
    int cash;                ///< Cash transactions past the next working day.
    int other;
    std::string oldest;      ///< The earliest date among them; empty if none.
};

/* --- History and checks ----------------------------------------------- */

struct Event
{
    int64_t number;
    int64_t time;
    std::string type;
    std::string signature;
    std::string text;
};

struct Issue
{
    std::string code;        ///< E.g. "gap", "changed", "history-changed".
    std::string series;
    std::string period;
    int64_t number;
    std::string voucher;     ///< The voucher concerned, if one is.
};

/* --- Reports ---------------------------------------------------------- */

struct TrialBalanceRow
{
    std::string account;
    std::string number;
    std::string name;
    std::string type;
    std::string opening;     ///< Balance sheet accounts: all before the period.
                             ///< Income and expense: from the start of the
                             ///< fiscal year that the period begins in.
    std::string debit;       ///< The period's debits, in the company's currency.
    std::string credit;      ///< The period's credits, as a positive number.
    std::string closing;
};

struct TrialBalance
{
    std::string from;
    std::string to;
    std::string currency;
    std::vector<TrialBalanceRow> rows; ///< Accounts with a balance or movement.
    std::string debit;                 ///< Sums of the period.
    std::string credit;
};

struct LedgerLine
{
    std::string voucher_id;
    std::string voucher;     ///< "A 12"; empty for one that is not posted.
    std::string date;
    std::string text;
    std::string amount;
    std::string balance;     ///< The account's balance after this line.
};

struct Ledger
{
    Account account;
    std::string from;
    std::string to;
    std::string opening;
    std::string closing;
    std::vector<LedgerLine> lines;
};

/* --- The company's books ---------------------------------------------- */

class Company
{
public:
    /** Create a company in new storage. @a uri is the engine's:
     *  "sqlite3:///path", "postgres://user:password@host/database" (an
     *  existing, empty database) or "xml:///path". */
    static Result<std::unique_ptr<Company>> create (const std::string& uri,
                                                    const NewCompany& company);
    static Result<std::unique_ptr<Company>> open (const std::string& uri);
    ~Company ();
    Company (const Company&) = delete;
    Company& operator= (const Company&) = delete;

    /** Who the commands that follow are made by; signs what they do. */
    void set_actor (const std::string& actor);

    CompanyInfo info () const;

    std::vector<Account> accounts () const;
    Result<Account> account (const std::string& id) const;
    Result<Account> create_account (const NewAccount& account);
    Result<Account> update_account (const std::string& id,
                                    const AccountChange& change,
                                    const std::string& etag);
    Result<void> delete_account (const std::string& id, const std::string& etag);

    Result<Voucher> create_voucher (const NewVoucher& voucher);
    Result<Voucher> voucher (const std::string& id) const;
    Result<Page<Voucher>> vouchers (const VoucherQuery& query) const;
    /** Only a voucher that is not posted can be changed or deleted. */
    Result<Voucher> update_voucher (const std::string& id,
                                    const NewVoucher& voucher,
                                    const std::string& etag);
    Result<void> delete_voucher (const std::string& id, const std::string& etag);

    Result<LockPlan> plan_lock (const std::string& date) const;
    Result<LockOutcome> lock_through (const std::string& date);
    Overdue overdue () const;
    Result<Correction> correct (const std::string& id,
                                const CorrectionRequest& request);

    std::vector<FiscalYear> fiscal_years () const;
    Result<FiscalYear> define_fiscal_year (const std::string& start,
                                           const std::string& end);
    /** Lock the fiscal year that @a date lies in through its last day and
     *  mark it closed. */
    Result<LockOutcome> close_fiscal_year (const std::string& date);
    /** Enter the voucher for the result of the fiscal year that @a date
     *  lies in: between @a result_account (income statement) and
     *  @a equity_account (balance sheet), dated the last day of the year.
     *  It is preliminary until the year is locked. */
    Result<Voucher> result_voucher (const std::string& date,
                                    const std::string& result_account,
                                    const std::string& equity_account);

    Page<Event> history (int limit = 100, const std::string& after = "") const;
    std::vector<Issue> verify () const;

    Result<TrialBalance> trial_balance (const std::string& from,
                                        const std::string& to) const;
    Result<Ledger> ledger (const std::string& account, const std::string& from,
                           const std::string& to) const;

private:
    struct Impl;
    explicit Company (std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m;
};

} // namespace gcse

#endif /* GCSE_CORE_HPP */
