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

#include <dialogs/dialog_anvil_login.h>

#include <anvil_auth/anvil_auth.h>
#include <anvil_auth/anvil_api_defs.h>
#include <anvil_auth/anvil_auth_config.h>

#include <kiplatform/anvil_theme.h>
#include <thread_pool.h>
#include <build_version.h>
#include <widgets/ui_common.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/datetime.h>
#include <wx/graphics.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

/*
 * Anvil CAD sign-in screen, drawn natively: a populated circuit-board brand panel on the left,
 * cut along a diagonal board edge, and a light page carrying the sign-in card on the right.
 * Every colour is an ANVIL:: theme token (anvil_theme.h) and every functional string is a
 * translatable wxString — nothing is hard-coded at the point of use.  API endpoints/fields come
 * from anvil_api_defs.h and deployment values from ANVIL_AUTH_CONFIG; neither appears here.
 *
 * Painting convention: a surface that mixes shapes with text does BOTH through one
 * wxGraphicsContext.  wxGraphicsContext output is deferred, so gc shapes interleaved with
 * plain-wxDC text land on top of that text; the two are never mixed on the same surface.
 *
 * Seam convention: the diagonal board edge and the light page's gradient are both defined in
 * DIALOG coordinates, and every window that touches them (brand panel, form panel, card, status
 * bar) paints its own slice from those same equations.  That is what keeps the seams invisible —
 * a window that painted the gradient in its own local space would show up as a rectangle.
 */

namespace
{

constexpr double PI_D = 3.14159265358979323846;


// -------------------------------------------------------------------------------------
// text + shape helpers
// -------------------------------------------------------------------------------------

/// Draw @a aText at ( aX, aY ) with @a aTracking extra pixels between characters (the
/// letterspaced "console" look the brand style uses for small mono labels).
int drawTracked( wxDC& aDC, const wxString& aText, int aX, int aY, int aTracking )
{
    int x = aX;

    for( wxUniChar c : aText )
    {
        wxString glyph( c );
        aDC.DrawText( glyph, x, aY );
        x += aDC.GetTextExtent( glyph ).x + aTracking;
    }

    return x - aX;
}


/// Measured width of drawTracked() output without drawing it.
int trackedWidth( wxDC& aDC, const wxString& aText, int aTracking )
{
    int width = 0;

    for( wxUniChar c : aText )
        width += aDC.GetTextExtent( wxString( c ) ).x + aTracking;

    return width;
}


/// wxGraphicsContext flavour of drawTracked(); font and colour must already be set.
double drawTrackedGC( wxGraphicsContext* aGc, const wxString& aText, double aX, double aY,
                      double aTracking )
{
    double x = aX;

    for( wxUniChar c : aText )
    {
        wxString glyph( c );
        double   w, h, descent, lead;

        aGc->DrawText( glyph, x, aY );
        aGc->GetTextExtent( glyph, &w, &h, &descent, &lead );
        x += w + aTracking;
    }

    return x - aX;
}


/// wxGraphicsContext flavour of trackedWidth(); the font must already be set.
double trackedWidthGC( wxGraphicsContext* aGc, const wxString& aText, double aTracking )
{
    double width = 0.0;

    for( wxUniChar c : aText )
    {
        double w, h, descent, lead;
        aGc->GetTextExtent( wxString( c ), &w, &h, &descent, &lead );
        width += w + aTracking;
    }

    return width;
}


/// Width of @a aText in the context's current font.
double textWidthGC( wxGraphicsContext* aGc, const wxString& aText )
{
    double w, h, descent, lead;
    aGc->GetTextExtent( aText, &w, &h, &descent, &lead );
    return w;
}


/// Line height of the context's current font.
double lineHeightGC( wxGraphicsContext* aGc )
{
    double w, h, descent, lead;
    aGc->GetTextExtent( wxS( "Xg" ), &w, &h, &descent, &lead );
    return h;
}


/// The same colour at a different opacity — used for the translucent plates, the copper
/// artwork and the card halo, all of which must read against whatever they sit on.
wxColour withAlpha( const wxColour& aColour, unsigned char aAlpha )
{
    return wxColour( aColour.Red(), aColour.Green(), aColour.Blue(), aAlpha );
}


/// Rounded box with an optional fill and an optional border; pass wxNullColour to skip either.
void drawRoundedBox( wxGraphicsContext* aGc, double aX, double aY, double aW, double aH,
                     double aRadius, const wxColour& aFill, const wxColour& aBorder,
                     double aBorderWidth )
{
    if( aFill.IsOk() )
        aGc->SetBrush( wxBrush( aFill ) );
    else
        aGc->SetBrush( *wxTRANSPARENT_BRUSH );

    if( aBorder.IsOk() && aBorderWidth > 0.0 )
        aGc->SetPen( wxPen( aBorder, aBorderWidth ) );
    else
        aGc->SetPen( *wxTRANSPARENT_PEN );

    aGc->DrawRoundedRectangle( aX, aY, aW, aH, aRadius );
}


/// Rounded box filled with an already-prepared (typically gradient) brush.
void drawRoundedBoxBrush( wxGraphicsContext* aGc, double aX, double aY, double aW, double aH,
                          double aRadius, const wxGraphicsBrush& aFill, const wxColour& aBorder,
                          double aBorderWidth )
{
    wxGraphicsPath path = aGc->CreatePath();
    path.AddRoundedRectangle( aX, aY, aW, aH, aRadius );

    aGc->SetBrush( aFill );
    aGc->FillPath( path );

    if( aBorder.IsOk() && aBorderWidth > 0.0 )
    {
        aGc->SetPen( wxPen( aBorder, aBorderWidth ) );
        aGc->StrokePath( path );
    }
}


// -------------------------------------------------------------------------------------
// dialog-space geometry — see the "seam convention" note at the top of the file
// -------------------------------------------------------------------------------------

/// @a aWindow's top-left corner in the top-level dialog's client coordinates.
wxPoint originInDialog( const wxWindow* aWindow )
{
    wxPoint origin( 0, 0 );

    for( const wxWindow* w = aWindow; w && !w->IsTopLevel(); w = w->GetParent() )
        origin += w->GetPosition();

    return origin;
}


/// Client size of the top-level dialog @a aWindow lives in.
wxSize dialogClientSize( const wxWindow* aWindow )
{
    const wxWindow* top = aWindow;

    while( top && !top->IsTopLevel() )
        top = top->GetParent();

    return top ? top->GetClientSize() : aWindow->GetClientSize();
}


/// x of the cut between the dark board and the light page at height @a aY, in dialog client
/// coordinates.  The board leans to the left as it descends, the way a routed board edge does.
/// Both ends stay inside the brand panel's own width, so only that panel ever paints the cut.
double brandSeamX( const wxSize& aWindow, double aY )
{
    const double top = aWindow.x * 0.614;
    const double bottom = aWindow.x * 0.487;

    return top + ( bottom - top ) * ( aY / std::max( 1.0, (double) aWindow.y ) );
}


/// A stroking pen with round caps and joins, so a filleted polyline stays smooth end to end.
wxGraphicsPen roundPen( wxGraphicsContext* aGc, const wxColour& aColour, double aWidth )
{
    return aGc->CreatePen(
            wxGraphicsPenInfo( aColour, aWidth ).Cap( wxCAP_ROUND ).Join( wxJOIN_ROUND ) );
}


/// One waypoint on the cut edge.  The cut is a single architectural slant: the light page
/// meets the board along one unbroken diagonal, so the table only carries its two ends.  y is
/// a fraction of the window height and dx an offset from the base diagonal as a fraction of
/// the window width; both ends run off-screen so the slant never shows a start or a stop.
struct SEAM_STEP
{
    double y;
    double dx;
};

const SEAM_STEP g_seamSteps[] =
{
    { -0.080,  0.000 },
    {  1.080,  0.000 }
};


/// The cut edge as a polyline in dialog client coordinates, top to bottom, pulled @a aInset
/// pixels west of the true edge (0 for the edge itself, positive for an inset outline).
void buildSeamPolyline( std::vector<wxPoint2DDouble>& aOut, const wxSize& aWindow, double aInset )
{
    for( const SEAM_STEP& step : g_seamSteps )
    {
        const double y = step.y * aWindow.y;

        aOut.push_back( wxPoint2DDouble( brandSeamX( aWindow, y ) + step.dx * aWindow.x - aInset,
                                         y ) );
    }
}


/// Emit @a aPts as a path whose every interior corner is filleted, the way a router with a
/// round cutter takes a corner.  This is the shape the cut edge wants: a 45 degree chamfer
/// reads as a facet and a run of them reads as a sawtooth, where a fillet reads as one
/// continuous milled edge.  The radius is clamped per corner to a share of the shorter
/// adjacent leg, so a short leg degrades to a tight fillet instead of overrunning its
/// neighbour.
void addRoundedPolyline( wxGraphicsPath& aPath, const std::vector<wxPoint2DDouble>& aPts,
                         double aRadius, bool aClose )
{
    std::vector<wxPoint2DDouble> pts;

    for( const wxPoint2DDouble& pt : aPts )
    {
        if( pts.empty() || std::abs( pt.m_x - pts.back().m_x ) > 1.0
            || std::abs( pt.m_y - pts.back().m_y ) > 1.0 )
        {
            pts.push_back( pt );
        }
    }

    const int n = (int) pts.size();

    if( n < 2 )
        return;

    auto legLen = []( const wxPoint2DDouble& aFrom, const wxPoint2DDouble& aTo ) -> double
    {
        const double dx = aTo.m_x - aFrom.m_x;
        const double dy = aTo.m_y - aFrom.m_y;

        return std::max( 1e-6, std::sqrt( dx * dx + dy * dy ) );
    };

    auto radiusAt = [&]( int aIndex ) -> double
    {
        const wxPoint2DDouble& prev = pts[( aIndex + n - 1 ) % n];
        const wxPoint2DDouble& cur = pts[aIndex];
        const wxPoint2DDouble& next = pts[( aIndex + 1 ) % n];

        return std::min( aRadius,
                         std::min( legLen( prev, cur ), legLen( cur, next ) ) * 0.45 );
    };

    if( aClose )
    {
        // Start halfway along the closing leg, so the corner at pts[0] is filleted like every
        // other one instead of staying square.
        aPath.MoveToPoint( ( pts[n - 1].m_x + pts[0].m_x ) * 0.5,
                           ( pts[n - 1].m_y + pts[0].m_y ) * 0.5 );

        for( int i = 0; i < n; ++i )
        {
            const wxPoint2DDouble& next = pts[( i + 1 ) % n];

            aPath.AddArcToPoint( pts[i].m_x, pts[i].m_y, next.m_x, next.m_y, radiusAt( i ) );
        }

        aPath.CloseSubpath();
        return;
    }

    aPath.MoveToPoint( pts[0] );

    for( int i = 1; i < n - 1; ++i )
        aPath.AddArcToPoint( pts[i].m_x, pts[i].m_y, pts[i + 1].m_x, pts[i + 1].m_y,
                             radiusAt( i ) );

    aPath.AddLineToPoint( pts[n - 1] );
}


/// The light page's ground, expressed in dialog coordinates and shifted into the local space of
/// a window whose top-left corner is at @a aOrigin.
wxGraphicsBrush pageGroundBrush( wxGraphicsContext* aGc, const wxPoint& aOrigin,
                                 const wxSize& aWindow )
{
    return aGc->CreateLinearGradientBrush( 0, -aOrigin.y, 0, aWindow.y - aOrigin.y,
                                           ANVIL::LOGIN_PAGE_TOP, ANVIL::LOGIN_PAGE_BG );
}


/// The page side of the cut: nothing but the thin shadow the board throws onto the paper,
/// tight enough that it reads as the edge itself rather than as a band beside it.  The split
/// has to be one line — no echo tracks, no second edge.  Both windows that touch the page
/// paint this from the same dialog-space equations, which is what lets the shading run
/// straight through the boundary between them.
void paintPageEdge( wxGraphicsContext* aGc, const wxPoint& aOrigin, const wxSize& aWindow,
                    double aPx )
{
    auto strokeAt = [&]( double aShift, const wxColour& aInk, double aWidth )
    {
        std::vector<wxPoint2DDouble> seam;
        buildSeamPolyline( seam, aWindow, aOrigin.x - aShift * aPx );

        for( wxPoint2DDouble& pt : seam )
            pt.m_y -= aOrigin.y;

        wxGraphicsPath path = aGc->CreatePath();
        addRoundedPolyline( path, seam, 30 * aPx, false );

        aGc->SetBrush( *wxTRANSPARENT_BRUSH );
        aGc->SetPen( roundPen( aGc, aInk, aWidth * aPx ) );
        aGc->StrokePath( path );
    };

    // One shadow only, hugging the cut: enough to seat the board over the paper, too tight
    // to read as a second edge of its own.
    strokeAt( 1.5, wxColour( 12, 34, 27, 30 ), 3.0 );
}


/// The dark board ground, in the same shifted dialog space as pageGroundBrush().
wxGraphicsBrush brandGroundBrush( wxGraphicsContext* aGc, const wxPoint& aOrigin,
                                  const wxSize& aWindow )
{
    return aGc->CreateLinearGradientBrush( -aOrigin.x, -aOrigin.y, aWindow.x * 0.62 - aOrigin.x,
                                           aWindow.y - aOrigin.y, ANVIL::LOGIN_GRAD_TOP,
                                           ANVIL::LOGIN_GRAD_BOTTOM );
}


// =====================================================================================
// ARTWORK — the drawn brand illustration.  Everything here is vector, so it scales with
// DPI and needs no resource lookup this early in startup: sign-in is the first window the
// application opens, before any bitmap cache exists.
// =====================================================================================

/// One vertex of a normalised routing polyline.  x/y are fractions of the artwork box
/// ( 0..1 ), so the whole plot rescales with the panel.  ox/oy are bus fan-out multipliers:
/// member i of a parallel bus draws this vertex shifted by ( i * pitch * ox, i * pitch * oy )
/// pixels.  Giving the straight runs an offset perpendicular to their travel and the corner
/// chamfers an offset perpendicular to the 45 degree leg is what keeps the members of a bus
/// exactly side by side while they turn a corner together, the way a real router fans them out.
struct BOARD_NODE
{
    double x;
    double y;
    int    ox;
    int    oy;
};

/// Vertex pool for the wide track layer.  Same vertex grammar as the fine copper, but these
/// are the heavy power and bus tracks: stroked 9..12 px with round caps and round joins, so
/// they read as poured copper rather than as etched signal traces.  They are laid down first
/// and the fine copper is routed over the top of them.
const BOARD_NODE g_trackNodes[] =
{
    // 0..5   right-hand spine: down from the top edge, jog west, down, jog back east
    {  0.800, 0.000,  +1,  0 },
    {  0.800, 0.230,  +1, +1 },
    {  0.760, 0.285,  +1, +1 },
    {  0.760, 0.560,  +1, -1 },
    {  0.800, 0.615,  +1, -1 },
    {  0.800, 0.775,  +1,  0 },

    // 6..9   fan east out of the card gutter, 45 down, out towards the cut edge
    {  0.688, 0.430,   0, +1 },
    {  0.780, 0.430,  +1, -1 },
    {  0.840, 0.492,  +1, -1 },
    {  0.872, 0.492,   0, +1 },

    // 10..13 the bottom bundle: down the left margin, round the chamfer, east along the foot
    {  0.030, 0.700,  +1,  0 },
    {  0.030, 0.880,  +1, -1 },
    {  0.080, 0.930,  +1, -1 },
    {  0.775, 0.930,   0, -1 },

    // 14..17 plate feed: down from the top edge, 45 west, down beside the product plate
    {  0.360, 0.000,  +1,  0 },
    {  0.360, 0.105,  +1, +1 },
    {  0.312, 0.155,  +1, +1 },
    {  0.312, 0.242,  +1,  0 },

    // 18..21 lower right: up from the bottom edge, 45 up-east, up into the DIP land pattern
    {  0.628, 1.000,  +1,  0 },
    {  0.628, 0.862,  +1, +1 },
    {  0.690, 0.800,  +1, +1 },
    {  0.690, 0.732,  +1,  0 },

    // 22..25 top right: in from the cut edge, 45 down-west, down to the SOIC land pattern
    {  0.958, 0.090,   0, +1 },
    {  0.900, 0.090,  +1, +1 },
    {  0.858, 0.134,  +1, +1 },
    {  0.858, 0.262,  +1,  0 },

    // 26..29 top left corner bundle: up the left margin, round the chamfer, east along the top
    {  0.032, 0.300,  +1,  0 },
    {  0.032, 0.120,  +1, +1 },
    {  0.088, 0.062,  +1, +1 },
    {  0.286, 0.062,   0, +1 }
};

/// One wide track run.  Same slice-plus-style shape as BOARD_TRACE, but the bus pitch is
/// carried per run, because a heavy bundle spreads much wider than a signal bus.
struct BOARD_TRACK
{
    int    first;    ///< index of the first vertex in g_trackNodes
    int    count;    ///< vertex count
    int    members;  ///< parallel tracks drawn from this spine
    double width;    ///< stroke width, in aPx units
    double pitch;    ///< centre to centre spacing of the members, in aPx units
    bool   lit;      ///< drawn in the brighter of the two track greens
};

const BOARD_TRACK g_boardTracks[] =
{
    {  0, 6, 3, 6.5, 12.0, false },
    {  6, 4, 2, 6.0, 11.0, true  },
    { 10, 4, 4, 5.5, 10.0, false },
    { 14, 4, 2, 5.5, 10.0, false },
    { 18, 4, 2, 7.0, 12.0, true  },
    { 22, 4, 2, 7.0, 12.0, false },
    { 26, 4, 4, 5.5, 10.0, false }
};

/// Poured copper patches: the blunt rounded landing areas the wide tracks run into.
struct BOARD_POUR
{
    double x;
    double y;
    double w;
    double h;
    bool   lit;
};

const BOARD_POUR g_boardPours[] =
{
    { 0.842, 0.246, 0.030, 0.034, false },
    { 0.748, 0.768, 0.026, 0.030, true  },
    { 0.292, 0.052, 0.024, 0.024, false },
    { 0.664, 0.418, 0.024, 0.028, true  },
    { 0.024, 0.292, 0.022, 0.022, false }
};

/// Vertex pool for every routed net on the artwork.  Consecutive vertices are always laid out
/// so the connecting segment is horizontal, vertical, or an intended 45 degree chamfer; the
/// painter re-derives the exact 45 in pixel space, because aW and aH scale differently.
/// The plot is dense along all four edges — the top band, both gutters and the bottom band —
/// while the region x [ 0.16, 0.67 ] / y [ 0.28, 0.90 ] is deliberately left almost empty so
/// the opaque product plate, hero line and workflow cards can sit over it.
const BOARD_NODE g_boardNodes[] =
{
    // 0..3   bus A: four traces in from the left edge, right, 45 down, then down to a pad row
    {  0.000, 0.048,   0, +1 },
    {  0.150, 0.048,  -1, +1 },
    {  0.186, 0.092,  -1, +1 },
    {  0.186, 0.212,  -1,  0 },

    // 4..7   bus B: three traces in from the right edge, left, 45 up, then up into the top band
    {  1.000, 0.470,   0, +1 },
    {  0.800, 0.470,  -1, +1 },
    {  0.756, 0.418,  -1, +1 },
    {  0.756, 0.150,  -1,  0 },

    // 8..11  bus C: three traces up from the bottom edge, 45 up-right, then right to a pad row
    {  0.700, 1.000,  +1,  0 },
    {  0.700, 0.826,  +1, +1 },
    {  0.746, 0.772,  +1, +1 },
    {  0.955, 0.772,   0, +1 },

    // 12..17 single: down from the top edge, two 45 jogs east, down into the right third
    {  0.760, 0.000,   0,  0 },
    {  0.760, 0.072,   0,  0 },
    {  0.800, 0.118,   0,  0 },
    {  0.930, 0.118,   0,  0 },
    {  0.972, 0.166,   0,  0 },
    {  0.972, 0.330,   0,  0 },

    // 18..21 single: in from the right edge, 45 up-left, then up to a pad
    {  1.000, 0.700,   0,  0 },
    {  0.900, 0.700,   0,  0 },
    {  0.858, 0.648,   0,  0 },
    {  0.858, 0.560,   0,  0 },

    // 22..27 fine: long haul in from the left edge, stepping up through the top band
    {  0.000, 0.245,   0,  0 },
    {  0.255, 0.245,   0,  0 },
    {  0.300, 0.196,   0,  0 },
    {  0.470, 0.196,   0,  0 },
    {  0.512, 0.150,   0,  0 },
    {  0.620, 0.150,   0,  0 },

    // 28..31 single: in from the right edge along the bottom, 45 up-left, up to a pad
    {  1.000, 0.945,   0,  0 },
    {  0.800, 0.945,   0,  0 },
    {  0.756, 0.892,   0,  0 },
    {  0.756, 0.830,   0,  0 },

    // 32..35 fine: along the very bottom from the left edge, 45 up-right into the corner block
    {  0.000, 0.955,   0,  0 },
    {  0.580, 0.955,   0,  0 },
    {  0.624, 0.910,   0,  0 },
    {  0.658, 0.910,   0,  0 },

    // 36..39 bus D: three traces down from the top edge at the far left, 45 east, down to pads
    {  0.062, 0.000,  +1,  0 },
    {  0.062, 0.100,  +1, +1 },
    {  0.106, 0.144,  +1, +1 },
    {  0.106, 0.258,  +1,  0 },

    // 40..45 single: down from the top edge mid-panel, two 45 jogs east, out along the top band
    {  0.300, 0.000,   0,  0 },
    {  0.300, 0.062,   0,  0 },
    {  0.344, 0.106,   0,  0 },
    {  0.470, 0.106,   0,  0 },
    {  0.512, 0.062,   0,  0 },
    {  0.648, 0.062,   0,  0 },

    // 46..49 single: down the right gutter from the top edge, 45 east, down onto the SOIC pad
    {  0.828, 0.000,   0,  0 },
    {  0.828, 0.190,   0,  0 },
    {  0.872, 0.234,   0,  0 },
    {  0.872, 0.296,   0,  0 },

    // 50..53 bus E: two traces down the left margin rail, 45 east along the bottom band
    {  0.014, 0.272,  +1,  0 },
    {  0.014, 0.858,  +1, +1 },
    {  0.058, 0.902,  +1, +1 },
    {  0.150, 0.902,   0, +1 },

    // 54..57 single: out of the card gutter, 45 down-east, straight to the right edge
    {  0.700, 0.360,   0,  0 },
    {  0.800, 0.360,   0,  0 },
    {  0.842, 0.402,   0,  0 },
    {  1.000, 0.402,   0,  0 },

    // 58..61 single: in from the right edge, 45 up-left, up to a pad above the lower gutter
    {  1.000, 0.620,   0,  0 },
    {  0.930, 0.620,   0,  0 },
    {  0.888, 0.578,   0,  0 },
    {  0.888, 0.520,   0,  0 },

    // 62..65 bus F: two traces in from the right edge low down, 45 up-left, up to a pad row
    {  1.000, 0.868,   0, +1 },
    {  0.888, 0.868,  -1, +1 },
    {  0.846, 0.826,  -1, +1 },
    {  0.846, 0.700,  -1,  0 },

    // 66..69 single: the DIP fan-out in the right gutter, 45 down-west, down to the bottom band
    {  0.690, 0.596,   0,  0 },
    {  0.660, 0.596,   0,  0 },
    {  0.660, 0.860,   0,  0 },
    {  0.660, 0.930,   0,  0 },

    // 70..73 single: short link across the top-left corner, 45 down-east into a pad
    {  0.000, 0.310,   0,  0 },
    {  0.030, 0.310,   0,  0 },
    {  0.030, 0.196,   0,  0 },
    {  0.140, 0.196,   0,  0 },

    // 74..77 fine: right gutter cross-link between the two lower nets
    {  0.756, 0.700,   0,  0 },
    {  0.800, 0.658,   0,  0 },
    {  0.960, 0.658,   0,  0 },
    {  0.960, 0.510,   0,  0 },

    // 78..89 landing stubs: the top lead of each standing resistor up to its own pad, and the
    // bottom lead down to another, so the populated parts read as soldered onto the plot
    // instead of floating over it
    {  0.074, 0.310,   0,  0 },
    {  0.074, 0.278,   0,  0 },
    {  0.101, 0.310,   0,  0 },
    {  0.101, 0.278,   0,  0 },
    {  0.128, 0.310,   0,  0 },
    {  0.128, 0.278,   0,  0 },
    {  0.074, 0.429,   0,  0 },
    {  0.074, 0.460,   0,  0 },
    {  0.101, 0.429,   0,  0 },
    {  0.101, 0.460,   0,  0 },
    {  0.128, 0.429,   0,  0 },
    {  0.128, 0.460,   0,  0 },

    // 90..93 landing stubs: the SOIC and the DIP in the left gutter out to their own pads
    {  0.141, 0.521,   0,  0 },
    {  0.166, 0.521,   0,  0 },
    {  0.142, 0.761,   0,  0 },
    {  0.166, 0.761,   0,  0 }
};

/// One routed net: a slice of g_boardNodes plus how it is painted.  members > 1 means the slice
/// is a bus spine and that many parallel traces are drawn from it, one bus pitch apart.
struct BOARD_TRACE
{
    int    first;    ///< index of the first vertex in g_boardNodes
    int    count;    ///< vertex count
    int    members;  ///< 1 for a single trace, 2..4 for a parallel bus
    double width;    ///< stroke width, in aPx units
    bool   lit;      ///< drawn in the bright copper colour instead of the dim one
    bool   pad;      ///< terminate the last vertex of every member with a drilled pad
    bool   glow;     ///< halo the end pad of the first member
    bool   dots;     ///< mark every corner of the first member with a solder junction dot
};

/// The nets of the plot: six parallel buses, a dozen singles and the component landing stubs.
const BOARD_TRACE g_boardTraces[] =
{
    {  0, 4, 4, 3.0, false, true, true,  false },
    {  4, 4, 3, 3.0, false, true, false, false },
    {  8, 4, 3, 3.0, false, true, false, false },
    { 12, 6, 1, 2.0, true,  true, false, true  },
    { 18, 4, 1, 2.0, false, true, true,  true  },
    { 22, 6, 1, 1.4, true,  true, true,  true  },
    { 28, 4, 1, 2.0, true,  true, false, true  },
    { 32, 4, 1, 1.4, false, true, false, false },
    { 36, 4, 3, 2.6, false, true, false, false },
    { 40, 6, 1, 1.6, true,  true, false, true  },
    { 46, 4, 1, 2.0, false, true, true,  true  },
    { 50, 4, 2, 2.2, false, true, false, false },
    { 54, 4, 1, 1.6, true,  false, false, true  },
    { 58, 4, 1, 2.0, false, true, true,  true  },
    { 62, 4, 2, 2.4, false, true, false, false },
    { 66, 4, 1, 1.6, false, true, false, true  },
    { 70, 4, 1, 1.4, true,  true, false, true  },
    { 74, 4, 1, 1.4, false, true, false, true  },
    { 78, 2, 1, 1.6, false, true, false, false },
    { 80, 2, 1, 1.6, true,  true, false, false },
    { 82, 2, 1, 1.6, false, true, false, false },
    { 84, 2, 1, 1.6, false, true, false, false },
    { 86, 2, 1, 1.6, true,  true, false, false },
    { 88, 2, 1, 1.6, false, true, false, false },
    { 90, 2, 1, 1.6, false, true, false, false },
    { 92, 2, 1, 1.6, false, true, false, false }
};

/// Free standing vias, normalised centre plus whether the via carries a halo.  All of them sit
/// outside the reserved sparse region.
struct BOARD_VIA
{
    double x;
    double y;
    bool   glow;
};

/// Unplated fiducial / mounting rings.  Normalised centre plus the radius as a multiple of the
/// via radius, so they scale with the rest of the copper.
struct BOARD_RING
{
    double x;
    double y;
    double scale;
};

const BOARD_RING g_boardRings[] =
{
    { 0.090, 0.200, 2.6 },
    { 0.212, 0.264, 1.9 },
    { 0.742, 0.288, 2.2 },
    { 0.948, 0.355, 2.6 },
    { 0.964, 0.650, 2.4 },
    { 0.848, 0.930, 2.0 },
    { 0.038, 0.640, 2.2 },
    { 0.470, 0.038, 1.8 }
};

const BOARD_VIA g_boardVias[] =
{
    { 0.075, 0.155, true  },
    { 0.310, 0.062, false },
    { 0.395, 0.112, false },
    { 0.470, 0.048, false },
    { 0.612, 0.070, true  },
    { 0.660, 0.235, false },
    { 0.845, 0.300, true  },
    { 0.945, 0.575, false },
    { 0.690, 0.560, false },
    { 0.930, 0.865, false },
    { 0.208, 0.116, false },
    { 0.545, 0.238, false },
    { 0.718, 0.052, false },
    { 0.905, 0.212, false },
    { 0.996, 0.268, false },
    { 0.028, 0.088, false },
    { 0.146, 0.052, false },
    { 0.795, 0.560, true  },
    { 0.628, 0.976, false },
    { 0.882, 0.744, false },
    { 0.038, 0.930, false },
    { 0.352, 0.972, false },
    { 0.980, 0.700, false },
    { 0.735, 0.216, false }
};


/// A soft radial bloom on the copper, as if a lit track were bleeding light into the solder
/// mask.  Radius is a fraction of the artwork's width so the blooms scale with the panel.
struct BOARD_BLOOM
{
    double        x;
    double        y;
    double        r;
    unsigned char alpha;
};

const BOARD_BLOOM g_boardBlooms[] =
{
    { 0.076, 0.152, 0.105,  95 },
    { 0.612, 0.070, 0.085,  70 },
    { 0.845, 0.298, 0.110,  85 },
    { 0.036, 0.470, 0.090,  70 },
    { 0.152, 0.928, 0.100,  75 },
    { 0.700, 0.558, 0.105,  85 },
    { 0.928, 0.862, 0.080,  60 },
    { 0.300, 0.062, 0.070,  55 }
};


void paintBoardArtwork( wxGraphicsContext* aGc, double aW, double aH, double aPx,
                        const wxColour& aTrace, const wxColour& aTraceLit,
                        const wxColour& aGround )
{
    if( !aGc || aW <= 0.0 || aH <= 0.0 )
        return;

    const double pitch      = 9.0 * aPx;   // bus centre to centre spacing
    const double padR       = 5.0 * aPx;
    const double padDrillR  = 2.0 * aPx;
    const double viaR       = 3.5 * aPx;
    const double viaDrillR  = 1.5 * aPx;

    const double dotR       = 2.6 * aPx;   // solder junction marker at a routed corner

    // Every copper on the plot is one blend of the two trace tokens, so the four layers stay
    // in the same hue family: the heavy tracks sit just above the board ground, the etched
    // signal traces well above that, and the lit ones close to the highlight itself.
    auto blend = [&]( double aMix, unsigned char aAlpha ) -> wxColour
    {
        auto mix = []( int aFrom, int aTo, double aT ) -> unsigned char
        {
            return (unsigned char) ( aFrom + ( aTo - aFrom ) * aT );
        };

        return wxColour( mix( aTrace.Red(), aTraceLit.Red(), aMix ),
                         mix( aTrace.Green(), aTraceLit.Green(), aMix ),
                         mix( aTrace.Blue(), aTraceLit.Blue(), aMix ), aAlpha );
    };

    const wxColour dim      = blend( 0.16, 220 );
    const wxColour lit      = blend( 0.62, 205 );
    const wxColour trackDim = blend( 0.045, 255 );
    const wxColour trackLit = blend( 0.110, 255 );

    // Append one table segment as copper: an axis aligned leg plus an exact 45 degree leg.  The
    // table is normalised, so aW and aH stretch an intended diagonal; taking the shorter of the
    // two pixel deltas as the diagonal run and spending the remainder on a straight leg keeps
    // every segment horizontal, vertical or exactly 45 degrees at any panel aspect ratio.
    auto addSegment = [&]( wxGraphicsPath& aPath, double aX0, double aY0, double aX1,
                           double aY1 )
    {
        const double dx = aX1 - aX0;
        const double dy = aY1 - aY0;
        const double ax = dx < 0.0 ? -dx : dx;
        const double ay = dy < 0.0 ? -dy : dy;
        const double sx = dx < 0.0 ? -1.0 : 1.0;
        const double sy = dy < 0.0 ? -1.0 : 1.0;

        if( ax > ay )
        {
            // run out horizontally first, then dive away at 45 degrees
            aPath.AddLineToPoint( aX0 + sx * ( ax - ay ), aY0 );
            aPath.AddLineToPoint( aX1, aY1 );
        }
        else
        {
            // take the 45 degrees first, then finish along the vertical
            aPath.AddLineToPoint( aX0 + sx * ax, aY0 + sy * ax );
            aPath.AddLineToPoint( aX1, aY1 );
        }
    };

    auto drawDisc = [&]( double aCx, double aCy, double aR, const wxColour& aFill )
    {
        aGc->SetPen( *wxTRANSPARENT_PEN );
        aGc->SetBrush( wxBrush( aFill ) );
        aGc->DrawEllipse( aCx - aR, aCy - aR, 2.0 * aR, 2.0 * aR );
    };

    // three stroked rings of falling alpha, so a pad or via reads as if it were backlit
    auto drawGlow = [&]( double aCx, double aCy, double aR )
    {
        const double ringScale[3] = { 1.8, 2.8, 4.0 };
        const int    ringAlpha[3] = { 80, 40, 16 };

        aGc->SetBrush( *wxTRANSPARENT_BRUSH );

        for( int i = 0; i < 3; ++i )
        {
            const double rr = aR * ringScale[i];
            const wxColour ringColour( aTraceLit.Red(), aTraceLit.Green(), aTraceLit.Blue(),
                                       ringAlpha[i] );

            aGc->SetPen( wxPen( ringColour, 1.0 * aPx ) );
            aGc->DrawEllipse( aCx - rr, aCy - rr, 2.0 * rr, 2.0 * rr );
        }
    };

    auto drawPad = [&]( double aCx, double aCy, const wxColour& aCopper, bool aGlow )
    {
        drawDisc( aCx, aCy, padR, aCopper );
        drawDisc( aCx, aCy, padDrillR, aGround );

        if( aGlow )
            drawGlow( aCx, aCy, padR );
    };

    const int traceCount = (int) ( sizeof( g_boardTraces ) / sizeof( g_boardTraces[0] ) );
    const int viaCount   = (int) ( sizeof( g_boardVias ) / sizeof( g_boardVias[0] ) );
    const int trackCount = (int) ( sizeof( g_boardTracks ) / sizeof( g_boardTracks[0] ) );
    const int pourCount  = (int) ( sizeof( g_boardPours ) / sizeof( g_boardPours[0] ) );

    // The heavy track layer goes down first and the fine copper is routed over it.  Round caps
    // and round joins are what separate the two layers by eye: poured copper has blunt ends and
    // swept corners, etched signal traces have square ends and hard mitres.
    for( int p = 0; p < pourCount; ++p )
    {
        const BOARD_POUR& pour = g_boardPours[p];
        const double      pw   = pour.w * aW;
        const double      ph   = pour.h * aH;

        aGc->SetPen( *wxTRANSPARENT_PEN );
        aGc->SetBrush( wxBrush( pour.lit ? trackLit : trackDim ) );
        aGc->DrawRoundedRectangle( pour.x * aW, pour.y * aH, pw, ph,
                                   std::min( pw, ph ) * 0.32 );
    }

    for( int t = 0; t < trackCount; ++t )
    {
        const BOARD_TRACK& track  = g_boardTracks[t];
        const wxColour     copper = track.lit ? trackLit : trackDim;

        aGc->SetBrush( *wxTRANSPARENT_BRUSH );
        aGc->SetPen( aGc->CreatePen( wxGraphicsPenInfo( copper, track.width * aPx )
                                             .Cap( wxCAP_ROUND )
                                             .Join( wxJOIN_ROUND ) ) );

        for( int m = 0; m < track.members; ++m )
        {
            const double   off  = m * track.pitch * aPx;
            wxGraphicsPath path = aGc->CreatePath();

            for( int n = 0; n < track.count; ++n )
            {
                const BOARD_NODE& node = g_trackNodes[track.first + n];
                const double      nx   = node.x * aW + off * node.ox;
                const double      ny   = node.y * aH + off * node.oy;

                if( n == 0 )
                {
                    path.MoveToPoint( nx, ny );
                }
                else
                {
                    const BOARD_NODE& prev = g_trackNodes[track.first + n - 1];

                    addSegment( path, prev.x * aW + off * prev.ox,
                                prev.y * aH + off * prev.oy, nx, ny );
                }
            }

            aGc->StrokePath( path );
        }
    }

    // fine copper next, so pads and vias always land on top of the routing
    for( int t = 0; t < traceCount; ++t )
    {
        const BOARD_TRACE& trace  = g_boardTraces[t];
        const wxColour     copper = trace.lit ? lit : dim;

        for( int m = 0; m < trace.members; ++m )
        {
            const double   off  = m * pitch;
            wxGraphicsPath path = aGc->CreatePath();

            for( int n = 0; n < trace.count; ++n )
            {
                const BOARD_NODE& node = g_boardNodes[trace.first + n];
                const double      px   = node.x * aW + off * node.ox;
                const double      py   = node.y * aH + off * node.oy;

                if( n == 0 )
                {
                    path.MoveToPoint( px, py );
                }
                else
                {
                    const BOARD_NODE& prev = g_boardNodes[trace.first + n - 1];

                    addSegment( path, prev.x * aW + off * prev.ox,
                                prev.y * aH + off * prev.oy, px, py );
                }
            }

            aGc->SetBrush( *wxTRANSPARENT_BRUSH );
            aGc->SetPen( wxPen( copper, trace.width * aPx ) );
            aGc->StrokePath( path );
        }
    }

    // every net lands on a drilled pad, one per bus member
    for( int t = 0; t < traceCount; ++t )
    {
        const BOARD_TRACE& trace = g_boardTraces[t];

        if( !trace.pad )
            continue;

        const BOARD_NODE& node   = g_boardNodes[trace.first + trace.count - 1];
        const wxColour    copper = trace.lit ? lit : dim;

        for( int m = 0; m < trace.members; ++m )
        {
            const double off = m * pitch;

            drawPad( node.x * aW + off * node.ox, node.y * aH + off * node.oy, copper,
                     trace.glow && m == 0 );
        }
    }

    // a solder junction dot on every corner the router turned, so the routing reads as connected
    // rather than as free-floating strokes
    for( int t = 0; t < traceCount; ++t )
    {
        const BOARD_TRACE& trace = g_boardTraces[t];

        if( !trace.dots )
            continue;

        const wxColour copper = trace.lit ? lit : dim;

        for( int n = 1; n < trace.count - 1; ++n )
        {
            const BOARD_NODE& node = g_boardNodes[trace.first + n];

            drawDisc( node.x * aW, node.y * aH, dotR, copper );
        }
    }

    for( int v = 0; v < viaCount; ++v )
    {
        const BOARD_VIA& via = g_boardVias[v];
        const double     cx  = via.x * aW;
        const double     cy  = via.y * aH;

        drawDisc( cx, cy, viaR, dim );
        drawDisc( cx, cy, viaDrillR, aGround );

        if( via.glow )
            drawGlow( cx, cy, viaR );
    }

    const int ringCount = (int) ( sizeof( g_boardRings ) / sizeof( g_boardRings[0] ) );

    aGc->SetBrush( *wxTRANSPARENT_BRUSH );

    for( int r = 0; r < ringCount; ++r )
    {
        const BOARD_RING& ring = g_boardRings[r];
        const double      rr   = viaR * ring.scale;

        aGc->SetPen( wxPen( dim, 1.6 * aPx ) );
        aGc->DrawEllipse( ring.x * aW - rr, ring.y * aH - rr, 2.0 * rr, 2.0 * rr );
    }

    // Blooms go on last and over everything: they are light bleeding out of the copper, so
    // they have to wash over the traces they sit on rather than hide behind them.
    const int bloomCount = (int) ( sizeof( g_boardBlooms ) / sizeof( g_boardBlooms[0] ) );

    aGc->SetPen( *wxTRANSPARENT_PEN );

    for( int b = 0; b < bloomCount; ++b )
    {
        const BOARD_BLOOM& bloom = g_boardBlooms[b];
        const double       cx    = bloom.x * aW;
        const double       cy    = bloom.y * aH;
        const double       rr    = bloom.r * aW;

        aGc->SetBrush( aGc->CreateRadialGradientBrush(
                cx, cy, cx, cy, rr, withAlpha( aTraceLit, bloom.alpha ),
                withAlpha( aTraceLit, 0 ) ) );
        aGc->DrawEllipse( cx - rr, cy - rr, 2.0 * rr, 2.0 * rr );
    }
}

void drawResistorMark( wxGraphicsContext* aGc, double aX, double aY, double aW, double aH,
                       const wxColour& aBody, const wxColour& aLead, double aStroke,
                       bool aVertical = false );


/// Colour bands painted across an axial resistor body.  Both values are fractions of the
/// body width: the band centre measured from the body's left edge, and the band's own width.
struct RESISTOR_BAND
{
    double centre;
    double width;
};

const RESISTOR_BAND g_resistorBands[] =
{
    { 0.28, 0.09 },
    { 0.50, 0.09 },
    { 0.72, 0.09 }
};


void drawResistorMark( wxGraphicsContext* aGc, double aX, double aY, double aW, double aH,
                       const wxColour& aBody, const wxColour& aLead, double aStroke,
                       bool aVertical )
{
    if( !aGc || aW <= 0.0 || aH <= 0.0 )
        return;

    // A standing resistor is the same drawing turned a quarter turn about the box centre, so
    // the body, bands, leads and gloss all stay in one place.
    if( aVertical )
    {
        const double cx = aX + aW * 0.5;
        const double cy = aY + aH * 0.5;

        aGc->PushState();
        aGc->Translate( cx, cy );
        aGc->Rotate( 3.14159265358979323846 * 0.5 );
        aGc->Translate( -cx, -cy );

        drawResistorMark( aGc, cx - aH * 0.5, cy - aW * 0.5, aH, aW, aBody, aLead, aStroke,
                          false );

        aGc->PopState();
        return;
    }

    auto lighten =
            []( const wxColour& aSrc, int aAmount, int aAlpha ) -> wxColour
            {
                const int r = ( aSrc.Red() + aAmount > 255 ) ? 255 : aSrc.Red() + aAmount;
                const int g = ( aSrc.Green() + aAmount > 255 ) ? 255 : aSrc.Green() + aAmount;
                const int b = ( aSrc.Blue() + aAmount > 255 ) ? 255 : aSrc.Blue() + aAmount;
                return wxColour( r, g, b, aAlpha );
            };

    const double unit = ( aStroke > 0.0 ) ? aStroke : 1.0;

    const double bodyX = aX + aW * 0.20;
    const double bodyY = aY + aH * 0.20;
    const double bodyW = aW * 0.60;
    const double bodyH = aH * 0.60;
    const double midY = aY + aH * 0.50;

    double radius = bodyH * 0.34;

    if( radius > bodyW * 0.45 )
        radius = bodyW * 0.45;

    // Body first, so the leads and bands sit on top of it.
    aGc->SetPen( *wxTRANSPARENT_PEN );
    aGc->SetBrush( wxBrush( aBody ) );
    aGc->DrawRoundedRectangle( bodyX, bodyY, bodyW, bodyH, radius );

    // Axial leads, out of both ends at mid height.  The end points are pulled in by half the
    // pen width so the round cap still lands inside the box.
    double leadW = unit * 1.1;

    if( leadW > aH * 0.16 )
        leadW = aH * 0.16;

    const double leadCap = leadW * 0.5;

    aGc->SetBrush( *wxTRANSPARENT_BRUSH );
    aGc->SetPen( wxPen( aLead, leadW ) );

    wxGraphicsPath leads = aGc->CreatePath();
    leads.MoveToPoint( aX + leadCap, midY );
    leads.AddLineToPoint( bodyX + bodyW * 0.04, midY );
    leads.MoveToPoint( bodyX + bodyW * 0.96, midY );
    leads.AddLineToPoint( aX + aW - leadCap, midY );
    aGc->StrokePath( leads );

    // Three value bands across the middle of the body.
    const int bandCount = (int) ( sizeof( g_resistorBands ) / sizeof( g_resistorBands[0] ) );
    const double bandY = bodyY + bodyH * 0.08;
    const double bandH = bodyH * 0.84;
    const wxColour bandColour( aLead.Red(), aLead.Green(), aLead.Blue(), 150 );

    aGc->SetPen( *wxTRANSPARENT_PEN );
    aGc->SetBrush( wxBrush( bandColour ) );

    for( int i = 0; i < bandCount; ++i )
    {
        const double bandW = bodyW * g_resistorBands[i].width;
        const double bandX = bodyX + bodyW * g_resistorBands[i].centre - bandW * 0.5;
        aGc->DrawRectangle( bandX, bandY, bandW, bandH );
    }

    // Highlight along the upper third of the body.
    aGc->SetBrush( *wxTRANSPARENT_BRUSH );
    aGc->SetPen( wxPen( lighten( aBody, 70, 120 ), unit * 0.7 ) );

    wxGraphicsPath gloss = aGc->CreatePath();
    gloss.MoveToPoint( bodyX + bodyW * 0.12, bodyY + bodyH / 3.0 );
    gloss.AddLineToPoint( bodyX + bodyW * 0.88, bodyY + bodyH / 3.0 );
    aGc->StrokePath( gloss );
}


void drawSoicMark( wxGraphicsContext* aGc, double aX, double aY, double aW, double aH,
                   const wxColour& aBody, const wxColour& aPin, double aStroke )
{
    if( !aGc || aW <= 0.0 || aH <= 0.0 )
        return;

    auto lighten =
            []( const wxColour& aSrc, int aAmount, int aAlpha ) -> wxColour
            {
                const int r = ( aSrc.Red() + aAmount > 255 ) ? 255 : aSrc.Red() + aAmount;
                const int g = ( aSrc.Green() + aAmount > 255 ) ? 255 : aSrc.Green() + aAmount;
                const int b = ( aSrc.Blue() + aAmount > 255 ) ? 255 : aSrc.Blue() + aAmount;
                return wxColour( r, g, b, aAlpha );
            };

    const int pinCount = 4;
    const double unit = ( aStroke > 0.0 ) ? aStroke : 1.0;

    const double bodyX = aX + aW * 0.18;
    const double bodyW = aW * 0.64;
    const double bodyY = aY + aH * 0.08;
    const double bodyH = aH * 0.84;

    double radius = bodyW * 0.14;

    if( radius > bodyH * 0.30 )
        radius = bodyH * 0.30;

    aGc->SetPen( *wxTRANSPARENT_PEN );
    aGc->SetBrush( wxBrush( aBody ) );
    aGc->DrawRoundedRectangle( bodyX, bodyY, bodyW, bodyH, radius );

    // Gull-wing pins, four a side, sticking out by 16% of the box width.
    double pinW = unit * 1.6;

    if( pinW > bodyH * 0.10 )
        pinW = bodyH * 0.10;

    const double pinCap = pinW * 0.5;
    const double pinLen = aW * 0.16;
    const double outerL = bodyX - pinLen + pinCap;
    const double outerR = bodyX + bodyW + pinLen - pinCap;
    const double innerL = bodyX + bodyW * 0.05;
    const double innerR = bodyX + bodyW * 0.95;

    aGc->SetBrush( *wxTRANSPARENT_BRUSH );
    aGc->SetPen( wxPen( aPin, pinW ) );

    wxGraphicsPath pins = aGc->CreatePath();

    for( int i = 0; i < pinCount; ++i )
    {
        const double pinY = bodyY + bodyH * ( ( i + 0.5 ) / (double) pinCount );
        pins.MoveToPoint( outerL, pinY );
        pins.AddLineToPoint( innerL, pinY );
        pins.MoveToPoint( innerR, pinY );
        pins.AddLineToPoint( outerR, pinY );
    }

    aGc->StrokePath( pins );

    // Highlight just inside the top edge, kept clear of the pin-1 dot.
    aGc->SetPen( wxPen( lighten( aBody, 60, 110 ), unit * 0.7 ) );

    wxGraphicsPath gloss = aGc->CreatePath();
    gloss.MoveToPoint( bodyX + bodyW * 0.32, bodyY + bodyH * 0.13 );
    gloss.AddLineToPoint( bodyX + bodyW * 0.90, bodyY + bodyH * 0.13 );
    aGc->StrokePath( gloss );

    // Pin-1 dot at the inset top left corner of the body.
    double dotR = bodyW * 0.07;

    if( dotR > bodyH * 0.11 )
        dotR = bodyH * 0.11;

    const double dotX = bodyX + bodyW * 0.14;
    const double dotY = bodyY + bodyH * 0.20;

    aGc->SetPen( *wxTRANSPARENT_PEN );
    aGc->SetBrush( wxBrush( aPin ) );
    aGc->DrawEllipse( dotX - dotR, dotY - dotR, dotR * 2.0, dotR * 2.0 );
}


void drawDipMark( wxGraphicsContext* aGc, double aX, double aY, double aW, double aH,
                  const wxColour& aBody, const wxColour& aPin, double aStroke )
{
    if( !aGc || aW <= 0.0 || aH <= 0.0 )
        return;

    auto lighten =
            []( const wxColour& aSrc, int aAmount, int aAlpha ) -> wxColour
            {
                const int r = ( aSrc.Red() + aAmount > 255 ) ? 255 : aSrc.Red() + aAmount;
                const int g = ( aSrc.Green() + aAmount > 255 ) ? 255 : aSrc.Green() + aAmount;
                const int b = ( aSrc.Blue() + aAmount > 255 ) ? 255 : aSrc.Blue() + aAmount;
                return wxColour( r, g, b, aAlpha );
            };

    const double pi = 3.14159265358979323846;
    const int pinCount = 7;
    const double unit = ( aStroke > 0.0 ) ? aStroke : 1.0;

    const double bodyX = aX + aW * 0.18;
    const double bodyW = aW * 0.64;
    const double bodyY = aY + aH * 0.06;
    const double bodyH = aH * 0.88;

    double radius = bodyW * 0.07;

    if( radius > bodyH * 0.14 )
        radius = bodyH * 0.14;

    aGc->SetPen( *wxTRANSPARENT_PEN );
    aGc->SetBrush( wxBrush( aBody ) );
    aGc->DrawRoundedRectangle( bodyX, bodyY, bodyW, bodyH, radius );

    // Seven through-hole pins a side.
    double pinW = unit * 1.5;

    if( pinW > bodyH * 0.055 )
        pinW = bodyH * 0.055;

    const double pinCap = pinW * 0.5;
    const double pinLen = aW * 0.16;
    const double outerL = bodyX - pinLen + pinCap;
    const double outerR = bodyX + bodyW + pinLen - pinCap;
    const double innerL = bodyX + bodyW * 0.05;
    const double innerR = bodyX + bodyW * 0.95;

    aGc->SetBrush( *wxTRANSPARENT_BRUSH );
    aGc->SetPen( wxPen( aPin, pinW ) );

    wxGraphicsPath pins = aGc->CreatePath();

    for( int i = 0; i < pinCount; ++i )
    {
        const double pinY = bodyY + bodyH * ( ( i + 0.5 ) / (double) pinCount );
        pins.MoveToPoint( outerL, pinY );
        pins.AddLineToPoint( innerL, pinY );
        pins.MoveToPoint( innerR, pinY );
        pins.AddLineToPoint( outerR, pinY );
    }

    aGc->StrokePath( pins );

    // Orientation notch: lower half circle centred on the middle of the body's top edge.
    // Angles run from +X and increase clockwise on screen, so 0 -> pi sweeps down through the
    // body.
    double notchR = bodyW * 0.11;

    if( notchR > bodyH * 0.16 )
        notchR = bodyH * 0.16;

    const double notchX = bodyX + bodyW * 0.5;

    aGc->SetPen( wxPen( aPin, unit * 0.9 ) );

    wxGraphicsPath notch = aGc->CreatePath();
    notch.MoveToPoint( notchX + notchR, bodyY );
    notch.AddArc( notchX, bodyY, notchR, 0.0, pi, true );
    aGc->StrokePath( notch );

    // Highlight along the top edge, split around the notch.
    aGc->SetPen( wxPen( lighten( aBody, 60, 110 ), unit * 0.7 ) );

    const double glossY = bodyY + bodyH * 0.07;

    wxGraphicsPath gloss = aGc->CreatePath();
    gloss.MoveToPoint( bodyX + bodyW * 0.30, glossY );
    gloss.AddLineToPoint( bodyX + bodyW * 0.36, glossY );
    gloss.MoveToPoint( bodyX + bodyW * 0.64, glossY );
    gloss.AddLineToPoint( bodyX + bodyW * 0.92, glossY );
    aGc->StrokePath( gloss );

    // Pin-1 dot at the inset top left corner of the body.
    double dotR = bodyW * 0.06;

    if( dotR > bodyH * 0.07 )
        dotR = bodyH * 0.07;

    const double dotX = bodyX + bodyW * 0.13;
    const double dotY = bodyY + bodyH * 0.15;

    aGc->SetPen( *wxTRANSPARENT_PEN );
    aGc->SetBrush( wxBrush( aPin ) );
    aGc->DrawEllipse( dotX - dotR, dotY - dotR, dotR * 2.0, dotR * 2.0 );
}

/// A point expressed as a fraction of an icon's bounding box: x runs left to right and y runs
/// top to bottom, both normally inside the 0..1 range.
struct MARK_PT
{
    double x;
    double y;
};

/// Resistor zig-zag: x is the fraction along the resistor body, y the signed amplitude across
/// the wire ( -1 above the wire, +1 below it ). Five peaks, half a step in from each end.
const MARK_PT c_resistorZigZag[] = {
    { 0.10, -1.0 }, { 0.30, 1.0 }, { 0.50, -1.0 }, { 0.70, 1.0 }, { 0.90, -1.0 }
};

/// Factory silhouette, traced from the foot of the left wall, up over two saw-tooth roof steps
/// and down the right wall. Closing the subpath draws the base line.
const MARK_PT c_factoryOutline[] = {
    { 0.380, 0.880 }, { 0.380, 0.500 }, { 0.590, 0.640 }, { 0.590, 0.500 },
    { 0.800, 0.640 }, { 0.800, 0.880 }
};

/// Erlenmeyer flask, traced from the top of the left neck wall, down the sloping shoulder,
/// along the flat base and back up to the top of the right neck wall. The mouth stays open.
const MARK_PT c_flaskOutline[] = {
    { 0.180, 0.200 }, { 0.180, 0.480 }, { 0.070, 0.880 }, { 0.400, 0.880 },
    { 0.290, 0.480 }, { 0.290, 0.200 }
};


void drawSchematicMark( wxGraphicsContext* aGc, double aX, double aY, double aW, double aH,
                        const wxColour& aColour, double aStroke )
{
    if( !aGc || aW <= 0.0 || aH <= 0.0 )
        return;

    auto px = [&]( double aFx ) { return aX + aFx * aW; };
    auto py = [&]( double aFy ) { return aY + aFy * aH; };

    // Circles keep their aspect on a non-square box by sizing from the shorter side.
    const double unit = ( aW < aH ) ? aW : aH;

    const double loopL = 0.140;     // wire loop, fractions of the box
    const double loopR = 0.840;
    const double loopT = 0.160;
    const double loopB = 0.680;

    const double resL = 0.300;      // resistor body along the top edge
    const double resR = 0.620;
    const double resAmp = 0.075;    // zig-zag amplitude, fraction of the height

    const double diodeY = 0.340;    // diode triangle base on the right edge
    const double tipY = 0.520;      // triangle tip, also the cathode bar
    const double diodeHalf = 0.085; // triangle half width, fraction of the width
    const double barHalf = 0.105;

    const double stubX = 0.400;     // stub dropping out of the bottom edge
    const double stubY = 0.840;
    const double termR = 0.055 * unit;
    const double dotR = 0.042 * unit;

    wxGraphicsPath wires = aGc->CreatePath();

    // Top edge, interrupted by the resistor body.
    wires.MoveToPoint( px( loopL ), py( loopT ) );
    wires.AddLineToPoint( px( resL ), py( loopT ) );
    wires.MoveToPoint( px( resR ), py( loopT ) );
    wires.AddLineToPoint( px( loopR ), py( loopT ) );

    // Right edge, interrupted by the diode.
    wires.MoveToPoint( px( loopR ), py( loopT ) );
    wires.AddLineToPoint( px( loopR ), py( diodeY ) );
    wires.MoveToPoint( px( loopR ), py( tipY ) );
    wires.AddLineToPoint( px( loopR ), py( loopB ) );

    // Bottom and left edges close the loop.
    wires.MoveToPoint( px( loopR ), py( loopB ) );
    wires.AddLineToPoint( px( loopL ), py( loopB ) );
    wires.AddLineToPoint( px( loopL ), py( loopT ) );

    // Stub leaving the bottom edge, ending on the open terminal.
    wires.MoveToPoint( px( stubX ), py( loopB ) );
    wires.AddLineToPoint( px( stubX ), py( stubY ) );

    aGc->SetBrush( *wxTRANSPARENT_BRUSH );
    aGc->SetPen( wxPen( aColour, aStroke ) );
    aGc->StrokePath( wires );

    // Interior detail: the two components, a touch lighter than the wires.
    wxGraphicsPath parts = aGc->CreatePath();

    const int peaks = (int) ( sizeof( c_resistorZigZag ) / sizeof( c_resistorZigZag[0] ) );

    parts.MoveToPoint( px( resL ), py( loopT ) );

    for( int i = 0; i < peaks; ++i )
    {
        const double zx = resL + c_resistorZigZag[i].x * ( resR - resL );
        const double zy = loopT + c_resistorZigZag[i].y * resAmp;
        parts.AddLineToPoint( px( zx ), py( zy ) );
    }

    parts.AddLineToPoint( px( resR ), py( loopT ) );

    // Diode triangle pointing down, cathode bar across its tip.
    parts.MoveToPoint( px( loopR - diodeHalf ), py( diodeY ) );
    parts.AddLineToPoint( px( loopR + diodeHalf ), py( diodeY ) );
    parts.AddLineToPoint( px( loopR ), py( tipY ) );
    parts.CloseSubpath();
    parts.MoveToPoint( px( loopR - barHalf ), py( tipY ) );
    parts.AddLineToPoint( px( loopR + barHalf ), py( tipY ) );

    aGc->SetPen( wxPen( aColour, aStroke * 0.8 ) );
    aGc->StrokePath( parts );

    // Open terminal circle at the end of the stub.
    aGc->DrawEllipse( px( stubX ) - termR, py( stubY ), termR * 2.0, termR * 2.0 );

    // Junction dot on the bottom left corner of the loop.
    aGc->SetPen( *wxTRANSPARENT_PEN );
    aGc->SetBrush( wxBrush( aColour ) );
    aGc->DrawEllipse( px( loopL ) - dotR, py( loopB ) - dotR, dotR * 2.0, dotR * 2.0 );
}


void drawPcbStackMark( wxGraphicsContext* aGc, double aX, double aY, double aW, double aH,
                       const wxColour& aColour, double aStroke )
{
    if( !aGc || aW <= 0.0 || aH <= 0.0 )
        return;

    auto px = [&]( double aFx ) { return aX + aFx * aW; };
    auto py = [&]( double aFy ) { return aY + aFy * aH; };

    const double unit = ( aW < aH ) ? aW : aH;

    const double cx = 0.500;        // diamond centre, fraction of the width
    const double halfW = 0.440;     // diamond half width, fraction of the width
    const double halfH = 0.160;     // diamond half height, fraction of the height
    const double layerGap = 0.220;  // vertical offset per layer, fraction of the height
    const double topCy = 0.280;     // centre of the top layer
    const double botCy = topCy + 2.0 * layerGap;

    auto diamond = [&]( double aCy )
    {
        wxGraphicsPath path = aGc->CreatePath();
        path.MoveToPoint( px( cx ), py( aCy - halfH ) );
        path.AddLineToPoint( px( cx + halfW ), py( aCy ) );
        path.AddLineToPoint( px( cx ), py( aCy + halfH ) );
        path.AddLineToPoint( px( cx - halfW ), py( aCy ) );
        path.CloseSubpath();
        return path;
    };

    // Isometric coordinates on the top layer: aU runs along the left-to-top edge, aV along the
    // left-to-bottom edge, so 0..1 in both stays inside the diamond.
    auto ix = [&]( double aU, double aV ) { return px( cx - halfW + ( aU + aV ) * halfW ); };
    auto iy = [&]( double aU, double aV ) { return py( topCy + ( aV - aU ) * halfH ); };

    aGc->SetBrush( *wxTRANSPARENT_BRUSH );
    aGc->SetPen( wxPen( aColour, aStroke ) );

    aGc->StrokePath( diamond( botCy ) );
    aGc->StrokePath( diamond( topCy + layerGap ) );
    aGc->StrokePath( diamond( topCy ) );

    // Left and right flanks tie the top and bottom layers into one slab.
    wxGraphicsPath sides = aGc->CreatePath();
    sides.MoveToPoint( px( cx - halfW ), py( topCy ) );
    sides.AddLineToPoint( px( cx - halfW ), py( botCy ) );
    sides.MoveToPoint( px( cx + halfW ), py( topCy ) );
    sides.AddLineToPoint( px( cx + halfW ), py( botCy ) );
    aGc->StrokePath( sides );

    // Two traces running along the isometric grain of the top layer.
    wxGraphicsPath traces = aGc->CreatePath();
    traces.MoveToPoint( ix( 0.34, 0.16 ), iy( 0.34, 0.16 ) );
    traces.AddLineToPoint( ix( 0.34, 0.84 ), iy( 0.34, 0.84 ) );
    traces.MoveToPoint( ix( 0.58, 0.16 ), iy( 0.58, 0.16 ) );
    traces.AddLineToPoint( ix( 0.58, 0.84 ), iy( 0.58, 0.84 ) );

    aGc->SetPen( wxPen( aColour, aStroke * 0.8 ) );
    aGc->StrokePath( traces );

    // Filled via on the top layer.
    const double viaR = 0.045 * unit;
    const double viaX = ix( 0.75, 0.42 );
    const double viaY = iy( 0.75, 0.42 );

    aGc->SetPen( *wxTRANSPARENT_PEN );
    aGc->SetBrush( wxBrush( aColour ) );
    aGc->DrawEllipse( viaX - viaR, viaY - viaR, viaR * 2.0, viaR * 2.0 );
}


void drawFabPlantMark( wxGraphicsContext* aGc, double aX, double aY, double aW, double aH,
                       const wxColour& aColour, double aStroke )
{
    if( !aGc || aW <= 0.0 || aH <= 0.0 )
        return;

    auto px = [&]( double aFx ) { return aX + aFx * aW; };
    auto py = [&]( double aFy ) { return aY + aFy * aH; };

    const double groundY = 0.880;   // shared base line for the plant and the flask
    const double stackL = 0.845;    // chimney, fractions of the width
    const double stackR = 0.925;
    const double stackT = 0.260;

    aGc->SetBrush( *wxTRANSPARENT_BRUSH );
    aGc->SetPen( wxPen( aColour, aStroke ) );

    const int fabCount = (int) ( sizeof( c_factoryOutline ) / sizeof( c_factoryOutline[0] ) );

    wxGraphicsPath plant = aGc->CreatePath();

    plant.MoveToPoint( px( c_factoryOutline[0].x ), py( c_factoryOutline[0].y ) );

    for( int i = 1; i < fabCount; ++i )
        plant.AddLineToPoint( px( c_factoryOutline[i].x ), py( c_factoryOutline[i].y ) );

    plant.CloseSubpath();

    // Chimney, standing on the same base line and tied back to the factory wall.
    plant.MoveToPoint( px( stackL ), py( groundY ) );
    plant.AddLineToPoint( px( stackL ), py( stackT ) );
    plant.AddLineToPoint( px( stackR ), py( stackT ) );
    plant.AddLineToPoint( px( stackR ), py( groundY ) );
    plant.AddLineToPoint( px( c_factoryOutline[fabCount - 1].x ), py( groundY ) );

    aGc->StrokePath( plant );

    // Flask, standing a little in front of the plant.
    const int flaskCount = (int) ( sizeof( c_flaskOutline ) / sizeof( c_flaskOutline[0] ) );

    wxGraphicsPath flask = aGc->CreatePath();

    flask.MoveToPoint( px( c_flaskOutline[0].x ), py( c_flaskOutline[0].y ) );

    for( int i = 1; i < flaskCount; ++i )
        flask.AddLineToPoint( px( c_flaskOutline[i].x ), py( c_flaskOutline[i].y ) );

    aGc->StrokePath( flask );

    // Liquid line across the lower third of the sloping body.
    const MARK_PT& neckL = c_flaskOutline[1];
    const MARK_PT& footL = c_flaskOutline[2];
    const MARK_PT& footR = c_flaskOutline[3];
    const MARK_PT& neckR = c_flaskOutline[4];

    const double fill = 0.670;
    const double liquidY = neckL.y + fill * ( footL.y - neckL.y );
    const double leftX = neckL.x + fill * ( footL.x - neckL.x );
    const double rightX = neckR.x + fill * ( footR.x - neckR.x );

    wxGraphicsPath liquid = aGc->CreatePath();
    liquid.MoveToPoint( px( leftX ), py( liquidY ) );
    liquid.AddLineToPoint( px( rightX ), py( liquidY ) );

    aGc->SetPen( wxPen( aColour, aStroke * 0.8 ) );
    aGc->StrokePath( liquid );
}

// -------------------------------------------------------------------------------------
// small UI glyphs
// -------------------------------------------------------------------------------------

/// Envelope outline filling the box ( aX, aY, aW, aH ).
void drawMailGlyph( wxGraphicsContext* aGc, double aX, double aY, double aW, double aH,
                    const wxColour& aColour, double aStroke )
{
    aGc->SetPen( wxPen( aColour, aStroke ) );
    aGc->SetBrush( *wxTRANSPARENT_BRUSH );
    aGc->DrawRoundedRectangle( aX, aY, aW, aH, aStroke * 2.0 );

    wxGraphicsPath flap = aGc->CreatePath();
    flap.MoveToPoint( aX + aStroke, aY + aH * 0.24 );
    flap.AddLineToPoint( aX + aW * 0.5, aY + aH * 0.64 );
    flap.AddLineToPoint( aX + aW - aStroke, aY + aH * 0.24 );
    aGc->StrokePath( flap );
}


/// Shield filling the box ( aX, aY, aW, aH ), either outlined or solid.
void drawShieldGlyph( wxGraphicsContext* aGc, double aX, double aY, double aW, double aH,
                      const wxColour& aColour, bool aFilled, double aStroke )
{
    wxGraphicsPath p = aGc->CreatePath();
    p.MoveToPoint( aX + aW * 0.5, aY );
    p.AddLineToPoint( aX + aW, aY + aH * 0.20 );
    p.AddLineToPoint( aX + aW, aY + aH * 0.50 );
    p.AddCurveToPoint( aX + aW, aY + aH * 0.80, aX + aW * 0.78, aY + aH * 0.95,
                       aX + aW * 0.5, aY + aH );
    p.AddCurveToPoint( aX + aW * 0.22, aY + aH * 0.95, aX, aY + aH * 0.80, aX, aY + aH * 0.50 );
    p.AddLineToPoint( aX, aY + aH * 0.20 );
    p.CloseSubpath();

    if( aFilled )
    {
        aGc->SetBrush( wxBrush( aColour ) );
        aGc->SetPen( *wxTRANSPARENT_PEN );
        aGc->FillPath( p );
    }
    else
    {
        aGc->SetBrush( *wxTRANSPARENT_BRUSH );
        aGc->SetPen( wxPen( aColour, aStroke ) );
        aGc->StrokePath( p );
    }
}


/// Closed padlock centred on ( aCx, aCy ), @a aSize tall overall.
void drawLockGlyph( wxGraphicsContext* aGc, double aCx, double aCy, double aSize,
                    const wxColour& aColour )
{
    const double bodyW = aSize * 0.62;
    const double bodyH = aSize * 0.44;
    const double bodyY = aCy - aSize * 0.04;

    wxGraphicsPath shackle = aGc->CreatePath();
    shackle.AddArc( aCx, bodyY, aSize * 0.22, PI_D, 2.0 * PI_D, true );

    aGc->SetPen( wxPen( aColour, aSize * 0.13 ) );
    aGc->SetBrush( *wxTRANSPARENT_BRUSH );
    aGc->StrokePath( shackle );

    drawRoundedBox( aGc, aCx - bodyW * 0.5, bodyY, bodyW, bodyH, aSize * 0.10, aColour,
                    wxNullColour, 0.0 );
}


/// Tick mark filling the box ( aX, aY, aW, aH ).
void drawCheckGlyph( wxGraphicsContext* aGc, double aX, double aY, double aW, double aH,
                     const wxColour& aColour, double aStroke )
{
    wxGraphicsPath p = aGc->CreatePath();
    p.MoveToPoint( aX + aW * 0.18, aY + aH * 0.52 );
    p.AddLineToPoint( aX + aW * 0.42, aY + aH * 0.76 );
    p.AddLineToPoint( aX + aW * 0.84, aY + aH * 0.24 );

    aGc->SetPen( wxPen( aColour, aStroke ) );
    aGc->SetBrush( *wxTRANSPARENT_BRUSH );
    aGc->StrokePath( p );
}


/// Right-pointing arrow whose shaft starts at ( aX, aY ) and runs @a aLen to the right.
void drawArrowGlyph( wxGraphicsContext* aGc, double aX, double aY, double aLen,
                     const wxColour& aColour, double aStroke )
{
    const double head = aLen * 0.34;

    wxGraphicsPath p = aGc->CreatePath();
    p.MoveToPoint( aX, aY );
    p.AddLineToPoint( aX + aLen, aY );
    p.MoveToPoint( aX + aLen - head, aY - head );
    p.AddLineToPoint( aX + aLen, aY );
    p.AddLineToPoint( aX + aLen - head, aY + head );

    aGc->SetPen( wxPen( aColour, aStroke ) );
    aGc->SetBrush( *wxTRANSPARENT_BRUSH );
    aGc->StrokePath( p );
}


// -------------------------------------------------------------------------------------
// TRACKED_LABEL — small uppercase mono label with letter tracking (e.g. "WORK EMAIL")
// -------------------------------------------------------------------------------------

class TRACKED_LABEL : public wxWindow
{
public:
    TRACKED_LABEL( wxWindow* aParent, const wxString& aText, const wxColour& aFg,
                   const wxColour& aBg ) :
            wxWindow( aParent, wxID_ANY ),
            m_text( aText.Upper() ),
            m_fg( aFg ),
            m_bg( aBg )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );

        wxFont font = KIUI::GetMonospacedUIFont();
        font.SetFractionalPointSize( GetFont().GetFractionalPointSize() * 0.82 );
        SetFont( font );

        Bind( wxEVT_PAINT, &TRACKED_LABEL::onPaint, this );

        SetMinSize( calcSize() );
    }

private:
    wxSize calcSize()
    {
        wxClientDC dc( this );
        dc.SetFont( GetFont() );
        return wxSize( trackedWidth( dc, m_text, FromDIP( 2 ) ),
                       dc.GetTextExtent( wxS( "X" ) ).y + FromDIP( 2 ) );
    }

    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        dc.SetBackground( wxBrush( m_bg ) );
        dc.Clear();
        dc.SetFont( GetFont() );
        dc.SetTextForeground( m_fg );
        drawTracked( dc, m_text, 0, FromDIP( 1 ), FromDIP( 2 ) );
    }

    wxString m_text;
    wxColour m_fg;
    wxColour m_bg;
};

} // namespace


// -----------------------------------------------------------------------------------------
// ANVIL_LOGIN_BUTTON — the rounded action buttons on the sign-in card
//
// PRIMARY is the filled deep-emerald call to action, SECONDARY the outlined light variant.
// The glyph is anchored to the left edge and the label is centred across the whole button,
// which is what the brand comp specifies.
// -----------------------------------------------------------------------------------------

class ANVIL_LOGIN_BUTTON : public wxWindow
{
public:
    enum STYLE
    {
        PRIMARY,
        SECONDARY
    };

    enum GLYPH
    {
        GLYPH_NONE,
        GLYPH_MAIL,
        GLYPH_SHIELD
    };

    ANVIL_LOGIN_BUTTON( wxWindow* aParent, const wxString& aLabel, STYLE aStyle, GLYPH aGlyph,
                        std::function<void()> aOnClick ) :
            wxWindow( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                      wxFULL_REPAINT_ON_RESIZE ),
            m_label( aLabel ),
            m_style( aStyle ),
            m_glyph( aGlyph ),
            m_onClick( std::move( aOnClick ) ),
            m_hover( false ),
            m_pressed( false )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );

        wxFont font = GetFont();
        font.MakeBold();
        SetFont( font );

        SetMinSize( wxSize( -1, FromDIP( 46 ) ) );
        SetCursor( wxCursor( wxCURSOR_HAND ) );

        Bind( wxEVT_PAINT, &ANVIL_LOGIN_BUTTON::onPaint, this );
        Bind( wxEVT_ENTER_WINDOW, [this]( wxMouseEvent& ) { m_hover = true; Refresh(); } );
        Bind( wxEVT_LEAVE_WINDOW, [this]( wxMouseEvent& )
              {
                  m_hover = false;
                  m_pressed = false;
                  Refresh();
              } );
        Bind( wxEVT_LEFT_DOWN, [this]( wxMouseEvent& ) { m_pressed = true; Refresh(); } );
        Bind( wxEVT_LEFT_UP, [this]( wxMouseEvent& )
              {
                  bool fire = m_pressed && IsEnabled();
                  m_pressed = false;
                  Refresh();

                  if( fire && m_onClick )
                      m_onClick();
              } );
    }

    void SetLabelText( const wxString& aLabel )
    {
        m_label = aLabel;
        Refresh();
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize sz = GetClientSize();

        // The parent is the white card; clear to it so the rounded corners show it through.
        dc.SetBackground( wxBrush( ANVIL::LOGIN_SURFACE ) );
        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc( wxGraphicsContext::Create( dc ) );

        if( !gc )
            return;

        gc->SetAntialiasMode( wxANTIALIAS_DEFAULT );

        const double px = FromDIP( 100 ) / 100.0;

        wxColour fill;
        wxColour border;
        wxColour ink;

        if( m_style == PRIMARY )
        {
            fill = ANVIL::CAP_ACTIVE;
            border = wxNullColour;
            ink = ANVIL::ON_ACCENT;

            if( !IsEnabled() )
                fill = ANVIL::LOGIN_MUTED;
            else if( m_pressed )
                fill = fill.ChangeLightness( 88 );
            else if( m_hover )
                fill = fill.ChangeLightness( 124 );
        }
        else
        {
            fill = ANVIL::LOGIN_SURFACE;
            border = ANVIL::LOGIN_CARD_BORDER;
            ink = IsEnabled() ? ANVIL::LOGIN_INK : ANVIL::LOGIN_MUTED;

            if( IsEnabled() && m_pressed )
                fill = ANVIL::LOGIN_PAGE_BG.ChangeLightness( 97 );
            else if( IsEnabled() && m_hover )
                fill = ANVIL::LOGIN_PAGE_BG;

            if( IsEnabled() && ( m_hover || m_pressed ) )
                border = ANVIL::LOGIN_FIELD_FOCUS;
        }

        const double stroke = px;

        drawRoundedBox( gc.get(), stroke * 0.5, stroke * 0.5, sz.x - stroke, sz.y - stroke,
                        9 * px, fill, border, stroke );

        const double glyphSize = 18 * px;
        const double glyphX = 22 * px;
        const double glyphY = ( sz.y - glyphSize ) * 0.5;

        switch( m_glyph )
        {
        case GLYPH_MAIL:
            drawMailGlyph( gc.get(), glyphX, ( sz.y - glyphSize * 0.72 ) * 0.5, glyphSize,
                           glyphSize * 0.72, ink, 1.4 * px );
            break;

        case GLYPH_SHIELD:
            drawShieldGlyph( gc.get(), glyphX + glyphSize * 0.1, glyphY, glyphSize * 0.8,
                             glyphSize, ink, false, 1.4 * px );
            break;

        case GLYPH_NONE:
            break;
        }

        gc->SetFont( GetFont(), ink );

        const double textW = textWidthGC( gc.get(), m_label );
        const double textH = lineHeightGC( gc.get() );

        gc->DrawText( m_label, ( sz.x - textW ) * 0.5, ( sz.y - textH ) * 0.5 );
    }

    wxString              m_label;
    STYLE                 m_style;
    GLYPH                 m_glyph;
    std::function<void()> m_onClick;
    bool                  m_hover;
    bool                  m_pressed;
};


namespace
{

// -------------------------------------------------------------------------------------
// FIELD_BOX — rounded input container wrapping a borderless wxTextCtrl, so the field LOOKS
// like the comp while typing/selection/clipboard/IME stay fully native.  The border lights
// up while the inner control holds focus.
// -------------------------------------------------------------------------------------

class FIELD_BOX : public wxPanel
{
public:
    FIELD_BOX( wxWindow* aParent, wxTextCtrl** aCtrlOut, long aTextStyle, bool aMailGlyph,
               int aHeight ) :
            wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE ),
            m_mailGlyph( aMailGlyph ),
            m_focused( false )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        Bind( wxEVT_PAINT, &FIELD_BOX::onPaint, this );

        m_ctrl = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                 wxDefaultSize, aTextStyle | wxBORDER_NONE );
        m_ctrl->SetBackgroundColour( ANVIL::LOGIN_FIELD_BG );
        m_ctrl->SetForegroundColour( ANVIL::LOGIN_INK );

        // The focus ring is painted by this panel, but focus arrives at the inner control.
        m_ctrl->Bind( wxEVT_SET_FOCUS, [this]( wxFocusEvent& aEvt )
                      {
                          m_focused = true;
                          Refresh();
                          aEvt.Skip();
                      } );
        m_ctrl->Bind( wxEVT_KILL_FOCUS, [this]( wxFocusEvent& aEvt )
                      {
                          m_focused = false;
                          Refresh();
                          aEvt.Skip();
                      } );

        const int inset = aMailGlyph ? FromDIP( 44 ) : FromDIP( 14 );

        wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
        sizer->AddStretchSpacer();
        sizer->Add( m_ctrl, 0, wxEXPAND | wxLEFT, inset );
        sizer->AddStretchSpacer();
        SetSizer( sizer );

        SetMinSize( wxSize( -1, FromDIP( aHeight ) ) );

        *aCtrlOut = m_ctrl;
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize sz = GetClientSize();

        dc.SetBackground( wxBrush( ANVIL::LOGIN_SURFACE ) );
        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc( wxGraphicsContext::Create( dc ) );

        if( !gc )
            return;

        gc->SetAntialiasMode( wxANTIALIAS_DEFAULT );

        const double px = FromDIP( 100 ) / 100.0;
        const wxColour border = m_focused ? ANVIL::LOGIN_FIELD_FOCUS : ANVIL::LOGIN_FIELD_BORDER;

        drawRoundedBox( gc.get(), px * 0.5, px * 0.5, sz.x - px, sz.y - px, 9 * px,
                        ANVIL::LOGIN_FIELD_BG, border, px );

        if( m_mailGlyph )
        {
            const double w = 17 * px;
            const double h = w * 0.74;
            drawMailGlyph( gc.get(), 15 * px, ( sz.y - h ) * 0.5, w, h,
                           m_focused ? ANVIL::ACCENT : ANVIL::LOGIN_PLACEHOLDER, 1.3 * px );
        }
    }

    wxTextCtrl* m_ctrl;
    bool        m_mailGlyph;
    bool        m_focused;
};


// -------------------------------------------------------------------------------------
// LINK_LABEL — small clickable text (resend / change email)
// -------------------------------------------------------------------------------------

class LINK_LABEL : public wxStaticText
{
public:
    LINK_LABEL( wxWindow* aParent, const wxString& aText, std::function<void()> aOnClick ) :
            wxStaticText( aParent, wxID_ANY, aText ),
            m_onClick( std::move( aOnClick ) )
    {
        SetBackgroundColour( ANVIL::LOGIN_SURFACE );
        SetForegroundColour( ANVIL::ACCENT );
        SetCursor( wxCursor( wxCURSOR_HAND ) );

        wxFont font = GetFont();
        font.SetFractionalPointSize( font.GetFractionalPointSize() * 0.92 );
        font.MakeBold();
        SetFont( font );

        Bind( wxEVT_LEFT_UP, [this]( wxMouseEvent& )
              {
                  if( IsEnabled() && m_onClick )
                      m_onClick();
              } );
    }

    /// An explicit foreground colour survives being disabled, so grey the link out by hand —
    /// otherwise a resend link on cooldown still reads as clickable emerald.
    bool Enable( bool aEnable ) override
    {
        const bool changed = wxStaticText::Enable( aEnable );

        SetForegroundColour( aEnable ? ANVIL::ACCENT : ANVIL::LOGIN_MUTED );
        Refresh();

        return changed;
    }

private:
    std::function<void()> m_onClick;
};


// -------------------------------------------------------------------------------------
// BADGE_ICON — the ringed emerald disc with the shield-and-lock mark at the head of the card
// -------------------------------------------------------------------------------------

class BADGE_ICON : public wxWindow
{
public:
    explicit BADGE_ICON( wxWindow* aParent ) : wxWindow( aParent, wxID_ANY )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( FromDIP( wxSize( 60, 60 ) ) );
        Bind( wxEVT_PAINT, &BADGE_ICON::onPaint, this );
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize sz = GetClientSize();

        dc.SetBackground( wxBrush( ANVIL::LOGIN_SURFACE ) );
        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc( wxGraphicsContext::Create( dc ) );

        if( !gc )
            return;

        gc->SetAntialiasMode( wxANTIALIAS_DEFAULT );

        const double px = FromDIP( 100 ) / 100.0;
        const double side = std::min( sz.x, sz.y );
        const double cx = sz.x * 0.5;
        const double cy = sz.y * 0.5;

        // pale outer ring, then the deep emerald disc it frames
        gc->SetBrush( wxBrush( ANVIL::LOGIN_BADGE_BG ) );
        gc->SetPen( *wxTRANSPARENT_PEN );
        gc->DrawEllipse( cx - side * 0.5, cy - side * 0.5, side, side );

        const double disc = side * 0.74;

        gc->SetBrush( wxBrush( ANVIL::CAP_ACTIVE ) );
        gc->SetPen( wxPen( withAlpha( ANVIL::ACCENT, 120 ), px ) );
        gc->DrawEllipse( cx - disc * 0.5, cy - disc * 0.5, disc, disc );

        const double shieldH = side * 0.42;
        const double shieldW = shieldH * 0.84;

        drawShieldGlyph( gc.get(), cx - shieldW * 0.5, cy - shieldH * 0.5, shieldW, shieldH,
                         ANVIL::ON_ACCENT, true, 0.0 );
        drawLockGlyph( gc.get(), cx, cy - shieldH * 0.04, shieldH * 0.50, ANVIL::CAP_ACTIVE );
    }
};


// -------------------------------------------------------------------------------------
// RULE_LABEL — a hairline with an optional centred caption ("Sign in with", "OR")
// -------------------------------------------------------------------------------------

class RULE_LABEL : public wxWindow
{
public:
    RULE_LABEL( wxWindow* aParent, const wxString& aText ) :
            wxWindow( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                      wxFULL_REPAINT_ON_RESIZE ),
            m_text( aText )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );

        wxFont font = GetFont();
        font.SetFractionalPointSize( font.GetFractionalPointSize() * 0.88 );
        SetFont( font );

        Bind( wxEVT_PAINT, &RULE_LABEL::onPaint, this );

        wxClientDC dc( this );
        dc.SetFont( GetFont() );
        SetMinSize( wxSize( -1, m_text.IsEmpty()
                                        ? FromDIP( 1 )
                                        : dc.GetTextExtent( wxS( "Xg" ) ).y + FromDIP( 4 ) ) );
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize sz = GetClientSize();

        dc.SetBackground( wxBrush( ANVIL::LOGIN_SURFACE ) );
        dc.Clear();

        dc.SetPen( wxPen( ANVIL::LOGIN_DIVIDER, FromDIP( 1 ) ) );

        const int mid = sz.y / 2;

        if( m_text.IsEmpty() )
        {
            dc.DrawLine( 0, mid, sz.x, mid );
            return;
        }

        dc.SetFont( GetFont() );
        dc.SetTextForeground( ANVIL::LOGIN_MUTED );

        const wxSize ext = dc.GetTextExtent( m_text );
        const int    gap = FromDIP( 12 );
        const int    x = ( sz.x - ext.x ) / 2;

        dc.DrawLine( 0, mid, x - gap, mid );
        dc.DrawLine( x + ext.x + gap, mid, sz.x, mid );
        dc.DrawText( m_text, x, ( sz.y - ext.y ) / 2 );
    }

    wxString m_text;
};


// -------------------------------------------------------------------------------------
// TRUST_LINE — shield + reassurance strapline at the foot of the card
// -------------------------------------------------------------------------------------

class TRUST_LINE : public wxWindow
{
public:
    TRUST_LINE( wxWindow* aParent, const wxString& aText ) :
            wxWindow( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                      wxFULL_REPAINT_ON_RESIZE ),
            m_text( aText )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );

        wxFont font = GetFont();
        font.SetFractionalPointSize( font.GetFractionalPointSize() * 0.88 );
        SetFont( font );

        Bind( wxEVT_PAINT, &TRUST_LINE::onPaint, this );

        wxClientDC dc( this );
        dc.SetFont( GetFont() );
        SetMinSize( wxSize( -1, dc.GetTextExtent( wxS( "Xg" ) ).y + FromDIP( 6 ) ) );
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize sz = GetClientSize();

        dc.SetBackground( wxBrush( ANVIL::LOGIN_SURFACE ) );
        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc( wxGraphicsContext::Create( dc ) );

        if( !gc )
            return;

        gc->SetAntialiasMode( wxANTIALIAS_DEFAULT );
        gc->SetFont( GetFont(), ANVIL::LOGIN_MUTED );

        const double px = FromDIP( 100 ) / 100.0;
        const double textW = textWidthGC( gc.get(), m_text );
        const double textH = lineHeightGC( gc.get() );
        const double glyphH = textH * 1.05;
        const double glyphW = glyphH * 0.84;
        const double gap = 8 * px;

        double x = ( sz.x - ( glyphW + gap + textW ) ) * 0.5;

        drawShieldGlyph( gc.get(), x, ( sz.y - glyphH ) * 0.5, glyphW, glyphH, ANVIL::ACCENT,
                         false, 1.2 * px );
        drawCheckGlyph( gc.get(), x + glyphW * 0.16, ( sz.y - glyphH ) * 0.5 + glyphH * 0.16,
                        glyphW * 0.68, glyphH * 0.62, ANVIL::ACCENT, 1.2 * px );

        gc->DrawText( m_text, x + glyphW + gap, ( sz.y - textH ) * 0.5 );
    }

    wxString m_text;
};


// -------------------------------------------------------------------------------------
// CARD_PANEL — the white rounded sign-in card, with an emerald halo and a soft drop shadow,
// floating on the light page.  All form widgets are its children and clear to LOGIN_SURFACE.
// -------------------------------------------------------------------------------------

class CARD_PANEL : public wxPanel
{
public:
    /// Distance from the panel edge to the card edge; the halo and shadow live in that gutter.
    static constexpr int GUTTER = 14;

    explicit CARD_PANEL( wxWindow* aParent ) :
            wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        Bind( wxEVT_PAINT, &CARD_PANEL::onPaint, this );
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize  sz = GetClientSize();
        const wxPoint org = originInDialog( this );
        const wxSize  win = dialogClientSize( this );

        std::unique_ptr<wxGraphicsContext> gc( wxGraphicsContext::Create( dc ) );

        if( !gc )
            return;

        gc->SetAntialiasMode( wxANTIALIAS_DEFAULT );

        // The gutter shows the page through, so it is painted from the page's own dialog-space
        // gradient rather than a flat local fill, or it would read as a rectangle.
        gc->SetPen( *wxTRANSPARENT_PEN );
        gc->SetBrush( pageGroundBrush( gc.get(), org, win ) );
        gc->DrawRectangle( 0, 0, sz.x, sz.y );

        const double px = FromDIP( 100 ) / 100.0;
        const double gutter = GUTTER * px;
        const double radius = 18 * px;

        // Emerald halo, then a neutral shadow: concentric low-alpha outlines rather than a
        // blur, which wxGraphicsContext has no portable equivalent for.
        for( int i = 7; i >= 1; --i )
        {
            const double o = i * 1.6 * px;

            drawRoundedBox( gc.get(), gutter - o, gutter - o, sz.x - 2 * gutter + 2 * o,
                            sz.y - 2 * gutter + 2 * o, radius + o, wxNullColour,
                            withAlpha( ANVIL::LOGIN_CARD_GLOW, (unsigned char) ( 26 - i * 3 ) ),
                            1.8 * px );
        }

        for( int i = 5; i >= 1; --i )
        {
            const double o = i * 1.4 * px;

            drawRoundedBox( gc.get(), gutter - o, gutter - o + px * 2,
                            sz.x - 2 * gutter + 2 * o, sz.y - 2 * gutter + 2 * o, radius + o,
                            wxNullColour, withAlpha( ANVIL::LOGIN_GRAD_TOP, 8 ), 1.6 * px );
        }

        drawRoundedBox( gc.get(), gutter, gutter, sz.x - 2 * gutter, sz.y - 2 * gutter, radius,
                        ANVIL::LOGIN_SURFACE, withAlpha( ANVIL::LOGIN_CARD_GLOW, 60 ), px );
    }
};


// -------------------------------------------------------------------------------------
// BRAND_PANEL — the left panel: emerald board ground, copper artwork and populated
// components, the product plate, the hero line and the workflow cards.  Fully painted.
// -------------------------------------------------------------------------------------

/// A component silhouette placed on the board, in 0..1 panel-relative coordinates.
struct PART_MARK
{
    enum KIND
    {
        RESISTOR,
        RESISTOR_V,
        SOIC,
        DIP
    };

    KIND   kind;
    double x;
    double y;
    double w;
    double h;
};

/// The parts sit in the gutters the content leaves free: the left margin and the right third.
/// The three resistors stand on end side by side, the way a hand-populated bias network does.
const PART_MARK BRAND_PARTS[] = {
    { PART_MARK::RESISTOR_V, 0.061, 0.310, 0.026, 0.119 },
    { PART_MARK::RESISTOR_V, 0.088, 0.310, 0.026, 0.119 },
    { PART_MARK::RESISTOR_V, 0.115, 0.310, 0.026, 0.119 },
    { PART_MARK::SOIC,       0.045, 0.486, 0.096, 0.070 },
    { PART_MARK::DIP,        0.050, 0.712, 0.092, 0.098 },
    { PART_MARK::SOIC,       0.700, 0.140, 0.070, 0.050 },
    { PART_MARK::DIP,        0.716, 0.672, 0.078, 0.128 },
    { PART_MARK::SOIC,       0.836, 0.318, 0.062, 0.046 }
};


class BRAND_PANEL : public wxPanel
{
public:
    explicit BRAND_PANEL( wxWindow* aParent ) :
            wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( aParent->FromDIP( wxSize( 520, -1 ) ) );
        Bind( wxEVT_PAINT, &BRAND_PANEL::onPaint, this );
    }

private:
    using GLYPH_FN = void ( * )( wxGraphicsContext*, double, double, double, double,
                                 const wxColour&, double );

    struct FEATURE
    {
        wxString title;
        wxString caption;
        GLYPH_FN glyph;
    };

    /// Component silhouettes scattered over the copper.
    void paintParts( wxGraphicsContext* aGc, double aW, double aH, double aPx )
    {
        const wxColour pin = withAlpha( ANVIL::LOGIN_COMP_PIN, 190 );

        for( const PART_MARK& part : BRAND_PARTS )
        {
            const double x = part.x * aW;
            const double y = part.y * aH;
            const double w = part.w * aW;
            const double h = part.h * aH;

            switch( part.kind )
            {
            case PART_MARK::RESISTOR:
                drawResistorMark( aGc, x, y, w, h, ANVIL::LOGIN_COMP_BODY, pin, 1.4 * aPx,
                                  false );
                break;

            case PART_MARK::RESISTOR_V:
                drawResistorMark( aGc, x, y, w, h, ANVIL::LOGIN_COMP_BODY, pin, 1.4 * aPx,
                                  true );
                break;

            case PART_MARK::SOIC:
                drawSoicMark( aGc, x, y, w, h, ANVIL::LOGIN_COMP_BODY, pin, 1.4 * aPx );
                break;

            case PART_MARK::DIP:
                drawDipMark( aGc, x, y, w, h, ANVIL::LOGIN_COMP_BODY, pin, 1.4 * aPx );
                break;
            }
        }
    }

    /// The routed board outline: chamfered corners, following the diagonal cut on the right.
    void paintBoardOutline( wxGraphicsContext* aGc, const wxSize& aSize, const wxPoint& aOrigin,
                            const wxSize& aWindow, double aPx )
    {
        // The outline is the board's own milled edge, so it follows the stepped cut on the
        // right rather than cutting the corner straight across it.
        auto outline = [&]( double aInset, unsigned char aAlpha, double aWidth )
        {
            const double top = aInset;
            const double bottom = aSize.y - aInset;

            std::vector<wxPoint2DDouble> seam;
            buildSeamPolyline( seam, aWindow, aOrigin.x + aInset );

            std::vector<wxPoint2DDouble> pts;
            pts.push_back( wxPoint2DDouble( aInset, top ) );

            for( const wxPoint2DDouble& pt : seam )
            {
                pts.push_back( wxPoint2DDouble(
                        pt.m_x, std::min( bottom, std::max( top, pt.m_y - aOrigin.y ) ) ) );
            }

            pts.push_back( wxPoint2DDouble( aInset, bottom ) );

            wxGraphicsPath p = aGc->CreatePath();
            addRoundedPolyline( p, pts, 26 * aPx, true );

            aGc->SetBrush( *wxTRANSPARENT_BRUSH );
            aGc->SetPen( wxPen( withAlpha( ANVIL::LOGIN_BOARD_EDGE, aAlpha ), aWidth ) );
            aGc->StrokePath( p );
        };

        outline( 18 * aPx, 165, 2.2 * aPx );
        outline( 27 * aPx, 85, 1.4 * aPx );
        outline( 36 * aPx, 38, 1.0 * aPx );
    }

    /// Bordered product plate: "ANVIL CAD" over "EDA Design Suite".  Returns its height.
    double paintPlate( wxGraphicsContext* aGc, double aCentreX, double aTop, double aPx )
    {
        const wxFont base = GetFont();

        wxFont wordmark = base;
        wordmark.MakeBold();
        wordmark.SetFractionalPointSize( base.GetFractionalPointSize() * 3.10 );

        wxFont sub = base;
        sub.SetFractionalPointSize( base.GetFractionalPointSize() * 1.72 );

        aGc->SetFont( wordmark, ANVIL::BONE );

        const double tracking = 6 * aPx;
        const double markW = trackedWidthGC( aGc, wxS( "ANVIL CAD" ), tracking );
        const double markH = lineHeightGC( aGc );

        aGc->SetFont( sub, ANVIL::ACCEL );
        const double subW = textWidthGC( aGc, _( "EDA Design Suite" ) );
        const double subH = lineHeightGC( aGc );

        const double plateW = std::max( markW, subW ) + 76 * aPx;
        const double plateH = markH + subH + 40 * aPx;
        const double plateX = aCentreX - plateW * 0.5;

        // halo, so the plate lifts off the copper behind it
        for( int i = 4; i >= 1; --i )
        {
            const double o = i * 2.0 * aPx;

            drawRoundedBox( aGc, plateX - o, aTop - o, plateW + 2 * o, plateH + 2 * o,
                            12 * aPx + o, wxNullColour,
                            withAlpha( ANVIL::LOGIN_BOARD_EDGE, (unsigned char) ( 30 - i * 5 ) ),
                            2.0 * aPx );
        }

        drawRoundedBox( aGc, plateX, aTop, plateW, plateH, 12 * aPx,
                        withAlpha( ANVIL::LOGIN_TILE_BG, 225 ),
                        withAlpha( ANVIL::LOGIN_BOARD_EDGE, 150 ), 1.6 * aPx );

        // package-style ticks on the plate edges
        aGc->SetPen( wxPen( withAlpha( ANVIL::ACCENT, 175 ), 2.4 * aPx ) );

        for( int i = 1; i <= 4; ++i )
        {
            const double y = aTop + plateH * i / 5.0;

            wxGraphicsPath tick = aGc->CreatePath();
            tick.MoveToPoint( plateX - 12 * aPx, y );
            tick.AddLineToPoint( plateX, y );
            tick.MoveToPoint( plateX + plateW, y );
            tick.AddLineToPoint( plateX + plateW + 12 * aPx, y );
            aGc->StrokePath( tick );
        }

        aGc->SetFont( wordmark, ANVIL::BONE );
        drawTrackedGC( aGc, wxS( "ANVIL CAD" ), aCentreX - markW * 0.5, aTop + 16 * aPx,
                       tracking );

        aGc->SetFont( sub, ANVIL::ACCEL );
        aGc->DrawText( _( "EDA Design Suite" ), aCentreX - subW * 0.5,
                       aTop + 16 * aPx + markH + 4 * aPx );

        return plateH;
    }

    /// One workflow card: icon tile, title, caption, trailing arrow.
    void paintFeature( wxGraphicsContext* aGc, const FEATURE& aFeature, double aX, double aY,
                       double aW, double aH, double aPx )
    {
        wxGraphicsBrush fill =
                aGc->CreateLinearGradientBrush( aX, aY, aX + aW, aY + aH,
                                                withAlpha( ANVIL::LOGIN_TILE_BG, 225 ),
                                                withAlpha( ANVIL::LOGIN_TILE_BG, 120 ) );

        drawRoundedBoxBrush( aGc, aX, aY, aW, aH, 12 * aPx, fill, ANVIL::LOGIN_TILE_BORDER,
                             1.3 * aPx );

        // The icon tile is a landscape rounded square, near enough the full height of the
        // card: on the comp it reads as a chip footprint, not as a small badge.
        const double inset = aH * 0.10;
        const double tileH = aH - inset * 2;
        const double tileW = tileH * 1.20;
        const double tileX = aX + inset * 1.3;

        drawRoundedBox( aGc, tileX, aY + inset, tileW, tileH, 10 * aPx,
                        withAlpha( ANVIL::CAP_ACTIVE, 170 ),
                        withAlpha( ANVIL::LOGIN_BOARD_EDGE, 110 ), 1.3 * aPx );

        aFeature.glyph( aGc, tileX + tileW * 0.22, aY + inset + tileH * 0.20, tileW * 0.56,
                        tileH * 0.60, ANVIL::ACCENT, 1.9 * aPx );

        const wxFont base = GetFont();

        wxFont title = base;
        title.MakeBold();
        title.SetFractionalPointSize( base.GetFractionalPointSize() * 1.95 );

        wxFont caption = base;
        caption.SetFractionalPointSize( base.GetFractionalPointSize() * 1.36 );

        const double textX = tileX + tileW + 22 * aPx;

        aGc->SetFont( title, ANVIL::ACCENT );
        const double titleH = lineHeightGC( aGc );

        aGc->SetFont( caption, withAlpha( ANVIL::BONE, 195 ) );
        const double captionH = lineHeightGC( aGc );
        const double textY = aY + ( aH - ( titleH + captionH + 6 * aPx ) ) * 0.5;

        aGc->SetFont( title, ANVIL::ACCENT );
        aGc->DrawText( aFeature.title, textX, textY );

        aGc->SetFont( caption, withAlpha( ANVIL::BONE, 195 ) );
        aGc->DrawText( aFeature.caption, textX, textY + titleH + 6 * aPx );

        drawArrowGlyph( aGc, aX + aW - 44 * aPx, aY + aH * 0.5, 20 * aPx,
                        withAlpha( ANVIL::ACCENT, 215 ), 1.8 * aPx );
    }

    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize  sz = GetClientSize();
        const wxPoint org = originInDialog( this );
        const wxSize  win = dialogClientSize( this );

        std::unique_ptr<wxGraphicsContext> gc( wxGraphicsContext::Create( dc ) );

        if( !gc )
            return;

        gc->SetAntialiasMode( wxANTIALIAS_DEFAULT );

        const double px = FromDIP( 100 ) / 100.0;

        // --- board ground, copper, parts -------------------------------------------------
        gc->SetPen( *wxTRANSPARENT_PEN );
        gc->SetBrush( brandGroundBrush( gc.get(), org, win ) );
        gc->DrawRectangle( 0, 0, sz.x, sz.y );

        paintBoardArtwork( gc.get(), sz.x, sz.y, px, ANVIL::LOGIN_TRACE, ANVIL::LOGIN_TRACE_HI,
                           ANVIL::LOGIN_GRAD_TOP );
        paintParts( gc.get(), sz.x, sz.y, px );

        // --- the light page eats the corner along the board's cut edge --------------------
        const double seamTop = brandSeamX( win, org.y ) - org.x;

        {
            // The seam table runs from above the top edge to below the bottom one, so the two
            // corners that close the ring land off-panel and their fillets never show.
            auto seamAt = [&]( double aShift, std::vector<wxPoint2DDouble>& aOut )
            {
                std::vector<wxPoint2DDouble> seam;
                buildSeamPolyline( seam, win, org.x - aShift );

                for( const wxPoint2DDouble& pt : seam )
                    aOut.push_back( wxPoint2DDouble( pt.m_x, pt.m_y - org.y ) );
            };

            std::vector<wxPoint2DDouble> pts;
            seamAt( 0.0, pts );

            const double offEdge = sz.x + 200 * px;
            pts.push_back( wxPoint2DDouble( offEdge, sz.y + 200 * px ) );
            pts.push_back( wxPoint2DDouble( offEdge, -200 * px ) );

            wxGraphicsPath wedge = gc->CreatePath();
            addRoundedPolyline( wedge, pts, 30 * px, true );

            gc->SetPen( *wxTRANSPARENT_PEN );
            gc->SetBrush( pageGroundBrush( gc.get(), org, win ) );
            gc->FillPath( wedge );

            paintPageEdge( gc.get(), org, win, px );
        }

        paintBoardOutline( gc.get(), sz, org, win, px );

        // --- geometry: a left gutter wide enough for the parts, then the content column ----
        const double margin = std::min( std::max( sz.x * 0.165, 40 * px ), 240 * px );
        const double contentW = std::min( std::max( sz.x * 0.505, 300 * px ), 680 * px );

        const wxFont base = GetFont();

        wxFont eyebrow = KIUI::GetMonospacedUIFont();
        eyebrow.SetFractionalPointSize( base.GetFractionalPointSize() * 0.94 );

        wxFont heading = base;
        heading.MakeBold();
        heading.SetFractionalPointSize( base.GetFractionalPointSize() * 2.85 );

        gc->SetFont( heading, ANVIL::BONE );
        const double headingH = lineHeightGC( gc.get() );

        double y = sz.y * 0.055;

        // The plate is centred over the board itself, not over the content column.
        y += paintPlate( gc.get(), seamTop * 0.5, y, px );

        // The card stack breathes with the display: it shrinks on a short screen and grows on
        // a tall one (never the type, which stays at its designed size), and whatever room is
        // left over is split above and below so the block sits centred under the plate.
        double cardH = 97 * px;
        double cardGap = 6 * px;
        double bottomH = 92 * px;
        double leadIn = 56 * px;

        auto blockHeight = [&]()
        {
            return leadIn + 32 * px + headingH + 34 * px + 3 * ( cardH + cardGap ) + 22 * px
                   + bottomH;
        };

        const double avail = sz.y - y - 44 * px;
        const double needed = blockHeight();

        if( needed > 0 )
        {
            const double k = std::min( std::max( avail / needed, 0.62 ), 1.30 );
            cardH *= k;
            cardGap *= k;
            bottomH *= k;
            leadIn *= k;
        }

        y += leadIn + std::max( 0.0, ( avail - blockHeight() ) * 0.45 );

        // --- eyebrow ---------------------------------------------------------------------
        gc->SetFont( eyebrow, ANVIL::ACCENT );
        drawTrackedGC( gc.get(), _( "ELECTRONICS DESIGN AUTOMATION" ), margin, y, 2.4 * px );

        y += 32 * px;

        // --- hero line -------------------------------------------------------------------
        gc->SetFont( heading, ANVIL::BONE );
        gc->DrawText( _( "A Unified Electronics Workflow." ), margin, y );
        y += headingH + 34 * px;

        // --- workflow cards --------------------------------------------------------------
        const FEATURE features[3] = {
            { _( "Schematic" ), _( "Capture & Annotate your design." ), drawSchematicMark },
            { _( "PCB Layout" ), _( "Route multiple layers, effortlessly." ), drawPcbStackMark },
            { _( "Fabrication" ), _( "Generate precise fabrication sets." ), drawFabPlantMark }
        };

        for( const FEATURE& feature : features )
        {
            paintFeature( gc.get(), feature, margin, y, contentW, cardH, px );
            y += cardH + cardGap;
        }

        // --- closing card ----------------------------------------------------------------
        y += 22 * px - cardGap;

        drawRoundedBox( gc.get(), margin, y, contentW, bottomH, 12 * px,
                        withAlpha( ANVIL::CAP_ACTIVE, 95 ),
                        withAlpha( ANVIL::LOGIN_TILE_BORDER, 165 ), 1.3 * px );

        const double discR = bottomH * 0.24;
        const double discX = margin + 26 * px;
        const double discY = y + bottomH * 0.5 - discR;

        gc->SetPen( *wxTRANSPARENT_PEN );
        gc->SetBrush( wxBrush( ANVIL::ACCENT ) );
        gc->DrawEllipse( discX, discY, discR * 2, discR * 2 );

        drawShieldGlyph( gc.get(), discX + discR * 0.52, discY + discR * 0.44, discR * 0.96,
                         discR * 1.12, ANVIL::ON_ACCENT, true, 0.0 );
        drawCheckGlyph( gc.get(), discX + discR * 0.70, discY + discR * 0.62, discR * 0.60,
                        discR * 0.76, ANVIL::CAP_ACTIVE, 1.8 * px );

        wxFont claim = base;
        claim.MakeBold();
        claim.SetFractionalPointSize( base.GetFractionalPointSize() * 1.30 );

        wxFont claimSub = base;
        claimSub.SetFractionalPointSize( base.GetFractionalPointSize() * 1.31 );

        const double claimX = discX + discR * 2 + 20 * px;

        gc->SetFont( claim, ANVIL::ACCENT );
        const double claimH = lineHeightGC( gc.get() );

        gc->SetFont( claimSub, withAlpha( ANVIL::BONE, 190 ) );
        const double claimSubH = lineHeightGC( gc.get() );

        const double claimY = y + ( bottomH - ( claimH + claimSubH + 5 * px ) ) * 0.5;

        gc->SetFont( claim, ANVIL::ACCENT );
        drawTrackedGC( gc.get(), _( "ONE PROJECT. EVERY LAYER." ), claimX, claimY, 1.6 * px );

        gc->SetFont( claimSub, withAlpha( ANVIL::BONE, 190 ) );
        gc->DrawText( _( "Built for precision. Designed for engineers." ), claimX,
                      claimY + claimH + 5 * px );
    }
};


// -------------------------------------------------------------------------------------
// FORM_PANEL — the light page the sign-in card floats on
// -------------------------------------------------------------------------------------

class FORM_PANEL : public wxPanel
{
public:
    explicit FORM_PANEL( wxWindow* aParent ) :
            wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        Bind( wxEVT_PAINT, &FORM_PANEL::onPaint, this );
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize  sz = GetClientSize();
        const wxPoint org = originInDialog( this );
        const wxSize  win = dialogClientSize( this );

        std::unique_ptr<wxGraphicsContext> gc( wxGraphicsContext::Create( dc ) );

        if( !gc )
            return;

        gc->SetAntialiasMode( wxANTIALIAS_DEFAULT );

        gc->SetPen( *wxTRANSPARENT_PEN );
        gc->SetBrush( pageGroundBrush( gc.get(), org, win ) );
        gc->DrawRectangle( 0, 0, sz.x, sz.y );

        // The cut edge itself is off to the left of this panel, but its shadow and the single
        // pale track reach in over the top of the page; painting them from the same equations
        // the brand panel uses is what keeps them unbroken across the two windows.
        paintPageEdge( gc.get(), org, win, FromDIP( 100 ) / 100.0 );
    }
};


} // namespace


// -----------------------------------------------------------------------------------------
// DIALOG_ANVIL_LOGIN
// -----------------------------------------------------------------------------------------

DIALOG_ANVIL_LOGIN::DIALOG_ANVIL_LOGIN( wxWindow* aParent ) :
        DIALOG_SHIM( aParent, wxID_ANY, wxS( "ANVIL CAD | EDA DESIGN SUITE" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxMINIMIZE_BOX | wxMAXIMIZE_BOX
                             | wxRESIZE_BORDER ),
        m_book( nullptr ),
        m_headingLabel( nullptr ),
        m_subLabel( nullptr ),
        m_emailCtrl( nullptr ),
        m_sendButton( nullptr ),
        m_otpCtrl( nullptr ),
        m_verifyButton( nullptr ),
        m_resendLink( nullptr ),
        m_changeEmailLink( nullptr ),
        m_errorLabel( nullptr ),
        m_resendTimer( this ),
        m_resendRemaining( 0 ),
        m_busy( false ),
        m_closed( false )
{
    wxBoxSizer* split = new wxBoxSizer( wxHORIZONTAL );

    split->Add( new BRAND_PANEL( this ), 13, wxEXPAND );

    FORM_PANEL* form = new FORM_PANEL( this );
    split->Add( form, 8, wxEXPAND );

    // ---- right page: the card, centred, with the legal line beneath ----------------------
    wxBoxSizer* formOuter = new wxBoxSizer( wxHORIZONTAL );
    wxBoxSizer* column = new wxBoxSizer( wxVERTICAL );

    column->AddStretchSpacer( 1 );

    CARD_PANEL* card = new CARD_PANEL( form );
    wxBoxSizer* cardOuter = new wxBoxSizer( wxVERTICAL );
    wxBoxSizer* cardSizer = new wxBoxSizer( wxVERTICAL );

    const wxFont base = GetFont();

    // ---- card header: badge + title + subtitle, shared by every page -------------------
    {
        wxBoxSizer* header = new wxBoxSizer( wxHORIZONTAL );
        header->Add( new BADGE_ICON( card ), 0, wxALIGN_CENTER_VERTICAL );

        wxBoxSizer* headerText = new wxBoxSizer( wxVERTICAL );

        m_headingLabel = new wxStaticText( card, wxID_ANY, wxEmptyString );
        {
            wxFont headingFont = base;
            headingFont.MakeBold();
            headingFont.SetFractionalPointSize( base.GetFractionalPointSize() * 1.5 );
            m_headingLabel->SetFont( headingFont );
            m_headingLabel->SetForegroundColour( ANVIL::LOGIN_INK );
            m_headingLabel->SetBackgroundColour( ANVIL::LOGIN_SURFACE );
        }
        headerText->Add( m_headingLabel, 0 );

        m_subLabel = new wxStaticText( card, wxID_ANY, wxEmptyString );
        m_subLabel->SetForegroundColour( ANVIL::LOGIN_MUTED );
        m_subLabel->SetBackgroundColour( ANVIL::LOGIN_SURFACE );
        headerText->Add( m_subLabel, 0, wxTOP, FromDIP( 4 ) );

        header->Add( headerText, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP( 16 ) );
        cardSizer->Add( header, 0, wxEXPAND );
    }

    m_book = new wxSimplebook( card, wxID_ANY );
    m_book->AddPage( buildEmailPage( m_book ), wxEmptyString, true );
    m_book->AddPage( buildOtpPage( m_book ), wxEmptyString, false );
    m_book->AddPage( buildOpeningPage( m_book ), wxEmptyString, false );
    // Cap the form width: on a full-screen dialog a proportional column would stretch the
    // email field across half the display.
    m_book->SetMinSize( FromDIP( wxSize( 400, -1 ) ) );
    cardSizer->Add( m_book, 0, wxEXPAND | wxTOP, FromDIP( 24 ) );

    m_errorLabel = new wxStaticText( card, wxID_ANY, wxEmptyString );
    m_errorLabel->SetForegroundColour( ANVIL::LOGIN_ERROR );
    m_errorLabel->SetBackgroundColour( ANVIL::LOGIN_SURFACE );
    {
        wxFont errFont = base;
        errFont.SetFractionalPointSize( base.GetFractionalPointSize() * 0.9 );
        m_errorLabel->SetFont( errFont );
    }
    cardSizer->Add( m_errorLabel, 0, wxEXPAND | wxTOP, FromDIP( 8 ) );

    cardSizer->Add( new RULE_LABEL( card, wxEmptyString ), 0, wxEXPAND | wxTOP, FromDIP( 16 ) );
    cardSizer->Add( new TRUST_LINE( card, _( "Secure. Reliable. Engineered for teams." ) ), 0,
                    wxEXPAND | wxTOP, FromDIP( 12 ) );

    // GUTTER of the border is the halo/shadow gutter; the rest is the card's inner padding.
    cardOuter->Add( cardSizer, 1, wxEXPAND | wxALL, FromDIP( CARD_PANEL::GUTTER + 30 ) );
    card->SetSizer( cardOuter );

    column->Add( card, 0, wxEXPAND );
    column->AddStretchSpacer( 1 );

    // footer: © line, on the page rather than on the card
    {
        // Note SetLabelText, not the ctor label: the text contains '&', which a plain label
        // would eat as a mnemonic marker.  The build version rides along here because the
        // brand plate no longer carries it and support needs it readable before sign-in.
        wxStaticText* footer = new wxStaticText( form, wxID_ANY, wxEmptyString );
        footer->SetLabelText( wxString::Format( wxS( "© %d Anvil CAD · " ),
                                                wxDateTime::Now().GetYear() )
                              + _( "Confidential & Proprietary" )
                              + wxString::Format( wxS( " · v%s" ), GetSemanticVersion() ) );
        footer->SetForegroundColour( ANVIL::LOGIN_MUTED );
        footer->SetBackgroundColour( ANVIL::LOGIN_PAGE_BG );

        wxFont footerFont = KIUI::GetMonospacedUIFont();
        footerFont.SetFractionalPointSize( base.GetFractionalPointSize() * 0.72 );
        footer->SetFont( footerFont );

        // Wrapped and non-expanding: as the widest child it would otherwise dictate the
        // column width and stretch the card across the display.
        footer->Wrap( FromDIP( 440 ) );
        column->Add( footer, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP( 22 ) );
    }

    formOuter->AddStretchSpacer( 1 );
    formOuter->Add( column, 0, wxEXPAND );
    formOuter->AddStretchSpacer( 1 );
    form->SetSizer( formOuter );

    SetSizer( split );

    setHeader( _( "Welcome Back" ), _( "Sign in to access your workspace" ) );

    // The sign-in screen is the app's first surface, so it opens maximized rather than in a
    // small floating box -- but the caption now carries minimize/maximize boxes, so the size
    // the maximize box toggles back to has to be set before maximizing.
    // DIALOG_SHIM::Show() would later restore a remembered geometry over this, so the
    // Show() override below re-applies it.
    applyStartupGeometry();

    Bind( wxEVT_TIMER, &DIALOG_ANVIL_LOGIN::onResendTick, this, m_resendTimer.GetId() );

    SetInitialFocus( m_emailCtrl );
}


DIALOG_ANVIL_LOGIN::~DIALOG_ANVIL_LOGIN()
{
    m_closed = true;
    m_resendTimer.Stop();

    // A worker may still be talking to the server; give it a bounded chance to notice
    // m_closed and unwind before the members it might touch go away.
    for( int i = 0; i < 100 && m_busy.load(); ++i )
        wxMilliSleep( 50 );
}


wxWindow* DIALOG_ANVIL_LOGIN::buildEmailPage( wxWindow* aParent )
{
    wxPanel* page = new wxPanel( aParent );
    page->SetBackgroundColour( ANVIL::LOGIN_SURFACE );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    sizer->Add( new TRACKED_LABEL( page, _( "WORK EMAIL" ), ANVIL::LOGIN_INK,
                                   ANVIL::LOGIN_SURFACE ),
                0, wxBOTTOM, FromDIP( 8 ) );

    FIELD_BOX* fieldBox = new FIELD_BOX( page, &m_emailCtrl, wxTE_PROCESS_ENTER, true, 46 );
    sizer->Add( fieldBox, 0, wxEXPAND );

    m_emailCtrl->SetHint( wxS( "you@yourcompany.com" ) );
    m_emailCtrl->Bind( wxEVT_TEXT_ENTER, [this]( wxCommandEvent& ) { onSendOtp(); } );

    sizer->Add( new RULE_LABEL( page, _( "Sign in with" ) ), 0, wxEXPAND | wxTOP,
                FromDIP( 20 ) );

    m_sendButton = new ANVIL_LOGIN_BUTTON( page, _( "Sign in with Email OTP" ),
                                           ANVIL_LOGIN_BUTTON::PRIMARY,
                                           ANVIL_LOGIN_BUTTON::GLYPH_MAIL,
                                           [this]() { onSendOtp(); } );
    sizer->Add( m_sendButton, 0, wxEXPAND | wxTOP, FromDIP( 14 ) );


    page->SetSizer( sizer );
    return page;
}


wxWindow* DIALOG_ANVIL_LOGIN::buildOtpPage( wxWindow* aParent )
{
    wxPanel* page = new wxPanel( aParent );
    page->SetBackgroundColour( ANVIL::LOGIN_SURFACE );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    sizer->Add( new TRACKED_LABEL( page, _( "VERIFICATION CODE" ), ANVIL::LOGIN_INK,
                                   ANVIL::LOGIN_SURFACE ),
                0, wxBOTTOM, FromDIP( 8 ) );

    FIELD_BOX* fieldBox = new FIELD_BOX( page, &m_otpCtrl,
                                         wxTE_PROCESS_ENTER | wxTE_CENTRE, false, 58 );
    sizer->Add( fieldBox, 0, wxEXPAND );

    // The code is digits only, exactly OTP_LENGTH of them, in a widely-spaced mono face.
    wxFont otpFont = KIUI::GetMonospacedUIFont();
    otpFont.SetFractionalPointSize( GetFont().GetFractionalPointSize() * 1.6 );
    otpFont.MakeBold();
    m_otpCtrl->SetFont( otpFont );
    m_otpCtrl->SetMaxLength( ANVIL_API::OTP_LENGTH );

    m_otpCtrl->Bind( wxEVT_TEXT_ENTER, [this]( wxCommandEvent& ) { onVerifyOtp(); } );
    m_otpCtrl->Bind( wxEVT_CHAR, []( wxKeyEvent& aEvt )
                     {
                         int code = aEvt.GetKeyCode();

                         // let control keys and digits through; swallow everything else
                         if( code < WXK_SPACE || code == WXK_DELETE || wxIsdigit( code ) )
                             aEvt.Skip();
                     } );

    m_verifyButton = new ANVIL_LOGIN_BUTTON( page, _( "Verify & Continue" ),
                                             ANVIL_LOGIN_BUTTON::PRIMARY,
                                             ANVIL_LOGIN_BUTTON::GLYPH_SHIELD,
                                             [this]() { onVerifyOtp(); } );
    sizer->Add( m_verifyButton, 0, wxEXPAND | wxTOP, FromDIP( 18 ) );

    wxBoxSizer* links = new wxBoxSizer( wxHORIZONTAL );
    m_resendLink = new LINK_LABEL( page, _( "Resend code" ), [this]() { onResend(); } );
    m_changeEmailLink = new LINK_LABEL( page, _( "Change email" ),
                                        [this]() { onChangeEmail(); } );
    links->Add( m_resendLink, 0 );
    links->AddStretchSpacer();
    links->Add( m_changeEmailLink, 0 );
    sizer->Add( links, 0, wxEXPAND | wxTOP, FromDIP( 14 ) );

    page->SetSizer( sizer );
    return page;
}


wxWindow* DIALOG_ANVIL_LOGIN::buildOpeningPage( wxWindow* aParent )
{
    wxPanel* page = new wxPanel( aParent );
    page->SetBackgroundColour( ANVIL::LOGIN_SURFACE );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    sizer->Add( new TRACKED_LABEL( page, _( "SESSION ESTABLISHED" ), ANVIL::ACCENT,
                                   ANVIL::LOGIN_SURFACE ),
                0, wxBOTTOM, FromDIP( 8 ) );

    page->SetSizer( sizer );
    return page;
}


void DIALOG_ANVIL_LOGIN::applyStartupGeometry()
{
    int index = wxDisplay::GetFromWindow( this );

    if( index == wxNOT_FOUND )
        index = 0;

    const wxRect work = wxDisplay( index ).GetClientArea();

    // Floor for a restored-down window: below this the brand panel and the sign-in card start
    // fighting over the width, so refuse to be dragged any smaller.
    SetMinSize( wxSize( std::min( FromDIP( 1000 ), work.width ),
                        std::min( FromDIP( 640 ), work.height ) ) );

    // What the maximize box toggles back to: a comfortable window centred on the work area,
    // never larger than the display can hold.
    const wxSize restored( std::min( FromDIP( 1280 ), work.width ),
                           std::min( FromDIP( 820 ), work.height ) );

    SetSize( wxRect( work.x + ( work.width - restored.x ) / 2,
                     work.y + ( work.height - restored.y ) / 2, restored.x, restored.y ) );

    Maximize( true );
}


bool DIALOG_ANVIL_LOGIN::Show( bool aShow )
{
    const bool ret = DIALOG_SHIM::Show( aShow );

    // DIALOG_SHIM restores a saved position/size keyed on the dialog title; for a full-page
    // sign-in screen that is never what we want, so take the geometry back afterwards.
    if( aShow )
        applyStartupGeometry();

    return ret;
}


void DIALOG_ANVIL_LOGIN::ShowOpeningState()
{
    const ANVIL_USER user = ANVIL_AUTH::GetUser();

    if( user.email.IsEmpty() )
        setHeader( _( "You're signed in" ), _( "Opening your workspace…" ) );
    else
        setHeader( _( "You're signed in" ), wxString::Format( _( "%s — opening your "
                                                                "workspace…" ), user.Label() ) );

    m_book->SetSelection( 2 );
    clearError();
    Layout();
    Update();       // paint it now: the caller is about to block building the main window
}


bool DIALOG_ANVIL_LOGIN::isPlausibleEmail( const wxString& aEmail )
{
    const int at = aEmail.Find( '@' );

    if( at <= 0 )
        return false;

    const wxString domain = aEmail.Mid( at + 1 );

    return !domain.IsEmpty() && domain.Find( '@' ) == wxNOT_FOUND
           && domain.Find( '.' ) != wxNOT_FOUND && !aEmail.Contains( wxS( " " ) );
}


void DIALOG_ANVIL_LOGIN::setHeader( const wxString& aTitle, const wxString& aSubtitle )
{
    m_headingLabel->SetLabel( aTitle );

    // Wrap at the header's own width, not the card's: the badge sits to its left.
    m_subLabel->SetLabel( aSubtitle );
    m_subLabel->Wrap( FromDIP( 320 ) );

    Layout();
}


void DIALOG_ANVIL_LOGIN::showError( const wxString& aMessage )
{
    m_errorLabel->SetForegroundColour( ANVIL::LOGIN_ERROR );
    m_errorLabel->SetLabel( aMessage );
    m_errorLabel->Wrap( FromDIP( 400 ) );
    Layout();
}



void DIALOG_ANVIL_LOGIN::clearError()
{
    m_errorLabel->SetLabel( wxEmptyString );
    Layout();
}


void DIALOG_ANVIL_LOGIN::setBusy( bool aBusy )
{
    m_emailCtrl->Enable( !aBusy );
    m_otpCtrl->Enable( !aBusy );
    m_sendButton->Enable( !aBusy );
    m_verifyButton->Enable( !aBusy );
    m_resendLink->Enable( !aBusy );
    m_changeEmailLink->Enable( !aBusy );

    if( !aBusy )
    {
        m_sendButton->SetLabelText( _( "Sign in with Email OTP" ) );
        m_verifyButton->SetLabelText( _( "Verify & Continue" ) );
    }
}






void DIALOG_ANVIL_LOGIN::onSendOtp()
{
    if( m_busy )
        return;

    const wxString email = m_emailCtrl->GetValue().Trim().Trim( false );

    if( !isPlausibleEmail( email ) )
    {
        showError( _( "Enter a valid email address." ) );
        return;
    }

    if( !ANVIL_AUTH_CONFIG::IsConfigured() )
    {
        showError( wxString::Format( _( "No API key configured. Add ApiKey to %s and restart." ),
                                     ANVIL_AUTH_CONFIG::ConfigFilePath() ) );
        return;
    }

    clearError();
    m_busy = true;
    setBusy( true );
    m_sendButton->SetLabelText( _( "Sending…" ) );

    GetKiCadThreadPool().detach_task(
            [this, email]()
            {
                wxString error;
                bool     ok = false;

                // Nothing may escape a pool task: an exception here would bypass the
                // m_busy reset below and leave the dialog stuck in its "Sending…" state.
                try
                {
                    ok = ANVIL_AUTH::RequestOtp( email, error );
                }
                catch( const std::exception& e )
                {
                    error = wxString::FromUTF8( e.what() );
                }
                catch( ... )
                {
                    error = _( "Unexpected failure while contacting the server." );
                }

                if( !m_closed )
                {
                    CallAfter(
                            [this, ok, error, email]()
                            {
                                setBusy( false );

                                if( ok )
                                {
                                    m_email = email;
                                    setHeader( _( "Check your email" ),
                                               wxString::Format(
                                                       _( "We sent a %d-digit code to %s." ),
                                                       ANVIL_API::OTP_LENGTH, email ) );
                                    m_book->SetSelection( 1 );
                                    m_otpCtrl->Clear();
                                    m_otpCtrl->SetFocus();
                                    startResendCooldown();
                                    Layout();
                                }
                                else
                                {
                                    showError( error );
                                }
                            } );
                }

                m_busy = false;
            } );
}


void DIALOG_ANVIL_LOGIN::onVerifyOtp()
{
    if( m_busy )
        return;

    const wxString otp = m_otpCtrl->GetValue().Trim().Trim( false );

    if( (int) otp.Length() != ANVIL_API::OTP_LENGTH )
    {
        showError( wxString::Format( _( "Enter the %d-digit code from the email." ),
                                     ANVIL_API::OTP_LENGTH ) );
        return;
    }

    clearError();
    m_busy = true;
    setBusy( true );
    m_verifyButton->SetLabelText( _( "Verifying…" ) );

    const wxString email = m_email;

    GetKiCadThreadPool().detach_task(
            [this, email, otp]()
            {
                wxString error;
                bool     ok = false;

                // See onSendOtp(): a pool task must never let an exception escape.
                try
                {
                    ok = ANVIL_AUTH::VerifyOtp( email, otp, error );
                }
                catch( const std::exception& e )
                {
                    error = wxString::FromUTF8( e.what() );
                }
                catch( ... )
                {
                    error = _( "Unexpected failure while contacting the server." );
                }

                if( !m_closed )
                {
                    CallAfter(
                            [this, ok, error]()
                            {
                                setBusy( false );

                                if( ok )
                                {
                                    EndModal( wxID_OK );
                                }
                                else
                                {
                                    showError( error );
                                    m_otpCtrl->SelectAll();
                                    m_otpCtrl->SetFocus();
                                }
                            } );
                }

                m_busy = false;
            } );
}


void DIALOG_ANVIL_LOGIN::onResend()
{
    if( m_resendRemaining > 0 || m_busy )
        return;

    // Jump back through the normal send path with the remembered address.
    m_emailCtrl->SetValue( m_email );
    onSendOtp();
}


void DIALOG_ANVIL_LOGIN::onChangeEmail()
{
    if( m_busy )
        return;

    m_resendTimer.Stop();
    m_resendRemaining = 0;
    clearError();
    setHeader( _( "Welcome Back" ), _( "Sign in to access your workspace" ) );
    m_book->SetSelection( 0 );
    m_emailCtrl->SetFocus();
    m_emailCtrl->SelectAll();
    Layout();
}


void DIALOG_ANVIL_LOGIN::startResendCooldown()
{
    m_resendRemaining = ANVIL_API::RESEND_COOLDOWN_SECS;
    m_resendLink->Enable( false );
    m_resendLink->SetLabel(
            wxString::Format( _( "Resend code (%ds)" ), m_resendRemaining ) );
    m_resendTimer.Start( 1000 );
}


void DIALOG_ANVIL_LOGIN::onResendTick( wxTimerEvent& )
{
    if( --m_resendRemaining > 0 )
    {
        m_resendLink->SetLabel(
                wxString::Format( _( "Resend code (%ds)" ), m_resendRemaining ) );
    }
    else
    {
        m_resendTimer.Stop();
        m_resendRemaining = 0;
        m_resendLink->SetLabel( _( "Resend code" ) );
        m_resendLink->Enable( true );
    }

    Layout();
}
