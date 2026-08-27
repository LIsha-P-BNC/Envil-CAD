/*
 * This program source code file is part of Anvil, a free EDA CAD application.
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

/**
 * @file ui_common.h
 * Functions to provide common constants and other functions to assist
 * in making a consistent UI
 */

#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <kicommon.h>
#include "report_severity.h"      // enum SEVERITY
#include <kiplatform/anvil_theme.h>
#include <wx/string.h>
#include <wx/font.h>

class wxBitmap;
class wxBitmapBundle;
class wxColour;
class wxSize;
class wxTextCtrl;
class wxTextEntry;
class wxWindow;
class wxMenuItem;
class wxMenu;

/**
 * Used for holding indeterminate values, such as with multiple selections
 * holding different values or controls which do not wish to set a value.
 */
#define INDETERMINATE_STATE _( "-- mixed values --" )
#define INDETERMINATE_ACTION _( "-- leave unchanged --" )

namespace KIUI
{

const int c_IndicatorSizeDIP = 10;


/**
 * Get the standard margin around a widget in the Anvil UI
 * @return margin in pixels
 */
KICOMMON_API int GetStdMargin();

/**
 * Return the size of @a aSingleLine of text when it is rendered in @a aWindow
 * using whatever font is currently set in that window.
 */
KICOMMON_API wxSize GetTextSize( const wxString& aSingleLine, wxWindow* aWindow );

/**
 * Anvil mono chrome icons: repaint every pixel of @a aBitmap in @a aColor, keeping the
 * per-pixel alpha (anti-aliased edges) and the bitmap's DPI scale factor — turning the
 * multi-colour icon art into a flat single-colour glyph (e.g. a Bone-white toolbar icon
 * that flips to Signal Emerald while hovered).
 */
KICOMMON_API wxBitmap RecolorFlat( const wxBitmap& aBitmap, const wxColour& aColor );

/**
 * True when the user has picked the NEMI Emerald **Light** app theme
 * (COMMON_SETTINGS `appearance.app_theme` == APP_THEME::LIGHT).
 *
 * This is the one place that decides "which Anvil theme are we in"; everything else asks the
 * palette (ANVIL::IsLight()) once SyncAnvilTheme() has run.
 */
KICOMMON_API bool AnvilLightTheme();

/**
 * Flip the **kicommon** copy of the ANVIL palette (see the DLL note in anvil_theme.h).
 *
 * kicommon is a shared library, so one call covers the whole process for everything that lives
 * in it (dialog_shim, indicator_icon, ...).  The `common` static library, by contrast, is linked
 * separately into anvilcad.exe and each _kiface DLL, so each of those needs its own
 * ANVIL::SetMode() — which is exactly what SyncAnvilTheme() below pairs up.
 */
KICOMMON_API void SetAnvilThemeMode( bool aLight );

/**
 * Point BOTH palette copies reachable from the caller — this module's own and kicommon's — at
 * the persisted app theme.
 *
 * Deliberately inline: ANVIL::SetMode() has to run against the *caller's* copy of the palette
 * globals, so this cannot be a function exported from a library.  Call it before a module paints
 * anything Anvil-themed (EDA_BASE_FRAME's ctor, the program init paths, the theme toggle).
 */
inline void SyncAnvilTheme()
{
    const bool light = AnvilLightTheme();

    ANVIL::SetMode( light ? ANVIL::MODE::LIGHT : ANVIL::MODE::DARK );
    SetAnvilThemeMode( light );
}

/**
 * Set @a aFont's face to @a aFaceName, but only when that face is really installed.
 *
 * wxFont::SetFaceName() INVALIDATES the font when the face is missing: IsOk() goes false and
 * every later query answers from a null font — GetPointSize() returns 0, GetFaceName() an empty
 * string.  A brand face like "Space Grotesk" is not on a stock Windows box, so calling
 * SetFaceName() directly quietly destroyed the font on any machine that lacked it (that is what
 * collapsed the AUI pane-caption band to 6 px and clipped every pane title).
 *
 * @return true when the face existed and was applied; false leaves @a aFont untouched.
 */
KICOMMON_API bool ApplyFontFace( wxFont& aFont, const wxString& aFaceName );

KICOMMON_API wxFont GetUIFont();

KICOMMON_API wxFont GetMonospacedUIFont();

KICOMMON_API wxFont GetControlFont( wxWindow* aWindow );
KICOMMON_API wxFont GetInfoFont( wxWindow* aWindow );
KICOMMON_API wxFont GetSmallInfoFont( wxWindow* aWindow );
KICOMMON_API wxFont GetDockedPaneFont( wxWindow* aWindow );
KICOMMON_API wxFont GetStatusFont( wxWindow* aWindow );

/**
 * GetInfoFont() in the brand monospaced face — for dense identifier lists (layer names, object
 * names) where the mockups expect the same mono the status strip and project tree use.
 *
 * Set this on the CONTAINER window, not on each label: sibling code sizes those labels with
 * container->GetTextExtent(), so a font that only reaches the labels would mis-measure them.
 */
KICOMMON_API wxFont GetMonoInfoFont( wxWindow* aWindow );

/**
 * Set the minimum pixel width on a text control in order to make a text
 * string be fully visible within it.
 *
 * The current font within the text control is considered.  The text can come either from
 * the control or be given as an argument.  If the text control is larger than needed, then
 * nothing is done.
 *
 * @param aCtrl the text control to potentially make wider.
 * @param aString the text that is used in sizing the control's pixel width.
 * If NULL, then
 *   the text already within the control is used.
 * @return true if the \a aCtrl had its size changed, else false.
 */
KICOMMON_API bool EnsureTextCtrlWidth( wxTextCtrl* aCtrl, const wxString* aString = nullptr );

/**
 * Select the number (or "?") in a reference for ease of editing.
 */
KICOMMON_API void SelectReferenceNumber( wxTextEntry* aTextEntry );

/**
 * Ellipsize text (at the end) to be no more than 1/3 of the window width.
 *
 * @return shortened text ending with an ellipsis.
 */
KICOMMON_API wxString EllipsizeStatusText( wxWindow* aWindow, const wxString& aString );

/**
 * Ellipsize text (at the end) to be no more than 36 characters.
 *
 * @return shortened text ending with an ellipsis.
 */
KICOMMON_API wxString EllipsizeMenuText( const wxString& aString );

/**
 * Check if a input control has focus.
 *
 * @param aFocus Control that has focus, if null, wxWidgets will be queried
 */
KICOMMON_API bool IsInputControlFocused( wxWindow* aFocus = nullptr );

/**
 * Check if a input control has focus.
 *
 * @param aFocus Control that test if editable
 * @return True if control is input and editable OR control is not a input. False if control is
 *         input and not editable.
 */
KICOMMON_API bool IsInputControlEditable( wxWindow* aControl );

KICOMMON_API bool IsModalDialogFocused();

/**
 * Makes a window read-only.  Does some extra work over wxWindow::Disable() to make sure you
 * can still scroll around in sub-windows.
 */
KICOMMON_API void Disable( wxWindow* aWindow );

KICOMMON_API extern const wxString s_FocusStealableInputName;


/**
 * Add a bitmap to a menuitem.
 *
 * It is added only if use images in menus config option allows it.  For wxITEM_CHECK
 * or wxITEM_RADIO menuitems, the bitmap is added only on Windows, other platforms do
 * not support it
 *
 * @param aMenu is the menuitem.
 * @param aImage is the icon to add to aMenu.
 */
KICOMMON_API void AddBitmapToMenuItem( wxMenuItem* aMenu, const wxBitmapBundle& aImage );


/**
 * Create and insert a menu item with an icon into \a aMenu.
 *
 * @param aMenu is the menu to add the new item.
 * @param aId is the command ID for the new menu item.
 * @param aText is the string for the new menu item.
 * @param aImage is the icon to add to the new menu item.
 * @param aType is the type of menu :wxITEM_NORMAL (default), wxITEM_CHECK ...
 * @return a pointer to the new created wxMenuItem.
 */
KICOMMON_API wxMenuItem* AddMenuItem( wxMenu* aMenu, int aId, const wxString& aText,
                                      const wxBitmapBundle& aImage,
                                      wxItemKind            aType = wxITEM_NORMAL );


/**
 * Create and insert a menu item with an icon and a help message string into \a aMenu.
 *
 * @param aMenu is the menu to add the new item.
 * @param aId is the command ID for the new menu item.
 * @param aText is the string for the new menu item.
 * @param aHelpText is the help message string for the new menu item.
 * @param aImage is the icon to add to the new menu item.
 * @param aType is the type of menu :wxITEM_NORMAL (default), wxITEM_CHECK ...
 * @return a pointer to the new created wxMenuItem.
 */
KICOMMON_API wxMenuItem* AddMenuItem( wxMenu* aMenu, int aId, const wxString& aText,
                                      const wxString& aHelpText, const wxBitmapBundle& aImage,
                                      wxItemKind aType = wxITEM_NORMAL );


/**
 * Create and insert a menu item with an icon into \a aSubMenu in \a aMenu.
 *
 * @param aMenu is the menu to add the new submenu item.
 * @param aSubMenu is the submenu to add the new menu.
 * @param aId is the command ID for the new menu item.
 * @param aText is the string for the new menu item.
 * @param aImage is the icon to add to the new menu item.
 * @return a pointer to the new created wxMenuItem,
 */
KICOMMON_API wxMenuItem* AddMenuItem( wxMenu* aMenu, wxMenu* aSubMenu, int aId,
                                      const wxString& aText, const wxBitmapBundle& aImage );


/**
 * Create and insert a menu item with an icon and a help message string into
 * \a aSubMenu in \a aMenu.
 *
 * @param aMenu is the menu to add the new submenu item.
 * @param aSubMenu is the submenu to add the new menu.
 * @param aId is the command ID for the new menu item.
 * @param aText is the string for the new menu item.
 * @param aHelpText is the help message string for the new menu item.
 * @param aImage is the icon to add to the new menu item.
 * @return a pointer to the new created wxMenuItem.
 */
KICOMMON_API wxMenuItem* AddMenuItem( wxMenu* aMenu, wxMenu* aSubMenu, int aId,
                                      const wxString& aText, const wxString& aHelpText,
                                      const wxBitmapBundle& aImage );
}

KICOMMON_API SEVERITY SeverityFromString( const wxString& aSeverity );

KICOMMON_API wxString SeverityToString( const SEVERITY& aSeverity );

#endif // UI_COMMON_H
