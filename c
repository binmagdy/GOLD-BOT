import time
import re
import requests
from bs4 import BeautifulSoup
from datetime import datetime

# ================= CONFIG =================
BOT_TOKEN = "PUT_YOUR_REAL_TOKEN_HERE"
CHAT_ID = "-1003532220511"

URL_GOLD_24 = "https://ta3weem.com/en/gold-prices/GOLD24K"
URL_GOLD_21 = "https://ta3weem.com/en/gold-prices/GOLD21K"
URL_GOLD_18 = "https://ta3weem.com/en/gold-prices/GOLD18K"

URL_BANK_NBE = "https://ta3weem.com/en/banks/national-bank-of-egypt-nbe"
URL_BANK_BM  = "https://ta3weem.com/en/banks/banque-misr-bm"
URL_BANK_CIB = "https://ta3weem.com/en/banks/commercial-international-bank-cib"

# Silver (Global spot)
URL_SILVER_EGYPT = "https://goldpricez.com/silver-rates/egypt"

HEADERS = {"User-Agent": "Mozilla/5.0"}
TIMEZONE_LABEL = "Cairo"
OZ_TO_GRAM = 31.1035
# ==========================================


def send(text: str):
    url = f"https://api.telegram.org/bot8208559778:AAGTx_xrMlVYnq1_jzLdRz_8Oa8-8lnNh0E/sendMessage"
    payload = {"chat_id": CHAT_ID, "text": text, "parse_mode": "HTML"}
    r = requests.post(url, json=payload, timeout=20)
    r.raise_for_status()


def first_number(text: str) -> float:
    t = text.replace(",", "").strip()
    m = re.search(r"\d+(?:\.\d+)?", t)
    if not m:
        raise ValueError(f"Cannot parse number from: {text!r}")
    return float(m.group(0))


def fetch_html(url: str) -> BeautifulSoup:
    html = requests.get(url, headers=HEADERS, timeout=25).text
    return BeautifulSoup(html, "html.parser")


def parse_best_buy_sell_from_gold_page(url: str):
    soup = fetch_html(url)
    rows = soup.select("table tbody tr")

    best_buy = None
    best_sell = None

    for row in rows:
        cols = [c.get_text(" ", strip=True) for c in row.select("td")]
        if len(cols) < 3:
            continue

        source = cols[0]
        try:
            buy = first_number(cols[1])
            sell = first_number(cols[2])
        except Exception:
            continue

        if best_buy is None or buy > best_buy[1]:
            best_buy = (source, buy)

        if best_sell is None or sell < best_sell[1]:
            best_sell = (source, sell)

    if not best_buy or not best_sell:
        raise RuntimeError(f"Could not extract Buy/Sell from: {url}")

    return best_buy, best_sell


def parse_usd_rates_from_bank_page(url: str):
    soup = fetch_html(url)
    rows = soup.select("table tbody tr")

    for row in rows:
        txt = row.get_text(" ", strip=True).lower()
        if "usd" in txt or "us dollar" in txt or "u.s. dollar" in txt:
            cols = [c.get_text(" ", strip=True) for c in row.select("td")]
            nums = [first_number(c) for c in cols if re.search(r"\d", c)]
            if len(nums) >= 2:
                return nums[0], nums[1]  # buy, sell

    raise RuntimeError(f"USD row not found on bank page: {url}")


def parse_silver_global_usd_oz():
    soup = fetch_html(URL_SILVER_EGYPT)
    text = soup.get_text(" ", strip=True)

    # best-effort: "Silver Price $xx.xx"
    m = re.search(r"Silver Price\s*\$?\s*(\d+(?:\.\d+)?)", text, re.IGNORECASE)
    if not m:
        return None
    return float(m.group(1))


def implied_silver_egp_per_gram(silver_usd_oz: float, usd_egp_sell: float) -> float:
    # EGP/gram = (USD/oz * EGP/USD) / 31.1035
    return (silver_usd_oz * usd_egp_sell) / OZ_TO_GRAM


def fmt_money(x: float) -> str:
    return f"{x:,.2f}"


def build_message():
    now = datetime.now().strftime("%Y-%m-%d %H:%M")

    # Gold (best only)
    g24_buy, g24_sell = parse_best_buy_sell_from_gold_page(URL_GOLD_24)
    g21_buy, g21_sell = parse_best_buy_sell_from_gold_page(URL_GOLD_21)
    g18_buy, g18_sell = parse_best_buy_sell_from_gold_page(URL_GOLD_18)

    # USD banks
    nbe_buy, nbe_sell = parse_usd_rates_from_bank_page(URL_BANK_NBE)
    bm_buy, bm_sell = parse_usd_rates_from_bank_page(URL_BANK_BM)
    cib_buy, cib_sell = parse_usd_rates_from_bank_page(URL_BANK_CIB)

    usd_sell_avg = (nbe_sell + bm_sell + cib_sell) / 3.0

    # Silver
    silver_usd_oz = parse_silver_global_usd_oz()
    if silver_usd_oz is not None:
        silver_egp_g = implied_silver_egp_per_gram(silver_usd_oz, usd_sell_avg)
        silver_note_ar = "محسوب (Spot × متوسط سعر بيع الدولار)"
        silver_note_en = "Implied (Spot × avg USD sell)"
        silver_egp_g_txt = fmt_money(silver_egp_g)
        silver_usd_oz_txt = fmt_money(silver_usd_oz)
    else:
        silver_note_ar = "غير متاح"
        silver_note_en = "N/A"
        silver_egp_g_txt = "N/A"
        silver_usd_oz_txt = "N/A"

    # --- Tables (monospace) ---
    banks_table = (
        "BANK            BUY        SELL\n"
        f"NBE           {fmt_money(nbe_buy):>8}   {fmt_money(nbe_sell):>8}\n"
        f"Banque Misr   {fmt_money(bm_buy):>8}   {fmt_money(bm_sell):>8}\n"
        f"CIB           {fmt_money(cib_buy):>8}   {fmt_money(cib_sell):>8}\n"
        f"AVG (SELL)                 {fmt_money(usd_sell_avg):>8}\n"
    )

    gold_table = (
        "KARAT   BEST BUY (SRC)                 VALUE     BEST SELL (SRC)                VALUE\n"
        f"24K     {g24_buy[0][:22]:<22}         {fmt_money(g24_buy[1]):>8}     {g24_sell[0][:22]:<22}        {fmt_money(g24_sell[1]):>8}\n"
        f"21K     {g21_buy[0][:22]:<22}         {fmt_money(g21_buy[1]):>8}     {g21_sell[0][:22]:<22}        {fmt_money(g21_sell[1]):>8}\n"
        f"18K     {g18_buy[0][:22]:<22}         {fmt_money(g18_buy[1]):>8}     {g18_sell[0][:22]:<22}        {fmt_money(g18_sell[1]):>8}\n"
    )

    metals_table = (
        "METAL      GLOBAL (USD/oz)    EGYPT (EGP/g)\n"
        f"SILVER     {silver_usd_oz_txt:>12}      {silver_egp_g_txt:>12}\n"
    )

    # --- Message bilingual ---
    msg = (
        f"⏰ <b>تحديث كل ساعة</b> — ({TIMEZONE_LABEL})\n"
        f"{now}\n\n"
        "💵 <b>الدولار / الجنيه (البنوك)</b>\n"
        f"<pre>{banks_table}</pre>\n"
        "🥇 <b>الذهب (جنيه/جرام) — أفضل فقط</b>\n"
        f"<pre>{gold_table}</pre>\n"
        "🥈 <b>الفضة</b>\n"
        f"<pre>{metals_table}</pre>\n"
        f"ملاحظة الفضة مصر: {silver_note_ar}\n"
        "\n"
        "────────────────────\n"
        "\n"
        f"⏰ <b>Hourly Update</b> — ({TIMEZONE_LABEL})\n"
        f"{now}\n\n"
        "💵 <b>USD/EGP (Banks)</b>\n"
        f"<pre>{banks_table}</pre>\n"
        "🥇 <b>Gold (EGP/gram) — Best only</b>\n"
        f"<pre>{gold_table}</pre>\n"
        "🥈 <b>Silver</b>\n"
        f"<pre>{metals_table}</pre>\n"
        f"Silver Egypt note: {silver_note_en}"
    )

    return msg


# =============== RUN ===============
# 1) Send immediately for testing
send(build_message())

# 2) Then every hour from server start (first after 1 hour)
time.sleep(60 * 60)
while True:
    try:
        send(build_message())
    except Exception as e:
        print("Error:", e)
    time.sleep(60 * 60)
