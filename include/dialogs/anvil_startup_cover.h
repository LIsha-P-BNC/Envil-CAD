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

#ifndef ANVIL_STARTUP_COVER_H_
#define ANVIL_STARTUP_COVER_H_

#include <kicommon.h>

#include <wx/frame.h>

class ANVIL_LOADING_TRACK;


/**
 * The window that stands in for the application while it is being built.
 *
 * Bringing up the shell takes seconds — the manager frame, the editor kifaces, the symbol,
 * footprint and design-block libraries — and every one of them runs on the UI thread.  With
 * nothing on screen that reads as a blank desktop, or worse a bare unpainted main window, so
 * this cover goes up first and comes down only once the shell has actually painted.
 *
 * It wears the sign-in screen's own surfaces (the populated board on the left, the light page
 * and card on the right, from dialog_anvil_login.cpp, where this class is implemented so it
 * can share them) with the sign-in form replaced by a captioned progress rail.  Handing over
 * from the gate to this window therefore reads as one window changing what it shows rather
 * than a second window appearing.
 *
 * It is a plain framed window, not a borderless splash: it carries a caption with minimize
 * and maximize boxes so it can be moved aside like anything else.  It has no close box, and
 * refuses a close from the system menu, because the code driving it holds a raw pointer for
 * the whole of startup.
 *
 * Nothing here is auth-aware beyond reading the signed-in user's name for the subtitle; the
 * cover is shown on every launch, signed in or freshly signed in.
 */
class KICOMMON_API ANVIL_STARTUP_COVER : public wxFrame
{
public:
    ANVIL_STARTUP_COVER();

    /**
     * Move the loading rail to @a aFraction (0..1) and, when given, re-caption it with the
     * step now under way.
     *
     * Startup owns the UI thread in multi-second blocks, so nothing driven by a timer can be
     * relied on to keep moving: the rail is advanced from the outside instead, as each real
     * step clears, and every call paints synchronously (pumping whatever else is waiting to be
     * drawn) before returning.  The bar therefore stands still exactly as long as the step it
     * names actually takes.
     *
     * The pump also runs anything the caller queued with CallAfter() just before this call,
     * which is how deferred startup work (library preloading) ends up running behind the cover
     * rather than in front of a frozen main window.
     */
    void SetProgress( double aFraction, const wxString& aStep = wxEmptyString );

    /**
     * Size the cover to fill the work area of the display it opens on, then show and paint it
     * whole.
     *
     * The paint has to happen here.  The caller's very next act is to block for seconds
     * building the main window, with no event loop left to service a repaint asked for later,
     * and a window shown but never drawn stands there full of whatever the desktop had under
     * it.
     */
    bool Show( bool aShow = true ) override;

private:
    ANVIL_LOADING_TRACK* m_track;
};

#endif  // ANVIL_STARTUP_COVER_H_
