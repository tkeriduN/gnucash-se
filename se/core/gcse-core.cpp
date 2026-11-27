// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Isak Adamssvärd

/** @file gcse-core.cpp
 *  @brief The command layer on top of the GnuCash engine and GnuCash
 *  SE's module in it. Design: docs/design/07-kommandolager.md.
 */

#include <config.h>

#include "gcse-core.hpp"

#include <glib.h>

#include <gnc-se.hpp>

#include <Account.h>
#include <Account.hpp>
#include <Split.h>
#include <Transaction.h>
#include <TransLog.h>
#include <gnc-commodity.h>
#include <gnc-date.h>
#include <gnc-engine.h>
#include <gnc-session.h>
#include <qof.h>
#include <qofbook.h>
#include <qofsession.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <tuple>
#include <utility>

namespace gcse
{

namespace
{

/* --- Small things ----------------------------------------------------- */

std::unexpected<Error>
refuse (std::string code, std::string message, std::string field = "")
{
    return std::unexpected (
        Error {std::move (code), std::move (message), std::move (field)});
}

std::string
text (const char* value)
{
    return value ? value : "";
}

std::optional<GDate>
parse_date (const std::string& value)
{
    int year = 0, month = 0, day = 0;
    char extra = 0;
    if (value.size () != 10 ||
        std::sscanf (value.c_str (), "%4d-%2d-%2d%c", &year, &month, &day,
                     &extra) != 3 ||
        value[4] != '-' || value[7] != '-' ||
        !g_date_valid_dmy (static_cast<GDateDay> (day),
                           static_cast<GDateMonth> (month),
                           static_cast<GDateYear> (year)))
        return std::nullopt;
    GDate date;
    g_date_clear (&date, 1);
    g_date_set_dmy (&date, static_cast<GDateDay> (day),
                    static_cast<GDateMonth> (month),
                    static_cast<GDateYear> (year));
    return date;
}

std::string
format_date (const GDate& date)
{
    if (!g_date_valid (&date))
        return "";
    char buffer[16];
    std::snprintf (buffer, sizeof buffer, "%04d-%02d-%02d",
                   static_cast<int> (g_date_get_year (&date)),
                   static_cast<int> (g_date_get_month (&date)),
                   static_cast<int> (g_date_get_day (&date)));
    return buffer;
}

/* A decimal number as text, e.g. "-1234.50": an optional sign, digits,
 * and at most six decimals. No thousands separators, no exponent. */
std::optional<gnc_numeric>
parse_amount (const std::string& value)
{
    size_t pos = 0;
    bool negative = false;
    if (pos < value.size () && (value[pos] == '-' || value[pos] == '+'))
        negative = value[pos++] == '-';
    int64_t units = 0;
    int64_t denom = 1;
    int digits = 0;
    bool decimals = false;
    for (; pos < value.size (); ++pos)
    {
        auto c {value[pos]};
        if (c == '.' && !decimals)
        {
            decimals = true;
            continue;
        }
        if (c < '0' || c > '9' || ++digits > 17)
            return std::nullopt;
        units = units * 10 + (c - '0');
        if (decimals && (denom *= 10) > 1000000)
            return std::nullopt;
    }
    if (digits == 0)
        return std::nullopt;
    return gnc_numeric_create (negative ? -units : units, denom);
}

/* The number with as many decimals as the currency has. */
std::string
format_amount (gnc_numeric value, int fraction)
{
    auto fixed {gnc_numeric_convert (value, fraction,
                                     GNC_HOW_RND_ROUND_HALF_UP)};
    auto units {gnc_numeric_num (fixed)};
    bool negative {units < 0};
    auto magnitude {static_cast<uint64_t> (negative ? -units : units)};
    int decimals = 0;
    for (int f = fraction; f > 1; f /= 10)
        ++decimals;
    auto whole {std::to_string (magnitude / static_cast<uint64_t> (fraction))};
    auto part {std::to_string (magnitude % static_cast<uint64_t> (fraction))};
    std::string result {negative ? "-" : ""};
    result += whole;
    if (decimals > 0)
    {
        result += '.';
        if (part.size () < static_cast<size_t> (decimals))
            result.append (static_cast<size_t> (decimals) - part.size (), '0');
        result += part;
    }
    return result;
}

std::string
id_of (const GncGUID* guid)
{
    if (!guid || guid_equal (guid, guid_null ()))
        return "";
    char buffer[GUID_ENCODING_LENGTH + 1];
    guid_to_string_buff (guid, buffer);
    return buffer;
}

std::optional<GncGUID>
parse_id (const std::string& id)
{
    GncGUID guid;
    if (id.size () != GUID_ENCODING_LENGTH || !string_to_guid (id.c_str (), &guid))
        return std::nullopt;
    return guid;
}

/* A checksum of a resource's content: its etag. */
class Digest
{
public:
    Digest () : m_sum {g_checksum_new (G_CHECKSUM_SHA256)} {}
    ~Digest () { g_checksum_free (m_sum); }
    Digest (const Digest&) = delete;
    Digest& operator= (const Digest&) = delete;
    void add (const std::string& field)
    {
        auto length {std::to_string (field.size ()) + ":"};
        g_checksum_update (m_sum,
                           reinterpret_cast<const guchar*> (length.data ()),
                           static_cast<gssize> (length.size ()));
        g_checksum_update (m_sum,
                           reinterpret_cast<const guchar*> (field.data ()),
                           static_cast<gssize> (field.size ()));
    }
    std::string etag () const
    {
        return std::string {g_checksum_get_string (m_sum)}.substr (0, 20);
    }

private:
    GChecksum* m_sum;
};

struct TypeName
{
    GNCAccountType type;
    const char* name;
};

const std::array<TypeName, 9> account_types {{
    {ACCT_TYPE_ASSET, "asset"},
    {ACCT_TYPE_BANK, "bank"},
    {ACCT_TYPE_CASH, "cash"},
    {ACCT_TYPE_RECEIVABLE, "receivable"},
    {ACCT_TYPE_PAYABLE, "payable"},
    {ACCT_TYPE_LIABILITY, "liability"},
    {ACCT_TYPE_EQUITY, "equity"},
    {ACCT_TYPE_INCOME, "income"},
    {ACCT_TYPE_EXPENSE, "expense"},
}};

std::string
type_name (GNCAccountType type)
{
    for (const auto& entry : account_types)
        if (entry.type == type)
            return entry.name;
    return "other";
}

std::optional<GNCAccountType>
parse_type (const std::string& name)
{
    for (const auto& entry : account_types)
        if (name == entry.name)
            return entry.type;
    return std::nullopt;
}

bool
is_result_type (GNCAccountType type)
{
    return type == ACCT_TYPE_INCOME || type == ACCT_TYPE_EXPENSE;
}

const char*
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
    case GncSeIssueType::UNLOCKED_BEFORE_LOCK_DATE: return "unposted-before-lock-date";
    case GncSeIssueType::CORRECTION_WITHOUT_ORIGINAL: return "correction-without-original";
    case GncSeIssueType::HISTORY_CHANGED: return "history-changed";
    case GncSeIssueType::PERIOD_MISMATCH: return "period-mismatch";
    }
    return "unknown";
}

/* The engine holds one book per process. */
bool company_is_open = false;

void
start_engine ()
{
    static bool started = false;
    if (!started)
    {
        qof_init ();
        gnc_engine_init (0, nullptr);
        started = true;
    }
    /* The engine's transaction log is a file beside the book; the
     * processing history is what GnuCash SE keeps. */
    xaccLogDisable ();
}

} // namespace

/* --- The company ------------------------------------------------------ */

struct Company::Impl
{
    QofSession* session {};
    QofBook* book {};
    gnc_commodity* currency {};
    int fraction {100};
    bool whole_file {false}; /* XML: stored when the whole file is saved. */
    bool broken {false};
    std::string actor;

    ~Impl ()
    {
        if (!session)
            return;
        if (whole_file && !broken)
            qof_session_save (session, nullptr);
        qof_session_end (session);
        /* Destroys the session, which is the current one. */
        gnc_clear_current_session ();
        xaccLogDisable ();
        company_is_open = false;
    }

    ::Account* root () const { return gnc_book_get_root_account (book); }

    std::string amount (gnc_numeric value) const
    {
        return format_amount (value, fraction);
    }

    ::Account* find_account (const std::string& id) const
    {
        auto guid {parse_id (id)};
        auto found {guid ? xaccAccountLookup (&*guid, book) : nullptr};
        /* Not the root, and not an account of the engine's own trees. */
        if (!found || found == root () || gnc_account_get_root (found) != root ())
            return nullptr;
        return found;
    }

    /* A transaction that is one of the company's vouchers: it has lines,
     * on the company's accounts, and is not voided. */
    bool is_voucher (const Transaction* trans) const
    {
        auto first {xaccTransGetSplit (trans, 0)};
        auto account {first ? xaccSplitGetAccount (first) : nullptr};
        return account && gnc_account_get_root (account) == root () &&
            !xaccTransGetVoidStatus (trans);
    }

    Transaction* find_voucher (const std::string& id) const
    {
        auto guid {parse_id (id)};
        auto found {guid ? xaccTransLookup (&*guid, book) : nullptr};
        return found && is_voucher (found) ? found : nullptr;
    }

    gcse::Account to_account (const ::Account* account) const
    {
        auto parent {gnc_account_get_parent (account)};
        gcse::Account result {
            id_of (xaccAccountGetGUID (account)),
            text (xaccAccountGetCode (account)),
            text (xaccAccountGetName (account)),
            type_name (xaccAccountGetType (account)),
            text (gnc_commodity_get_mnemonic (xaccAccountGetCommodity (account))),
            parent && parent != root () ? id_of (xaccAccountGetGUID (parent)) : "",
            text (xaccAccountGetDescription (account)),
            xaccAccountGetPlaceholder (account) != 0,
            xaccAccountGetHidden (account) != 0,
            ""};
        Digest digest;
        for (const auto& field :
             {result.id, result.number, result.name, result.type,
              result.currency, result.parent, result.description})
            digest.add (field);
        digest.add (result.placeholder ? "1" : "0");
        digest.add (result.hidden ? "1" : "0");
        result.etag = digest.etag ();
        return result;
    }

    std::string series_of (const Transaction* trans) const
    {
        auto series {gnc_se_trans_get_series (trans)};
        return series.empty () ? gnc_se_book_get_default_series (book) : series;
    }

    Voucher to_voucher (const Transaction* trans) const
    {
        Voucher result {};
        result.id = id_of (xaccTransGetGUID (trans));
        result.date = format_date (xaccTransGetDatePostedGDate (trans));
        result.text = text (xaccTransGetDescription (trans));
        result.series = series_of (trans);
        result.posted = gnc_se_trans_is_locked (trans) != 0;
        result.period = gnc_se_trans_get_period (trans);
        result.number = gnc_se_trans_get_number (trans);
        result.voucher = gnc_se_trans_get_voucher (trans);
        result.entered = xaccTransGetDateEntered (trans);
        result.sequence = gnc_se_trans_get_seq (trans);
        /* In the order the lines were entered; lines without a place
         * (made by the engine, as in a correcting entry) after them, as
         * the engine has them. */
        std::vector<const Split*> splits;
        for (auto node = xaccTransGetSplitList (trans); node; node = node->next)
            splits.push_back (GNC_SPLIT (node->data));
        std::stable_sort (splits.begin (), splits.end (),
                          [] (const Split* a, const Split* b)
                          {
                              auto la {gnc_se_split_get_line (a)};
                              auto lb {gnc_se_split_get_line (b)};
                              return la > 0 && (lb <= 0 || la < lb);
                          });
        for (auto split : splits)
        {
            auto account {xaccSplitGetAccount (split)};
            bool foreign {account && !gnc_commodity_equal (
                    xaccAccountGetCommodity (account), currency)};
            result.lines.push_back (
                {account ? id_of (xaccAccountGetGUID (account)) : "",
                 amount (xaccSplitGetValue (split)),
                 foreign ? format_amount (
                               xaccSplitGetAmount (split),
                               gnc_commodity_get_fraction (
                                   xaccAccountGetCommodity (account)))
                         : "",
                 text (xaccSplitGetMemo (split)),
                 account ? text (xaccAccountGetCode (account)) : "",
                 account ? text (xaccAccountGetName (account)) : ""});
        }
        auto link {[] (const Transaction* other) {
            return other ? id_of (xaccTransGetGUID (other)) : std::string {};
        }};
        result.corrects = link (gnc_se_trans_get_corrected (trans));
        result.corrected_by = link (gnc_se_trans_get_correction (trans));
        result.replaces = link (gnc_se_trans_get_replaced (trans));
        result.signature = gnc_se_trans_get_signature (trans);
        result.reason = gnc_se_trans_get_reason (trans);

        Digest digest;
        for (const auto& field :
             {result.id, result.date, result.text, result.series,
              result.voucher, result.corrects, result.corrected_by,
              result.replaces, result.signature, result.reason})
            digest.add (field);
        for (const auto& line : result.lines)
            for (const auto& field :
                 {line.account, line.amount, line.foreign_amount, line.text})
                digest.add (field);
        result.etag = digest.etag ();
        return result;
    }

    FiscalYear to_year (const GncSeFiscalYear& year) const
    {
        return {year.key, format_date (year.start), format_date (year.end),
                year.stored, year.closed, year.closed_by};
    }

    /* A voucher as it is to be written, checked. */
    struct Line
    {
        ::Account* account;
        gnc_numeric value;  /* In the company's currency. */
        gnc_numeric amount; /* In the account's currency. */
        std::string text;
    };
    struct Checked
    {
        GDate date;
        std::string text;
        std::string series; /* Empty: the default. */
        std::vector<Line> lines;
    };

    Result<Checked> check (const NewVoucher& voucher) const
    {
        Checked checked {};
        auto date {parse_date (voucher.date)};
        if (!date)
            return refuse ("date.invalid", "The date is not a date.", "date");
        if (gnc_se_date_is_locked (book, *date))
            return refuse ("voucher.date-locked",
                           "The date is in a period that is posted.", "date");
        checked.date = *date;
        checked.text = voucher.text;

        if (!voucher.series.empty () &&
            voucher.series != gnc_se_book_get_default_series (book))
        {
            auto all {gnc_se_book_get_series (book)};
            if (std::none_of (all.begin (), all.end (),
                              [&] (const GncSeSeries& series)
                              { return series.code == voucher.series; }))
                return refuse ("voucher.unknown-series",
                               "The company has no such voucher series.",
                               "series");
            checked.series = voucher.series;
        }

        if (voucher.lines.size () < 2)
            return refuse ("voucher.too-few-lines",
                           "A voucher has at least two lines.", "lines");
        auto sum {gnc_numeric_zero ()};
        for (size_t i = 0; i < voucher.lines.size (); ++i)
        {
            const auto& line {voucher.lines[i]};
            auto field {"lines[" + std::to_string (i) + "]"};
            auto account {find_account (line.account)};
            if (!account)
                return refuse ("account.not-found", "There is no such account.",
                               field + ".account");
            if (xaccAccountGetPlaceholder (account))
                return refuse ("voucher.heading-account",
                               "The account is a heading and takes no vouchers.",
                               field + ".account");
            auto value {parse_amount (line.amount)};
            if (!value)
                return refuse ("amount.invalid", "The amount is not a number.",
                               field + ".amount");
            if (gnc_numeric_zero_p (*value))
                return refuse ("voucher.zero-line", "A line has no amount.",
                               field + ".amount");
            auto amount {*value};
            auto commodity {xaccAccountGetCommodity (account)};
            if (!gnc_commodity_equal (commodity, currency))
            {
                auto foreign {parse_amount (line.foreign_amount)};
                if (!foreign || gnc_numeric_zero_p (*foreign))
                    return refuse ("voucher.foreign-amount-missing",
                                   "The account is in another currency; the "
                                   "amount in that currency is needed.",
                                   field + ".foreign_amount");
                if (gnc_numeric_negative_p (*foreign) !=
                    gnc_numeric_negative_p (*value))
                    return refuse ("voucher.foreign-amount-sign",
                                   "The two amounts of a line have the same "
                                   "sign.", field + ".foreign_amount");
                amount = gnc_numeric_convert (
                    *foreign, gnc_commodity_get_fraction (commodity),
                    GNC_HOW_RND_ROUND_HALF_UP);
            }
            else if (!line.foreign_amount.empty ())
                return refuse ("voucher.foreign-amount-unexpected",
                               "The account is in the company's currency.",
                               field + ".foreign_amount");
            auto fixed {gnc_numeric_convert (*value, fraction,
                                             GNC_HOW_RND_ROUND_HALF_UP)};
            if (!gnc_numeric_equal (fixed, *value))
                return refuse ("amount.too-many-decimals",
                               "The amount has more decimals than the currency.",
                               field + ".amount");
            if (gnc_commodity_equal (commodity, currency))
                amount = fixed;
            sum = gnc_numeric_add_fixed (sum, fixed);
            checked.lines.push_back ({account, fixed, amount, line.text});
        }
        if (!gnc_numeric_zero_p (sum))
            return refuse ("voucher.unbalanced",
                           "Debit and credit differ by " + amount (sum) + ".",
                           "lines");
        return checked;
    }

    void write_lines (Transaction* trans, const Checked& checked) const
    {
        xaccTransSetCurrency (trans, currency);
        xaccTransSetDatePostedGDate (trans, checked.date);
        xaccTransSetDescription (trans, checked.text.c_str ());
        int64_t place = 0;
        for (const auto& line : checked.lines)
        {
            auto split {xaccMallocSplit (book)};
            xaccSplitSetParent (split, trans);
            gnc_se_split_set_line (split, ++place);
            xaccSplitSetAccount (split, line.account);
            xaccSplitSetAmount (split, line.amount);
            xaccSplitSetValue (split, line.value);
            xaccSplitSetMemo (split, line.text.c_str ());
        }
    }

    /* Runs a command as one unit in the storage. */
    template <typename Work> auto run (Work&& work) -> decltype (work ())
    {
        if (broken)
            return refuse ("reload-required",
                           "An earlier command failed halfway; the company "
                           "has to be loaded again.");
        qof_session_pop_error (session);
        gnc_se_set_actor (actor);
        if (!gnc_se_command_begin (book))
        {
            gnc_se_set_actor ("");
            return refuse ("storage.failed", "The storage does not answer.");
        }
        auto result {work ()};
        gnc_se_set_actor ("");
        auto error {qof_session_pop_error (session)};
        if (error != ERR_BACKEND_NO_ERR)
        {
            /* What is in memory is no longer what is stored. */
            gnc_se_command_rollback (book);
            broken = true;
            return refuse ("storage.failed",
                           "The change could not be stored (error " +
                           std::to_string (static_cast<int> (error)) + ").");
        }
        if (!gnc_se_command_commit (book))
        {
            broken = true;
            return refuse ("storage.failed", "The change could not be stored.");
        }
        return result;
    }

    std::vector<Transaction*> all_vouchers () const
    {
        struct Collect
        {
            const Impl* impl;
            std::vector<Transaction*> found;
        } collect {this, {}};
        qof_collection_foreach (
            qof_book_get_collection (book, GNC_ID_TRANS),
            [] (QofInstance* instance, gpointer data)
            {
                auto into {static_cast<Collect*> (data)};
                auto trans {GNC_TRANSACTION (instance)};
                if (into->impl->is_voucher (trans))
                    into->found.push_back (trans);
            },
            &collect);
        return collect.found;
    }
};

Company::Company (std::unique_ptr<Impl> impl) : m {std::move (impl)} {}
Company::~Company () = default;

Result<std::unique_ptr<Company>>
Company::create (const std::string& uri, const NewCompany& company)
{
    if (company_is_open)
        return refuse ("process.busy",
                       "A company is already open in this process.");
    if (company.name.empty ())
        return refuse ("company.name-missing", "The company has no name.",
                       "name");
    start_engine ();

    auto impl {std::make_unique<Impl> ()};
    impl->session = qof_session_new (qof_book_new ());
    impl->book = qof_session_get_book (impl->session);
    /* From here on the session is the engine's current one, so that the
     * Impl's destructor can let go of it. */
    gnc_set_current_session (impl->session);
    company_is_open = true;

    impl->currency = gnc_commodity_table_lookup (
        gnc_commodity_table_get_table (impl->book), GNC_COMMODITY_NS_CURRENCY,
        company.currency.c_str ());
    if (!impl->currency)
        return refuse ("currency.unknown", "There is no such currency.",
                       "currency");
    impl->fraction = gnc_commodity_get_fraction (impl->currency);
    if (!gnc_se_book_set_fiscal_year_start (impl->book,
                                            company.fiscal_year_start_month,
                                            company.fiscal_year_start_day))
        return refuse ("fiscal-year.start-invalid",
                       "A fiscal year starts on the first day of a month.",
                       "fiscal_year_start_month");
    (void)impl->root ();
    gnc_se_book_set_setting (impl->book, "company-name", company.name);
    gnc_se_book_set_setting (impl->book, "organisation-number",
                             company.organisation_number);
    gnc_se_book_set_setting (impl->book, "currency", company.currency);

    qof_session_begin (impl->session, uri.c_str (), SESSION_NEW_STORE);
    auto error {qof_session_pop_error (impl->session)};
    if (error == ERR_BACKEND_STORE_EXISTS)
        return refuse ("storage.exists", "There is already a book there.");
    if (error != ERR_BACKEND_NO_ERR)
        return refuse ("storage.failed",
                       "The storage could not be created (error " +
                       std::to_string (static_cast<int> (error)) + ").");
    qof_session_save (impl->session, nullptr);
    error = qof_session_pop_error (impl->session);
    if (error != ERR_BACKEND_NO_ERR)
        return refuse ("storage.failed",
                       "The company could not be stored (error " +
                       std::to_string (static_cast<int> (error)) + ").");
    /* The company is closed and opened again. The SQL backend goes on
     * treating what it has just written for the first time as new, and
     * would insert it once more at the next change; what has been loaded
     * it updates. A loaded book is also what the server always runs on. */
    impl.reset ();
    return open (uri);
}

Result<std::unique_ptr<Company>>
Company::open (const std::string& uri)
{
    if (company_is_open)
        return refuse ("process.busy",
                       "A company is already open in this process.");
    start_engine ();

    auto impl {std::make_unique<Impl> ()};
    impl->session = qof_session_new (qof_book_new ());
    gnc_set_current_session (impl->session);
    company_is_open = true;

    qof_session_begin (impl->session, uri.c_str (), SESSION_NORMAL_OPEN);
    auto error {qof_session_pop_error (impl->session)};
    if (error == ERR_BACKEND_LOCKED)
        return refuse ("storage.locked",
                       "The company is open somewhere else.");
    if (error == ERR_BACKEND_NO_SUCH_DB || error == ERR_FILEIO_FILE_NOT_FOUND)
        return refuse ("storage.not-found", "There is no company there.");
    if (error != ERR_BACKEND_NO_ERR)
        return refuse ("storage.failed",
                       "The storage could not be opened (error " +
                       std::to_string (static_cast<int> (error)) + ").");
    qof_session_load (impl->session, nullptr);
    error = qof_session_pop_error (impl->session);
    if (error != ERR_BACKEND_NO_ERR)
        return refuse ("storage.failed",
                       "The company could not be loaded (error " +
                       std::to_string (static_cast<int> (error)) + ").");
    impl->book = qof_session_get_book (impl->session);
    if (!gnc_se_book_is_se (impl->book))
        return refuse ("company.not-gnucash-se",
                       "The book is not kept in GnuCash SE.");
    impl->currency = gnc_commodity_table_lookup (
        gnc_commodity_table_get_table (impl->book), GNC_COMMODITY_NS_CURRENCY,
        gnc_se_book_get_setting (impl->book, "currency").c_str ());
    if (!impl->currency)
        return refuse ("company.currency-missing",
                       "The company has no currency.");
    impl->fraction = gnc_commodity_get_fraction (impl->currency);
    impl->whole_file = gnc_se_book_get_history_store (impl->book) == nullptr;
    /* A new version of the program, and what has changed elsewhere, go
     * into the processing history. */
    gnc_se_command_begin (impl->book);
    gnc_se_history_sync (impl->book);
    gnc_se_command_commit (impl->book);
    return std::unique_ptr<Company> {new Company {std::move (impl)}};
}

void
Company::set_actor (const std::string& actor)
{
    m->actor = actor;
}

CompanyInfo
Company::info () const
{
    auto lock_date {gnc_se_book_get_lock_date (m->book)};
    auto [month, day] = gnc_se_book_get_fiscal_year_start (m->book);
    return {id_of (qof_book_get_guid (m->book)),
            gnc_se_book_get_setting (m->book, "company-name"),
            gnc_se_book_get_setting (m->book, "organisation-number"),
            text (gnc_commodity_get_mnemonic (m->currency)),
            lock_date ? format_date (*lock_date) : "", month, day};
}

/* --- Accounts --------------------------------------------------------- */

std::vector<Account>
Company::accounts () const
{
    std::vector<Account> result;
    auto list {gnc_account_get_descendants_sorted (m->root ())};
    for (auto node = list; node; node = node->next)
        result.push_back (m->to_account (static_cast<::Account*> (node->data)));
    g_list_free (list);
    return result;
}

Result<Account>
Company::account (const std::string& id) const
{
    auto found {m->find_account (id)};
    if (!found)
        return refuse ("account.not-found", "There is no such account.");
    return m->to_account (found);
}

Result<Account>
Company::create_account (const NewAccount& account)
{
    if (account.name.empty ())
        return refuse ("account.name-missing", "The account has no name.",
                       "name");
    auto type {parse_type (account.type)};
    if (!type)
        return refuse ("account.type-unknown", "There is no such account type.",
                       "type");
    if (!account.number.empty () &&
        gnc_account_lookup_by_code (m->root (), account.number.c_str ()))
        return refuse ("account.number-taken",
                       "Another account has that number.", "number");
    auto parent {m->root ()};
    if (!account.parent.empty () && !(parent = m->find_account (account.parent)))
        return refuse ("account.parent-not-found",
                       "There is no such account to put it under.", "parent");
    auto commodity {m->currency};
    if (!account.currency.empty () &&
        !(commodity = gnc_commodity_table_lookup (
              gnc_commodity_table_get_table (m->book),
              GNC_COMMODITY_NS_CURRENCY, account.currency.c_str ())))
        return refuse ("currency.unknown", "There is no such currency.",
                       "currency");

    return m->run ([&] () -> Result<Account> {
        auto created {xaccMallocAccount (m->book)};
        xaccAccountBeginEdit (created);
        xaccAccountSetCode (created, account.number.c_str ());
        xaccAccountSetName (created, account.name.c_str ());
        xaccAccountSetType (created, *type);
        xaccAccountSetCommodity (created, commodity);
        xaccAccountSetDescription (created, account.description.c_str ());
        xaccAccountSetPlaceholder (created, account.placeholder);
        gnc_account_append_child (parent, created);
        xaccAccountCommitEdit (created);
        return m->to_account (created);
    });
}

Result<Account>
Company::update_account (const std::string& id, const AccountChange& change,
                         const std::string& etag)
{
    auto found {m->find_account (id)};
    if (!found)
        return refuse ("account.not-found", "There is no such account.");
    if (m->to_account (found).etag != etag)
        return refuse ("conflict", "The account has been changed by someone "
                                   "else.");
    if (change.name && change.name->empty ())
        return refuse ("account.name-missing", "The account has no name.",
                       "name");
    std::optional<GNCAccountType> type;
    if (change.type)
    {
        type = parse_type (*change.type);
        if (!type)
            return refuse ("account.type-unknown",
                           "There is no such account type.", "type");
        if (*type != xaccAccountGetType (found) &&
            xaccAccountGetSplitsSize (found) > 0)
            return refuse ("account.in-use",
                           "The type of an account with vouchers cannot be "
                           "changed.", "type");
    }
    if (change.number && !change.number->empty ())
    {
        auto other {gnc_account_lookup_by_code (m->root (),
                                                change.number->c_str ())};
        if (other && other != found)
            return refuse ("account.number-taken",
                           "Another account has that number.", "number");
    }
    ::Account* parent = nullptr;
    if (change.parent)
    {
        parent = change.parent->empty () ? m->root ()
                                         : m->find_account (*change.parent);
        if (!parent)
            return refuse ("account.parent-not-found",
                           "There is no such account to put it under.",
                           "parent");
        for (auto above = parent; above; above = gnc_account_get_parent (above))
            if (above == found)
                return refuse ("account.parent-invalid",
                               "An account cannot be put under itself.",
                               "parent");
    }
    if (change.placeholder && *change.placeholder &&
        xaccAccountGetSplitsSize (found) > 0)
        return refuse ("account.in-use",
                       "An account with vouchers cannot become a heading.",
                       "placeholder");

    return m->run ([&] () -> Result<Account> {
        xaccAccountBeginEdit (found);
        if (change.number)
            xaccAccountSetCode (found, change.number->c_str ());
        if (change.name)
            xaccAccountSetName (found, change.name->c_str ());
        if (type)
            xaccAccountSetType (found, *type);
        if (change.description)
            xaccAccountSetDescription (found, change.description->c_str ());
        if (change.placeholder)
            xaccAccountSetPlaceholder (found, *change.placeholder);
        if (change.hidden)
            xaccAccountSetHidden (found, *change.hidden);
        if (parent && parent != gnc_account_get_parent (found))
            gnc_account_append_child (parent, found);
        xaccAccountCommitEdit (found);
        return m->to_account (found);
    });
}

Result<void>
Company::delete_account (const std::string& id, const std::string& etag)
{
    auto found {m->find_account (id)};
    if (!found)
        return refuse ("account.not-found", "There is no such account.");
    if (m->to_account (found).etag != etag)
        return refuse ("conflict", "The account has been changed by someone "
                                   "else.");
    if (gnc_account_n_children (found) > 0)
        return refuse ("account.has-children",
                       "The account has accounts under it.");
    if (xaccAccountGetSplitsSize (found) > 0)
        return refuse ("account.in-use", "The account has vouchers.");

    return m->run ([&] () -> Result<void> {
        xaccAccountBeginEdit (found);
        xaccAccountDestroy (found);
        return {};
    });
}

/* --- Vouchers --------------------------------------------------------- */

Result<Voucher>
Company::create_voucher (const NewVoucher& voucher)
{
    auto checked {m->check (voucher)};
    if (!checked)
        return std::unexpected (checked.error ());

    return m->run ([&] () -> Result<Voucher> {
        auto trans {xaccMallocTransaction (m->book)};
        xaccTransBeginEdit (trans);
        xaccTransSetDateEnteredSecs (trans, gnc_time (nullptr));
        m->write_lines (trans, *checked);
        xaccTransCommitEdit (trans);
        if (!checked->series.empty ())
            gnc_se_trans_set_series (trans, checked->series);
        return m->to_voucher (trans);
    });
}

Result<Voucher>
Company::voucher (const std::string& id) const
{
    auto found {m->find_voucher (id)};
    if (!found)
        return refuse ("voucher.not-found", "There is no such voucher.");
    return m->to_voucher (found);
}

Result<Page<Voucher>>
Company::vouchers (const VoucherQuery& query) const
{
    std::optional<GDate> from, to;
    if (!query.from.empty () && !(from = parse_date (query.from)))
        return refuse ("date.invalid", "The date is not a date.", "from");
    if (!query.to.empty () && !(to = parse_date (query.to)))
        return refuse ("date.invalid", "The date is not a date.", "to");
    ::Account* account = nullptr;
    if (!query.account.empty () && !(account = m->find_account (query.account)))
        return refuse ("account.not-found", "There is no such account.",
                       "account");
    /* The cursor is the place of the last voucher of the page before:
     * its date, its place in the order of entry and its identity. */
    using Key = std::tuple<time64, int64_t, std::string>;
    std::optional<Key> after;
    if (!query.after.empty ())
    {
        long long posted = 0, sequence = 0;
        char id[GUID_ENCODING_LENGTH + 1] = "";
        if (std::sscanf (query.after.c_str (), "%lld.%lld.%32s", &posted,
                         &sequence, id) != 3)
            return refuse ("cursor.invalid", "The cursor is not one of ours.",
                           "after");
        after = Key {posted, sequence, id};
    }
    auto key_of {[] (const Transaction* trans) {
        return Key {xaccTransRetDatePosted (trans), gnc_se_trans_get_seq (trans),
                    id_of (xaccTransGetGUID (trans))};
    }};

    std::vector<std::pair<Key, Transaction*>> selected;
    for (auto trans : m->all_vouchers ())
    {
        auto posted {xaccTransGetDatePostedGDate (trans)};
        if ((from && g_date_compare (&posted, &*from) < 0) ||
            (to && g_date_compare (&posted, &*to) > 0))
            continue;
        if (query.posted &&
            *query.posted != (gnc_se_trans_is_locked (trans) != 0))
            continue;
        if (!query.series.empty () && m->series_of (trans) != query.series)
            continue;
        if (account && !xaccTransFindSplitByAccount (trans, account))
            continue;
        auto key {key_of (trans)};
        if (after && key <= *after)
            continue;
        selected.emplace_back (std::move (key), trans);
    }
    std::sort (selected.begin (), selected.end ());

    auto limit {static_cast<size_t> (std::clamp (query.limit, 1, 1000))};
    Page<Voucher> page;
    for (size_t i = 0; i < selected.size () && i < limit; ++i)
        page.items.push_back (m->to_voucher (selected[i].second));
    if (selected.size () > limit)
    {
        const auto& last {selected[limit - 1].first};
        page.next = std::to_string (std::get<0> (last)) + "." +
            std::to_string (std::get<1> (last)) + "." + std::get<2> (last);
    }
    return page;
}

Result<Voucher>
Company::update_voucher (const std::string& id, const NewVoucher& voucher,
                         const std::string& etag)
{
    auto found {m->find_voucher (id)};
    if (!found)
        return refuse ("voucher.not-found", "There is no such voucher.");
    if (gnc_se_trans_is_locked (found))
        return refuse ("voucher.posted",
                       "A posted voucher cannot be changed. Correct it with a "
                       "correcting entry.");
    if (m->to_voucher (found).etag != etag)
        return refuse ("conflict", "The voucher has been changed by someone "
                                   "else.");
    auto checked {m->check (voucher)};
    if (!checked)
        return std::unexpected (checked.error ());

    return m->run ([&] () -> Result<Voucher> {
        xaccTransBeginEdit (found);
        std::vector<Split*> old;
        for (auto node = xaccTransGetSplitList (found); node; node = node->next)
            old.push_back (GNC_SPLIT (node->data));
        for (auto split : old)
            xaccSplitDestroy (split);
        m->write_lines (found, *checked);
        xaccTransCommitEdit (found);
        if (!gnc_se_last_rejection ().empty ())
            return refuse ("voucher.refused", gnc_se_last_rejection ());
        auto series {checked->series.empty ()
                         ? gnc_se_book_get_default_series (m->book)
                         : checked->series};
        if (series != m->series_of (found))
            gnc_se_trans_set_series (found, series);
        return m->to_voucher (found);
    });
}

Result<void>
Company::delete_voucher (const std::string& id, const std::string& etag)
{
    auto found {m->find_voucher (id)};
    if (!found)
        return refuse ("voucher.not-found", "There is no such voucher.");
    if (gnc_se_trans_is_locked (found))
        return refuse ("voucher.posted",
                       "A posted voucher cannot be deleted. Correct it with a "
                       "correcting entry.");
    if (m->to_voucher (found).etag != etag)
        return refuse ("conflict", "The voucher has been changed by someone "
                                   "else.");

    return m->run ([&] () -> Result<void> {
        xaccTransBeginEdit (found);
        xaccTransDestroy (found);
        xaccTransCommitEdit (found);
        return {};
    });
}

/* --- Posting, correcting, fiscal years -------------------------------- */

Result<LockPlan>
Company::plan_lock (const std::string& date) const
{
    auto through {parse_date (date)};
    if (!through)
        return refuse ("date.invalid", "The date is not a date.", "date");
    auto plan {gnc_se_plan_lock (m->book, *through)};
    if (!plan.ok)
        return refuse ("lock.refused", plan.error);
    LockPlan result {date, plan.total, {}};
    for (const auto& group : plan.groups)
        result.groups.push_back (
            {group.series, group.period, group.count, group.first_number});
    return result;
}

Result<LockOutcome>
Company::lock_through (const std::string& date)
{
    auto through {parse_date (date)};
    if (!through)
        return refuse ("date.invalid", "The date is not a date.", "date");
    /* Whatever would stop the lock is found before anything is changed. */
    auto plan {gnc_se_plan_lock (m->book, *through)};
    if (!plan.ok)
        return refuse ("lock.refused", plan.error);

    return m->run ([&] () -> Result<LockOutcome> {
        auto result {gnc_se_lock_through (m->book, *through, m->actor)};
        if (!result.ok)
            return refuse ("lock.refused", result.error);
        return LockOutcome {date, result.locked};
    });
}

Overdue
Company::overdue () const
{
    GDate today;
    gnc_gdate_set_today (&today);
    auto found {gnc_se_overdue (m->book, today)};
    return {found.cash, found.other,
            found.oldest ? format_date (*found.oldest) : ""};
}

Result<Correction>
Company::correct (const std::string& id, const CorrectionRequest& request)
{
    auto found {m->find_voucher (id)};
    if (!found)
        return refuse ("voucher.not-found", "There is no such voucher.");
    auto date {parse_date (request.date)};
    if (!date)
        return refuse ("date.invalid", "The date is not a date.", "date");
    if (!gnc_se_trans_is_locked (found))
        return refuse ("voucher.not-posted",
                       "Only a posted voucher is corrected with a correcting "
                       "entry.");
    if (gnc_se_trans_get_correction (found))
        return refuse ("voucher.already-corrected",
                       "The voucher already has a correcting entry.");
    if (gnc_se_date_is_locked (m->book, *date))
        return refuse ("voucher.date-locked",
                       "The date is in a period that is posted.", "date");

    return m->run ([&] () -> Result<Correction> {
        auto made {gnc_se_correct (found, *date, m->actor, request.reason,
                                   request.with_replacement)};
        if (!made.ok)
            return refuse ("correction.refused", made.error);
        Correction result {m->to_voucher (made.correction), std::nullopt};
        if (made.replacement)
            result.replacement = m->to_voucher (made.replacement);
        return result;
    });
}

std::vector<FiscalYear>
Company::fiscal_years () const
{
    std::vector<FiscalYear> result;
    for (const auto& year : gnc_se_book_get_fiscal_years (m->book))
        result.push_back (m->to_year (year));
    return result;
}

Result<FiscalYear>
Company::define_fiscal_year (const std::string& start, const std::string& end)
{
    auto first {parse_date (start)};
    auto last {parse_date (end)};
    if (!first)
        return refuse ("date.invalid", "The date is not a date.", "start");
    if (!last)
        return refuse ("date.invalid", "The date is not a date.", "end");

    return m->run ([&] () -> Result<FiscalYear> {
        auto defined {gnc_se_book_define_fiscal_year (m->book, *first, *last)};
        if (!defined.ok)
            return refuse ("fiscal-year.refused", defined.error);
        return m->to_year (gnc_se_book_get_fiscal_year (m->book, *first));
    });
}

Result<LockOutcome>
Company::close_fiscal_year (const std::string& date)
{
    auto within {parse_date (date)};
    if (!within)
        return refuse ("date.invalid", "The date is not a date.", "date");

    return m->run ([&] () -> Result<LockOutcome> {
        auto result {gnc_se_close_fiscal_year (m->book, *within, m->actor)};
        if (!result.ok)
            return refuse ("fiscal-year.refused", result.error);
        auto year {gnc_se_book_get_fiscal_year (m->book, *within)};
        return LockOutcome {format_date (year.end), result.locked};
    });
}

Result<Voucher>
Company::result_voucher (const std::string& date,
                         const std::string& result_account,
                         const std::string& equity_account)
{
    auto within {parse_date (date)};
    if (!within)
        return refuse ("date.invalid", "The date is not a date.", "date");
    auto result {m->find_account (result_account)};
    auto equity {m->find_account (equity_account)};
    if (!result || !is_result_type (xaccAccountGetType (result)))
        return refuse ("result.account-invalid",
                       "The result is entered on an income or expense account.",
                       "result_account");
    if (!equity || xaccAccountGetType (equity) != ACCT_TYPE_EQUITY)
        return refuse ("result.account-invalid",
                       "The result is carried to an equity account.",
                       "equity_account");

    auto year {gnc_se_book_get_fiscal_year (m->book, *within)};
    auto from {gnc_time64_get_day_start_gdate (&year.start)};
    auto to {gnc_time64_get_day_end_gdate (&year.end)};
    /* Income is credit and expenses debit, so the sum of all income and
     * expense accounts is the loss; its negation is the profit. */
    auto sum {gnc_numeric_zero ()};
    for (auto trans : m->all_vouchers ())
    {
        auto posted {xaccTransRetDatePosted (trans)};
        if (posted < from || posted > to)
            continue;
        for (auto node = xaccTransGetSplitList (trans); node; node = node->next)
        {
            auto split {GNC_SPLIT (node->data)};
            if (is_result_type (xaccAccountGetType (xaccSplitGetAccount (split))))
                sum = gnc_numeric_add_fixed (sum, xaccSplitGetValue (split));
        }
    }
    if (gnc_numeric_zero_p (sum))
        return refuse ("result.nothing-to-enter",
                       "The year's income and expenses already sum to zero.");

    NewVoucher voucher {format_date (year.end), "Årets resultat " + year.key, "",
                        {{result_account, m->amount (gnc_numeric_neg (sum)), "",
                          "", "", ""},
                         {equity_account, m->amount (sum), "", "", "", ""}}};
    return create_voucher (voucher);
}

/* --- History and checks ----------------------------------------------- */

Page<Event>
Company::history (int limit, const std::string& after) const
{
    auto from {after.empty () ? 0 : g_ascii_strtoll (after.c_str (), nullptr, 10)};
    auto count {static_cast<size_t> (std::clamp (limit, 1, 1000))};
    Page<Event> page;
    for (const auto& event : gnc_se_book_get_history (m->book))
    {
        if (event.number <= from)
            continue;
        if (page.items.size () == count)
        {
            page.next = std::to_string (page.items.back ().number);
            break;
        }
        page.items.push_back ({event.number, event.time, event.type,
                               event.signature, event.text});
    }
    return page;
}

std::vector<Issue>
Company::verify () const
{
    std::vector<Issue> result;
    for (const auto& issue : gnc_se_verify (m->book))
        result.push_back ({issue_code (issue.type), issue.series, issue.period,
                           issue.number, id_of (&issue.guid)});
    return result;
}

/* --- Reports ---------------------------------------------------------- */

Result<TrialBalance>
Company::trial_balance (const std::string& from, const std::string& to) const
{
    auto first {parse_date (from)};
    auto last {parse_date (to)};
    if (!first)
        return refuse ("date.invalid", "The date is not a date.", "from");
    if (!last || g_date_compare (&*last, &*first) < 0)
        return refuse ("date.invalid", "The period ends before it begins.",
                       "to");
    auto start {gnc_time64_get_day_start_gdate (&*first)};
    auto end {gnc_time64_get_day_end_gdate (&*last)};
    auto year {gnc_se_book_get_fiscal_year (m->book, *first)};
    auto year_start {gnc_time64_get_day_start_gdate (&year.start)};

    TrialBalance result {from, to, text (gnc_commodity_get_mnemonic (m->currency)),
                         {}, "", ""};
    auto total_debit {gnc_numeric_zero ()};
    auto total_credit {gnc_numeric_zero ()};
    auto list {gnc_account_get_descendants_sorted (m->root ())};
    for (auto node = list; node; node = node->next)
    {
        auto account {static_cast<::Account*> (node->data)};
        bool result_account {is_result_type (xaccAccountGetType (account))};
        auto opening {gnc_numeric_zero ()};
        auto debit {gnc_numeric_zero ()};
        auto credit {gnc_numeric_zero ()};
        bool any = false;
        for (auto split : xaccAccountGetSplits (account))
        {
            auto trans {xaccSplitGetParent (split)};
            if (!trans || xaccTransGetVoidStatus (trans))
                continue;
            auto posted {xaccTransRetDatePosted (trans)};
            auto value {xaccSplitGetValue (split)};
            if (posted > end)
                continue;
            if (posted < start)
            {
                if (!result_account || posted >= year_start)
                {
                    opening = gnc_numeric_add_fixed (opening, value);
                    any = true;
                }
                continue;
            }
            any = true;
            if (gnc_numeric_negative_p (value))
                credit = gnc_numeric_add_fixed (credit, gnc_numeric_neg (value));
            else
                debit = gnc_numeric_add_fixed (debit, value);
        }
        if (!any)
            continue;
        total_debit = gnc_numeric_add_fixed (total_debit, debit);
        total_credit = gnc_numeric_add_fixed (total_credit, credit);
        auto closing {gnc_numeric_sub_fixed (
            gnc_numeric_add_fixed (opening, debit), credit)};
        result.rows.push_back (
            {id_of (xaccAccountGetGUID (account)),
             text (xaccAccountGetCode (account)),
             text (xaccAccountGetName (account)),
             type_name (xaccAccountGetType (account)), m->amount (opening),
             m->amount (debit), m->amount (credit), m->amount (closing)});
    }
    g_list_free (list);
    result.debit = m->amount (total_debit);
    result.credit = m->amount (total_credit);
    return result;
}

Result<Ledger>
Company::ledger (const std::string& account, const std::string& from,
                 const std::string& to) const
{
    auto found {m->find_account (account)};
    if (!found)
        return refuse ("account.not-found", "There is no such account.",
                       "account");
    auto first {parse_date (from)};
    auto last {parse_date (to)};
    if (!first)
        return refuse ("date.invalid", "The date is not a date.", "from");
    if (!last || g_date_compare (&*last, &*first) < 0)
        return refuse ("date.invalid", "The period ends before it begins.",
                       "to");
    auto start {gnc_time64_get_day_start_gdate (&*first)};
    auto end {gnc_time64_get_day_end_gdate (&*last)};
    bool result_account {is_result_type (xaccAccountGetType (found))};
    auto year {gnc_se_book_get_fiscal_year (m->book, *first)};
    auto year_start {gnc_time64_get_day_start_gdate (&year.start)};

    Ledger result {m->to_account (found), from, to, "", "", {}};
    auto balance {gnc_numeric_zero ()};
    std::vector<const Split*> period;
    for (auto split : xaccAccountGetSplits (found))
    {
        auto trans {xaccSplitGetParent (split)};
        if (!trans || xaccTransGetVoidStatus (trans))
            continue;
        auto posted {xaccTransRetDatePosted (trans)};
        if (posted > end)
            continue;
        if (posted < start)
        {
            if (!result_account || posted >= year_start)
                balance = gnc_numeric_add_fixed (balance,
                                                 xaccSplitGetValue (split));
            continue;
        }
        period.push_back (split);
    }
    /* By date, and within a date in the order of entry. */
    std::stable_sort (period.begin (), period.end (),
                      [] (const Split* a, const Split* b)
                      {
                          auto ta {xaccSplitGetParent (a)};
                          auto tb {xaccSplitGetParent (b)};
                          return std::make_pair (xaccTransRetDatePosted (ta),
                                                 gnc_se_trans_get_seq (ta)) <
                                 std::make_pair (xaccTransRetDatePosted (tb),
                                                 gnc_se_trans_get_seq (tb));
                      });
    result.opening = m->amount (balance);
    for (auto split : period)
    {
        auto trans {xaccSplitGetParent (split)};
        auto value {xaccSplitGetValue (split)};
        balance = gnc_numeric_add_fixed (balance, value);
        auto memo {text (xaccSplitGetMemo (split))};
        result.lines.push_back (
            {id_of (xaccTransGetGUID (trans)), gnc_se_trans_get_voucher (trans),
             format_date (xaccTransGetDatePostedGDate (trans)),
             memo.empty () ? text (xaccTransGetDescription (trans)) : memo,
             m->amount (value), m->amount (balance)});
    }
    result.closing = m->amount (balance);
    return result;
}

} // namespace gcse
