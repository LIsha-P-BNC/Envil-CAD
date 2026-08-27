/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright (C) 2020 Ian McInerney <Ian.S.McInerney at ieee.org>
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

#ifndef KIPLATFORM_UI_H_
#define KIPLATFORM_UI_H_

#include <wx/cursor.h>

class wxChoice;
class wxDialog;
class wxNonOwnedWindow;
class wxTopLevelWindow;
class wxWindow;

namespace KIPLATFORM
{
    namespace UI
    {
        /**
         * Determine if the desktop interface is currently using a dark theme or a light theme.
         *
         * @return true if a dark theme is being used.
         */
        bool IsDarkTheme();

        wxColour GetDialogBGColour();

        /**
         * Pass the current focus to the window. On OSX this will forcefully give the focus to
         * the desired window, while on MSW and GTK it will simply call the wxWidgets SetFocus()
         * function.
         *
         * @param aWindow is the window to pass focus to
         */
        void ForceFocus( wxWindow* aWindow );

        /**
         * Check to see if the given window is the currently active window (e.g. the window
         * in the foreground the user is interacting with).
         *
         * @param aWindow is the window to check
         */
        bool IsWindowActive( wxWindow* aWindow );

        /**
         * Move a window's parent to be the top-level window and force the window to be on top.
         *
         * This only has an affect for OSX, it is a NOP for GTK and MSW.
         *
         * Apple in its infinite wisdom will raise a disabled window before even passing
         * us the event, so we have no way to stop it.  Instead, we must set an order on
         * the windows so that the quasi-modal will be pushed in front of the disabled
         * window when it is raised.
         *
         * @param aWindow is the window to reparent
         */
        void ReparentModal( wxNonOwnedWindow* aWindow );

        void ReparentWindow( wxNonOwnedWindow* aWindow, wxTopLevelWindow* aParent );

        /*
         * An ugly hack to fix an issue on OSX: cmd+c closes the dialog instead of copying the
         * text if a button with wxID_CANCEL is used in a wxStdDialogButtonSizer created by
         * wxFormBuilder: the label is &Cancel, and this accelerator key has priority over the
         * standard copy accelerator.
         * Note: problem also exists in other languages; for instance cmd+a closes dialogs in
         * German because the button is &Abbrechen.
         */
        void FixupCancelButtonCmdKeyCollision( wxWindow* aWindow );

        /**
         * Checks if we designated a stock cursor for this OS as "OK" or else we may need to load a custom one
         *
         * @param aCursor is wxStockCursor we want to see if its acceptable
         */
        bool IsStockCursorOk( wxStockCursor aCursor );

        /**
         * Configure a wxChoice control to support a lot of entries by disabling functionality that makes
         * adding new items become very expensive.
         *
         * @param aChoice is the choice box to modify
         */
        void LargeChoiceBoxHack( wxChoice* aChoice );

        /**
         * Configure a wxChoice control to ellipsize the shown text in the button with the ellipses
         * placed at the end of the string.
         *
         * @param aChoice is the choice box to ellipsize
         */
        void EllipsizeChoiceBox( wxChoice* aChoice );

        /**
         * Tries to determine the pixel scaling factor currently in use for the window.  Under wx3.0, GTK
         * fails to properly detect the scale factor.
         * @param aWindow pointer to the window to check
         * @return Pixel scale factor in use, defaulting to the wxWidgets method
         */
        double GetPixelScaleFactor( const wxWindow* aWindow );

        /**
         * Tries to determine the content scaling factor currently in use for the window.
         * The content scaling factor is typically settable by the user and may differ from the
         * pixel scaling factor.
         */
        double GetContentScaleFactor( const wxWindow* aWindow );

        /**
         * Return the background and foreground colors for info bars in the current scheme
         */
        void GetInfoBarColours( wxColour& aFGColour, wxColour& aBGColour );

        /**
         * Tries to determine the size of the viewport of a scrollable widget (wxDataViewCtrl, wxGrid)
         * that won't be obscured by scrollbars.
         * @param aWindow pointer to the scrollable widget to check
         * @return Viewport size that won't be obscured by scrollbars
         */
        wxSize GetUnobscuredSize( const wxWindow* aWindow );

        /**
         * Used to set overlay/non-overlay scrolling mode in a window.
         * Implemented only on GTK.
         */
        void SetOverlayScrolling( const wxWindow* aWindow, bool overlay );

        /**
         * If the user has disabled icons system-wide, we check that here
         */
        bool AllowIconsInMenus();

        /**
         * Returns the mouse position in screen coordinates.
         * If we've just warped the cursor, returns the new coordinates.
         */
        wxPoint GetMousePosition();

        /**
         * Move the mouse cursor to a specific position relative to the window
         * @param aWindow Window in which to position to mouse cursor
         * @param aX destination x position
         * @param aY destination y position
         * @return true if the warp was successful
         */
        bool WarpPointer( wxWindow* aWindow, int aX, int aY );

        /**
         * Configures the IME mode of a given control handle
         */
        void ImmControl( wxWindow* aWindow, bool aEnable );

        /**
         * Asks the IME to cancel
         */
        void ImeNotifyCancelComposition( wxWindow* aWindow );

        /**
         * On Wayland, restricts the pointer movement to a rectangle slightly bigger than the given `wxWindow`.
         * This way, the cursor doesn't exit the (bigger) application window and we retain control on it.
         * Required to make the infinite mouse-drag work with fast movement.
         * See https://gitlab.com/kicad/code/kicad/-/issues/7207#note_1562089503
         * @param aWindow Window in which to position to mouse cursor
         * @return true if infinite panning is supported
         */
        bool InfiniteDragPrepareWindow( wxWindow* aWindow );

        /**
         * On Wayland, allows the cursor to freely move again after a drag (see `InfiniteDragPrepareWindow`).
         */
        void InfiniteDragReleaseWindow();

        /**
         * Ensure that a window is visible on the screen.  On MacOS, this will make it visible
         * in all Spaces.  Other platforms are nops.
         *
         * @param aWindow window to make visible
         */
        void EnsureVisible( wxWindow* aWindow );

        /**
         * Intended to set the floating window level in macOS on a window
         */
        void SetFloatLevel( wxWindow* aWindow );

        /**
         * Release a modal window's parent-child relationship with its parent window.
         * This only has an effect on macOS, it is a NOP for GTK and MSW.
         *
         * On macOS, modal dialogs are attached as child windows using addChildWindow,
         * which causes them to disappear when dragged to a different monitor. This
         * function removes that child relationship and sets the window to a floating
         * level, allowing it to be freely moved across monitors while still staying
         * above other windows.
         */
        void ReleaseChildWindow( wxNonOwnedWindow* aWindow );

        /**
         * Configure a file dialog to show network and virtual file systems.
         *
         * On GTK, file dialogs default to showing only local files, which excludes
         * GVFS-mounted filesystems like Google Drive, SMB shares, SFTP connections,
         * and removable media mounted through GVFS. This function configures the
         * dialog to also show these non-local filesystems.
         *
         * This function must be called after creating the dialog but before calling
         * ShowModal().
         *
         * This is a NOP on Windows and macOS where network filesystems are shown by default.
         *
         * @param aDialog is the file dialog to configure
         */
        void AllowNetworkFileSystems( wxDialog* aDialog );

        /**
         * Give a borderless popup window the soft drop shadow the OS gives native menus, so a
         * flat custom popup (dropdown / flyout) reads as floating above the window instead of
         * painted onto it.
         *
         * On Windows this enables the CS_DROPSHADOW window-class style.  On GTK and macOS the
         * window manager already shadows popups, so this is a NOP.
         *
         * @param aWindow is the (already-created) popup window to shadow
         */
        void AddDropShadow( wxWindow* aWindow );

        /**
         * Overdraw the border a NATIVE control paints for itself (combo box / choice, text or
         * search field, ...) with a flat 1px frame in @a aEdgeColour.
         *
         * In dark mode the borders Windows draws for these controls are a glaring light grey
         * (or even a white 3D sunken edge on unthemed fields), which breaks the flat dark
         * chrome of the Anvil mockups — wx offers no API to recolour them since they are
         * painted by the OS control / uxtheme, not by wx.
         *
         * The control's window procedure is subclassed; after every WM_PAINT / WM_NCPAINT the
         * outer pixel ring is repainted in @a aEdgeColour and any remaining native border rings
         * (e.g. the 2px WS_EX_CLIENTEDGE) in @a aInnerColour, so every state repaint (hover,
         * focus, theme change) stays flat.  Calling it again on the same control just updates
         * the colours.  This is a NOP on GTK and macOS.
         *
         * @param aWindow is the native control whose border to flatten
         * @param aEdgeColour is the flat border colour (typically ANVIL::CONTROL_EDGE)
         * @param aInnerColour fills border rings inside the outer one (typically the control bg)
         */
        void FlattenNativeBorder( wxWindow* aWindow, const wxColour& aEdgeColour,
                                  const wxColour& aInnerColour );

        /**
         * Re-point a native control's visual style at the light or dark Explorer theme.
         *
         * Windows applies the "DarkMode_Explorer" uxtheme to tree / list controls created
         * while MSW dark mode is active, and offers no app-wide re-theme at runtime — so
         * after the Anvil light/dark toggle a control keeps its start-up hover, selection
         * and scrollbar chrome (a dark hover row on a white tree, or the reverse).  Calling
         * this on the toggle flips that native layer in place.  NOP on GTK and macOS.
         *
         * @param aWindow is the native control (e.g. a wxTreeCtrl) to re-theme
         * @param aDark selects the dark Explorer theme (true) or the light one (false)
         */
        void SetDarkExplorerTheme( wxWindow* aWindow, bool aDark );
    }
}

#endif // KIPLATFORM_UI_H_
