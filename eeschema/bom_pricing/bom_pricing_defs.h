/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BOM_PRICING_DEFS_H
#define BOM_PRICING_DEFS_H

/**
 * Every externally-owned URL and tunable the BOM pricing lookups depend on, in one place.
 *
 * These are public, keyless endpoints (the user chose no distributor API keys), so they can
 * change shape or start refusing us at any time.  When that happens the repair should be a
 * one-file edit here (or in the matching *_price_source.cpp), never a hunt through the tree.
 */
namespace BOM_PRICING
{

// LCSC catalog pricing (USD) via JLCPCB's keyless parts API — the direct wmsc.lcsc.com
// endpoints sit behind a bot wall (403), this one answers automated clients.
constexpr const char* JLC_SEARCH_URL =
        "https://cart.jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/smtGood/selectSmtComponentList";
constexpr const char* JLC_SEARCH_BODY_FMT =
        "{\"keyword\":\"%s\",\"currentPage\":1,\"pageSize\":10,\"searchSource\":\"search\"}";

// Findchips aggregates many distributors (DigiKey, Mouser, element14, Arrow, TME, ...) on one
// server-rendered page; element14 Asia-Pacific rows come back in INR directly.
constexpr const char* FINDCHIPS_SEARCH_URL_FMT = "https://www.findchips.com/search/%s";

// element14 India serves INR prices server-side; an exact-MPN search 302s to the product page.
// NOTE: fronted by Akamai, which 403s most automated clients — kept as an optional source
// only; Findchips carries the same element14 prices reliably.
constexpr const char* E14_IN_SEARCH_URL_FMT = "https://in.element14.com/search?st=%s";
constexpr const char* E14_IN_BASE_URL       = "https://in.element14.com";

// Keyless USD->INR rate sources, tried in order; the constant is the last resort so a BOM is
// never priced at zero just because the FX endpoints are down.  Update when touched.
constexpr const char* FX_PRIMARY_URL    = "https://open.er-api.com/v6/latest/USD";
constexpr const char* FX_FALLBACK_URL   = "https://api.frankfurter.app/latest?from=USD&to=INR";
constexpr double      FALLBACK_USD_INR  = 96.5;

// Sent on every request: these endpoints refuse clients that don't look like a browser.
constexpr const char* BROWSER_USER_AGENT =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/129.0.0.0 Safari/537.36";

constexpr long PER_REQUEST_TIMEOUT_SECS = 5;    // total per-request cap; payloads are small
constexpr int  INTER_REQUEST_DELAY_MS   = 400;  // politeness throttle between network hits
constexpr int  MAX_ATTEMPTS_PER_REQUEST = 2;    // 1 retry, matching the installer's non-fatal style

constexpr int  POSITIVE_CACHE_TTL_HOURS_DEFAULT = 24;
constexpr int  NEGATIVE_CACHE_TTL_HOURS         = 6;  // unknown parts recover quicker
constexpr int  FX_CACHE_TTL_HOURS               = 24;

} // namespace BOM_PRICING

#endif // BOM_PRICING_DEFS_H
