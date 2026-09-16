/*
* This program source code file is part of Anvil, a free EDA CAD application.
*
* Copyright (C) 2020 Mark Roszko <mark.roszko@gmail.com>
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

#ifndef KIPLATFORM_APP_H_
#define KIPLATFORM_APP_H_

class wxEvent;
class wxString;
class wxWindow;

namespace KIPLATFORM
{
    namespace APP
    {
        /**
         * Perform application-specific initialization tasks. These tasks should be called
         * after the wxApp is constructed (e.g. inside the OnInit method).
         *
         * @return true if init successful, false if unsuccessful
         */
        bool Init();

        void EnableDarkMode( bool aForce );

        /**
         * Switch the OS-level per-app dark mode at RUNTIME, without restarting.
         *
         * wxMSW's own dark mode (MSWEnableDarkMode) can only be established before the first
         * window exists and can never be turned off again, so Anvil enables it unconditionally
         * at start-up and then steers the effective mode through here: the per-app preferred
         * mode (the same uxtheme machinery wx uses) plus a WM_THEMECHANGED sweep over every
         * open window, so native menus, scrollbars, captions and control themes follow the
         * flip immediately.  Pair with ANVIL::SetMode()/KIUI::SyncAnvilTheme(), which repaint
         * the app-drawn chrome.  No-op on non-Windows platforms.
         */
        void SetLiveDarkMode( bool aDark );

        /**
         * Anvil "Vibrant Purple & Indigo" theme: when enabled, the MSW dark-mode palette
         * (window/panel backgrounds, text, highlight, the native menu bar and title bar) is
         * tinted dark purple instead of the default grey.  Must be called BEFORE EnableDarkMode()
         * so the colours are in place when the native controls are created.  Only affects the
         * window chrome — the drawing canvas is painted from the colour theme, not these colours.
         */
        void SetDarkModePurple( bool aOn );

        /**
         * App-wide event filter hook; call from wxApp::FilterEvent() BEFORE any other handling.
         *
         * On Windows this guards the live-light theme against wx's always-on dark-mode paint
         * paths (see the implementation for the wxSpinButton story: its dark-mode OnPaint
         * both inverts the natively light-rendered pixels and trips a wx assert from inside
         * WM_PAINT, which recurses into an application crash).  No-op elsewhere.
         *
         * @return wxApp::FilterEvent semantics: -1 to continue normal processing, 0/1 when the
         *         event was fully handled here.
         */
        int LiveThemeEventFilter( wxEvent& aEvent );

        /**
         * Tries to attach a console window with stdout, stderr and stdin.
         *
         * @param aTryAlloc try to allocate the console if cannot attach to it.
         * @return true if attach successful, false if unsuccessful
         */
        bool AttachConsole( bool aTryAlloc );

        /**
         * Checks if the Operating System is explicitly unsupported and we want to prevent
         * users from sending bug reports and show them a disclaimer on startup.
         *
         * @return true if unsupported
         */
        bool IsOperatingSystemUnsupported();

        /**
         * Registers the application for restart with the OS with the given command line string to pass as args
         *
         * @param aCommandLine is string the OS will invoke the application with
         */
        bool RegisterApplicationRestart( const wxString& aCommandLine );

        /**
         * Unregisters the application from automatic restart
         *
         * Depending on OS, this may not be required
         */
        bool UnregisterApplicationRestart();

        /**
         * Whether or not the window supports setting a shutdown block reason
         */
        bool SupportsShutdownBlockReason();

        /**
         * Sets the block reason why the window/application is preventing OS shutdown.
         * This should be set far ahead of any close event.
         *
         * This is mainly intended for Windows platforms where this is a native feature.
         *
         * @param aWindow that will have a shutdown blocker message
         * @param aReason to display why the shutdown block is occurring
         */
        void SetShutdownBlockReason( wxWindow* aWindow, const wxString& aReason );

        /**
         * Removes any shutdown block reason set
         *
         * @param aWindow that has a shutdown block reason set
         */
        void RemoveShutdownBlockReason( wxWindow* aWindow );

        /**
         * Forces wxTimers to fire more promptly on Win32.
         *
         * wxTimers on win32 are not real timers
         * They live in the message pump at the absolute lowest priority (only when no other events are pending)
         * This functions "peeks" the message pump which causes them to get queued immediately
         *
         * Call as needed in an application to ensure timers are dispatched
         */
        void ForceTimerMessagesToBeCreatedIfNecessary();

        /**
         * Inserts a search path for loading dynamic libraries.  The exact place this new path ends
         * up in the dynamic library search order is platform-dependent, but generally this can be
         * used to make sure dynamic libraries are found in non-standard runtime situations.
         *
         * @param aPath is the full path to insert
         */
        void AddDynamicLibrarySearchPath( const wxString& aPath );
    }
}

#endif // KIPLATFORM_UI_H_
