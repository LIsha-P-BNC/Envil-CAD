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
#include <wx/timer.h>

class wxSimplebook;
class wxStaticText;
class wxTextCtrl;
class ANVIL_LOGIN_BUTTON;


/**
 * The Anvil CAD sign-in gate — email + OTP login.
 *
 * A circuit-board brand panel on the left and a light page carrying a single white sign-in
 * card on the right.  Three steps in one card via a wxSimplebook: enter email → "SIGN IN
 * WITH EMAIL OTP", then enter the emailed code → "VERIFY & CONTINUE", then the signed-in
 * hand-off page.  The card header (shield badge, title, subtitle) is shared by all three
 * pages and re-labelled as the book turns.
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
    explicit DIALOG_ANVIL_LOGIN( wxWindow* aParent );
    ~DIALOG_ANVIL_LOGIN() override;

    /**
     * Switch to the "signed in — opening your workspace" page and repaint immediately.
     *
     * Call this after ShowModal() returns wxID_OK and keep the dialog on screen while the
     * main window is built: destroying it first leaves the desktop bare for as long as
     * startup takes, which reads as the app having restarted.
     */
    void ShowOpeningState();

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


    void clearError();
    void setBusy( bool aBusy );
    void startResendCooldown();
    void onResendTick( wxTimerEvent& aEvent );

    static bool isPlausibleEmail( const wxString& aEmail );

    /// Set the minimum and windowed ("restore down") geometry, then show maximized.
    void applyStartupGeometry();

private:
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

    wxString            m_email;            // email the OTP was sent to
    wxTimer             m_resendTimer;
    int                 m_resendRemaining;

    std::atomic<bool>   m_busy;             // a network call is in flight
    std::atomic<bool>   m_closed;           // dialog is going away; drop late results
};

#endif // DIALOG_ANVIL_LOGIN_H_
