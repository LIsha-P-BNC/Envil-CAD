/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Stream-draining thread pattern adapted from dialog_export_step_process.cpp.
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

#include "dialog_freeroute.h"

#include <wx/sizer.h>
#include <wx/gauge.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/process.h>
#include <wx/thread.h>
#include <wx/filename.h>

wxDEFINE_EVENT( wxEVT_FR_STDIN, wxThreadEvent );
wxDEFINE_EVENT( wxEVT_FR_STDERR, wxThreadEvent );


/**
 * Drains the launched process's stdout/stderr on a joinable thread and posts the
 * text to the dialog on the main thread; also detects completion via the queue.
 */
class FR_STDSTREAM_THREAD : public wxThread
{
public:
    FR_STDSTREAM_THREAD( wxEvtHandler* aHandler, wxProcess* aProcess,
                         wxMessageQueue<DIALOG_FREEROUTE::STATE_MESSAGE>& aQueue ) :
            wxThread( wxTHREAD_JOINABLE ),
            m_queue( aQueue )
    {
        m_process = aProcess;
        m_handler = aHandler;
        m_bufferSize = 256 * 1024;
        m_buffer = new char[m_bufferSize];
    }

    ~FR_STDSTREAM_THREAD() override { delete[] m_buffer; }

private:
    ExitCode Entry() override;
    void     drainInput();

    wxMessageQueue<DIALOG_FREEROUTE::STATE_MESSAGE>& m_queue;
    wxEvtHandler* m_handler;
    wxProcess*    m_process;
    char*         m_buffer;
    size_t        m_bufferSize;
};


wxThread::ExitCode FR_STDSTREAM_THREAD::Entry()
{
    ExitCode c = reinterpret_cast<ExitCode>( 0 );

    while( true )
    {
        if( TestDestroy() )
        {
            wxProcess::Kill( m_process->GetPid(), wxSIGKILL );
            c = reinterpret_cast<ExitCode>( 1 );
            break;
        }

        DIALOG_FREEROUTE::STATE_MESSAGE m = DIALOG_FREEROUTE::STATE_MESSAGE::SENTINEL;
        wxMessageQueueError e = m_queue.ReceiveTimeout( 10, m );

        if( e == wxMSGQUEUE_NO_ERROR )
        {
            if( m == DIALOG_FREEROUTE::STATE_MESSAGE::PROCESS_COMPLETE )
            {
                drainInput();
                c = reinterpret_cast<ExitCode>( 0 );
                break;
            }
            else if( m == DIALOG_FREEROUTE::STATE_MESSAGE::REQUEST_EXIT )
            {
                wxProcess::Kill( m_process->GetPid(), wxSIGKILL );
                c = reinterpret_cast<ExitCode>( 1 );
                break;
            }
        }
        else if( e == wxMSGQUEUE_TIMEOUT )
        {
            drainInput();
        }
    }

    return c;
}


void FR_STDSTREAM_THREAD::drainInput()
{
    if( !m_process->IsInputOpened() )
        return;

    wxString       fromOut, fromErr;
    wxInputStream* stream;

    while( m_process->IsInputAvailable() )
    {
        stream = m_process->GetInputStream();
        stream->Read( m_buffer, m_bufferSize );
        fromOut << wxString( m_buffer, stream->LastRead() );
    }

    while( m_process->IsErrorAvailable() )
    {
        stream = m_process->GetErrorStream();
        stream->Read( m_buffer, m_bufferSize );
        fromErr << wxString( m_buffer, stream->LastRead() );
    }

    if( !fromOut.IsEmpty() )
    {
        wxThreadEvent* ev = new wxThreadEvent( wxEVT_FR_STDIN );
        ev->SetString( fromOut );
        m_handler->QueueEvent( ev );
    }

    if( !fromErr.IsEmpty() )
    {
        wxThreadEvent* ev = new wxThreadEvent( wxEVT_FR_STDERR );
        ev->SetString( fromErr );
        m_handler->QueueEvent( ev );
    }
}


DIALOG_FREEROUTE::DIALOG_FREEROUTE( wxWindow* aParent, const wxString& aCommand,
                                    const wxString& aSesPath ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Autoroute (FreeRouting)" ), wxDefaultPosition,
                     wxSize( 620, 420 ), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_gauge( nullptr ),
        m_log( nullptr ),
        m_cancelBtn( nullptr ),
        m_process( nullptr ),
        m_stdioThread( nullptr ),
        m_command( aCommand ),
        m_sesPath( aSesPath ),
        m_finished( false )
{
    wxBoxSizer* top = new wxBoxSizer( wxVERTICAL );

    wxStaticText* label = new wxStaticText( this, wxID_ANY,
            _( "Routing the board with FreeRouting. Large boards can take several "
               "minutes; you can cancel at any time." ) );
    label->Wrap( 580 );
    top->Add( label, 0, wxALL | wxEXPAND, 10 );

    m_gauge = new wxGauge( this, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize,
                           wxGA_HORIZONTAL );
    top->Add( m_gauge, 0, wxLEFT | wxRIGHT | wxEXPAND, 10 );

    m_log = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                            wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP );
    top->Add( m_log, 1, wxALL | wxEXPAND, 10 );

    m_cancelBtn = new wxButton( this, wxID_CANCEL, _( "Cancel" ) );
    top->Add( m_cancelBtn, 0, wxALIGN_RIGHT | wxALL, 10 );

    SetSizer( top );
    Layout();

    Bind( wxEVT_END_PROCESS, &DIALOG_FREEROUTE::onProcessTerminate, this );
    Bind( wxEVT_FR_STDIN, &DIALOG_FREEROUTE::onThreadInput, this );
    Bind( wxEVT_FR_STDERR, &DIALOG_FREEROUTE::onThreadInput, this );
    Bind( wxEVT_CLOSE_WINDOW, &DIALOG_FREEROUTE::onClose, this );
    m_cancelBtn->Bind( wxEVT_BUTTON, &DIALOG_FREEROUTE::onCancel, this );

    finishDialogSettings();
}


DIALOG_FREEROUTE::~DIALOG_FREEROUTE()
{
    delete m_stdioThread;
    delete m_process;
}


bool DIALOG_FREEROUTE::TransferDataToWindow()
{
    m_log->AppendText( _( "Command line:" ) + wxT( "\n" ) + m_command + wxT( "\n\n" ) );

    m_process = new wxProcess( this );
    m_process->Redirect();

    m_stdioThread = new FR_STDSTREAM_THREAD( this, m_process, m_msgQueue );
    m_stdioThread->Run();

    if( !m_stdioThread->IsRunning() )
    {
        m_log->AppendText( _( "Unable to start the output reader thread." ) + wxT( "\n" ) );
        delete m_stdioThread;
        m_stdioThread = nullptr;
        return true;
    }

    m_gauge->Pulse();

    long pid = wxExecute( m_command, wxEXEC_ASYNC, m_process );

    if( pid == 0 )
    {
        m_log->AppendText( _( "Failed to launch the FreeRouting process." ) + wxT( "\n" ) );
        stopThreadAndProcess();
        m_finished = true;
        // leave the dialog open so the user can read the error; Cancel closes it
    }

    return true;
}


void DIALOG_FREEROUTE::onThreadInput( wxThreadEvent& aEvent )
{
    m_log->AppendText( aEvent.GetString() );
    m_gauge->Pulse();
}


void DIALOG_FREEROUTE::stopThreadAndProcess()
{
    if( m_stdioThread && m_stdioThread->IsRunning() )
    {
        m_msgQueue.Post( STATE_MESSAGE::REQUEST_EXIT );
        m_stdioThread->Wait();
    }

    delete m_stdioThread;
    m_stdioThread = nullptr;
}


void DIALOG_FREEROUTE::onProcessTerminate( wxProcessEvent& aEvent )
{
    if( m_stdioThread && m_stdioThread->IsRunning() )
    {
        m_msgQueue.Post( STATE_MESSAGE::PROCESS_COMPLETE );
        m_stdioThread->Wait();
        delete m_stdioThread;
        m_stdioThread = nullptr;
    }

    m_finished = true;
    m_gauge->SetRange( 1 );

    int  exitCode = aEvent.GetExitCode();
    bool sesOk = !m_sesPath.IsEmpty() && wxFileName::FileExists( m_sesPath );

    if( exitCode == 0 && sesOk )
    {
        m_gauge->SetValue( 1 );
        EndModal( wxID_OK );
    }
    else
    {
        m_gauge->SetValue( 0 );
        m_log->AppendText( wxT( "\n*** " )
                           + wxString::Format( _( "FreeRouting did not complete (exit code %d)." ),
                                               exitCode )
                           + wxT( " ***\n" ) );

        if( !sesOk )
            m_log->AppendText( _( "No routed session (.ses) file was produced." ) + wxT( "\n" ) );

        m_cancelBtn->SetLabel( _( "Close" ) );
    }
}


void DIALOG_FREEROUTE::onCancel( wxCommandEvent& aEvent )
{
    // After completion the button is a plain Close; before, it kills the router.
    if( !m_finished )
        stopThreadAndProcess();

    EndModal( wxID_CANCEL );
}


void DIALOG_FREEROUTE::onClose( wxCloseEvent& aEvent )
{
    if( !m_finished )
        stopThreadAndProcess();

    aEvent.Skip();
}
