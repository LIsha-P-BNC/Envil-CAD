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
#include <widgets/ui_common.h>

#include <algorithm>
#include <memory>

#include <wx/dcbuffer.h>
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
 * NEMI Suite sign-in look, drawn natively: emerald gradient brand panel on the left, light
 * blueprint-grid form surface on the right.  Every colour is an ANVIL:: theme token
 * (anvil_theme.h) and every functional string is a translatable wxString — nothing is
 * hard-coded at the point of use.  API endpoints/fields come from anvil_api_defs.h and
 * deployment values from ANVIL_AUTH_CONFIG; neither appears in this file.
 */

namespace
{

/// Draw @a aText at ( aX, aY ) with @a aTracking extra pixels between characters (the
/// letterspaced "console" look the comp uses for small mono labels).
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
// ANVIL_NOTCH_BUTTON — the deep-emerald action button with the cut bottom-right corner
// -----------------------------------------------------------------------------------------

class ANVIL_NOTCH_BUTTON : public wxWindow
{
public:
    ANVIL_NOTCH_BUTTON( wxWindow* aParent, const wxString& aLabel,
                        std::function<void()> aOnClick ) :
            wxWindow( aParent, wxID_ANY ),
            m_label( aLabel.Upper() ),
            m_onClick( std::move( aOnClick ) ),
            m_hover( false ),
            m_pressed( false )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );

        wxFont font = KIUI::GetMonospacedUIFont();
        font.SetFractionalPointSize( GetFont().GetFractionalPointSize() * 0.9 );
        font.MakeBold();
        SetFont( font );

        SetMinSize( wxSize( -1, FromDIP( 42 ) ) );
        SetCursor( wxCursor( wxCURSOR_HAND ) );

        Bind( wxEVT_PAINT, &ANVIL_NOTCH_BUTTON::onPaint, this );
        Bind( wxEVT_ENTER_WINDOW, [this]( wxMouseEvent& ) { m_hover = true; Refresh(); } );
        Bind( wxEVT_LEAVE_WINDOW, [this]( wxMouseEvent& )
              {
                  m_hover = false;
                  m_pressed = false;
                  Refresh();
              } );
        Bind( wxEVT_LEFT_DOWN, [this]( wxMouseEvent& ) { m_pressed = true; Refresh(); } );
        Bind( wxEVT_LEFT_UP, [this]( wxMouseEvent& aEvt )
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
        m_label = aLabel.Upper();
        Refresh();
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize sz = GetClientSize();

        // The parent is the light form surface; clear to it so the notch cut-out shows it.
        dc.SetBackground( wxBrush( ANVIL::LOGIN_SURFACE ) );
        dc.Clear();

        wxColour fill = ANVIL::CAP_ACTIVE;                       // deep emerald

        if( !IsEnabled() )
            fill = ANVIL::LOGIN_MUTED;
        else if( m_pressed )
            fill = fill.ChangeLightness( 85 );
        else if( m_hover )
            fill = fill.ChangeLightness( 118 );

        const int notch = FromDIP( 12 );

        wxPoint poly[5] = { wxPoint( 0, 0 ),
                            wxPoint( sz.x, 0 ),
                            wxPoint( sz.x, sz.y - notch ),
                            wxPoint( sz.x - notch, sz.y ),
                            wxPoint( 0, sz.y ) };

        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.SetBrush( wxBrush( fill ) );
        dc.DrawPolygon( 5, poly );

        dc.SetFont( GetFont() );
        dc.SetTextForeground( ANVIL::ON_ACCENT );

        const int tracking = FromDIP( 3 );
        int       textW = trackedWidth( dc, m_label, tracking );
        int       textH = dc.GetTextExtent( wxS( "X" ) ).y;

        drawTracked( dc, m_label, ( sz.x - textW ) / 2, ( sz.y - textH ) / 2, tracking );
    }

    wxString              m_label;
    std::function<void()> m_onClick;
    bool                  m_hover;
    bool                  m_pressed;
};


namespace
{

// -------------------------------------------------------------------------------------
// FIELD_BOX — bordered input container wrapping a borderless wxTextCtrl, so the field
// LOOKS like the comp while typing/selection/clipboard/IME stay fully native.
// -------------------------------------------------------------------------------------

class FIELD_BOX : public wxPanel
{
public:
    FIELD_BOX( wxWindow* aParent, wxTextCtrl** aCtrlOut, long aTextStyle ) :
            wxPanel( aParent, wxID_ANY )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        Bind( wxEVT_PAINT, &FIELD_BOX::onPaint, this );

        m_ctrl = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                 wxDefaultSize, aTextStyle | wxBORDER_NONE );
        m_ctrl->SetBackgroundColour( ANVIL::LOGIN_FIELD_BG );
        m_ctrl->SetForegroundColour( ANVIL::LOGIN_INK );

        wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
        sizer->AddStretchSpacer();
        sizer->Add( m_ctrl, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP( 12 ) );
        sizer->AddStretchSpacer();
        SetSizer( sizer );

        SetMinSize( wxSize( -1, FromDIP( 40 ) ) );

        *aCtrlOut = m_ctrl;
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize sz = GetClientSize();

        dc.SetBackground( wxBrush( ANVIL::LOGIN_SURFACE ) );
        dc.Clear();

        dc.SetPen( wxPen( ANVIL::LOGIN_FIELD_BORDER, FromDIP( 1 ) ) );
        dc.SetBrush( wxBrush( ANVIL::LOGIN_FIELD_BG ) );
        dc.DrawRectangle( 0, 0, sz.x, sz.y );
    }

    wxTextCtrl* m_ctrl;
};


// -------------------------------------------------------------------------------------
// LINK_LABEL — small clickable text (resend / change email / server settings)
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
        font.SetFractionalPointSize( font.GetFractionalPointSize() * 0.9 );
        font.SetUnderlined( true );
        SetFont( font );

        Bind( wxEVT_LEFT_UP, [this]( wxMouseEvent& )
              {
                  if( IsEnabled() && m_onClick )
                      m_onClick();
              } );
    }

private:
    std::function<void()> m_onClick;
};


// -------------------------------------------------------------------------------------
// BRAND_PANEL — the left gradient panel; fully painted, no child widgets
// -------------------------------------------------------------------------------------

class BRAND_PANEL : public wxPanel
{
public:
    explicit BRAND_PANEL( wxWindow* aParent ) : wxPanel( aParent, wxID_ANY )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( aParent->FromDIP( wxSize( 470, -1 ) ) );
        Bind( wxEVT_PAINT, &BRAND_PANEL::onPaint, this );
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize sz = GetClientSize();

        const int margin = FromDIP( 44 );

        // The wxGraphicsContext work (gradient + logo rings) must be finished and flushed
        // BEFORE any plain-DC text lands, or the deferred gc output paints over the text.
        {
            std::unique_ptr<wxGraphicsContext> gc( wxGraphicsContext::Create( dc ) );

            // Diagonal emerald gradient, near-black top-left to rich emerald bottom-right.
            gc->SetBrush( gc->CreateLinearGradientBrush( 0, 0, sz.x, sz.y,
                                                         ANVIL::LOGIN_GRAD_TOP,
                                                         ANVIL::LOGIN_GRAD_BOTTOM ) );
            gc->DrawRectangle( 0, 0, sz.x, sz.y );

            // Twin-ring mark (the "oo" glyph of the comp), drawn, not bitmapped.
            const int r = FromDIP( 8 );
            const int cy = margin - FromDIP( 6 );

            gc->SetPen( wxPen( ANVIL::BONE, FromDIP( 2 ) ) );
            gc->SetBrush( *wxTRANSPARENT_BRUSH );
            gc->DrawEllipse( margin, cy - r, 2 * r, 2 * r );
            gc->DrawEllipse( margin + (int) ( 1.4 * r ), cy - r, 2 * r, 2 * r );
        }

        const wxFont base = GetFont();

        wxFont mono = KIUI::GetMonospacedUIFont();
        mono.SetFractionalPointSize( base.GetFractionalPointSize() * 0.78 );

        // --- header: wordmark --------------------------------------------------------
        {
            const int cy = margin - FromDIP( 6 );

            wxFont brand = base;
            brand.MakeBold();

            dc.SetFont( brand );
            int x = margin + FromDIP( 42 );
            dc.SetTextForeground( ANVIL::BONE );
            dc.DrawText( wxS( "NEMI" ), x, cy - FromDIP( 10 ) );
            int w = dc.GetTextExtent( wxS( "NEMI" ) ).x;
            dc.SetTextForeground( ANVIL::ACCENT );
            dc.DrawText( wxS( " Suite" ), x + w, cy - FromDIP( 10 ) );

            dc.SetFont( mono );
            dc.SetTextForeground( ANVIL::DIM_MENU );
            drawTracked( dc, wxS( "INTERNAL CONSOLES · V2.0" ), x, cy + FromDIP( 8 ),
                         FromDIP( 1 ) );
        }

        // --- hero block --------------------------------------------------------------
        int y = (int) ( sz.y * 0.36 );

        {
            // eyebrow: emerald dash + tracked label
            dc.SetPen( wxPen( ANVIL::ACCENT, FromDIP( 2 ) ) );
            dc.DrawLine( margin, y + FromDIP( 6 ), margin + FromDIP( 22 ), y + FromDIP( 6 ) );

            dc.SetFont( mono );
            dc.SetTextForeground( ANVIL::ACCEL );
            drawTracked( dc, wxS( "NEMI · INTERNAL PLATFORM" ), margin + FromDIP( 32 ), y,
                         FromDIP( 2 ) );

            y += FromDIP( 26 );

            wxFont heading = base;
            heading.MakeBold();
            heading.SetFractionalPointSize( base.GetFractionalPointSize() * 2.6 );
            dc.SetFont( heading );

            dc.SetTextForeground( ANVIL::BONE );
            dc.DrawText( _( "One suite." ), margin, y );
            y += dc.GetTextExtent( wxS( "X" ) ).y + FromDIP( 2 );

            dc.SetTextForeground( ANVIL::ACCENT );
            dc.DrawText( _( "Eleven consoles." ), margin, y );
            y += dc.GetTextExtent( wxS( "X" ) ).y + FromDIP( 18 );

            wxFont body = base;
            body.SetFractionalPointSize( base.GetFractionalPointSize() * 1.05 );
            dc.SetFont( body );
            dc.SetTextForeground( ANVIL::BONE.ChangeLightness( 90 ) );
            dc.DrawText( _( "Anvil envisions + engineers it · Orion manufactures it ·" ),
                         margin, y );
            y += dc.GetTextExtent( wxS( "X" ) ).y + FromDIP( 4 );
            dc.DrawText( _( "Frontier iterates it. Sign in to open your consoles." ), margin, y );
            y += dc.GetTextExtent( wxS( "X" ) ).y + FromDIP( 24 );
        }

        // --- console chips -----------------------------------------------------------
        {
            struct CHIP
            {
                wxString title;
                wxString caption;
            };

            const CHIP chips[3] = { { wxS( "Anvil" ), wxS( "ENVISION + ENGINEER" ) },
                                    { wxS( "Orion" ), wxS( "MANUFACTURE" ) },
                                    { wxS( "Frontier" ), wxS( "ITERATE" ) } };

            wxFont chipTitle = base;
            chipTitle.MakeBold();

            wxFont chipMono = mono;
            chipMono.SetFractionalPointSize( mono.GetFractionalPointSize() * 0.9 );

            int x = margin;

            for( const CHIP& chip : chips )
            {
                dc.SetFont( chipMono );
                int captionW = trackedWidth( dc, chip.caption, FromDIP( 1 ) );

                dc.SetFont( chipTitle );
                int titleW = dc.GetTextExtent( chip.title ).x;

                int w = std::max( titleW, captionW ) + FromDIP( 28 );
                int h = FromDIP( 48 );

                dc.SetPen( wxPen( ANVIL::BORDER, FromDIP( 1 ) ) );
                dc.SetBrush( *wxTRANSPARENT_BRUSH );
                dc.DrawRectangle( x, y, w, h );

                dc.SetTextForeground( ANVIL::BONE );
                dc.DrawText( chip.title, x + FromDIP( 14 ), y + FromDIP( 8 ) );

                dc.SetFont( chipMono );
                dc.SetTextForeground( ANVIL::DIM_MENU );
                drawTracked( dc, chip.caption, x + FromDIP( 14 ), y + FromDIP( 28 ),
                             FromDIP( 1 ) );

                x += w + FromDIP( 10 );
            }
        }

        // --- footer ------------------------------------------------------------------
        {
            dc.SetFont( mono );
            dc.SetTextForeground( ANVIL::DIM_MENU );

            const wxString footer = _( "SESSIONS & TIME-ON-SUITE ARE TRACKED FOR ANALYTICS" );
            int w = trackedWidth( dc, footer, FromDIP( 1 ) );
            drawTracked( dc, footer, ( sz.x - w ) / 2, sz.y - FromDIP( 24 ), FromDIP( 1 ) );
        }
    }
};


// -------------------------------------------------------------------------------------
// FORM_PANEL — the right surface: white with a fine blueprint grid
// -------------------------------------------------------------------------------------

class FORM_PANEL : public wxPanel
{
public:
    explicit FORM_PANEL( wxWindow* aParent ) : wxPanel( aParent, wxID_ANY )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        Bind( wxEVT_PAINT, &FORM_PANEL::onPaint, this );
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize sz = GetClientSize();

        dc.SetBackground( wxBrush( ANVIL::LOGIN_SURFACE ) );
        dc.Clear();

        dc.SetPen( wxPen( ANVIL::LOGIN_GRID, 1 ) );

        const int step = FromDIP( 46 );

        for( int x = step; x < sz.x; x += step )
            dc.DrawLine( x, 0, x, sz.y );

        for( int y = step; y < sz.y; y += step )
            dc.DrawLine( 0, y, sz.x, y );
    }
};


// -------------------------------------------------------------------------------------
// DIALOG_SERVER_SETTINGS — tiny configurator writing the .env values file
// -------------------------------------------------------------------------------------

class DIALOG_SERVER_SETTINGS : public wxDialog
{
public:
    explicit DIALOG_SERVER_SETTINGS( wxWindow* aParent ) :
            wxDialog( aParent, wxID_ANY, _( "Server Settings" ), wxDefaultPosition,
                      wxDefaultSize, wxDEFAULT_DIALOG_STYLE )
    {
        wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

        wxString base, key, projectId;
        ANVIL_AUTH_CONFIG::ReadFileValues( base, key, projectId );

        wxFlexGridSizer* grid = new wxFlexGridSizer( 2, FromDIP( 6 ), FromDIP( 8 ) );
        grid->AddGrowableCol( 1 );

        auto addRow = [&]( const wxString& aLabel, const wxString& aValue,
                           long aStyle ) -> wxTextCtrl*
        {
            grid->Add( new wxStaticText( this, wxID_ANY, aLabel ), 0,
                       wxALIGN_CENTER_VERTICAL );
            wxTextCtrl* ctrl = new wxTextCtrl( this, wxID_ANY, aValue, wxDefaultPosition,
                                               FromDIP( wxSize( 320, -1 ) ), aStyle );
            grid->Add( ctrl, 1, wxEXPAND );
            return ctrl;
        };

        m_base = addRow( _( "Server URL:" ), base, 0 );
        m_key = addRow( _( "API key:" ), key, wxTE_PASSWORD );
        m_projectId = addRow( _( "Project ID:" ), projectId, 0 );

        main->Add( grid, 1, wxEXPAND | wxALL, FromDIP( 12 ) );

        wxStaticText* hint = new wxStaticText(
                this, wxID_ANY,
                wxString::Format( _( "Stored in: %s" ), ANVIL_AUTH_CONFIG::ConfigFilePath() ) );
        hint->SetForegroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT ) );
        main->Add( hint, 0, wxLEFT | wxRIGHT, FromDIP( 12 ) );

        main->Add( CreateStdDialogButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL,
                   FromDIP( 12 ) );

        SetSizerAndFit( main );
        CentreOnParent();
    }

    bool TransferDataFromWindow() override
    {
        if( !ANVIL_AUTH_CONFIG::Save( m_base->GetValue().Trim().Trim( false ),
                                      m_key->GetValue().Trim().Trim( false ),
                                      m_projectId->GetValue().Trim().Trim( false ) ) )
        {
            wxMessageBox( _( "Could not write the configuration file." ), _( "Server Settings" ),
                          wxICON_ERROR, this );
            return false;
        }

        return true;
    }

private:
    wxTextCtrl* m_base;
    wxTextCtrl* m_key;
    wxTextCtrl* m_projectId;
};

} // namespace


// -----------------------------------------------------------------------------------------
// DIALOG_ANVIL_LOGIN
// -----------------------------------------------------------------------------------------

DIALOG_ANVIL_LOGIN::DIALOG_ANVIL_LOGIN( wxWindow* aParent ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Sign in" ), wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE ),
        m_book( nullptr ),
        m_emailCtrl( nullptr ),
        m_sendButton( nullptr ),
        m_otpInfo( nullptr ),
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

    split->Add( new BRAND_PANEL( this ), 11, wxEXPAND );

    FORM_PANEL* form = new FORM_PANEL( this );
    split->Add( form, 9, wxEXPAND );

    // ---- right column content, centred ----------------------------------------------
    wxBoxSizer* formOuter = new wxBoxSizer( wxHORIZONTAL );
    wxBoxSizer* column = new wxBoxSizer( wxVERTICAL );

    column->AddStretchSpacer( 4 );

    wxFont base = GetFont();

    wxStaticText* heading = new wxStaticText( form, wxID_ANY, _( "Sign in" ) );
    {
        wxFont headingFont = base;
        headingFont.MakeBold();
        headingFont.SetFractionalPointSize( base.GetFractionalPointSize() * 1.9 );
        heading->SetFont( headingFont );
        heading->SetForegroundColour( ANVIL::LOGIN_INK );
        heading->SetBackgroundColour( ANVIL::LOGIN_SURFACE );
    }
    column->Add( heading, 0 );

    wxStaticText* sub = new wxStaticText( form, wxID_ANY,
                                          _( "Access the NEMI console suite." ) );
    sub->SetForegroundColour( ANVIL::LOGIN_MUTED );
    sub->SetBackgroundColour( ANVIL::LOGIN_SURFACE );
    column->Add( sub, 0, wxTOP, FromDIP( 4 ) );

    m_book = new wxSimplebook( form, wxID_ANY );
    m_book->AddPage( buildEmailPage( m_book ), wxEmptyString, true );
    m_book->AddPage( buildOtpPage( m_book ), wxEmptyString, false );
    column->Add( m_book, 0, wxEXPAND | wxTOP, FromDIP( 22 ) );

    m_errorLabel = new wxStaticText( form, wxID_ANY, wxEmptyString );
    m_errorLabel->SetForegroundColour( ANVIL::LOGIN_ERROR );
    m_errorLabel->SetBackgroundColour( ANVIL::LOGIN_SURFACE );
    {
        wxFont errFont = base;
        errFont.SetFractionalPointSize( base.GetFractionalPointSize() * 0.9 );
        m_errorLabel->SetFont( errFont );
    }
    column->Add( m_errorLabel, 0, wxEXPAND | wxTOP, FromDIP( 10 ) );

    column->AddStretchSpacer( 5 );

    // footer: © line + server settings link
    {
        // Note SetLabelText, not the ctor label: the text contains '&', which a plain label
        // would eat as a mnemonic marker.
        wxStaticText* footer = new wxStaticText( form, wxID_ANY, wxEmptyString );
        footer->SetLabelText( wxString::Format( wxS( "© %d NEMI · " ),
                                                wxDateTime::Now().GetYear() )
                              + _( "CONFIDENTIAL & PROPRIETARY" ) );
        footer->SetForegroundColour( ANVIL::LOGIN_MUTED );
        footer->SetBackgroundColour( ANVIL::LOGIN_SURFACE );

        wxFont footerFont = KIUI::GetMonospacedUIFont();
        footerFont.SetFractionalPointSize( base.GetFractionalPointSize() * 0.72 );
        footer->SetFont( footerFont );

        wxBoxSizer* footerRow = new wxBoxSizer( wxHORIZONTAL );
        footerRow->Add( footer, 0, wxALIGN_CENTER_VERTICAL );
        footerRow->AddStretchSpacer();
        footerRow->Add( new LINK_LABEL( form, _( "Server settings" ),
                                        [this]() { onServerSettings(); } ),
                        0, wxALIGN_CENTER_VERTICAL );

        column->Add( footerRow, 0, wxEXPAND | wxBOTTOM, FromDIP( 16 ) );
    }

    formOuter->AddStretchSpacer( 1 );
    formOuter->Add( column, 5, wxEXPAND );
    formOuter->AddStretchSpacer( 1 );
    form->SetSizer( formOuter );

    SetSizer( split );
    SetClientSize( FromDIP( wxSize( 980, 560 ) ) );

    Bind( wxEVT_TIMER, &DIALOG_ANVIL_LOGIN::onResendTick, this, m_resendTimer.GetId() );

    SetInitialFocus( m_emailCtrl );
    CenterOnScreen();
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
                0, wxBOTTOM, FromDIP( 6 ) );

    FIELD_BOX* fieldBox = new FIELD_BOX( page, &m_emailCtrl, wxTE_PROCESS_ENTER );
    sizer->Add( fieldBox, 0, wxEXPAND );

    m_emailCtrl->Bind( wxEVT_TEXT_ENTER, [this]( wxCommandEvent& ) { onSendOtp(); } );

    m_sendButton = new ANVIL_NOTCH_BUTTON( page, _( "Send OTP" ), [this]() { onSendOtp(); } );
    sizer->Add( m_sendButton, 0, wxEXPAND | wxTOP, FromDIP( 18 ) );

    page->SetSizer( sizer );
    return page;
}


wxWindow* DIALOG_ANVIL_LOGIN::buildOtpPage( wxWindow* aParent )
{
    wxPanel* page = new wxPanel( aParent );
    page->SetBackgroundColour( ANVIL::LOGIN_SURFACE );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    sizer->Add( new TRACKED_LABEL( page, _( "ENTER CODE" ), ANVIL::LOGIN_INK,
                                   ANVIL::LOGIN_SURFACE ),
                0, wxBOTTOM, FromDIP( 6 ) );

    m_otpInfo = new wxStaticText( page, wxID_ANY, wxEmptyString );
    m_otpInfo->SetForegroundColour( ANVIL::LOGIN_MUTED );
    m_otpInfo->SetBackgroundColour( ANVIL::LOGIN_SURFACE );
    sizer->Add( m_otpInfo, 0, wxBOTTOM, FromDIP( 8 ) );

    FIELD_BOX* fieldBox = new FIELD_BOX( page, &m_otpCtrl,
                                         wxTE_PROCESS_ENTER | wxTE_CENTRE );
    sizer->Add( fieldBox, 0, wxEXPAND );

    // The code is digits only, exactly OTP_LENGTH of them, in a widely-spaced mono face.
    wxFont otpFont = KIUI::GetMonospacedUIFont();
    otpFont.SetFractionalPointSize( GetFont().GetFractionalPointSize() * 1.4 );
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

    m_verifyButton = new ANVIL_NOTCH_BUTTON( page, _( "Verify" ), [this]() { onVerifyOtp(); } );
    sizer->Add( m_verifyButton, 0, wxEXPAND | wxTOP, FromDIP( 18 ) );

    wxBoxSizer* links = new wxBoxSizer( wxHORIZONTAL );
    m_resendLink = new LINK_LABEL( page, _( "Resend code" ), [this]() { onResend(); } );
    m_changeEmailLink = new LINK_LABEL( page, _( "Change email" ),
                                        [this]() { onChangeEmail(); } );
    links->Add( m_resendLink, 0 );
    links->AddStretchSpacer();
    links->Add( m_changeEmailLink, 0 );
    sizer->Add( links, 0, wxEXPAND | wxTOP, FromDIP( 12 ) );

    page->SetSizer( sizer );
    return page;
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


void DIALOG_ANVIL_LOGIN::showError( const wxString& aMessage )
{
    m_errorLabel->SetLabel( aMessage );
    m_errorLabel->Wrap( m_errorLabel->GetParent()->GetClientSize().x / 2 );
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
        m_sendButton->SetLabelText( _( "Send OTP" ) );
        m_verifyButton->SetLabelText( _( "Verify" ) );
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
        showError( _( "Server is not configured." ) );
        onServerSettings();
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
                bool     ok = ANVIL_AUTH::RequestOtp( email, error );

                if( !m_closed )
                {
                    CallAfter(
                            [this, ok, error, email]()
                            {
                                setBusy( false );

                                if( ok )
                                {
                                    m_email = email;
                                    m_otpInfo->SetLabel( wxString::Format(
                                            _( "We emailed a code to %s." ), email ) );
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
                bool     ok = ANVIL_AUTH::VerifyOtp( email, otp, error );

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
    m_book->SetSelection( 0 );
    m_emailCtrl->SetFocus();
    m_emailCtrl->SelectAll();
    Layout();
}


void DIALOG_ANVIL_LOGIN::onServerSettings()
{
    DIALOG_SERVER_SETTINGS dlg( this );
    dlg.ShowModal();
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
