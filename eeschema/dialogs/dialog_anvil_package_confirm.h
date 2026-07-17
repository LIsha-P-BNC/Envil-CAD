/*
 * This program source code file is part of Anvil.
 *
 * Component package/library confirmation dialog.
 *
 * Shown before a symbol picked from the library chooser is placed on the sheet,
 * or before a brand-new symbol is finalized in the symbol editor, when
 * ADVANCED_CFG::m_ConfirmComponentPackage is enabled. Asks the user (a) whether
 * the part should be Through-Hole or Surface-Mount, and (b) which candidate
 * footprint -- gathered dynamically from the project's configured libraries via
 * the symbol's own FPFilters, same as the rest of KiCad -- should be assigned.
 */

#ifndef ANVIL_DIALOG_PACKAGE_CONFIRM_H
#define ANVIL_DIALOG_PACKAGE_CONFIRM_H

#include "dialog_shim.h"
#include <lib_id.h>
#include <wx/arrstr.h>

class SCH_BASE_FRAME;
class FOOTPRINT_SELECT_WIDGET;
class wxRadioBox;
class wxCommandEvent;


class DIALOG_ANVIL_PACKAGE_CONFIRM : public DIALOG_SHIM
{
public:
    enum class PACKAGE_PREF
    {
        THT,
        SMD
    };

    /**
     * @param aParent           owning schematic-editor frame; supplies the KIWAY used to
     *                          fetch footprint candidates from pcbnew.
     * @param aLibId            the symbol's LIB_ID (for the dialog title).
     * @param aSymbolName       human-readable name shown to the user (symbol Value/name).
     * @param aFpFilters        the symbol's FPFilters (glob patterns), used to scope the
     *                          candidate footprint list to parts that actually fit.
     * @param aDefaultFootprint the footprint currently assigned (if any); preselected.
     */
    DIALOG_ANVIL_PACKAGE_CONFIRM( SCH_BASE_FRAME* aParent, const LIB_ID& aLibId,
                                   const wxString& aSymbolName, const wxArrayString& aFpFilters,
                                   const wxString& aDefaultFootprint );

    ~DIALOG_ANVIL_PACKAGE_CONFIRM() override = default;

    PACKAGE_PREF GetPackagePreference() const { return m_packagePref; }

    /// Footprint LIB_ID string picked by the user; empty if the user left the existing default.
    wxString GetSelectedFootprint() const { return m_selectedFootprint; }

private:
    void onPackageTypeChanged( wxCommandEvent& aEvent );
    void onFootprintSelected( wxCommandEvent& aEvent );

    wxRadioBox*               m_packageTypeCtrl;
    FOOTPRINT_SELECT_WIDGET*  m_fpSelect;

    wxString                  m_defaultFootprint;
    wxString                  m_selectedFootprint;
    PACKAGE_PREF              m_packagePref;
};

#endif // ANVIL_DIALOG_PACKAGE_CONFIRM_H
