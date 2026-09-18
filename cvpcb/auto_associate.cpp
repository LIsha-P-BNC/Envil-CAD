/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * @file auto_associate.cpp
 */

// This file handle automatic selection of footprints, from .equ files which give
// a footprint LIB_ID associated to a component value.
// These associations have this form:
// 'FT232BL'		'QFP:LQFP-32_7x7mm_Pitch0.8mm'


#include <algorithm>
#include <vector>

#include <kiface_base.h>
#include <string_utils.h>
#include <macros.h>

#include <auto_associate.h>
#include <cvpcb_association.h>
#include <cvpcb_mainframe.h>
#include <footprint_filter.h>
#include <footprint_info.h>
#include <listboxes.h>
#include <project/project_file.h>
#include <wx/msgdlg.h>
#include <wx/tokenzr.h>

#define QUOTE   '\''


/**
 * Read the string between quotes.
 *
 * @return a the quoted string.
 */
wxString GetQuotedText( wxString& text )
{
    int i = text.Find( QUOTE );

    if( wxNOT_FOUND == i )
        return wxT( "" );

    wxString shrt = text.Mid( i + 1 );
    i = shrt.Find( QUOTE );

    if( wxNOT_FOUND == i )
        return wxT( "" );

    text = shrt.Mid( i + 1 );
    return shrt.Mid( 0, i );
}


// A sort compare function, used to sort a FOOTPRINT_EQUIVALENCE_LIST by cmp values
// (m_ComponentValue member)
bool sortListbyCmpValue( const FOOTPRINT_EQUIVALENCE& ref, const FOOTPRINT_EQUIVALENCE& test )
{
    return ref.m_ComponentValue.Cmp( test.m_ComponentValue ) > 0;
}


int CVPCB_MAINFRAME::buildEquivalenceList( FOOTPRINT_EQUIVALENCE_LIST& aList,
                                           wxString* aErrorMessages )
{
    char        line[1024];
    int         error_count = 0;
    FILE*       file;
    wxFileName  fn;
    wxString    tmp, error_msg;

    SEARCH_STACK& search  = Kiface().KifaceSearch();
    PROJECT_FILE& project = Prj().GetProjectFile();

    // Find equivalences in all available files, and populates the
    // equiv_List with all equivalences found in .equ files
    for( const wxString& equfile : project.m_EquivalenceFiles )
    {
        fn =  wxExpandEnvVars( equfile );

        if( fn.IsAbsolute() || fn.FileExists() )
            tmp = fn.GetFullPath();
        else
            tmp = search.FindValidPath( fn.GetFullPath() );

        if( !tmp )
        {
            error_count++;

            if( aErrorMessages )
            {
                error_msg.Printf( _( "Equivalence file '%s' could not be found." ),
                                  fn.GetFullName() );

                if( ! aErrorMessages->IsEmpty() )
                    *aErrorMessages << wxT( "\n\n" );

                *aErrorMessages += error_msg;
            }

            continue;
        }

        file = wxFopen( tmp, wxT( "rt" ) );

        if( file == nullptr )
        {
            error_count++;

            if( aErrorMessages )
            {
                error_msg.Printf( _( "Error opening equivalence file '%s'." ), tmp );

                if( ! aErrorMessages->IsEmpty() )
                    *aErrorMessages << wxT( "\n\n" );

                *aErrorMessages += error_msg;
            }

            continue;
        }

        while( GetLine( file, line, nullptr, sizeof( line ) ) != nullptr )
        {
            if( *line == 0 )
                continue;

            wxString wtext = From_UTF8( line );
            wxString value = GetQuotedText( wtext );

            if( value.IsEmpty() )
                continue;

            wxString footprint = GetQuotedText( wtext );

            if( footprint.IsEmpty() )
                continue;

            value.Replace( wxT( " " ), wxT( "_" ) );

            FOOTPRINT_EQUIVALENCE* equivItem = new FOOTPRINT_EQUIVALENCE();
            equivItem->m_ComponentValue = value;
            equivItem->m_FootprintFPID = footprint;
            aList.push_back( equivItem );
        }

        fclose( file );
    }

    return error_count;
}


// Package-hint tokens (e.g. "0603", "SOT-23", "TO-220") pulled from a component's value,
// symbol name and fields.  Package designators essentially always contain a digit; requiring
// one keeps ordinary words ("red", "power") from producing false footprint matches.
static wxArrayString getPackageHints( COMPONENT* aComponent )
{
    wxString text = aComponent->GetValue() + wxS( " " ) + aComponent->GetName();

    for( const auto& [fieldName, fieldValue] : aComponent->GetFields() )
        text << wxS( " " ) << fieldValue;

    wxArrayString     hints;
    wxStringTokenizer tokenizer( text.Lower(), wxS( " \t\r\n,;()[]" ), wxTOKEN_STRTOK );

    while( tokenizer.HasMoreTokens() )
    {
        wxString token = tokenizer.GetNextToken();

        if( token.length() < 3 )
            continue;

        bool hasDigit = false;

        for( wxUniChar c : token )
            hasDigit |= wxIsdigit( c );

        if( hasDigit && hints.Index( token ) == wxNOT_FOUND )
            hints.Add( token );
    }

    return hints;
}


// Hint matches dominate the score; a reference-prefix match ("R" -> "R_0603...") only breaks
// ties between candidates that hint-score equally.
static int scoreCandidate( const wxString& aFootprintName, const wxArrayString& aHints,
                           const wxString& aRefPrefix )
{
    wxString name  = aFootprintName.Lower();
    int      score = 0;

    for( const wxString& hint : aHints )
    {
        int pos = name.Find( hint );

        // An earlier match ranks higher: imperial/metric dual-named packages carry the
        // imperial size first ("R_0603_1608Metric"), so the hint "0603" must prefer that
        // over "R_0201_0603Metric" where it matches the metric half.
        if( pos != wxNOT_FOUND )
            score += 10 * (int) hint.length() - std::min( pos, 9 );
    }

    if( !aRefPrefix.IsEmpty() && name.StartsWith( aRefPrefix.Lower() + wxS( "_" ) ) )
        score += 1;

    return score;
}


// Find the best footprint for aComponent among the loaded libraries using its footprint
// filters, pin count and package hints.  Returns an empty string when no candidate stands
// out — guessing blindly among hundreds of filter matches would be worse than not assigning.
static wxString findBestFootprintMatch( FOOTPRINT_LIST& aList, COMPONENT* aComponent )
{
    const wxArrayString& filters = aComponent->GetFootprintFilters();
    wxArrayString        hints = getPackageHints( aComponent );

    wxString refPrefix;

    for( wxUniChar c : aComponent->GetReference() )
    {
        if( wxIsdigit( c ) )
            break;

        refPrefix << c;
    }

    // Without footprint filters, package hints are the only trustworthy signal.
    if( filters.IsEmpty() && hints.IsEmpty() )
        return wxEmptyString;

    auto collect = [&]( bool aUsePinCount ) -> std::vector<const FOOTPRINT_INFO*>
    {
        FOOTPRINT_FILTER filter( aList );

        if( !filters.IsEmpty() )
            filter.FilterByFootprintFilters( filters );

        if( aUsePinCount && aComponent->GetPinCount() > 0 )
            filter.FilterByPinCount( aComponent->GetPinCount() );

        std::vector<const FOOTPRINT_INFO*> found;

        for( FOOTPRINT_INFO& fp : filter )
            found.push_back( &fp );

        return found;
    };

    std::vector<const FOOTPRINT_INFO*> candidates = collect( true );

    // The netlist pin count can disagree with the footprint pad count (hidden pins,
    // thermal/mounting pads, multi-unit symbols); if it eliminated everything, retry on
    // the filters alone.
    if( candidates.empty() && !filters.IsEmpty() )
        candidates = collect( false );

    if( candidates.empty() )
        return wxEmptyString;

    auto libId = []( const FOOTPRINT_INFO* aInfo ) -> wxString
    {
        return aInfo->GetLibNickname() + wxS( ":" ) + aInfo->GetFootprintName();
    };

    if( candidates.size() == 1 )
        return libId( candidates[0] );

    const FOOTPRINT_INFO* best = nullptr;
    int                   bestScore = 0;

    for( const FOOTPRINT_INFO* candidate : candidates )
    {
        int score = scoreCandidate( candidate->GetFootprintName(), hints, refPrefix );

        if( score > bestScore )
        {
            best = candidate;
            bestScore = score;
        }
    }

    // Only assign when at least one package hint matched (score >= one hint's weight);
    // a bare reference-prefix point is not evidence of the right package.
    if( best && bestScore >= 30 )
        return libId( best );

    return wxEmptyString;
}


void CVPCB_MAINFRAME::AutomaticFootprintMatching()
{
    FOOTPRINT_EQUIVALENCE_LIST equivList;
    wxString                   msg;
    wxString                   error_msg;

    if( m_netlist.IsEmpty() )
        return;

    if( buildEquivalenceList( equivList, &error_msg ) )
        wxMessageBox( error_msg, _( "Equivalence File Load Error" ), wxOK | wxICON_WARNING, this );

    // Sort the association list by symbol value.  When sorted, finding duplicate definitions
    // (i.e. 2 or more items having the same symbol value) is easier.
    std::sort( equivList.begin(), equivList.end(), sortListbyCmpValue );

    // Display the number of footprint/symbol equivalences.
    msg.Printf( _( "%lu footprint/symbol equivalences found." ), (unsigned long)equivList.size() );
    SetStatusText( msg, 0 );

    // Now, associate each free component with a footprint
    m_skipComponentSelect = true;
    error_msg.Empty();

    bool firstAssoc = true;
    int  unassignedAtStart = 0;

    for( unsigned ii = 0; ii < m_netlist.GetCount(); ii++ )
    {
        if( m_netlist.GetComponent( ii )->GetFPID().empty() )
            unassignedAtStart++;
    }

    for( int kk = 0;  kk < (int) m_netlist.GetCount();  kk++ )
    {
        COMPONENT* component = m_netlist.GetComponent( kk );

        bool found = false;

        if( !component->GetFPID().empty() ) // the component has already a footprint
            continue;

        // Here a first attempt is made. We can have multiple equivItem of the same value.
        // When happens, using the footprint filter of components can remove the ambiguity by
        // filtering equivItem so one can use multiple equivList (for polar and non-polar caps
        // for example)
        wxString fpid_candidate;

        for( int idx = 0; idx < (int) equivList.size(); idx++ )
        {
            FOOTPRINT_EQUIVALENCE& equivItem = equivList[idx];

            if( equivItem.m_ComponentValue.CmpNoCase( component->GetValue() ) != 0 )
                continue;

            const FOOTPRINT_INFO* fp = m_FootprintsList->GetFootprintInfo( equivItem.m_FootprintFPID );

            bool equ_is_unique = true;
            int  next = idx+1;
            int  previous = idx-1;

            if( next < (int) equivList.size() && equivItem.m_ComponentValue == equivList[next].m_ComponentValue )
                equ_is_unique = false;

            if( previous >= 0 && equivItem.m_ComponentValue == equivList[previous].m_ComponentValue )
                equ_is_unique = false;

            // If the equivalence is unique, no ambiguity: use the association
            if( fp && equ_is_unique )
            {
                AssociateFootprint( CVPCB_ASSOCIATION( kk, equivItem.m_FootprintFPID ), firstAssoc );
                firstAssoc = false;
                found = true;
                break;
            }

            // Store the first candidate found in list, when equivalence is not unique
            // We use it later.
            if( fp && fpid_candidate.IsEmpty() )
                fpid_candidate = equivItem.m_FootprintFPID;

            // The equivalence is not unique: use the footprint filter to try to remove
            // ambiguity
            // if the footprint filter does not remove ambiguity, we will use fpid_candidate
            if( fp )
            {
                size_t filtercount = component->GetFootprintFilters().GetCount();
                found = ( filtercount == 0 ); // if no entries, do not filter

                for( size_t jj = 0; jj < filtercount && !found; jj++ )
                    found = fp->GetFootprintName().Matches( component->GetFootprintFilters()[jj] );
            }
            else
            {
                msg.Printf( _( "Component %s: footprint %s not found in any of the project footprint libraries." ),
                            component->GetReference(),
                            equivItem.m_FootprintFPID );

                if( !error_msg.IsEmpty() )
                    error_msg << wxT("\n\n");

                error_msg += msg;
            }

            if( found )
            {
                AssociateFootprint( CVPCB_ASSOCIATION( kk, equivItem.m_FootprintFPID ), firstAssoc );
                firstAssoc = false;
                break;
            }
        }

        if( found )
        {
            continue;
        }
        else if( !fpid_candidate.IsEmpty() )
        {
            AssociateFootprint( CVPCB_ASSOCIATION( kk, fpid_candidate ), firstAssoc );
            firstAssoc = false;
            continue;
        }

        // No equivalence matched (most projects have no .equ files at all).  Fall back to
        // matching the symbol's footprint filters, pin count and package hints against the
        // loaded footprint libraries.
        if( m_FootprintsList )
        {
            wxString match = findBestFootprintMatch( *m_FootprintsList, component );

            if( !match.IsEmpty() )
            {
                AssociateFootprint( CVPCB_ASSOCIATION( kk, match ), firstAssoc );
                firstAssoc = false;
                continue;
            }
        }

        // obviously the last chance: there's only one filter matching one footprint
        if( component->GetFootprintFilters().GetCount() == 1 )
        {
            // we do not need to analyze wildcards: single footprint do not
            // contain them and if there are wildcards it just will not match any
            if( m_FootprintsList->GetFootprintInfo( component->GetFootprintFilters()[0] ) )
            {
                AssociateFootprint( CVPCB_ASSOCIATION( kk, component->GetFootprintFilters()[0] ), firstAssoc );
                firstAssoc = false;
            }
        }
    }

    if( !error_msg.IsEmpty() )
        wxMessageBox( error_msg, _( "CvPcb Warning" ), wxOK | wxICON_WARNING, this );

    m_skipComponentSelect = false;
    m_symbolsListBox->Refresh();

    // Report the outcome; a silent no-op looks like a broken tool.
    int unassignedAtEnd = 0;

    for( unsigned ii = 0; ii < m_netlist.GetCount(); ii++ )
    {
        if( m_netlist.GetComponent( ii )->GetFPID().empty() )
            unassignedAtEnd++;
    }

    if( unassignedAtStart == 0 )
    {
        SetStatusText( _( "All symbols already have footprint associations." ), 0 );
    }
    else
    {
        msg.Printf( _( "Auto-assignment: %d footprint(s) assigned, %d symbol(s) left unassigned." ),
                    unassignedAtStart - unassignedAtEnd,
                    unassignedAtEnd );
        SetStatusText( msg, 0 );

        if( unassignedAtStart == unassignedAtEnd )
        {
            wxMessageBox( _( "No footprints could be assigned automatically.\n\n"
                             "Automatic assignment matches each symbol's footprint filters, "
                             "pin count and package hints (e.g. '0603', 'SOT-23' in the value "
                             "or fields) against the loaded footprint libraries, as well as "
                             "any configured footprint equivalence (.equ) files.\n\n"
                             "Add a package hint to the symbol value or fields, set footprint "
                             "filters in the symbol properties, or assign the ambiguous "
                             "footprints manually." ),
                          _( "Automatically Assign Footprints" ),
                          wxOK | wxICON_INFORMATION, this );
        }
    }
}
