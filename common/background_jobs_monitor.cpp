/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright (C) 2023 Mark Roszko <mark.roszko@gmail.com>
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

#include <unordered_map>

#include <wx/gauge.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/string.h>

#include <background_jobs_monitor.h>
#include <widgets/kistatusbar.h>


class BACKGROUND_JOB_PANEL : public wxPanel
{
public:
    BACKGROUND_JOB_PANEL( wxWindow* aParent, std::shared_ptr<BACKGROUND_JOB> aJob ) :
            wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxSize( -1, 75 ),
                     wxBORDER_SIMPLE ),
            m_job( aJob )
    {
        SetSizeHints( wxDefaultSize, wxDefaultSize );

        wxBoxSizer* mainSizer;
        mainSizer = new wxBoxSizer( wxVERTICAL );

        SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_3DLIGHT ) );

        m_stName = new wxStaticText( this, wxID_ANY, aJob->m_name );
        m_stName->Wrap( -1 );
        m_stName->SetFont( wxFont( wxNORMAL_FONT->GetPointSize(), wxFONTFAMILY_DEFAULT,
                                   wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false,
                                   wxEmptyString ) );
        mainSizer->Add( m_stName, 0, wxALL | wxEXPAND, 1 );

        m_stStatus = new wxStaticText( this, wxID_ANY, aJob->m_status, wxDefaultPosition,
                                       wxDefaultSize, 0 );
        m_stStatus->Wrap( -1 );
        mainSizer->Add( m_stStatus, 0, wxALL | wxEXPAND, 1 );

        m_progress = new wxGauge( this, wxID_ANY, aJob->m_maxProgress, wxDefaultPosition,
                                  wxDefaultSize, wxGA_HORIZONTAL );
        m_progress->SetValue( 0 );
        mainSizer->Add( m_progress, 0, wxALL | wxEXPAND, 1 );

        SetSizer( mainSizer );
        Layout();

        UpdateFromJob();
    }


    void UpdateFromJob()
    {
        m_stStatus->SetLabelText( m_job->m_status );
        m_progress->SetValue( m_job->m_currentProgress );
        m_progress->SetRange( m_job->m_maxProgress );
    }

private:
    wxGauge* m_progress;
    wxStaticText* m_stName;
    wxStaticText* m_stStatus;
    std::shared_ptr<BACKGROUND_JOB> m_job;
};


class BACKGROUND_JOB_LIST : public wxFrame
{
public:
    BACKGROUND_JOB_LIST( wxWindow* parent, const wxPoint& pos ) :
            wxFrame( parent, wxID_ANY, _( "Background Jobs" ), pos, wxSize( 300, 150 ),
                     wxFRAME_NO_TASKBAR | wxBORDER_SIMPLE )
    {
        SetSizeHints( wxDefaultSize, wxDefaultSize );

	    wxBoxSizer* bSizer1;
        bSizer1 = new wxBoxSizer( wxVERTICAL );

        m_scrolledWindow = new wxScrolledWindow( this, wxID_ANY, wxDefaultPosition,
                                                 wxSize( -1, -1 ), wxVSCROLL );
        m_scrolledWindow->SetScrollRate( 5, 5 );
        m_contentSizer = new wxBoxSizer( wxVERTICAL );

        m_scrolledWindow->SetSizer( m_contentSizer );
        m_scrolledWindow->Layout();
        m_contentSizer->Fit( m_scrolledWindow );
        bSizer1->Add( m_scrolledWindow, 1, wxEXPAND | wxALL, 0 );

        Bind( wxEVT_KILL_FOCUS, &BACKGROUND_JOB_LIST::onFocusLoss, this );

	    SetSizer( bSizer1 );
        Layout();

        SetFocus();
    }

    void onFocusLoss( wxFocusEvent& aEvent )
    {
        Close( true );
        aEvent.Skip();
    }


    void Add( std::shared_ptr<BACKGROUND_JOB> aJob )
    {
        BACKGROUND_JOB_PANEL* panel = new BACKGROUND_JOB_PANEL( m_scrolledWindow, aJob );
        m_contentSizer->Add( panel, 0, wxEXPAND | wxALL, 2 );
        m_scrolledWindow->Layout();
        m_contentSizer->Fit( m_scrolledWindow );

        // call this at this window otherwise the child panels don't resize width properly
        Layout();

        m_jobPanels[aJob] = panel;
    }


    void Remove( std::shared_ptr<BACKGROUND_JOB> aJob )
    {
        auto it = m_jobPanels.find( aJob );
        if( it != m_jobPanels.end() )
        {
            BACKGROUND_JOB_PANEL* panel = m_jobPanels[aJob];
            m_contentSizer->Detach( panel );
            panel->Destroy();

            m_jobPanels.erase( it );
        }
    }

    void UpdateJob( std::shared_ptr<BACKGROUND_JOB> aJob )
    {
        auto it = m_jobPanels.find( aJob );
        if( it != m_jobPanels.end() )
        {
            BACKGROUND_JOB_PANEL* panel = m_jobPanels[aJob];
            panel->UpdateFromJob();
        }
    }

private:
    wxScrolledWindow* m_scrolledWindow;
    wxBoxSizer*       m_contentSizer;
    std::unordered_map<std::shared_ptr<BACKGROUND_JOB>, BACKGROUND_JOB_PANEL*> m_jobPanels;
};


BACKGROUND_JOB_REPORTER::BACKGROUND_JOB_REPORTER( BACKGROUND_JOBS_MONITOR* aMonitor,
                                                  const std::shared_ptr<BACKGROUND_JOB>& aJob ) :
        PROGRESS_REPORTER_BASE( 1 ),
        m_monitor( aMonitor ),
        m_job( aJob )
{

}


bool BACKGROUND_JOB_REPORTER::updateUI()
{
    return !m_cancelled;
}


void BACKGROUND_JOB_REPORTER::Report( const wxString& aMessage )
{
    m_job->m_status = aMessage;
    m_monitor->jobUpdated( m_job );
}


void BACKGROUND_JOB_REPORTER::SetNumPhases( int aNumPhases )
{
    PROGRESS_REPORTER_BASE::SetNumPhases( aNumPhases );
    m_job->m_maxProgress.store( m_numPhases.load() );
    m_monitor->jobUpdated( m_job );
}


void BACKGROUND_JOB_REPORTER::AdvancePhase()
{
    PROGRESS_REPORTER_BASE::AdvancePhase();
    m_job->m_currentProgress.store( m_phase.load() );
    m_monitor->jobUpdated( m_job );
}


void BACKGROUND_JOB_REPORTER::SetCurrentProgress( double aProgress )
{
    PROGRESS_REPORTER_BASE::SetCurrentProgress( aProgress );
    m_job->m_maxProgress.store( 1000 );
    m_job->m_currentProgress.store( static_cast<int>( 1000 * aProgress ) );
    m_monitor->jobUpdated( m_job );
}


BACKGROUND_JOBS_MONITOR::BACKGROUND_JOBS_MONITOR()
{

}


std::shared_ptr<BACKGROUND_JOB> BACKGROUND_JOBS_MONITOR::Create( const wxString& aName )
{
    std::shared_ptr<BACKGROUND_JOB> job = std::make_shared<BACKGROUND_JOB>();

    job->m_name = aName;
    job->m_reporter = std::make_shared<BACKGROUND_JOB_REPORTER>( this, job );

    std::vector<BACKGROUND_JOB_LIST*> shownDialogs;

    {
        std::lock_guard<std::shared_mutex> lock( m_mutex );
        m_jobs.push_back( job );
        shownDialogs = m_shownDialogs;
    }

    // Issue the wx CallAfter() calls OUTSIDE the lock.  CallAfter() enters the wx event
    // system (which has its own locks); holding m_mutex across it can deadlock against the
    // exclusive lock taken by RegisterStatusBar()/UnregisterStatusBar() when an editor
    // frame is created/destroyed on the UI thread while worker threads report job progress.
    for( BACKGROUND_JOB_LIST* list : shownDialogs )
    {
        list->CallAfter(
                [=]()
                {
                    list->Add( job );
                } );
    }

    return job;
}


void BACKGROUND_JOBS_MONITOR::Remove( std::shared_ptr<BACKGROUND_JOB> aJob )
{
    std::vector<BACKGROUND_JOB_LIST*> shownDialogs;
    std::vector<KISTATUSBAR*>         statusBars;
    std::shared_ptr<BACKGROUND_JOB>   frontJob;

    {
        std::lock_guard<std::shared_mutex> lock( m_mutex );

        m_jobs.erase( std::remove_if( m_jobs.begin(), m_jobs.end(),
                                      [&]( std::shared_ptr<BACKGROUND_JOB> job )
                                      {
                                          return job == aJob;
                                      } ),
                      m_jobs.end() );

        shownDialogs = m_shownDialogs;
        statusBars   = m_statusBars;

        if( !m_jobs.empty() )
            frontJob = m_jobs.front();
    }

    // wx CallAfter() / jobUpdated() run OUTSIDE the lock (see Create()): never hold
    // m_mutex while calling into the wx event system.
    for( BACKGROUND_JOB_LIST* list : shownDialogs )
    {
        list->CallAfter(
                [=]()
                {
                    list->Remove( aJob );
                } );
    }

    if( frontJob )
    {
        jobUpdated( frontJob );
    }
    else
    {
        for( KISTATUSBAR* statusBar : statusBars )
        {
            statusBar->CallAfter(
                    [=]()
                    {
                        statusBar->HideBackgroundProgressBar();
                        statusBar->SetBackgroundStatusText( wxT( "" ) );
                    } );
        }
    }
}


void BACKGROUND_JOBS_MONITOR::onListWindowClosed( wxCloseEvent& aEvent )
{
    BACKGROUND_JOB_LIST* evtWindow = dynamic_cast<BACKGROUND_JOB_LIST*>( aEvent.GetEventObject() );

    {
        std::lock_guard<std::shared_mutex> lock( m_mutex );
        m_shownDialogs.erase( std::remove_if( m_shownDialogs.begin(), m_shownDialogs.end(),
                                              [&]( BACKGROUND_JOB_LIST* dialog )
                                              {
                                                  return dialog == evtWindow;
                                              } ),
                              m_shownDialogs.end() );
    }

    aEvent.Skip();
}


void BACKGROUND_JOBS_MONITOR::ShowList( wxWindow* aParent, wxPoint aPos )
{
    BACKGROUND_JOB_LIST* list = new BACKGROUND_JOB_LIST( aParent, aPos );

    std::vector<std::shared_ptr<BACKGROUND_JOB>> jobs;

    {
        std::lock_guard<std::shared_mutex> lock( m_mutex );
        jobs = m_jobs;
        m_shownDialogs.push_back( list );
    }

    // Populate the list OUTSIDE the lock (list->Add() creates wx widgets).
    for( const std::shared_ptr<BACKGROUND_JOB>& job : jobs )
        list->Add( job );

    list->Bind( wxEVT_CLOSE_WINDOW, &BACKGROUND_JOBS_MONITOR::onListWindowClosed, this );

    // correct the position
    wxSize windowSize = list->GetSize();
    list->SetPosition( aPos - windowSize );

    list->Show();
}


void BACKGROUND_JOBS_MONITOR::jobUpdated( std::shared_ptr<BACKGROUND_JOB> aJob )
{
    // This method is called from the reporters from potentially other threads.
    // Snapshot the data we need under the lock, then RELEASE the lock before issuing any
    // CallAfter(): holding m_mutex across a wx event-system call can deadlock against the
    // exclusive lock taken by RegisterStatusBar() when a new editor frame is opened on the
    // UI thread.  A plain (blocking) shared_lock is safe here because no caller enters this
    // method while already holding m_mutex (Remove()/RegisterStatusBar() call jobUpdated()
    // only after their own lock scope has closed), so there is no reentrancy on the
    // non-recursive shared_mutex.
    bool                              isFrontJob = false;
    std::vector<KISTATUSBAR*>         statusBars;
    std::vector<BACKGROUND_JOB_LIST*> shownDialogs;

    {
        std::shared_lock<std::shared_mutex> lock( m_mutex );

        // for now, we go and update the status bar if it's the first job in the vector
        if( !m_jobs.empty() && m_jobs.front() == aJob )
            isFrontJob = true;

        statusBars   = m_statusBars;
        shownDialogs = m_shownDialogs;
    }

    // we have to guard ui calls with CallAfter (and must do so without holding m_mutex)
    if( isFrontJob )
    {
        // update all status bar entries
        for( KISTATUSBAR* statusBar : statusBars )
        {
            statusBar->CallAfter(
                    [=]()
                    {
                        statusBar->ShowBackgroundProgressBar();
                        statusBar->SetBackgroundProgressMax( aJob->m_maxProgress );
                        statusBar->SetBackgroundProgress( aJob->m_currentProgress );
                        statusBar->SetBackgroundStatusText( aJob->m_status );
                    } );
        }
    }

    for( BACKGROUND_JOB_LIST* list : shownDialogs )
    {
        list->CallAfter(
                [=]()
                {
                    list->UpdateJob( aJob );
                } );
    }
}


void BACKGROUND_JOBS_MONITOR::RegisterStatusBar( KISTATUSBAR* aStatusBar )
{
    std::shared_ptr<BACKGROUND_JOB> frontJob;

    {
        std::lock_guard<std::shared_mutex> lock( m_mutex );
        m_statusBars.push_back( aStatusBar );

        // Capture front job while holding lock
        if( !m_jobs.empty() )
            frontJob = m_jobs.front();
    }

    // Make sure the newly-registered bar gets the active job, if any
    if( frontJob )
        jobUpdated( frontJob );
}


void BACKGROUND_JOBS_MONITOR::UnregisterStatusBar( KISTATUSBAR* aStatusBar )
{
    std::lock_guard<std::shared_mutex> lock( m_mutex );
    m_statusBars.erase( std::remove_if( m_statusBars.begin(), m_statusBars.end(),
                                        [&]( KISTATUSBAR* statusBar )
                                        {
                                            return statusBar == aStatusBar;
                                        } ),
                        m_statusBars.end() );
}
