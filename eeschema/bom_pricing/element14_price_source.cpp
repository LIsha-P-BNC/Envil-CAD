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

#include "element14_price_source.h"
#include "bom_pricing_defs.h"

#include <json_common.h>

#include <wx/string.h>

#include <algorithm>

namespace BOM_PRICING
{

namespace
{

struct EXTRACTED_PRICE
{
    bool     found = false;
    double   price = 0.0;
    wxString currency;
};


/// Price + currency from a JSON-LD offers value, which may be an object or an array of them.
EXTRACTED_PRICE readOffers( const nlohmann::json& aOffers )
{
    EXTRACTED_PRICE out;

    const nlohmann::json* offer = &aOffers;

    if( aOffers.is_array() )
    {
        if( aOffers.empty() )
            return out;

        offer = &aOffers.front();
    }

    if( !offer->is_object() )
        return out;

    auto priceIt = offer->find( "price" );

    if( priceIt == offer->end() )
        return out;

    double price = 0.0;

    if( priceIt->is_number() )
        price = priceIt->get<double>();
    else if( priceIt->is_string() )
        wxString( priceIt->get<std::string>() ).ToCDouble( &price );

    if( price <= 0.0 )
        return out;

    out.found = true;
    out.price = price;

    auto curIt = offer->find( "priceCurrency" );

    if( curIt != offer->end() && curIt->is_string() )
        out.currency = wxString::FromUTF8( curIt->get<std::string>() ).Upper();

    return out;
}


/// A JSON-LD node (possibly nested in @graph or an array) describing a Product with offers.
EXTRACTED_PRICE readJsonLdNode( const nlohmann::json& aNode, int aDepth = 0 )
{
    if( aDepth > 3 )
        return {};

    if( aNode.is_array() )
    {
        for( const nlohmann::json& item : aNode )
        {
            EXTRACTED_PRICE out = readJsonLdNode( item, aDepth + 1 );

            if( out.found )
                return out;
        }

        return {};
    }

    if( !aNode.is_object() )
        return {};

    auto typeIt = aNode.find( "@type" );

    if( typeIt != aNode.end() && typeIt->is_string()
        && wxString::FromUTF8( typeIt->get<std::string>() ).CmpNoCase( wxS( "Product" ) ) == 0 )
    {
        auto offersIt = aNode.find( "offers" );

        if( offersIt != aNode.end() )
        {
            EXTRACTED_PRICE out = readOffers( *offersIt );

            if( out.found )
                return out;
        }
    }

    auto graphIt = aNode.find( "@graph" );

    if( graphIt != aNode.end() )
        return readJsonLdNode( *graphIt, aDepth + 1 );

    return {};
}


/// Primary extraction: every <script type="application/ld+json"> block on the page.
EXTRACTED_PRICE extractFromJsonLd( const std::string& aHtml )
{
    const std::string marker = "application/ld+json";
    size_t            pos = 0;

    while( ( pos = aHtml.find( marker, pos ) ) != std::string::npos )
    {
        size_t open = aHtml.find( '>', pos );
        size_t close = aHtml.find( "</script", open == std::string::npos ? pos : open );

        pos += marker.size();

        if( open == std::string::npos || close == std::string::npos || close <= open )
            continue;

        nlohmann::json js = nlohmann::json::parse( aHtml.substr( open + 1, close - open - 1 ),
                                                   nullptr, /* throw */ false );

        if( js.is_discarded() )
            continue;

        EXTRACTED_PRICE out = readJsonLdNode( js );

        if( out.found )
            return out;
    }

    return {};
}


/// Fallback extraction: itemprop="price" / itemprop="priceCurrency" microdata attributes.
EXTRACTED_PRICE extractFromMicrodata( const std::string& aHtml )
{
    EXTRACTED_PRICE out;

    auto attrContent = []( const std::string& aHaystack, const std::string& aAttr ) -> wxString
    {
        size_t pos = aHaystack.find( aAttr );

        if( pos == std::string::npos )
            return wxEmptyString;

        // The content attribute sits on the same tag, before or after itemprop.
        size_t tagStart = aHaystack.rfind( '<', pos );
        size_t tagEnd = aHaystack.find( '>', pos );

        if( tagStart == std::string::npos || tagEnd == std::string::npos )
            return wxEmptyString;

        std::string tag = aHaystack.substr( tagStart, tagEnd - tagStart );
        size_t      contentPos = tag.find( "content=\"" );

        if( contentPos == std::string::npos )
            return wxEmptyString;

        contentPos += 9;
        size_t contentEnd = tag.find( '"', contentPos );

        if( contentEnd == std::string::npos )
            return wxEmptyString;

        return wxString::FromUTF8( tag.substr( contentPos, contentEnd - contentPos ) );
    };

    double price = 0.0;
    attrContent( aHtml, "itemprop=\"price\"" ).ToCDouble( &price );

    if( price <= 0.0 )
        return out;

    out.found = true;
    out.price = price;
    out.currency = attrContent( aHtml, "itemprop=\"priceCurrency\"" ).Upper();

    return out;
}


/// First product-page link (".../dp/<sku>") on a search-results page, absolutized.
std::string findProductLink( const std::string& aHtml )
{
    size_t pos = aHtml.find( "/dp/" );

    if( pos == std::string::npos )
        return {};

    size_t start = aHtml.rfind( '"', pos );
    size_t end = aHtml.find( '"', pos );

    if( start == std::string::npos || end == std::string::npos || end <= start )
        return {};

    std::string url = aHtml.substr( start + 1, end - start - 1 );

    if( url.find( "http" ) != 0 )
        url = std::string( E14_IN_BASE_URL ) + url;

    return url;
}

} // namespace


std::vector<PRICE_QUOTE> ELEMENT14_PRICE_SOURCE::FetchQuotes( const wxString& aMpn, int aQty )
{
    if( !m_apiKey.IsEmpty() )
        return fetchViaApi( aMpn, aQty );

    return fetchViaScrape( aMpn, aQty );
}


std::vector<PRICE_QUOTE> ELEMENT14_PRICE_SOURCE::fetchViaApi( const wxString& aMpn, int aQty )
{
    std::vector<PRICE_QUOTE> quotes;

    try
    {
        HTTP_GET_REQUEST req;
        req.url = wxString::Format( wxString::FromUTF8( E14_API_URL_FMT ),
                                    wxString::FromUTF8( UrlEscape( aMpn ) ),
                                    wxString::FromUTF8( UrlEscape( m_apiKey ) ) ).ToStdString();
        req.headers = { { "Accept", "application/json" } };
        req.timeoutSecs = PER_REQUEST_TIMEOUT_SECS;

        HTTP_GET_RESPONSE rsp;

        if( !m_http( req, rsp ) || rsp.status != 200 )
            return {};

        nlohmann::json js = nlohmann::json::parse( rsp.body, nullptr, /* throw */ false );

        if( js.is_discarded() || !js.is_object() )
            return {};

        // The wrapper key names the search type (manufacturerPartNumberSearchReturn, ...);
        // just find whichever top-level object carries the products array.
        const nlohmann::json* products = nullptr;

        for( const auto& [key, value] : js.items() )
        {
            if( value.is_object() && value.contains( "products" )
                && value["products"].is_array() )
            {
                products = &value["products"];
                break;
            }
        }

        if( !products )
            return {};

        wxString wantedMpn = aMpn;
        wantedMpn.Trim( true ).Trim( false );

        for( const nlohmann::json& product : *products )
        {
            if( !product.is_object() )
                continue;

            // Exact-MPN filter: a close-but-different part priced confidently is worse than
            // no price at all.  Multiple exact matches (cut tape vs reel packs) all go in;
            // the service keeps the cheapest.
            wxString productMpn = wxString::FromUTF8(
                    product.value( "translatedManufacturerPartNumber", std::string() ) );

            if( productMpn.Trim( true ).Trim( false ).CmpNoCase( wantedMpn ) != 0 )
                continue;

            auto pricesIt = product.find( "prices" );

            if( pricesIt == product.end() || !pricesIt->is_array() )
                continue;

            PRICE_QUOTE quote;
            quote.sourceName = Name();
            quote.currency = wxString::FromUTF8( E14_API_CURRENCY );

            for( const nlohmann::json& tier : *pricesIt )
            {
                if( !tier.is_object() )
                    continue;

                int    from = tier.value( "from", 1 );
                double cost = tier.value( "cost", 0.0 );

                if( cost > 0.0 )
                    quote.breaks.push_back( { std::max( from, 1 ), cost } );
            }

            if( quote.breaks.empty() )
                continue;

            quote.found = true;
            quote.price = PriceAtQty( quote.breaks, aQty );

            std::string sku = product.value( "sku", std::string() );

            if( !sku.empty() )
            {
                quote.productUrl = wxString::FromUTF8( std::string( E14_IN_BASE_URL )
                                                       + "/search?st=" + sku );
            }

            quotes.push_back( std::move( quote ) );
        }
    }
    catch( ... )
    {
        return {};
    }

    return quotes;
}


std::vector<PRICE_QUOTE> ELEMENT14_PRICE_SOURCE::fetchViaScrape( const wxString& aMpn, int aQty )
{
    PRICE_QUOTE quote;
    quote.sourceName = Name();

    try
    {
        HTTP_GET_REQUEST req;
        req.url = wxString::Format( wxString::FromUTF8( E14_IN_SEARCH_URL_FMT ),
                                    wxString::FromUTF8( UrlEscape( aMpn ) ) ).ToStdString();
        req.headers = { { "Accept", "text/html,application/xhtml+xml" },
                        { "Accept-Language", "en-IN,en;q=0.9" } };
        req.timeoutSecs = PER_REQUEST_TIMEOUT_SECS;
        req.followRedirects = true;   // exact-MPN searches 302 to the product page

        HTTP_GET_RESPONSE rsp;

        if( !m_http( req, rsp ) || rsp.status != 200 )
            return {};

        quote.productUrl = wxString::FromUTF8( req.url );

        EXTRACTED_PRICE extracted = extractFromJsonLd( rsp.body );

        if( !extracted.found )
            extracted = extractFromMicrodata( rsp.body );

        // Still a search-results page?  One extra hop to the first matching product.
        if( !extracted.found )
        {
            std::string productUrl = findProductLink( rsp.body );

            if( !productUrl.empty() )
            {
                HTTP_GET_REQUEST productReq = req;
                productReq.url = productUrl;

                HTTP_GET_RESPONSE productRsp;

                if( m_http( productReq, productRsp ) && productRsp.status == 200 )
                {
                    extracted = extractFromJsonLd( productRsp.body );

                    if( !extracted.found )
                        extracted = extractFromMicrodata( productRsp.body );

                    if( extracted.found )
                        quote.productUrl = wxString::FromUTF8( productUrl );
                }
            }
        }

        if( !extracted.found )
            return {};

        quote.found = true;
        quote.price = extracted.price;
        quote.currency = extracted.currency.IsEmpty() ? wxString( wxS( "INR" ) ) : extracted.currency;

        // JSON-LD carries a single (qty-1) offer price; parsing the HTML price-break table is
        // deliberately out of scope — too brittle for a keyless scrape.
        quote.breaks = { { 1, quote.price } };
        ( void) aQty;
    }
    catch( ... )
    {
        return {};
    }

    return quote.found ? std::vector<PRICE_QUOTE>{ quote } : std::vector<PRICE_QUOTE>{};
}

} // namespace BOM_PRICING
