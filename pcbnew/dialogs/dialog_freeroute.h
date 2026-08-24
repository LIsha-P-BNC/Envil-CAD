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
 */

#ifndef DIALOG_FREEROUTE_H
#define DIALOG_FREEROUTE_H

#include <dialog_shim.h>
#include <wx/msgqueue.h>

class wxProcess;
class wxThread;
class wxGauge;
class wxTextCtrl;
class wxButton;
class wxProcessEvent;
class wxThreadEvent;
class wxCommandEvent;
class wxCloseEvent;

/**
 * Runs the bundled FreeRouting engine on an exported Specctra .dsn and reports
 * when it finishes. The heavy work happens in an external Java process, drained
 * on a helper thread (same proven pattern as the STEP export log dialog) so the
 * GUI stays responsive and the user can cancel. ShowModal() returns wxID_OK
 * only when the router exited 0 and wrote the .ses; the caller then imports it.
 */
class DIALOG_FREEROUTE : public DIALOG_SHIM
{
public:
    enum class STATE_MESSAGE : int
    {
        PROCESS_COMPLETE,   ///< the process-terminate event was received from wx
        REQUEST_EXIT,       ///< ask the thread to exit and kill the process
        SENTINEL            ///< end-of-list dummy
    };

    /**
     * @param aParent      owner window
     * @param aCommand     full command line to launch (java -jar ... -de dsn -do ses ...)
     * @param aSesPath     the .ses the router is asked to write; existence gates success
     */
    DIALOG_FREEROUTE( wxWindow* aParent, const wxString& aCommand, const wxString& aSesPath );
    ~DIALOG_FREEROUTE() override;

private:
    void onProcessTerminate( wxProcessEvent& aEvent );
    void onThreadInput( wxThreadEvent& aEvent );
    void onCancel( wxCommandEvent& aEvent );
    void onClose( wxCloseEvent& aEvent );
    bool TransferDataToWindow() override;

    void stopThreadAndProcess();

    wxGauge*    m_gauge;
    wxTextCtrl* m_log;
    wxButton*   m_cancelBtn;

    wxProcess*                    m_process;
    wxThread*                     m_stdioThread;
    wxMessageQueue<STATE_MESSAGE> m_msgQueue;
    wxString                      m_command;
    wxString                      m_sesPath;
    bool                          m_finished;
};

#endif // DIALOG_FREEROUTE_H
