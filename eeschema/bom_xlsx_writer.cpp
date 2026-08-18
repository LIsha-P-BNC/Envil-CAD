/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "bom_xlsx_writer.h"

#include <fields_data_model.h>
#include <settings/bom_settings.h>

#include <wx/datetime.h>
#include <wx/filename.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#include <algorithm>
#include <vector>


// Indexes into <cellXfs> of styles.xml (see buildStylesXml)
enum XLSX_STYLE
{
    XF_DEFAULT = 0,
    XF_TITLE,           // "Bill of Materials" banner
    XF_RELEASE_LABEL,   // "Release:" (blue box, right aligned)
    XF_RELEASE_VALUE,   // release tag (blue box)
    XF_META,            // bold title-block entries
    XF_HEADER,          // green table header
    XF_DATA,            // normal data cell
    XF_DATA_CENTER,     // S.No cells
    XF_DATA_NUM,        // quantity cells
    XF_DATA_MONEY,      // price/subtotal cells (0.00)
    XF_TOTAL,           // totals-row quantity
    XF_TOTAL_MONEY,     // totals-row subtotal
    XF_BOLD,            // Approved / Notes / legend labels
    XF_INFO_LABEL,      // Project Information keys
    XF_INFO_VALUE       // Project Information values
};


static wxString xmlEscape( const wxString& aStr )
{
    wxString out = aStr;
    out.Replace( wxS( "&" ), wxS( "&amp;" ) );
    out.Replace( wxS( "<" ), wxS( "&lt;" ) );
    out.Replace( wxS( ">" ), wxS( "&gt;" ) );
    out.Replace( wxS( "\"" ), wxS( "&quot;" ) );
    return out;
}


// 0-based sheet column index -> "A", "B", ... "AA", ...
static wxString colLetter( int aCol )
{
    wxString letter;

    for( int c = aCol; c >= 0; c = c / 26 - 1 )
        letter.insert( 0, (wxChar) ( 'A' + c % 26 ) );

    return letter;
}


static wxString cellRef( int aCol, int aRow1 )
{
    return colLetter( aCol ) + wxString::Format( wxS( "%d" ), aRow1 );
}


static wxString inlineStrCell( int aCol, int aRow1, const wxString& aValue, int aStyle )
{
    return wxString::Format( wxS( "<c r=\"%s\" s=\"%d\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%s</t></is></c>" ),
                             cellRef( aCol, aRow1 ), aStyle, xmlEscape( aValue ) );
}


static wxString numberCell( int aCol, int aRow1, const wxString& aNumber, int aStyle )
{
    return wxString::Format( wxS( "<c r=\"%s\" s=\"%d\"><v>%s</v></c>" ),
                             cellRef( aCol, aRow1 ), aStyle, aNumber );
}


static wxString formulaCell( int aCol, int aRow1, const wxString& aFormula, int aStyle )
{
    return wxString::Format( wxS( "<c r=\"%s\" s=\"%d\"><f>%s</f></c>" ),
                             cellRef( aCol, aRow1 ), aStyle, xmlEscape( aFormula ) );
}


static bool isIntegral( const wxString& aStr )
{
    long dummy;
    return !aStr.IsEmpty() && aStr.ToLong( &dummy );
}


static bool isNumeric( const wxString& aStr )
{
    double dummy;
    return !aStr.IsEmpty() && aStr.ToCDouble( &dummy );
}


static wxString buildStylesXml()
{
    return wxS(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"6\">"
        "<font><sz val=\"10\"/><name val=\"Arial\"/></font>"
        "<font><b/><sz val=\"24\"/><name val=\"Arial\"/></font>"
        "<font><b/><sz val=\"16\"/><name val=\"Arial\"/></font>"
        "<font><b/><sz val=\"18\"/><name val=\"Arial\"/></font>"
        "<font><b/><sz val=\"10\"/><name val=\"Arial\"/></font>"
        "<font><b/><sz val=\"9\"/><name val=\"Arial\"/></font>"
        "</fonts>"
        "<fills count=\"4\">"
        "<fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFC5D9F1\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFCCFFCC\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "</fills>"
        "<borders count=\"2\">"
        "<border><left/><right/><top/><bottom/><diagonal/></border>"
        "<border><left style=\"thin\"><color indexed=\"64\"/></left>"
        "<right style=\"thin\"><color indexed=\"64\"/></right>"
        "<top style=\"thin\"><color indexed=\"64\"/></top>"
        "<bottom style=\"thin\"><color indexed=\"64\"/></bottom><diagonal/></border>"
        "</borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"15\">"
        // XF_DEFAULT
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        // XF_TITLE
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        // XF_RELEASE_LABEL
        "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\">"
        "<alignment horizontal=\"right\" wrapText=\"1\"/></xf>"
        // XF_RELEASE_VALUE
        "<xf numFmtId=\"0\" fontId=\"3\" fillId=\"2\" borderId=\"1\" xfId=\"0\"/>"
        // XF_META
        "<xf numFmtId=\"0\" fontId=\"4\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyAlignment=\"1\">"
        "<alignment horizontal=\"left\"/></xf>"
        // XF_HEADER
        "<xf numFmtId=\"0\" fontId=\"5\" fillId=\"3\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\">"
        "<alignment horizontal=\"left\" wrapText=\"1\"/></xf>"
        // XF_DATA
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\">"
        "<alignment horizontal=\"left\" vertical=\"top\" wrapText=\"1\"/></xf>"
        // XF_DATA_CENTER
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\">"
        "<alignment horizontal=\"center\" vertical=\"top\"/></xf>"
        // XF_DATA_NUM
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\">"
        "<alignment horizontal=\"right\" vertical=\"top\"/></xf>"
        // XF_DATA_MONEY
        "<xf numFmtId=\"2\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyAlignment=\"1\">"
        "<alignment horizontal=\"right\" vertical=\"top\"/></xf>"
        // XF_TOTAL
        "<xf numFmtId=\"0\" fontId=\"4\" fillId=\"0\" borderId=\"1\" xfId=\"0\"/>"
        // XF_TOTAL_MONEY
        "<xf numFmtId=\"2\" fontId=\"4\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\"/>"
        // XF_BOLD
        "<xf numFmtId=\"0\" fontId=\"4\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        // XF_INFO_LABEL
        "<xf numFmtId=\"0\" fontId=\"4\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyAlignment=\"1\">"
        "<alignment horizontal=\"left\"/></xf>"
        // XF_INFO_VALUE
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyAlignment=\"1\">"
        "<alignment horizontal=\"left\"/></xf>"
        "</cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        // dxf 0: light orange fill used by the "NP" conditional format
        "<dxfs count=\"1\"><dxf><fill><patternFill><bgColor rgb=\"FFFABF8F\"/></patternFill></fill></dxf></dxfs>"
        "</styleSheet>" );
}


bool BOM_XLSX_WRITER::Write( const wxString& aOutPath, FIELDS_EDITOR_GRID_DATA_MODEL* aModel,
                             const BOM_FMT_PRESET& aFmt, const PROJECT_INFO& aInfo, wxString* aErrMsg )
{
    auto setErr =
            [&]( const wxString& msg )
            {
                if( aErrMsg )
                    *aErrMsg = msg;
            };

    if( !aModel )
    {
        setErr( wxS( "No data model" ) );
        return false;
    }

    // --- Collect the shown columns in display order ------------------------------------

    struct SHEET_COL
    {
        int      modelCol;
        wxString label;
        int      maxLen;
    };

    std::vector<SHEET_COL> cols;
    int                    refPos = -1;       // positions within `cols`
    int                    valuePos = -1;
    int                    qtyPos = -1;
    int                    unitPricePos = -1;
    int                    subtotalPos = -1;

    for( const BOM_FIELD& field : aModel->GetFieldsOrdered() )
    {
        if( !field.show )
            continue;

        int modelCol = aModel->GetFieldNameCol( field.name );

        if( modelCol < 0 )
            continue;

        wxString lowerLabel = field.label.Lower();
        int      pos = (int) cols.size();

        if( aModel->ColIsReference( modelCol ) )
            refPos = pos;
        else if( aModel->ColIsValue( modelCol ) )
            valuePos = pos;
        else if( aModel->ColIsQuantity( modelCol ) )
            qtyPos = pos;
        else if( lowerLabel.Contains( wxS( "subtotal" ) ) )
            subtotalPos = pos;
        else if( lowerLabel.Contains( wxS( "price" ) ) )
            unitPricePos = pos;

        cols.push_back( { modelCol, field.label, (int) field.label.length() } );
    }

    if( cols.empty() )
    {
        setErr( _( "No columns are enabled for export" ) );
        return false;
    }

    // --- Collect the data rows ---------------------------------------------------------

    std::vector<std::vector<wxString>> rows;

    for( int row = 0; row < aModel->GetNumberRows(); ++row )
    {
        if( aModel->GetRowFlags( row ) == CHILD_ITEM )
            continue;

        std::vector<wxString> values;

        for( SHEET_COL& col : cols )
        {
            wxString value = aModel->GetExportValue( row, col.modelCol, aFmt.refDelimiter,
                                                     aFmt.refRangeDelimiter );
            col.maxLen = std::max( col.maxLen, (int) value.length() );
            values.push_back( value );
        }

        rows.push_back( std::move( values ) );
    }

    // --- Sheet geometry ----------------------------------------------------------------
    // Sheet column 0 is the generated "S.No" column; field columns start at 1.
    // Rows (1-based): 2 title, 3-5 metadata, 7 header, 8.. data, then totals/sign-off.

    const int HEADER_ROW = 7;
    const int FIRST_DATA_ROW = HEADER_ROW + 1;
    const int lastDataRow = HEADER_ROW + (int) rows.size();
    const int totalsRow = lastDataRow + 1;
    const int approvedRow = totalsRow + 1;
    const int legendRow = totalsRow + 9;      // matches the Altium template's legend offset
    const int lastSheetCol = (int) cols.size();

    auto sheetCol = []( int aPos ) { return aPos + 1; };

    wxString sheet1;
    sheet1 << wxS( "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>" )
           << wxS( "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">" );

    // Column widths
    sheet1 << wxS( "<cols>" )
           << wxS( "<col min=\"1\" max=\"1\" width=\"8.5\" customWidth=\"1\"/>" );

    for( size_t i = 0; i < cols.size(); ++i )
    {
        double width = std::min( 45.0, std::max( 10.0, cols[i].maxLen + 3.0 ) );
        sheet1 << wxString::Format( wxS( "<col min=\"%d\" max=\"%d\" width=\"%.1f\" customWidth=\"1\"/>" ),
                                    (int) i + 2, (int) i + 2, width );
    }

    sheet1 << wxS( "</cols><sheetData>" );

    // Title block
    wxString releaseTag = aInfo.releaseTag.IsEmpty() ? wxString( wxS( "NR" ) ) : aInfo.releaseTag;
    wxString variant = aInfo.variantName.IsEmpty() ? _( "None" ) : aInfo.variantName;

    sheet1 << wxS( "<row r=\"2\" ht=\"30\" customHeight=\"1\">" )
           << inlineStrCell( 0, 2, _( "Bill of Materials" ), XF_TITLE )
           << inlineStrCell( 4, 2, _( "Release:" ), XF_RELEASE_LABEL )
           << inlineStrCell( 5, 2, releaseTag, XF_RELEASE_VALUE )
           << wxS( "</row>" );
    sheet1 << wxS( "<row r=\"3\">" )
           << inlineStrCell( 0, 3, _( "Source Data From:" ), XF_META )
           << inlineStrCell( 2, 3, aInfo.sourceFilename, XF_META )
           << wxS( "</row>" );
    sheet1 << wxS( "<row r=\"4\">" )
           << inlineStrCell( 0, 4, _( "Project:" ), XF_META )
           << inlineStrCell( 2, 4, aInfo.projectFilename, XF_META )
           << wxS( "</row>" );
    sheet1 << wxS( "<row r=\"5\">" )
           << inlineStrCell( 0, 5, _( "Variant:" ), XF_META )
           << inlineStrCell( 2, 5, variant, XF_META )
           << wxS( "</row>" );

    // Header row
    sheet1 << wxString::Format( wxS( "<row r=\"%d\">" ), HEADER_ROW )
           << inlineStrCell( 0, HEADER_ROW, _( "S.No" ), XF_HEADER );

    for( size_t i = 0; i < cols.size(); ++i )
        sheet1 << inlineStrCell( sheetCol( (int) i ), HEADER_ROW, cols[i].label, XF_HEADER );

    sheet1 << wxS( "</row>" );

    // Data rows
    long totalQty = 0;

    for( size_t r = 0; r < rows.size(); ++r )
    {
        int sheetRow = FIRST_DATA_ROW + (int) r;

        sheet1 << wxString::Format( wxS( "<row r=\"%d\">" ), sheetRow );

        // S.No: self-numbering when a reference column exists, literal otherwise
        if( refPos >= 0 )
        {
            sheet1 << formulaCell( 0, sheetRow,
                                   wxString::Format( wxS( "IF(%s%d=\"\",\"\",ROW()-%d)" ),
                                                     colLetter( sheetCol( refPos ) ), sheetRow, HEADER_ROW ),
                                   XF_DATA_CENTER );
        }
        else
        {
            sheet1 << numberCell( 0, sheetRow, wxString::Format( wxS( "%d" ), (int) r + 1 ), XF_DATA_CENTER );
        }

        for( size_t i = 0; i < cols.size(); ++i )
        {
            const wxString& value = rows[r][i];
            int             col = sheetCol( (int) i );
            int             pos = (int) i;

            if( pos == qtyPos && isIntegral( value ) )
            {
                long qty = 0;
                value.ToLong( &qty );
                totalQty += qty;
                sheet1 << numberCell( col, sheetRow, value, XF_DATA_NUM );
            }
            else if( pos == subtotalPos && unitPricePos >= 0 && qtyPos >= 0 )
            {
                sheet1 << formulaCell( col, sheetRow,
                                       wxString::Format( wxS( "%s%d*%s%d" ),
                                                         colLetter( sheetCol( unitPricePos ) ), sheetRow,
                                                         colLetter( sheetCol( qtyPos ) ), sheetRow ),
                                       XF_DATA_MONEY );
            }
            else if( ( pos == unitPricePos || pos == subtotalPos ) && isNumeric( value ) )
            {
                sheet1 << numberCell( col, sheetRow, value, XF_DATA_MONEY );
            }
            else
            {
                sheet1 << inlineStrCell( col, sheetRow, value, XF_DATA );
            }
        }

        sheet1 << wxS( "</row>" );
    }

    // Totals row
    if( qtyPos >= 0 || subtotalPos >= 0 )
    {
        sheet1 << wxString::Format( wxS( "<row r=\"%d\">" ), totalsRow );

        if( qtyPos >= 0 )
        {
            wxString l = colLetter( sheetCol( qtyPos ) );
            sheet1 << formulaCell( sheetCol( qtyPos ), totalsRow,
                                   wxString::Format( wxS( "SUM(%s%d:%s%d)" ), l, FIRST_DATA_ROW, l, lastDataRow ),
                                   XF_TOTAL );
        }

        if( subtotalPos >= 0 )
        {
            wxString l = colLetter( sheetCol( subtotalPos ) );
            sheet1 << formulaCell( sheetCol( subtotalPos ), totalsRow,
                                   wxString::Format( wxS( "SUM(%s%d:%s%d)" ), l, FIRST_DATA_ROW, l, lastDataRow ),
                                   XF_TOTAL_MONEY );
        }

        sheet1 << wxS( "</row>" );
    }

    // Sign-off row
    sheet1 << wxString::Format( wxS( "<row r=\"%d\">" ), approvedRow )
           << inlineStrCell( 0, approvedRow, _( "Approved" ), XF_BOLD )
           << inlineStrCell( 2, approvedRow, _( "Notes" ), XF_BOLD )
           << wxS( "</row>" );

    // Legend block
    int legendHdrCol = qtyPos >= 0 ? sheetCol( qtyPos ) : lastSheetCol;

    sheet1 << wxString::Format( wxS( "<row r=\"%d\">" ), legendRow )
           << inlineStrCell( legendHdrCol, legendRow, _( "Release Notes" ), XF_BOLD )
           << wxS( "</row>" );
    sheet1 << wxString::Format( wxS( "<row r=\"%d\">" ), legendRow + 1 )
           << inlineStrCell( 1, legendRow + 1, wxS( "NP" ), XF_DEFAULT )
           << inlineStrCell( 2, legendRow + 1, _( "NON MOUNTING COMPONENTS" ), XF_DEFAULT )
           << inlineStrCell( legendHdrCol, legendRow + 1, _( "DATE" ), XF_BOLD )
           << wxS( "</row>" );
    sheet1 << wxString::Format( wxS( "<row r=\"%d\">" ), legendRow + 2 )
           << inlineStrCell( 2, legendRow + 2, _( "WIRING HARNESS COMPONENT DETAILS" ), XF_DEFAULT )
           << wxS( "</row>" );
    sheet1 << wxString::Format( wxS( "<row r=\"%d\">" ), legendRow + 3 )
           << inlineStrCell( 2, legendRow + 3, _( "NEW MOUNTING COMPONENTS" ), XF_DEFAULT )
           << wxS( "</row>" );

    sheet1 << wxS( "</sheetData>" );

    // Excel-style per-column dropdown filters over the whole table
    sheet1 << wxString::Format( wxS( "<autoFilter ref=\"A%d:%s%d\"/>" ),
                                HEADER_ROW, colLetter( lastSheetCol ), approvedRow );

    sheet1 << wxS( "<mergeCells count=\"1\"><mergeCell ref=\"A2:B2\"/></mergeCells>" );

    // "NP" highlighting on the value column and on the legend swatch
    if( valuePos >= 0 && !rows.empty() )
    {
        wxString l = colLetter( sheetCol( valuePos ) );
        sheet1 << wxString::Format( wxS( "<conditionalFormatting sqref=\"%s%d:%s%d\">"
                                         "<cfRule type=\"expression\" dxfId=\"0\" priority=\"1\">"
                                         "<formula>$%s%d=\"NP\"</formula></cfRule></conditionalFormatting>" ),
                                    l, FIRST_DATA_ROW, l, lastDataRow, l, FIRST_DATA_ROW );
    }

    sheet1 << wxString::Format( wxS( "<conditionalFormatting sqref=\"B%d\">"
                                     "<cfRule type=\"expression\" dxfId=\"0\" priority=\"2\">"
                                     "<formula>$B%d=\"NP\"</formula></cfRule></conditionalFormatting>" ),
                                legendRow + 1, legendRow + 1 );

    sheet1 << wxS( "</worksheet>" );

    // --- Sheet 2: Project Information --------------------------------------------------

    wxDateTime now = wxDateTime::Now();
    wxString   reportTime = now.Format( wxS( "%I:%M %p" ) );
    wxString   reportDate = now.Format( wxS( "%m/%d/%Y" ) );

    if( totalQty == 0 )
        totalQty = (long) rows.size();

    std::vector<std::pair<wxString, wxString>> info =
    {
        { _( "Project Full Path" ),             aInfo.projectFullPath },
        { _( "Project Filename" ),              aInfo.projectFilename },
        { _( "Variant Name" ),                  variant },
        { _( "Data-Source Filename" ),          aInfo.sourceFilename },
        { _( "Data-Source Full Path" ),         aInfo.sourceFullPath },
        { _( "Title" ),                         aInfo.title },
        { _( "Total Quantity" ),                wxString::Format( wxS( "%ld" ), totalQty ) },
        { _( "Report Time" ),                   reportTime },
        { _( "Report Date" ),                   reportDate },
        { _( "Report Date & Time" ),            reportDate + wxS( " " ) + reportTime },
        { _( "Output Name" ),                   _( "Bill of Materials" ) },
        { _( "Output Type" ),                   wxS( "BomReport" ) },
        { _( "Output Generator Name" ),         wxS( "BOM" ) },
        { _( "Output Generator Description" ),  _( "Bill of Materials" ) },
    };

    wxString sheet2;
    sheet2 << wxS( "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>" )
           << wxS( "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">" )
           << wxS( "<cols>" )
           << wxS( "<col min=\"1\" max=\"1\" width=\"30.5\" customWidth=\"1\"/>" )
           << wxS( "<col min=\"2\" max=\"2\" width=\"108.5\" customWidth=\"1\"/>" )
           << wxS( "</cols><sheetData>" );

    for( size_t i = 0; i < info.size(); ++i )
    {
        int row = (int) i + 1;
        sheet2 << wxString::Format( wxS( "<row r=\"%d\">" ), row )
               << inlineStrCell( 0, row, info[i].first, XF_INFO_LABEL )
               << inlineStrCell( 1, row, info[i].second, XF_INFO_VALUE )
               << wxS( "</row>" );
    }

    sheet2 << wxS( "</sheetData></worksheet>" );

    // --- Package everything into the zip container -------------------------------------

    wxFFileOutputStream fileStream( aOutPath );

    if( !fileStream.IsOk() )
    {
        setErr( wxString::Format( _( "Could not create file '%s'." ), aOutPath ) );
        return false;
    }

    wxZipOutputStream zip( fileStream );

    auto addFile =
            [&]( const wxString& aName, const wxString& aContent ) -> bool
            {
                if( !zip.PutNextEntry( aName ) )
                    return false;

                wxScopedCharBuffer utf8 = aContent.utf8_str();
                zip.Write( utf8.data(), utf8.length() );
                return zip.IsOk();
            };

    bool ok = true;

    ok &= addFile( wxS( "[Content_Types].xml" ), wxS(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet2.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
        "</Types>" ) );

    ok &= addFile( wxS( "_rels/.rels" ), wxS(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"xl/workbook.xml\"/>"
        "</Relationships>" ) );

    ok &= addFile( wxS( "xl/workbook.xml" ), wxS(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets>"
        "<sheet name=\"Manufacturing\" sheetId=\"1\" r:id=\"rId1\"/>"
        "<sheet name=\"Project Information\" sheetId=\"2\" r:id=\"rId2\"/>"
        "</sheets>"
        "</workbook>" ) );

    ok &= addFile( wxS( "xl/_rels/workbook.xml.rels" ), wxS(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
        "Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
        "Target=\"worksheets/sheet2.xml\"/>"
        "<Relationship Id=\"rId3\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
        "Target=\"styles.xml\"/>"
        "</Relationships>" ) );

    ok &= addFile( wxS( "xl/styles.xml" ), buildStylesXml() );
    ok &= addFile( wxS( "xl/worksheets/sheet1.xml" ), sheet1 );
    ok &= addFile( wxS( "xl/worksheets/sheet2.xml" ), sheet2 );

    ok &= zip.Close();
    fileStream.Close();

    if( !ok )
        setErr( wxString::Format( _( "Could not write '%s'." ), aOutPath ) );

    return ok;
}
