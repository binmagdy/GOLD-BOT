# -*- coding: utf-8 -*-

import time
import re
import requests
from bs4 import BeautifulSoup
from datetime import datetime
from zoneinfo import ZoneInfo

# ================== CONFIG ==================
import os
BOT_TOKEN = os.getenv("BOT_TOKEN", "")
CHAT_ID = os.getenv("CHAT_ID", "-1003532220511")
URL_GOLD = "https://goldbullioneg.com/%D8%A3%D8%B3%D8%B9%D8%A7%D8%B1-%D8%A7%D9%84%D8%B0%D9%87%D8%A8/"
URL_BANK_NBE = "https://ta3weem.com/en/banks/national-bank-of-egypt-nbe"
URL_BANK_BM  = "https://ta3weem.com/en/banks/banque-misr-bm"
URL_BANK_CIB = "https://ta3weem.com/en/banks/commercial-international-bank-cib"
URL_SILVER   = "https://goldpricez.com/silver-rates/egypt"

HEADERS = {"User-Agent": "Mozilla/5.0"}
OZ_TO_GRAM = 31.1035
TZ = ZoneInfo("Africa/Cairo")
INTERVAL = 1800  # كل ساعة
# ============================================

session = requests.Session()
session.headers.update(HEADERS)

# ذاكرة آخر القيم
last_vals = {}


# ================== HELPERS ==================
def send(msg: str):
    url = f"https://api.telegram.org/bot{BOT_TOKEN}/sendMessage"
    r = session.post(url, json={
        "chat_id": CHAT_ID,
        "text": msg,
        "parse_mode": "HTML",
        "disable_web_page_preview": True
    }, timeout=20)
    r.raise_for_status()


def safe_get(url, tries=3):
    for i in range(tries):
        try:
            r = session.get(url, timeout=25)
            r.raise_for_status()
            return r.text
        except Exception:
            if i == tries - 1:
                return None
            time.sleep(1)


def num(x: str):
    m = re.search(r"\d+(?:\.\d+)?", x.replace(",", ""))
    return float(m.group()) if m else None


def arrow(key: str, curr):
    prev = last_vals.get(key)

    if curr is None:
        return "   "

    if prev is None:
        last_vals[key] = curr
        return "   "

    if curr > prev:
        a = "🟢"
    elif curr < prev:
        a = "🔴"
    else:
        a = "⚪️"

    last_vals[key] = curr
    return a


def fmt_usd(x):
    return f"{x:.2f}" if x is not None else "N/A"


def fmt_gold(x):
    return str(int(round(x))) if x is not None else "N/A"


def fmt_usd_arrow(key, x):
    return f"{fmt_usd(x)} {arrow(key, x)}"


def fmt_gold_arrow(key, x):
    return f"{fmt_gold(x)} {arrow(key, x)}"
# ============================================


# ================== SCRAPERS =================
def get_gold(karat: int):
    html = safe_get(URL_GOLD)
    if not html:
        return None, None

    soup = BeautifulSoup(html, "html.parser")
    for row in soup.select("tr"):
        t = row.get_text(" ", strip=True)
        if f"جرام عيار {karat}" in t:
            nums = [float(x) for x in re.findall(r"\d+(?:\.\d+)?", t.replace(",", ""))]
            if len(nums) >= 2:
                return int(round(nums[-2])), int(round(nums[-1]))
    return None, None


def get_usd(url: str):
    html = safe_get(url)
    if not html:
        return None, None

    soup = BeautifulSoup(html, "html.parser")
    for row in soup.select("tr"):
        if "usd" in row.get_text(" ", strip=True).lower():
            nums = [num(c.get_text(" ", strip=True)) for c in row.select("td")]
            nums = [n for n in nums if n is not None]
            if len(nums) >= 2:
                return nums[0], nums[1]
    return None, None


def get_silver():
    html = safe_get(URL_SILVER)
    if not html:
        return None

    soup = BeautifulSoup(html, "html.parser")
    m = re.search(r"Silver Price\s*\$?\s*(\d+(?:\.\d+)?)",
                  soup.get_text(" ", strip=True))
    return float(m.group(1)) if m else None
# ============================================


def build_message():
    now = datetime.now(TZ).strftime("%Y-%m-%d %H:%M")

    g24b, g24s = get_gold(24)
    g21b, g21s = get_gold(21)
    g18b, g18s = get_gold(18)

    nbe_b, nbe_s = get_usd(URL_BANK_NBE)
    bm_b,  bm_s  = get_usd(URL_BANK_BM)
    cib_b, cib_s = get_usd(URL_BANK_CIB)

    silver_usd = get_silver()

    avg_sell = None
    if all(v is not None for v in [nbe_s, bm_s, cib_s]):
        avg_sell = (nbe_s + bm_s + cib_s) / 3

    silver_egp = None
    if silver_usd is not None and avg_sell is not None:
        silver_egp = (silver_usd * avg_sell) / OZ_TO_GRAM

    msg = f"""
تحديث كل ساعة – القاهرة
{now}

💵 <b>الدولار مقابل الجنيه (البنوك)</b>
<pre>
البنك          شراء               بيع
------------------------------------------------
الأهلي       {fmt_usd_arrow("nbe_buy", nbe_b)}      {fmt_usd_arrow("nbe_sell", nbe_s)}
بنك مصر     {fmt_usd_arrow("bm_buy", bm_b)}      {fmt_usd_arrow("bm_sell", bm_s)}
بنك (CIB)    {fmt_usd_arrow("cib_buy", cib_b)}      {fmt_usd_arrow("cib_sell", cib_s)}
</pre>

🥇 <b>الذهب (شراء / بيع)</b>
<pre>
العيار    شراء              بيع
------------------------------------------------
عيار 24  {fmt_gold_arrow("g24_buy", g24b)}      {fmt_gold_arrow("g24_sell", g24s)}
عيار 21  {fmt_gold_arrow("g21_buy", g21b)}      {fmt_gold_arrow("g21_sell", g21s)}
عيار 18  {fmt_gold_arrow("g18_buy", g18b)}      {fmt_gold_arrow("g18_sell", g18s)}
</pre>

🥈 <b>الفضة</b>
<pre>
عالمي : {fmt_usd(silver_usd)} {arrow("silver_usd", silver_usd)}
مصر :  {fmt_gold(silver_egp)} {arrow("silver_egp", silver_egp)}
</pre>

* سعر الفضة في مصر محسوب من السعر العالمي ومتوسط الدولار وبدون مصنعية *
BY : Ahmed Magdy
"""
    return msg.strip()


# ================== RUN ==================
while True:
    try:
        send(build_message())
    except Exception as e:
        print("Error:", e)

    time.sleep(INTERVAL)
