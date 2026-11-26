/********************************************************************\
 * gnc-plugin-se.cpp -- GnuCash SE: the Posting menu                *
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

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <string>
#include <vector>

#include "gnc-plugin-se.h"

#include <gnc-se.hpp>

#include "dialog-utils.h"
#include "gnc-component-manager.h"
#include "gnc-date-edit.h"
#include "gnc-file.h"
#include "gnc-hooks.h"
#include "gnc-main-window.h"
#include "gnc-ui.h"
#include "gnc-ui-util.h"
#include "qof.h"

#define PLUGIN_ACTIONS_NAME "gnc-plugin-se-actions"
#define PLUGIN_UI_FILENAME  "gnc-plugin-se.ui"

static QofLogModule log_module = GNC_MOD_GUI;

static void gnc_plugin_se_cmd_lock (GSimpleAction* simple, GVariant* parameter,
                                    gpointer user_data);
static void gnc_plugin_se_cmd_verify (GSimpleAction* simple,
                                      GVariant* parameter, gpointer user_data);
static void gnc_plugin_se_cmd_settings (GSimpleAction* simple,
                                        GVariant* parameter,
                                        gpointer user_data);
static void gnc_plugin_se_cmd_history (GSimpleAction* simple,
                                       GVariant* parameter, gpointer user_data);

static GActionEntry gnc_plugin_actions [] =
{
    { "SeMenuAction", nullptr, nullptr, nullptr, nullptr },
    { "SeLockAction", gnc_plugin_se_cmd_lock, nullptr, nullptr, nullptr },
    { "SeVerifyAction", gnc_plugin_se_cmd_verify, nullptr, nullptr, nullptr },
    { "SeSettingsAction", gnc_plugin_se_cmd_settings, nullptr, nullptr, nullptr },
    { "SeHistoryAction", gnc_plugin_se_cmd_history, nullptr, nullptr, nullptr },
};
static guint gnc_plugin_n_actions = G_N_ELEMENTS (gnc_plugin_actions);

static const gchar* gnc_plugin_load_ui_items [] =
{
    "ActionsPlaceholder0",
    nullptr,
};

/* Actions which must be disabled on a read-only book. */
static const gchar* plugin_writeable_actions[] =
{
    "SeLockAction",
    "SeSettingsAction",
    nullptr
};

struct _GncPluginSe
{
    GncPlugin gnc_plugin;
};

G_DEFINE_TYPE (GncPluginSe, gnc_plugin_se, GNC_TYPE_PLUGIN)

/* ------------------------------------------------------------------ */
/* Texts                                                              */

static std::string
format (const char* format, ...) G_GNUC_PRINTF (1, 2);

static std::string
format (const char* format, ...)
{
    va_list args;
    va_start (args, format);
    auto text {g_strdup_vprintf (format, args)};
    va_end (args);
    std::string result {text};
    g_free (text);
    return result;
}

static std::string
date_text (const GDate& date)
{
    char buffer[MAX_DATE_LENGTH + 1] = "";
    qof_print_gdate (buffer, sizeof buffer, &date);
    return buffer;
}

/* "A" for a continuous series, "A 2025" for a fiscal year of one. */
static std::string
period_text (const std::string& series, const std::string& period)
{
    return period.empty () || period == "all" ? series : series + " " + period;
}

static std::string
transaction_text (const QofBook* book, const GncGUID& guid)
{
    auto trans {xaccTransLookup (&guid, const_cast<QofBook*> (book))};
    if (!trans)
        return "";
    auto posted {xaccTransGetDatePostedGDate (trans)};
    auto description {xaccTransGetDescription (trans)};
    return date_text (posted) + " " + (description ? description : "");
}

static std::string
issue_text (const QofBook* book, const GncSeIssue& issue)
{
    auto series {period_text (issue.series, issue.period)};
    auto voucher {issue.series + " " + std::to_string (issue.number)};
    auto trans {transaction_text (book, issue.guid)};
    auto first {static_cast<int> (issue.number)};
    auto last {static_cast<int> (issue.number + issue.count - 1)};

    switch (issue.type)
    {
    case GncSeIssueType::GAP:
        if (issue.count == 1)
            return format (_("Series %s: number %d is missing."),
                           series.c_str (), first);
        return format (_("Series %s: numbers %d to %d are missing."),
                       series.c_str (), first, last);
    case GncSeIssueType::DUPLICATE_NUMBER:
        return format (_("Series %s: number %d is used more than once (%s)."),
                       series.c_str (), first, trans.c_str ());
    case GncSeIssueType::CHANGED:
        return format (_("%s (%s) has been changed after it was posted."),
                       voucher.c_str (), trans.c_str ());
    case GncSeIssueType::HEAD_MISMATCH:
        return format (_("Series %s: the last posted transaction does not "
                         "match what the series has recorded."),
                       series.c_str ());
    case GncSeIssueType::COUNTER_MISMATCH:
        return format (_("Series %s: the next number, %d, is lower than a "
                         "number that is already used."),
                       series.c_str (), first);
    case GncSeIssueType::UNKNOWN_SERIES:
        return format (_("There are posted transactions in series %s, which "
                         "the book does not have."), issue.series.c_str ());
    case GncSeIssueType::MISSING_READ_ONLY:
        return format (_("%s (%s) has lost its read-only mark."),
                       voucher.c_str (), trans.c_str ());
    case GncSeIssueType::LOCKED_AFTER_LOCK_DATE:
        return format (_("%s (%s) is posted but dated after the lock date."),
                       voucher.c_str (), trans.c_str ());
    case GncSeIssueType::UNLOCKED_BEFORE_LOCK_DATE:
        return format (_("A preliminary transaction is dated in the locked "
                         "period: %s. Move it to an open period or delete it."),
                       trans.c_str ());
    case GncSeIssueType::HISTORY_CHANGED:
        return format (_("The processing history has been changed: entry %d "
                         "is missing or altered."), first);
    case GncSeIssueType::CORRECTION_WITHOUT_ORIGINAL:
        return format (_("%s (%s) is a correcting entry, but the transaction "
                         "it corrects is missing or not posted."),
                       voucher.c_str (), trans.c_str ());
    }
    return "";
}

/* A message with a list below it that can be scrolled. */
static void
show_list (GtkWindow* parent, GtkMessageType type, const char* heading,
           const std::vector<std::string>& lines)
{
    const size_t max_lines = 200;
    std::string text;
    for (size_t i = 0; i < lines.size () && i < max_lines; ++i)
        text += lines[i] + "\n";
    if (lines.size () > max_lines)
        text += format (_("And %d more."),
                        static_cast<int> (lines.size () - max_lines));

    auto dialog {gtk_message_dialog_new (
            parent,
            static_cast<GtkDialogFlags> (GTK_DIALOG_MODAL |
                                         GTK_DIALOG_DESTROY_WITH_PARENT),
            type, GTK_BUTTONS_CLOSE, "%s", heading)};
    auto view {gtk_text_view_new ()};
    gtk_text_view_set_editable (GTK_TEXT_VIEW (view), FALSE);
    gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view), FALSE);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (view), GTK_WRAP_WORD);
    gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)),
                              text.c_str (), -1);
    auto scroll {gtk_scrolled_window_new (nullptr, nullptr)};
    gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (scroll),
                                                200);
    gtk_scrolled_window_set_min_content_width (GTK_SCROLLED_WINDOW (scroll),
                                               560);
    gtk_container_add (GTK_CONTAINER (scroll), view);
    auto area {gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog))};
    gtk_box_pack_start (GTK_BOX (area), scroll, TRUE, TRUE, 0);
    gtk_widget_show_all (scroll);

    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
}

/* ------------------------------------------------------------------ */
/* Check Voucher Series                                               */

/* @return true if nothing is wrong. */
static bool
verify_and_report (GtkWindow* parent, QofBook* book, bool report_success)
{
    auto issues {gnc_se_verify (book)};
    gnc_se_history_record_issues (book, issues);
    if (issues.empty ())
    {
        if (report_success)
            gnc_info_dialog (parent, "%s",
                             _("The voucher series are unbroken and no posted "
                               "transaction has been changed."));
        return true;
    }

    std::vector<std::string> lines;
    lines.reserve (issues.size ());
    for (const auto& issue : issues)
        lines.push_back (issue_text (book, issue));
    show_list (parent, GTK_MESSAGE_WARNING,
               _("The posted transactions do not match their voucher series. "
                 "The book has been changed outside GnuCash SE, or a "
                 "transaction has been added in a locked period."),
               lines);
    return false;
}

/* ------------------------------------------------------------------ */
/* Post Through…                                                      */

struct LockDialog
{
    QofBook* book;
    GtkWidget* dialog;
    GtkWidget* date_edit;
    GtkWidget* signature;
    GtkWidget* preview;
};

static bool is_blank (const char* text);

static GDate
lock_dialog_date (LockDialog* data)
{
    GDate date;
    g_date_clear (&date, 1);
    gnc_date_edit_get_gdate (GNC_DATE_EDIT (data->date_edit), &date);
    return date;
}

static void
lock_dialog_update (LockDialog* data)
{
    auto plan {gnc_se_plan_lock (data->book, lock_dialog_date (data))};
    std::string text;
    if (!plan.ok)
        text = plan.error;
    else if (plan.total == 0)
        text = _("There are no preliminary transactions up to that date. Only "
                 "the lock date will be moved.");
    else
    {
        text = format (ngettext ("%d transaction will be posted:",
                                 "%d transactions will be posted:", plan.total),
                       plan.total);
        for (const auto& group : plan.groups)
        {
            auto first {static_cast<int> (group.first_number)};
            auto last {first + group.count - 1};
            text += "\n";
            if (group.count == 1)
                /* Translators: series and fiscal year, then a voucher
                 * number: "A 2025: A 13". */
                text += format (_("%s: %s %d"),
                                period_text (group.series, group.period).c_str (),
                                group.series.c_str (), first);
            else
                /* Translators: series and fiscal year, then the first
                 * and the last voucher number: "A 2025: A 13 to A 24". */
                text += format (_("%s: %s %d to %s %d"),
                                period_text (group.series, group.period).c_str (),
                                group.series.c_str (), first,
                                group.series.c_str (), last);
        }
    }
    /* Who posts is recorded in the processing history. */
    bool signed_by {!is_blank (gtk_entry_get_text (GTK_ENTRY (data->signature)))};
    if (plan.ok && !signed_by)
    {
        text += "\n\n";
        text += _("Enter your signature to post.");
    }
    gtk_label_set_text (GTK_LABEL (data->preview), text.c_str ());
    gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog),
                                       GTK_RESPONSE_OK, plan.ok && signed_by);
}

static void
lock_dialog_date_changed_cb (GtkWidget* widget, gpointer user_data)
{
    lock_dialog_update (static_cast<LockDialog*> (user_data));
}

static GtkWidget*
wrapped_label (const char* text)
{
    auto label {gtk_label_new (text)};
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 70);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    return label;
}

/* The end of last month, or yesterday if that is already locked. */
static GDate
suggested_lock_date (const QofBook* book)
{
    GDate date;
    gnc_gdate_set_today (&date);
    g_date_set_day (&date, 1);
    g_date_subtract_days (&date, 1);
    if (gnc_se_date_is_locked (book, date))
    {
        gnc_gdate_set_today (&date);
        g_date_subtract_days (&date, 1);
    }
    return date;
}

static void
lock_dialog_run (GtkWindow* parent, QofBook* book)
{
    /* Do not number anything on top of a series that is already broken. */
    if (!verify_and_report (parent, book, false))
        return;

    LockDialog data {book, nullptr, nullptr, nullptr, nullptr};
    data.dialog = gtk_dialog_new_with_buttons (
        _("Post Transactions"), parent,
        static_cast<GtkDialogFlags> (GTK_DIALOG_MODAL |
                                     GTK_DIALOG_DESTROY_WITH_PARENT),
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Post"), GTK_RESPONSE_OK,
        nullptr);
    gtk_dialog_set_default_response (GTK_DIALOG (data.dialog),
                                     GTK_RESPONSE_CANCEL);

    auto box {gtk_box_new (GTK_ORIENTATION_VERTICAL, 12)};
    gtk_container_set_border_width (GTK_CONTAINER (box), 12);
    gtk_box_pack_start (
        GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (data.dialog))), box,
        TRUE, TRUE, 0);

    gtk_box_pack_start (
        GTK_BOX (box),
        wrapped_label (_("Posting gives the preliminary transactions up to and "
                         "including a date their voucher numbers, in the order "
                         "they were entered, and locks them. A posted "
                         "transaction cannot be changed or deleted; a mistake "
                         "is corrected with a correcting entry. Posting cannot "
                         "be undone.")),
        FALSE, FALSE, 0);

    auto lock_date {gnc_se_book_get_lock_date (book)};
    auto status {lock_date
                 ? format (_("The book is posted through %s."),
                           date_text (*lock_date).c_str ())
                 : std::string {_("Nothing has been posted in this book yet.")}};
    gtk_box_pack_start (GTK_BOX (box), wrapped_label (status.c_str ()), FALSE,
                        FALSE, 0);

    auto row {gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6)};
    gtk_box_pack_start (GTK_BOX (row), gtk_label_new (_("Post through:")),
                        FALSE, FALSE, 0);
    data.date_edit = gnc_date_edit_new (gnc_time (nullptr), FALSE, FALSE);
    auto suggested {suggested_lock_date (book)};
    gnc_date_edit_set_gdate (GNC_DATE_EDIT (data.date_edit), &suggested);
    gtk_box_pack_start (GTK_BOX (row), data.date_edit, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);

    auto signature_row {gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6)};
    gtk_box_pack_start (GTK_BOX (signature_row),
                        gtk_label_new (_("Your signature:")), FALSE, FALSE, 0);
    data.signature = gtk_entry_new ();
    gtk_box_pack_start (GTK_BOX (signature_row), data.signature, FALSE, FALSE,
                        0);
    gtk_box_pack_start (GTK_BOX (box), signature_row, FALSE, FALSE, 0);

    data.preview = wrapped_label ("");
    gtk_box_pack_start (GTK_BOX (box), data.preview, FALSE, FALSE, 0);

    g_signal_connect (data.date_edit, "date_changed",
                      G_CALLBACK (lock_dialog_date_changed_cb), &data);
    g_signal_connect (data.signature, "changed",
                      G_CALLBACK (lock_dialog_date_changed_cb), &data);
    lock_dialog_update (&data);
    gtk_widget_show_all (box);

    auto response {gtk_dialog_run (GTK_DIALOG (data.dialog))};
    auto date {lock_dialog_date (&data)};
    std::string signature {gtk_entry_get_text (GTK_ENTRY (data.signature))};
    gtk_widget_destroy (data.dialog);
    if (response != GTK_RESPONSE_OK)
        return;

    gnc_suspend_gui_refresh ();
    auto result {gnc_se_lock_through (book, date, signature)};
    gnc_resume_gui_refresh ();
    gnc_gui_refresh_all ();

    if (!result.ok)
    {
        gnc_error_dialog (parent, "%s", result.error.c_str ());
        return;
    }
    auto text {format (ngettext ("%d transaction was posted.",
                                 "%d transactions were posted.", result.locked),
                       result.locked)};
    text += " ";
    text += format (_("The book is posted through %s."),
                    date_text (date).c_str ());
    if (qof_book_session_not_saved (book))
    {
        text += " ";
        text += _("Save the book to keep the posting.");
    }
    gnc_info_dialog (parent, "%s", text.c_str ());
}

/* ------------------------------------------------------------------ */
/* Correcting a posted transaction                                    */

struct CorrectDialog
{
    QofBook* book;
    GtkWidget* dialog;
    GtkWidget* date_edit;
    GtkWidget* signature;
    GtkWidget* reason;
    GtkWidget* replacement;
    GtkWidget* message;
};

static GDate
correct_dialog_date (CorrectDialog* data)
{
    GDate date;
    g_date_clear (&date, 1);
    gnc_date_edit_get_gdate (GNC_DATE_EDIT (data->date_edit), &date);
    return date;
}

static bool
is_blank (const char* text)
{
    while (text && g_ascii_isspace (*text))
        ++text;
    return !text || !*text;
}

/* Says what is missing and lets the correction be made when nothing is. */
static void
correct_dialog_update (CorrectDialog* data)
{
    const char* message = "";
    if (gnc_se_date_is_locked (data->book, correct_dialog_date (data)))
        message = _("The date is in a locked period. A correcting entry is "
                    "dated in an open period.");
    else if (is_blank (gtk_entry_get_text (GTK_ENTRY (data->signature))))
        message = _("Enter your signature: a correction says who made it.");
    gtk_label_set_text (GTK_LABEL (data->message), message);
    gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog),
                                       GTK_RESPONSE_OK, !*message);
}

static void
correct_dialog_changed_cb (GtkWidget* widget, gpointer user_data)
{
    correct_dialog_update (static_cast<CorrectDialog*> (user_data));
}

static GtkWidget* grid_label (const char* text);

Transaction*
gnc_plugin_se_correct_transaction (GtkWindow* parent, Transaction* trans)
{
    g_return_val_if_fail (trans, nullptr);
    g_return_val_if_fail (gnc_se_trans_is_locked (trans), nullptr);
    auto book {xaccTransGetBook (trans)};
    auto voucher {gnc_se_trans_get_voucher (trans)};
    auto what {transaction_text (book, *xaccTransGetGUID (trans))};

    if (auto existing {gnc_se_trans_get_correction (trans)})
    {
        auto status {gnc_se_trans_is_locked (existing)
                     ? format (_("%s already has a correcting entry, %s."),
                               voucher.c_str (),
                               gnc_se_trans_get_voucher (existing).c_str ())
                     : format (_("%s already has a correcting entry, which is "
                                 "not posted yet."), voucher.c_str ())};
        return gnc_verify_dialog (parent, TRUE, "%s\n\n%s", status.c_str (),
                                  _("Jump to the transaction?"))
            ? existing : nullptr;
    }

    CorrectDialog data {book, nullptr, nullptr, nullptr, nullptr, nullptr,
                        nullptr};
    data.dialog = gtk_dialog_new_with_buttons (
        _("Correct a Posted Transaction"), parent,
        static_cast<GtkDialogFlags> (GTK_DIALOG_MODAL |
                                     GTK_DIALOG_DESTROY_WITH_PARENT),
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("Create Correcting _Entry"), GTK_RESPONSE_OK,
        nullptr);

    auto grid {gtk_grid_new ()};
    gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
    gtk_container_set_border_width (GTK_CONTAINER (grid), 12);
    gtk_box_pack_start (
        GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (data.dialog))), grid,
        TRUE, TRUE, 0);
    int row = 0;

    auto intro {format (_("%s (%s) is posted and cannot be changed. A "
                          "correcting entry takes it back with a voucher of "
                          "its own, and the two refer to each other."),
                        voucher.c_str (), what.c_str ())};
    gtk_grid_attach (GTK_GRID (grid), wrapped_label (intro.c_str ()), 0, row++,
                     2, 1);

    data.date_edit = gnc_date_edit_new (gnc_time (nullptr), FALSE, FALSE);
    gtk_grid_attach (GTK_GRID (grid),
                     grid_label (_("Date of the correcting entry:")), 0, row,
                     1, 1);
    gtk_grid_attach (GTK_GRID (grid), data.date_edit, 1, row++, 1, 1);

    data.signature = gtk_entry_new ();
    gtk_entry_set_activates_default (GTK_ENTRY (data.signature), TRUE);
    gtk_grid_attach (GTK_GRID (grid), grid_label (_("Your signature:")), 0, row,
                     1, 1);
    gtk_grid_attach (GTK_GRID (grid), data.signature, 1, row++, 1, 1);

    data.reason = gtk_entry_new ();
    gtk_widget_set_hexpand (data.reason, TRUE);
    gtk_entry_set_activates_default (GTK_ENTRY (data.reason), TRUE);
    gtk_grid_attach (GTK_GRID (grid), grid_label (_("Reason:")), 0, row, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), data.reason, 1, row++, 1, 1);

    data.replacement = gtk_check_button_new_with_mnemonic (
        _("Also create a _copy of the original to put right"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data.replacement), TRUE);
    gtk_grid_attach (GTK_GRID (grid), data.replacement, 0, row++, 2, 1);

    data.message = wrapped_label ("");
    gtk_grid_attach (GTK_GRID (grid), data.message, 0, row++, 2, 1);

    g_signal_connect (data.date_edit, "date_changed",
                      G_CALLBACK (correct_dialog_changed_cb), &data);
    g_signal_connect (data.signature, "changed",
                      G_CALLBACK (correct_dialog_changed_cb), &data);
    gtk_dialog_set_default_response (GTK_DIALOG (data.dialog), GTK_RESPONSE_OK);
    correct_dialog_update (&data);
    gtk_widget_show_all (grid);
    gtk_widget_grab_focus (data.signature);

    auto response {gtk_dialog_run (GTK_DIALOG (data.dialog))};
    auto date {correct_dialog_date (&data)};
    std::string signature {gtk_entry_get_text (GTK_ENTRY (data.signature))};
    std::string reason {gtk_entry_get_text (GTK_ENTRY (data.reason))};
    auto with_replacement {gtk_toggle_button_get_active (
            GTK_TOGGLE_BUTTON (data.replacement))};
    gtk_widget_destroy (data.dialog);
    if (response != GTK_RESPONSE_OK)
        return nullptr;

    gnc_suspend_gui_refresh ();
    auto result {gnc_se_correct (trans, date, signature, reason,
                                 with_replacement)};
    gnc_resume_gui_refresh ();
    if (!result.ok)
    {
        gnc_error_dialog (parent, "%s", result.error.c_str ());
        return nullptr;
    }
    return result.correction;
}

/* ------------------------------------------------------------------ */
/* Voucher Series and Fiscal Year…                                    */

struct SettingsDialog
{
    QofBook* book;
    GtkWidget* dialog;
    GtkWidget* series_list;
    GtkWidget* default_series;
    GtkWidget* new_code;
    GtkWidget* new_description;
    GtkWidget* new_type;
};

static std::string
series_text (const GncSeSeries& series)
{
    auto type {series.period_type == GncSePeriodType::CONTINUOUS
               ? _("continuous numbering")
               : _("new numbers every fiscal year")};
    if (series.description.empty ())
        return format ("%s (%s)", series.code.c_str (), type);
    /* Translators: code, description and kind of a voucher series. */
    return format (_("%s - %s (%s)"), series.code.c_str (),
                   series.description.c_str (), type);
}

static void
settings_dialog_show_series (SettingsDialog* data)
{
    auto all_series {gnc_se_book_get_series (data->book)};
    auto default_code {gnc_se_book_get_default_series (data->book)};

    std::string text;
    for (const auto& series : all_series)
        text += (text.empty () ? "" : "\n") + series_text (series);
    if (all_series.empty ())
        text = _("None yet. Series A, with new numbers every fiscal year, is "
                 "created the first time something is posted.");
    gtk_label_set_text (GTK_LABEL (data->series_list), text.c_str ());

    auto combo {GTK_COMBO_BOX_TEXT (data->default_series)};
    gtk_combo_box_text_remove_all (combo);
    if (all_series.empty ())
        gtk_combo_box_text_append (combo, default_code.c_str (),
                                   default_code.c_str ());
    for (const auto& series : all_series)
        gtk_combo_box_text_append (combo, series.code.c_str (),
                                   series.code.c_str ());
    gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), default_code.c_str ());
}

static void
settings_dialog_add_series_cb (GtkWidget* button, gpointer user_data)
{
    auto data {static_cast<SettingsDialog*> (user_data)};
    std::string code {gtk_entry_get_text (GTK_ENTRY (data->new_code))};
    std::string description {
        gtk_entry_get_text (GTK_ENTRY (data->new_description))};
    auto type {gtk_combo_box_get_active (GTK_COMBO_BOX (data->new_type)) == 1
               ? GncSePeriodType::CONTINUOUS : GncSePeriodType::FISCAL_YEAR};

    if (!gnc_se_book_add_series (data->book, code, description, type))
    {
        gnc_error_dialog (GTK_WINDOW (data->dialog), "%s",
                          _("The code of a voucher series is one to eight "
                            "letters or digits, and each series has its own."));
        return;
    }
    gtk_entry_set_text (GTK_ENTRY (data->new_code), "");
    gtk_entry_set_text (GTK_ENTRY (data->new_description), "");
    settings_dialog_show_series (data);
}

static GtkWidget*
grid_label (const char* text)
{
    auto label {gtk_label_new (text)};
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_widget_set_valign (label, GTK_ALIGN_START);
    return label;
}

static void
settings_dialog_run (GtkWindow* parent, QofBook* book)
{
    SettingsDialog data {book, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr};
    data.dialog = gtk_dialog_new_with_buttons (
        _("Voucher Series and Fiscal Year"), parent,
        static_cast<GtkDialogFlags> (GTK_DIALOG_MODAL |
                                     GTK_DIALOG_DESTROY_WITH_PARENT),
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_OK"), GTK_RESPONSE_OK,
        nullptr);

    auto grid {gtk_grid_new ()};
    gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
    gtk_container_set_border_width (GTK_CONTAINER (grid), 12);
    gtk_box_pack_start (
        GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (data.dialog))), grid,
        TRUE, TRUE, 0);
    int row = 0;

    /* The first day of the fiscal year. */
    auto [start_month, start_day] = gnc_se_book_get_fiscal_year_start (book);
    auto start_box {gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6)};
    auto day_spin {gtk_spin_button_new_with_range (1, 31, 1)};
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (day_spin), start_day);
    auto month_combo {gtk_combo_box_text_new ()};
    for (int month = 1; month <= 12; ++month)
    {
        GDate first;
        g_date_set_dmy (&first, 1, static_cast<GDateMonth> (month), 2001);
        char name[64] = "";
        g_date_strftime (name, sizeof name, "%B", &first);
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (month_combo), name);
    }
    gtk_combo_box_set_active (GTK_COMBO_BOX (month_combo), start_month - 1);
    gtk_box_pack_start (GTK_BOX (start_box), day_spin, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (start_box), month_combo, FALSE, FALSE, 0);
    gtk_grid_attach (GTK_GRID (grid),
                     grid_label (_("The fiscal year starts on:")), 0, row, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), start_box, 1, row++, 1, 1);

    /* How long posting may be put off. */
    auto deferral {gnc_se_book_get_deferral (book)};
    auto deferral_combo {gtk_combo_box_text_new ()};
    gtk_combo_box_text_append_text (
        GTK_COMBO_BOX_TEXT (deferral_combo),
        _("50 days after the end of the month"));
    gtk_combo_box_text_append_text (
        GTK_COMBO_BOX_TEXT (deferral_combo),
        _("50 days after the end of the quarter (turnover at most 3 million "
          "kronor)"));
    gtk_combo_box_set_active (GTK_COMBO_BOX (deferral_combo),
                              deferral == GncSeDeferral::QUARTER ? 1 : 0);
    gtk_grid_attach (GTK_GRID (grid),
                     grid_label (_("Transactions are posted at the latest:")),
                     0, row, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), deferral_combo, 1, row++, 1, 1);
    gtk_grid_attach (
        GTK_GRID (grid),
        wrapped_label (_("Cash payments are always posted the next working "
                         "day at the latest.")),
        1, row++, 1, 1);

    /* The series. */
    data.series_list = grid_label ("");
    gtk_grid_attach (GTK_GRID (grid), grid_label (_("Voucher series:")), 0,
                     row, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), data.series_list, 1, row++, 1, 1);

    data.default_series = gtk_combo_box_text_new ();
    gtk_grid_attach (GTK_GRID (grid),
                     grid_label (_("Series of new transactions:")), 0, row, 1,
                     1);
    gtk_grid_attach (GTK_GRID (grid), data.default_series, 1, row++, 1, 1);

    auto add_box {gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6)};
    data.new_code = gtk_entry_new ();
    gtk_entry_set_max_length (GTK_ENTRY (data.new_code), 8);
    gtk_entry_set_width_chars (GTK_ENTRY (data.new_code), 6);
    gtk_entry_set_placeholder_text (GTK_ENTRY (data.new_code), _("Code"));
    data.new_description = gtk_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (data.new_description),
                                    _("Description"));
    data.new_type = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (data.new_type),
                                    _("New numbers every fiscal year"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (data.new_type),
                                    _("Continuous numbering"));
    gtk_combo_box_set_active (GTK_COMBO_BOX (data.new_type), 0);
    auto add_button {gtk_button_new_with_mnemonic (_("_Add"))};
    g_signal_connect (add_button, "clicked",
                      G_CALLBACK (settings_dialog_add_series_cb), &data);
    gtk_box_pack_start (GTK_BOX (add_box), data.new_code, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (add_box), data.new_description, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (add_box), data.new_type, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (add_box), add_button, FALSE, FALSE, 0);
    gtk_grid_attach (GTK_GRID (grid), grid_label (_("New series:")), 0, row, 1,
                     1);
    gtk_grid_attach (GTK_GRID (grid), add_box, 1, row++, 1, 1);
    gtk_grid_attach (
        GTK_GRID (grid),
        wrapped_label (_("A series cannot be removed or changed once it has "
                         "been added.")),
        1, row++, 1, 1);

    settings_dialog_show_series (&data);
    gtk_widget_show_all (grid);

    while (gtk_dialog_run (GTK_DIALOG (data.dialog)) == GTK_RESPONSE_OK)
    {
        /* A setting is only stored if it has been changed: storing one
         * makes the book a GnuCash SE book. */
        auto month {gtk_combo_box_get_active (GTK_COMBO_BOX (month_combo)) + 1};
        auto day {gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (day_spin))};
        if (std::make_pair (month, day) !=
            gnc_se_book_get_fiscal_year_start (book) &&
            !gnc_se_book_set_fiscal_year_start (book, month, day))
        {
            gnc_error_dialog (GTK_WINDOW (data.dialog), "%s",
                              _("That is not a day on which a fiscal year can "
                                "start."));
            continue;
        }

        auto new_deferral {
            gtk_combo_box_get_active (GTK_COMBO_BOX (deferral_combo)) == 1
            ? GncSeDeferral::QUARTER : GncSeDeferral::MONTH};
        if (new_deferral != gnc_se_book_get_deferral (book))
            gnc_se_book_set_deferral (book, new_deferral);

        auto code {gtk_combo_box_get_active_id (
                GTK_COMBO_BOX (data.default_series))};
        if (code && gnc_se_book_get_default_series (book) != code)
            gnc_se_book_set_default_series (book, code);
        break;
    }
    gtk_widget_destroy (data.dialog);
}

/* ------------------------------------------------------------------ */
/* Processing History…                                                */

static void
save_history (GtkWindow* parent, const std::string& text)
{
    auto filename {gnc_file_dialog (parent, _("Save the Processing History"),
                                    nullptr, nullptr, GNC_FILE_DIALOG_EXPORT)};
    if (!filename)
        return;
    GError* error {nullptr};
    if (!g_file_set_contents (filename, text.c_str (),
                              static_cast<gssize> (text.size ()), &error))
    {
        gnc_error_dialog (parent, "%s", error->message);
        g_error_free (error);
    }
    g_free (filename);
}

static void
history_dialog_run (GtkWindow* parent, QofBook* book)
{
    auto text {gnc_se_history_text (book)};

    auto dialog {gtk_dialog_new_with_buttons (
            _("Processing History"), parent,
            static_cast<GtkDialogFlags> (GTK_DIALOG_MODAL |
                                         GTK_DIALOG_DESTROY_WITH_PARENT),
            _("_Save As…"), GTK_RESPONSE_APPLY,
            _("_Close"), GTK_RESPONSE_CLOSE,
            nullptr)};
    gtk_window_set_default_size (GTK_WINDOW (dialog), 900, 560);

    auto view {gtk_text_view_new ()};
    gtk_text_view_set_editable (GTK_TEXT_VIEW (view), FALSE);
    gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view), FALSE);
    gtk_text_view_set_monospace (GTK_TEXT_VIEW (view), TRUE);
    gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)),
                              text.c_str (), -1);
    auto scroll {gtk_scrolled_window_new (nullptr, nullptr)};
    gtk_container_add (GTK_CONTAINER (scroll), view);
    gtk_box_pack_start (
        GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), scroll,
        TRUE, TRUE, 0);
    gtk_widget_show_all (scroll);

    while (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_APPLY)
        save_history (GTK_WINDOW (dialog), text);
    gtk_widget_destroy (dialog);
}

/* ------------------------------------------------------------------ */
/* When a book has been opened                                        */

static void
remind_of_overdue (GtkWindow* parent, QofBook* book)
{
    GDate today;
    gnc_gdate_set_today (&today);
    auto overdue {gnc_se_overdue (book, today)};
    auto count {overdue.cash + overdue.other};
    if (count == 0 || !overdue.oldest)
        return;

    auto text {format (ngettext ("%d preliminary transaction should have been "
                                 "posted by now.",
                                 "%d preliminary transactions should have been "
                                 "posted by now.", count), count)};
    text += " ";
    text += format (_("The earliest is dated %s."),
                    date_text (*overdue.oldest).c_str ());
    if (overdue.cash > 0)
    {
        text += " ";
        text += format (ngettext ("%d of them is a cash payment, which is "
                                  "posted the next working day at the latest.",
                                  "%d of them are cash payments, which are "
                                  "posted the next working day at the latest.",
                                  overdue.cash), overdue.cash);
    }
    text += "\n\n";
    text += _("Post them with Actions > Posting > Post Through.");
    gnc_info_dialog (parent, "%s", text.c_str ());
}

static void
book_opened_cb (gpointer session, gpointer user_data)
{
    auto book {gnc_get_current_book ()};
    if (!book || !gnc_se_book_is_se (book))
        return;
    auto parent {gnc_ui_get_main_window (nullptr)};
    /* A new program version, and accounts and tax tables changed since
     * the book was last open here, go into the processing history. */
    gnc_se_history_sync (book);
    verify_and_report (parent, book, false);
    remind_of_overdue (parent, book);
}

/* ------------------------------------------------------------------ */
/* Command callbacks                                                  */

static void
gnc_plugin_se_cmd_lock (GSimpleAction* simple, GVariant* parameter,
                        gpointer user_data)
{
    auto data {static_cast<GncMainWindowActionData*> (user_data)};
    g_return_if_fail (data != nullptr);
    lock_dialog_run (GTK_WINDOW (data->window), gnc_get_current_book ());
}

static void
gnc_plugin_se_cmd_verify (GSimpleAction* simple, GVariant* parameter,
                          gpointer user_data)
{
    auto data {static_cast<GncMainWindowActionData*> (user_data)};
    g_return_if_fail (data != nullptr);
    verify_and_report (GTK_WINDOW (data->window), gnc_get_current_book (),
                       true);
}

static void
gnc_plugin_se_cmd_history (GSimpleAction* simple, GVariant* parameter,
                           gpointer user_data)
{
    auto data {static_cast<GncMainWindowActionData*> (user_data)};
    g_return_if_fail (data != nullptr);
    history_dialog_run (GTK_WINDOW (data->window), gnc_get_current_book ());
}

static void
gnc_plugin_se_cmd_settings (GSimpleAction* simple, GVariant* parameter,
                            gpointer user_data)
{
    auto data {static_cast<GncMainWindowActionData*> (user_data)};
    g_return_if_fail (data != nullptr);
    settings_dialog_run (GTK_WINDOW (data->window), gnc_get_current_book ());
}

/* ------------------------------------------------------------------ */
/* The plugin                                                         */

GncPlugin*
gnc_plugin_se_new (void)
{
    ENTER (" ");
    auto plugin {GNC_PLUGIN (g_object_new (GNC_TYPE_PLUGIN_SE, nullptr))};
    gnc_hook_add_dangler (HOOK_BOOK_OPENED, book_opened_cb, nullptr, nullptr);
    LEAVE (" ");
    return plugin;
}

static void
page_changed (GncMainWindow* window, GncPluginPage* page, gpointer user_data)
{
    auto action_group {gnc_main_window_get_action_group (window,
                                                         PLUGIN_ACTIONS_NAME)};
    if (action_group && qof_book_is_readonly (gnc_get_current_book ()))
        gnc_plugin_set_actions_enabled (G_ACTION_MAP (action_group),
                                        plugin_writeable_actions, FALSE);
}

static void
add_to_window (GncPlugin* plugin, GncMainWindow* window, GQuark type)
{
    g_signal_connect (window, "page_changed", G_CALLBACK (page_changed),
                      plugin);
}

static void
remove_from_window (GncPlugin* plugin, GncMainWindow* window, GQuark type)
{
    g_signal_handlers_disconnect_by_func (window, (gpointer) page_changed,
                                          plugin);
}

static void
gnc_plugin_se_class_init (GncPluginSeClass* klass)
{
    auto plugin_class {GNC_PLUGIN_CLASS (klass)};

    plugin_class->plugin_name        = GNC_PLUGIN_SE_NAME;
    plugin_class->actions_name       = PLUGIN_ACTIONS_NAME;
    plugin_class->actions            = gnc_plugin_actions;
    plugin_class->n_actions          = gnc_plugin_n_actions;
    plugin_class->ui_filename        = PLUGIN_UI_FILENAME;
    plugin_class->ui_updates         = gnc_plugin_load_ui_items;
    plugin_class->add_to_window      = add_to_window;
    plugin_class->remove_from_window = remove_from_window;
}

static void
gnc_plugin_se_init (GncPluginSe* plugin)
{
}
