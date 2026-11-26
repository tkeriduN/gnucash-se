"""Create a small book for trying GnuCash SE by hand or with xdotool."""
import sys
from gnucash import Session, Account, Transaction, Split, GncNumeric, SessionOpenMode
from gnucash.gnucash_core_c import ACCT_TYPE_BANK, ACCT_TYPE_INCOME, ACCT_TYPE_EXPENSE, ACCT_TYPE_CASH

path = sys.argv[1]
session = Session("xml://" + path, SessionOpenMode.SESSION_NEW_STORE)
book = session.book
sek = book.get_table().lookup("CURRENCY", "SEK")
root = book.get_root_account()

def account(code, name, kind):
    acc = Account(book)
    acc.SetCode(code)
    acc.SetName(name)
    acc.SetType(kind)
    acc.SetCommodity(sek)
    root.append_child(acc)
    return acc

bank = account("1930", "Företagskonto", ACCT_TYPE_BANK)
cash = account("1910", "Kassa", ACCT_TYPE_CASH)
sales = account("3001", "Försäljning", ACCT_TYPE_INCOME)
rent = account("5010", "Lokalhyra", ACCT_TYPE_EXPENSE)

def trans(day, month, year, text, ore, debit, credit):
    t = Transaction(book)
    t.BeginEdit()
    t.SetCurrency(sek)
    t.SetDate(day, month, year)
    t.SetDescription(text)
    for sign, acc in ((1, debit), (-1, credit)):
        s = Split(book)
        s.SetParent(t)
        s.SetAccount(acc)
        s.SetValue(GncNumeric(sign * ore, 100))
        s.SetAmount(GncNumeric(sign * ore, 100))
    t.CommitEdit()
    return t

trans(5, 10, 2026, "Faktura 101", 1250000, bank, sales)
trans(12, 10, 2026, "Kontantförsäljning", 80000, cash, sales)
trans(27, 10, 2026, "Hyra oktober", 900000, rent, bank)
trans(3, 11, 2026, "Faktura 102", 640000, bank, sales)
session.save()
session.end()
print("skapade", path)
