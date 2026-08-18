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
class ANVIL_NOTCH_BUTTON;


/**
 * The Anvil sign-in gate — NEMI-Suite-styled email + OTP login.
 *
 * Layout mirrors the NEMI Suite web sign-in: an emerald gradient brand panel on the left
 * and a light, blueprint-gridded form surface on the right.  Two steps in one dialog via a
 * wxSimplebook: enter email → "SEND OTP", then enter the emailed code → "VERIFY".
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

private:
    // page construction
    wxWindow* buildEmailPage( wxWindow* aParent );
    wxWindow* buildOtpPage( wxWindow* aParent );

    // actions
    void onSendOtp();
    void onVerifyOtp();
    void onResend();
    void onChangeEmail();
    void onServerSettings();

    void showError( const wxString& aMessage );
    void clearError();
    void setBusy( bool aBusy );
    void startResendCooldown();
    void onResendTick( wxTimerEvent& aEvent );

    static bool isPlausibleEmail( const wxString& aEmail );

private:
    wxSimplebook*       m_book;

    // email page
    wxTextCtrl*         m_emailCtrl;
    ANVIL_NOTCH_BUTTON* m_sendButton;

    // OTP page
    wxStaticText*       m_otpInfo;
    wxTextCtrl*         m_otpCtrl;
    ANVIL_NOTCH_BUTTON* m_verifyButton;
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
