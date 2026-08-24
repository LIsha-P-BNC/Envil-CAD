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

#ifndef DIALOG_ANVIL_LOGIN_H_
#define DIALOG_ANVIL_LOGIN_H_

#include <dialog_shim.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <wx/gdicmn.h>
#include <wx/timer.h>

class wxSimplebook;
class wxStaticText;
class wxTextCtrl;
class wxTopLevelWindow;
class ANVIL_LOADING_TRACK;
class ANVIL_LOGIN_BUTTON;


/**
 * The Anvil CAD sign-in gate — email + OTP login.
 *
 * One circuit board fills the window, and the sign-in card sits in a rounded pocket milled
 * into it: brand artwork to the left, the card seated in the copper to the right, joined by a
 * routed wall and a row of pads rather than split down a seam.  Three steps in one card via a
 * wxSimplebook: enter email → "SIGN IN WITH EMAIL OTP", then enter the emailed code →
 * "VERIFY & CONTINUE", then the signed-in hand-off page.  The card header (shield badge,
 * title, subtitle) is shared by all three pages and re-labelled as the book turns.
 *
 * All server traffic runs through ANVIL_AUTH on the shared thread pool; the UI thread never
 * blocks.  ShowModal() returns wxID_OK only after a successful OTP verification (the session
 * token is then already stored); anything else means the user bailed out and the caller
 * should refuse to start the application.
 *
 * Every colour comes from the ANVIL:: theme namespace (anvil_theme.h) and every string is a
 * translatable wxString — no literals hard-coded at the point of use.
 */
class KICOMMON_API DIALOG_ANVIL_LOGIN : public DIALOG_SHIM
{
public:
    /**
     * @param aParent is the usual dialog parent, and is normally nullptr: the gate is a
     *                top-level surface of its own, not a box floating over a window.
     * @param aCoverWindow, when given, is the window whose place on screen this gate takes
     *                (the main frame during a re-login).  The gate then opens on that
     *                window's display, matching its size and maximized state, so swapping
     *                one for the other reads as a single window changing what it shows.
     */
    explicit DIALOG_ANVIL_LOGIN( wxWindow* aParent, wxTopLevelWindow* aCoverWindow = nullptr );
    ~DIALOG_ANVIL_LOGIN() override;

    /**
     * Switch to the "signed in — opening your workspace" page and repaint immediately.
     *
     * Call this after ShowModal() returns wxID_OK and keep the dialog on screen while the
     * main window is built: destroying it first leaves the desktop bare for as long as
     * startup takes, which reads as the app having restarted.
     *
     * Call it only once the dialog is back on screen (Show( true )).  A repaint asked for
     * while the window is hidden is dropped, and the caller then blocks for seconds building
     * the main window with no event loop left to service a later one — which is what leaves
     * an unpainted, garbage-filled cover standing for the whole of startup.
     */
    void ShowOpeningState();

    /**
     * Move the loading rail on the "opening your workspace" page to @a aFraction (0..1) and,
     * when given, re-caption it with the step now under way.
     *
     * Startup owns the UI thread in multi-second blocks, so nothing driven by a timer can be
     * relied on to keep moving: the rail is advanced from the outside instead, as each real
     * step clears, and every call paints synchronously (pumping whatever else is waiting to
     * be drawn) before returning.  The bar therefore stands still exactly as long as the step
     * it names actually takes.
     *
     * A no-op unless the opening page is up, so callers need not track that themselves.
     */
    void SetOpeningProgress( double aFraction, const wxString& aStep = wxEmptyString );

    /**
     * Re-assert the maximized startup geometry after DIALOG_SHIM has applied whatever size
     * and position it remembered for this dialog: the sign-in screen opens as a full page.
     * The caption carries minimize and maximize boxes, so the user can iconize it or restore
     * it down to the windowed size set here.
     */
    bool Show( bool aShow ) override;

private:
    // page construction
    wxWindow* buildEmailPage( wxWindow* aParent );
    wxWindow* buildOtpPage( wxWindow* aParent );
    wxWindow* buildOpeningPage( wxWindow* aParent );

    // actions
    void onSendOtp();
    void onVerifyOtp();
    void onResend();
    void onChangeEmail();



    /// Re-label the shared card header (title + subtitle) for the page being shown.
    void setHeader( const wxString& aTitle, const wxString& aSubtitle );

    void showError( const wxString& aMessage );

    /**
     * Run @a aCall on the thread pool and hand its result back on the UI thread.
     *
     * The task holds no pointer to the dialog: it takes a share of m_async, whose mutex the
     * destructor takes to mark the dialog gone.  A late result is then dropped rather than
     * waited for, so closing the gate mid-request never freezes the window.
     */
    void runAsync( std::function<void( bool&, wxString& )> aCall,
                   std::function<void( bool, const wxString& )> aOnResult );


    /**
     * Recompute the board's page opening from the card's live geometry and repaint the two
     * windows that mill their edge around it.
     *
     * The opening is the card panel's own rectangle in dialog client coordinates, grown by a
     * margin so the card floats in a routed pocket instead of filling it edge to edge.  It
     * only settles once the sizers have run, so this is called after every layout and after
     * every turn of the book — the pages are not all the same height.
     */
    void updateOpening();

    void clearError();
    void setBusy( bool aBusy );
    void startResendCooldown();
    void onResendTick( wxTimerEvent& aEvent );

    static bool isPlausibleEmail( const wxString& aEmail );

    /// Set the minimum and windowed ("restore down") geometry, then show maximized (or, when
    /// covering a window, take over exactly the geometry that window occupies).
    void applyStartupGeometry();

    /// Shared between the dialog and its in-flight pool tasks; see runAsync().
    struct ASYNC_GATE
    {
        std::mutex    mutex;
        bool          alive = true;
        wxEvtHandler* handler = nullptr;
    };

private:
    wxTopLevelWindow*   m_coverWindow;      // window whose place on screen we take, or null

    std::shared_ptr<ASYNC_GATE> m_async;

    /// The routed opening in the board that the card sits in, in dialog client coordinates.
    /// Shared with every window that paints part of it, so the board, the page and the card
    /// all mill the same edge; see updateOpening().
    std::shared_ptr<wxRect> m_opening;

    wxWindow*           m_brandPanel;       // board artwork left of the card
    wxWindow*           m_formPanel;        // board artwork around and behind the card
    wxWindow*           m_cardPanel;        // the card itself; the opening is cut to fit it

    wxSimplebook*       m_book;

    // shared card header
    wxStaticText*       m_headingLabel;
    wxStaticText*       m_subLabel;

    // email page
    wxTextCtrl*         m_emailCtrl;
    ANVIL_LOGIN_BUTTON* m_sendButton;

    // OTP page
    wxTextCtrl*         m_otpCtrl;
    ANVIL_LOGIN_BUTTON* m_verifyButton;
    wxStaticText*       m_resendLink;
    wxStaticText*       m_changeEmailLink;

    wxStaticText*       m_errorLabel;

    // opening ("we are letting you in") page
    ANVIL_LOADING_TRACK* m_loadingTrack;

    wxString            m_email;            // email the OTP was sent to
    wxTimer             m_resendTimer;
    int                 m_resendRemaining;

    std::atomic<bool>   m_busy;             // a network call is in flight
};

#endif // DIALOG_ANVIL_LOGIN_H_
