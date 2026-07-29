/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * @author Russell Oliver <roliver8143@gmail.com>
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

/**
 * @file import_project.cpp
 * @brief routines for importing a non-KiCad project
 */

#include <wx/dir.h>
#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include <wx/ffile.h>

#include <confirm.h>
#include <kidialog.h>
#include <kiplatform/ui.h>
#include <wildcards_and_files_ext.h>
#include <io/pads/pads_common.h>

#include <sch_io/sch_io_mgr.h>
#include <pcb_io/pcb_io_mgr.h>

#include "kicad_manager_frame.h"
#include <import_proj.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>


bool KICAD_MANAGER_FRAME::ConvertProjectToAnvil( const wxFileName& aSrcPro,
                                                 const wxString& aDestDir,
                                                 bool aKeepOriginals,
                                                 wxFileName* aDestProOut )
{
    const wxString srcRoot = aSrcPro.GetPathWithSep();
    const bool     sameDir = wxFileName( aDestDir, wxEmptyString ).GetPath() == aSrcPro.GetPath();

    wxArrayString allFiles;
    wxDir::GetAllFiles( aSrcPro.GetPath(), &allFiles );

    auto extMapped = []( const wxString& aExt ) -> wxString
    {
        if( aExt == FILEEXT::ProjectFileExtension )
            return FILEEXT::AnvilProjectFileExtension;
        if( aExt == FILEEXT::KiCadSchematicFileExtension )
            return FILEEXT::AnvilSchematicFileExtension;
        if( aExt == FILEEXT::KiCadPcbFileExtension )
            return FILEEXT::AnvilPcbFileExtension;

        return aExt;
    };

    int converted = 0;

    for( const wxString& file : allFiles )
    {
        wxString rel = file;

        if( !rel.StartsWith( srcRoot, &rel ) )
            continue;

        // Skip housekeeping artifacts: locks, backups, local history, VCS internals.
        if( rel.Contains( wxT( "-backups" ) ) || rel.Contains( wxT( ".history" ) )
            || rel.Contains( wxT( ".git" ) ) || rel.EndsWith( FILEEXT::LockFileExtension )
            || wxFileName( rel ).GetFullName().StartsWith( FILEEXT::LockFilePrefix ) )
        {
            continue;
        }

        wxFileName relFn( rel );
        wxString   mapped = extMapped( relFn.GetExt() );
        bool       isProjectFile = mapped != relFn.GetExt();

        relFn.SetExt( mapped );

        // When converting into the same folder, non-project files stay untouched.
        if( sameDir && !isProjectFile )
            continue;

        wxFileName destFile( aDestDir + wxFileName::GetPathSeparator() + relFn.GetFullPath() );

        if( !destFile.DirExists() )
            destFile.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

        if( destFile.GetFullPath() != file )
            wxCopyFile( file, destFile.GetFullPath(), true );

        if( isProjectFile )
        {
            ++converted;

            if( !aKeepOriginals && destFile.GetFullPath() != file )
                wxRemoveFile( file );

            // Rewrite quoted internal references (hierarchical sheets, board/meta links).
            wxFFile  f( destFile.GetFullPath(), wxS( "rb" ) );
            wxString text;

            if( f.IsOpened() && f.ReadAll( &text, wxConvUTF8 ) )
            {
                f.Close();

                int hits = 0;
                hits += text.Replace( wxT( ".kicad_sch\"" ), wxT( ".anvil_sch\"" ) );
                hits += text.Replace( wxT( ".kicad_pcb\"" ), wxT( ".anvil_pcb\"" ) );
                hits += text.Replace( wxT( ".kicad_pro\"" ), wxT( ".anvil_pro\"" ) );

                if( hits > 0 )
                {
                    wxFFile out( destFile.GetFullPath(), wxS( "wb" ) );

                    if( out.IsOpened() )
                        out.Write( text, wxConvUTF8 );
                }
            }
        }
    }

    if( converted == 0 )
        return false;

    if( aDestProOut )
    {
        aDestProOut->SetPath( aDestDir );
        aDestProOut->SetName( aSrcPro.GetName() );
        aDestProOut->SetExt( FILEEXT::AnvilProjectFileExtension );
        aDestProOut->MakeAbsolute();
    }

    return true;
}


void KICAD_MANAGER_FRAME::importKiCadProjectFile( const wxString& aInputPath )
{
    wxFileName src( aInputPath );

    if( !CloseProject( true ) )
        return;

    wxDirDialog prodlg( this, _( "Anvil Project Destination" ), src.GetPath(),
                        wxDD_DEFAULT_STYLE );

    if( prodlg.ShowModal() == wxID_CANCEL )
        return;

    wxFileName anvilPro;

    if( !ConvertProjectToAnvil( src, prodlg.GetPath(), true, &anvilPro ) )
    {
        DisplayErrorMessage( this, _( "No KiCad project files were found to convert." ) );
        return;
    }

    LoadProject( anvilPro );
}


void KICAD_MANAGER_FRAME::ImportSymbolLibrary()
{
    wxString filter;
    filter << _( "All supported symbol libraries" )
           << wxT( "|*.kicad_sym;*.anvil_sym;*.lib;*.SchLib;*.IntLib" )
           << wxT( "|" ) << _( "KiCad symbol libraries" ) << wxT( " (*.kicad_sym)|*.kicad_sym" )
           << wxT( "|" ) << _( "Altium symbol libraries" )
           << wxT( " (*.SchLib;*.IntLib)|*.SchLib;*.IntLib" )
           << wxT( "|" ) << _( "Legacy symbol libraries" ) << wxT( " (*.lib)|*.lib" );

    wxFileDialog srcDlg( this, _( "Import Symbol Library" ), GetMruPath(), wxEmptyString,
                         filter, wxFD_OPEN | wxFD_FILE_MUST_EXIST );

    if( srcDlg.ShowModal() == wxID_CANCEL )
        return;

    wxFileName src( srcDlg.GetPath() );
    wxFileName destDefault( src );
    destDefault.SetExt( FILEEXT::AnvilSymbolLibFileExtension );

    wxFileDialog destDlg( this, _( "Save Anvil Symbol Library" ), src.GetPath(),
                          destDefault.GetFullName(),
                          _( "Anvil symbol library files" )
                                  + wxString( wxT( " (*.anvil_sym)|*.anvil_sym" ) ),
                          wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    if( destDlg.ShowModal() == wxID_CANCEL )
        return;

    // The conversion runs inside eeschema, where the symbol IO plugins live.
    // Launch the schematic frame if it isn't open yet.
    if( !Kiway().Player( FRAME_SCH, true ) )
    {
        DisplayErrorMessage( this, _( "Could not start the schematic editor to run the "
                                      "library conversion." ) );
        return;
    }

    std::string payload = std::string( src.GetFullPath().utf8_str() ) + "\n"
                          + std::string( destDlg.GetPath().utf8_str() );

    Kiway().ExpressMail( FRAME_SCH, MAIL_ENVIL_CONVERT_SYMLIB, payload, this );

    wxString result = wxString::FromUTF8( payload );

    if( result.StartsWith( wxT( "OK" ) ) )
    {
        DisplayInfoMessage( this, wxString::Format( _( "Library converted: %s" ),
                                                    result.Mid( 3 ) ) );
    }
    else
    {
        DisplayErrorMessage( this, wxString::Format( _( "Library conversion failed: %s" ),
                                                     result ) );
    }
}


void KICAD_MANAGER_FRAME::OnImportKiCadProject( wxCommandEvent& event )
{
    wxString     filter = _( "KiCad project files" ) + wxString( wxT( " (*.kicad_pro)|*.kicad_pro" ) );
    wxFileDialog inputdlg( this, _( "Import KiCad Project" ), GetMruPath(), wxEmptyString,
                           filter, wxFD_OPEN | wxFD_FILE_MUST_EXIST );

    KIPLATFORM::UI::AllowNetworkFileSystems( &inputdlg );

    if( inputdlg.ShowModal() == wxID_CANCEL )
        return;

    importKiCadProjectFile( inputdlg.GetPath() );
}


void KICAD_MANAGER_FRAME::OnImportProject( wxCommandEvent& event )
{
    // One Altium-style import wizard: pick a project of ANY supported format; the format
    // comes from the chosen filter row (or the extension when "All supported" is active),
    // and the result is always a native Anvil project.
    wxString filter;
    filter << _( "All supported projects" )
           << wxT( "|*.kicad_pro;*.PrjPcb;*.SchDoc;*.PcbDoc;*.CSPcbDoc;*.CMPcbDoc;*.SWPcbDoc;"
                   "*.csa;*.cpa;*.sch;*.brd;*.json;*.zip;*.epro;*.asc;*.txt;*.prj;*.pcb" )
           << wxT( "|" ) << _( "KiCad project files" ) << wxT( " (*.kicad_pro)|*.kicad_pro" )
           << wxT( "|" ) << FILEEXT::AltiumProjectFilesWildcard()
           << wxT( "|" ) << FILEEXT::CadstarArchiveFilesWildcard()
           << wxT( "|" ) << FILEEXT::EagleFilesWildcard()
           << wxT( "|" ) << FILEEXT::EasyEdaArchiveWildcard()
           << wxT( "|" ) << FILEEXT::EasyEdaProFileWildcard()
           << wxT( "|" ) << FILEEXT::PADSProjectFilesWildcard()
           << wxT( "|" ) << FILEEXT::GedaProjectFilesWildcard();

    wxFileDialog dlg( this, _( "Import Project" ), GetMruPath(), wxEmptyString, filter,
                      wxFD_OPEN | wxFD_FILE_MUST_EXIST );

    KIPLATFORM::UI::AllowNetworkFileSystems( &dlg );

    if( dlg.ShowModal() == wxID_CANCEL )
        return;

    wxString path = dlg.GetPath();
    wxString ext = wxFileName( path ).GetExt().Lower();
    int      idx = dlg.GetFilterIndex();

    // Filter rows: 0=all, 1=KiCad, 2=Altium, 3=CADSTAR, 4=Eagle, 5=EasyEDA Std,
    // 6=EasyEDA Pro, 7=PADS, 8=gEDA.  For row 0, infer the format from the extension.
    if( idx == 0 )
    {
        if( ext == wxT( "kicad_pro" ) )
            idx = 1;
        else if( ext == wxT( "prjpcb" ) || ext == wxT( "schdoc" ) || ext == wxT( "pcbdoc" )
                 || ext == wxT( "cspcbdoc" ) || ext == wxT( "cmpcbdoc" )
                 || ext == wxT( "swpcbdoc" ) )
            idx = 2;
        else if( ext == wxT( "csa" ) || ext == wxT( "cpa" ) )
            idx = 3;
        else if( ext == wxT( "brd" ) || ext == wxT( "sch" ) )
            idx = 4;
        else if( ext == wxT( "json" ) || ext == wxT( "zip" ) )
            idx = 5;
        else if( ext == wxT( "epro" ) )
            idx = 6;
        else if( ext == wxT( "asc" ) || ext == wxT( "txt" ) )
            idx = 7;
        else if( ext == wxT( "prj" ) || ext == wxT( "pcb" ) )
            idx = 8;
        else
        {
            DisplayErrorMessage( this, _( "Unrecognized project format." ) );
            return;
        }
    }

    switch( idx )
    {
    case 1:
        importKiCadProjectFile( path );
        break;
    case 2:
        importProjectFromFile( path, { "SchDoc" },
                               { "PcbDoc", "CSPcbDoc", "CMPcbDoc", "SWPcbDoc" },
                               SCH_IO_MGR::SCH_ALTIUM, PCB_IO_MGR::ALTIUM_DESIGNER );
        break;
    case 3:
        importProjectFromFile( path, { "csa" }, { "cpa" }, SCH_IO_MGR::SCH_CADSTAR_ARCHIVE,
                               PCB_IO_MGR::CADSTAR_PCB_ARCHIVE );
        break;
    case 4:
        importProjectFromFile( path, { "sch" }, { "brd" }, SCH_IO_MGR::SCH_EAGLE,
                               PCB_IO_MGR::EAGLE );
        break;
    case 5:
        importProjectFromFile( path, { "INPUT" }, { "INPUT" }, SCH_IO_MGR::SCH_EASYEDA,
                               PCB_IO_MGR::EASYEDA );
        break;
    case 6:
        importProjectFromFile( path, { "INPUT" }, { "INPUT" }, SCH_IO_MGR::SCH_EASYEDAPRO,
                               PCB_IO_MGR::EASYEDAPRO );
        break;
    case 7:
        importProjectFromFile( path, { "asc", "txt" }, { "asc", "txt" }, SCH_IO_MGR::SCH_PADS,
                               PCB_IO_MGR::PADS );
        break;
    case 8:
        importProjectFromFile( path, { "prj", "sch" }, { "pcb" }, SCH_IO_MGR::SCH_GEDA,
                               PCB_IO_MGR::GEDA_PCB );
        break;
    }
}


void KICAD_MANAGER_FRAME::ImportNonKiCadProject( const wxString& aWindowTitle,
                                                 const wxString& aFilesWildcard,
                                                 const std::vector<std::string>& aSchFileExtensions,
                                                 const std::vector<std::string>& aPcbFileExtensions,
                                                 int aSchFileType, int aPcbFileType )
{
    wxString default_dir = GetMruPath();
    int      style = wxFD_OPEN | wxFD_FILE_MUST_EXIST;

    wxFileDialog inputdlg( this, aWindowTitle, default_dir, wxEmptyString, aFilesWildcard, style );

    KIPLATFORM::UI::AllowNetworkFileSystems( &inputdlg );

    if( inputdlg.ShowModal() == wxID_CANCEL )
        return;

    importProjectFromFile( inputdlg.GetPath(), aSchFileExtensions, aPcbFileExtensions,
                           aSchFileType, aPcbFileType );
}


void KICAD_MANAGER_FRAME::importProjectFromFile( const wxString& aInputPath,
                                                 const std::vector<std::string>& aSchFileExtensions,
                                                 const std::vector<std::string>& aPcbFileExtensions,
                                                 int aSchFileType, int aPcbFileType )
{
    wxString msg;
    wxString inputPath = aInputPath;
    bool     isPadsProject = ( aSchFileType == SCH_IO_MGR::SCH_PADS
                               && aPcbFileType == PCB_IO_MGR::PADS );

    if( isPadsProject
        && PADS_COMMON::DetectPadsFileType( inputPath ) == PADS_COMMON::PADS_FILE_TYPE::UNKNOWN )
    {
        DisplayErrorMessage( this,
                             _( "The selected file does not appear to be a PADS ASCII schematic or PCB." ) );
        return;
    }

    // OK, we got a new project to open.  Time to close any existing project before we go on
    // to collect info about where to put the new one, etc.  Otherwise the workflow is kind of
    // disjoint.
    if( !CloseProject( true ) )
        return;

    std::vector<wxString> schFileExts( aSchFileExtensions.begin(), aSchFileExtensions.end() );
    std::vector<wxString> pcbFileExts( aPcbFileExtensions.begin(), aPcbFileExtensions.end() );

    IMPORT_PROJ_HELPER importProj( this, schFileExts, pcbFileExts );
    importProj.m_InputFile = inputPath;

    // Loop to allow the user to retry directory selection when cancelling the "not empty" warning
    for( ;; )
    {
        // Don't use wxFileDialog here.  On GTK builds, the default path is returned unless a
        // file is actually selected.
        wxDirDialog prodlg( this, _( "KiCad Project Destination" ),
                            importProj.m_InputFile.GetPath(), wxDD_DEFAULT_STYLE );

        if( prodlg.ShowModal() == wxID_CANCEL )
            return;

        wxString targetDir = prodlg.GetPath();

        importProj.m_TargetProj.SetPath( targetDir );
        importProj.m_TargetProj.SetName( importProj.m_InputFile.GetName() );
        importProj.m_TargetProj.SetExt( FILEEXT::ProjectFileExtension );
        importProj.m_TargetProj.MakeAbsolute();

        if( !importProj.m_TargetProj.DirExists() )
        {
            if( !importProj.m_TargetProj.Mkdir() )
            {
                msg.Printf( _( "Folder '%s' could not be created.\n\n"
                               "Make sure you have write permissions and try again." ),
                            importProj.m_TargetProj.GetPath() );
                DisplayErrorMessage( this, msg );
                continue;
            }

            break;
        }

        wxDir targetDirTest( targetDir );

        if( targetDirTest.IsOpened() && targetDirTest.HasFiles() )
        {
            msg = _( "The selected directory is not empty.  We recommend you "
                     "create projects in their own clean directory.\n\nDo you "
                     "want to create a new empty directory for the project?" );

            KIDIALOG dlg( this, msg, _( "Confirmation" ),
                          wxYES_NO | wxCANCEL | wxICON_WARNING );
            dlg.DoNotShowCheckbox( __FILE__, __LINE__ );

            int result = dlg.ShowModal();

            if( result == wxID_YES )
            {
                importProj.FindEmptyTargetDir();

                if( !wxMkdir( importProj.m_TargetProj.GetPath() ) )
                {
                    msg = _( "Error creating new directory. Please try a different path. The "
                             "project cannot be imported." );

                    KICAD_MESSAGE_DIALOG dirErrorDlg( this, msg, _( "Error" ),
                                                      wxOK_DEFAULT | wxICON_ERROR );
                    dirErrorDlg.ShowModal();
                    continue;
                }

                break;
            }
            else if( result == wxID_NO )
            {
                break;
            }

            // wxID_CANCEL — go back to directory selection
            continue;
        }

        targetDirTest.Close();
        break;
    }

    CreateNewProject( importProj.m_TargetProj.GetFullPath(), false /* Don't create stub files */ );
    LoadProject( importProj.m_TargetProj );

    if( isPadsProject )
        importProj.ImportPadsFiles();
    else
        importProj.ImportFiles( aSchFileType, aPcbFileType );

    // Save the freshly imported (still-unsaved) schematic and board to disk first, so the
    // conversion below picks them up and the destination ends as a clean all-Anvil project.
    if( KIWAY_PLAYER* sch = Kiway().Player( FRAME_SCH, false ) )
        sch->GetToolManager()->RunAction( ACTIONS::save );

    if( KIWAY_PLAYER* pcb = Kiway().Player( FRAME_PCB_EDITOR, false ) )
        pcb->GetToolManager()->RunAction( ACTIONS::save );

    // Anvil: store the imported result in the native Anvil format (rename in place --
    // these files were just created by the importer, there are no originals to keep).
    wxFileName anvilPro;

    if( ConvertProjectToAnvil( importProj.m_TargetProj, importProj.m_TargetProj.GetPath(),
                               false, &anvilPro ) )
    {
        LoadProject( anvilPro );

        // The importer's intermediate .kicad_pro survives the conversion because it is the
        // active project while converting; it's redundant once the Anvil project is loaded.
        wxFileName staleKicadPro( anvilPro );
        staleKicadPro.SetExt( FILEEXT::ProjectFileExtension );

        if( staleKicadPro.FileExists() )
            wxRemoveFile( staleKicadPro.GetFullPath() );
    }

    ReCreateTreePrj();
    m_active_project = true;
}


void KICAD_MANAGER_FRAME::OnImportAltiumProjectFiles( wxCommandEvent& event )
{
    ImportNonKiCadProject( _( "Import Altium Project Files" ),
                           FILEEXT::AltiumProjectFilesWildcard(), { "SchDoc" },
                           { "PcbDoc", "CSPcbDoc", "CMPcbDoc", "SWPcbDoc" },
                           SCH_IO_MGR::SCH_ALTIUM, PCB_IO_MGR::ALTIUM_DESIGNER );
}


void KICAD_MANAGER_FRAME::OnImportCadstarArchiveFiles( wxCommandEvent& event )
{
    ImportNonKiCadProject( _( "Import CADSTAR Archive Project Files" ),
                           FILEEXT::CadstarArchiveFilesWildcard(), { "csa" }, { "cpa" },
                           SCH_IO_MGR::SCH_CADSTAR_ARCHIVE, PCB_IO_MGR::CADSTAR_PCB_ARCHIVE );
}


void KICAD_MANAGER_FRAME::OnImportEagleFiles( wxCommandEvent& event )
{
    ImportNonKiCadProject( _( "Import Eagle Project Files" ), FILEEXT::EagleFilesWildcard(), { "sch" },
                           { "brd" }, SCH_IO_MGR::SCH_EAGLE, PCB_IO_MGR::EAGLE );
}


void KICAD_MANAGER_FRAME::OnImportEasyEdaFiles( wxCommandEvent& event )
{
    ImportNonKiCadProject( _( "Import EasyEDA Std Backup" ), FILEEXT::EasyEdaArchiveWildcard(), { "INPUT" },
                           { "INPUT" }, SCH_IO_MGR::SCH_EASYEDA, PCB_IO_MGR::EASYEDA );
}


void KICAD_MANAGER_FRAME::OnImportEasyEdaProFiles( wxCommandEvent& event )
{
    ImportNonKiCadProject( _( "Import EasyEDA Pro Project" ), FILEEXT::EasyEdaProFileWildcard(), { "INPUT" },
                           { "INPUT" }, SCH_IO_MGR::SCH_EASYEDAPRO, PCB_IO_MGR::EASYEDAPRO );
}


void KICAD_MANAGER_FRAME::OnImportPadsProjectFiles( wxCommandEvent& event )
{
    ImportNonKiCadProject( _( "Import PADS Project Files" ), FILEEXT::PADSProjectFilesWildcard(),
                           { "asc", "txt" }, { "asc", "txt" }, SCH_IO_MGR::SCH_PADS,
                           PCB_IO_MGR::PADS );
}


void KICAD_MANAGER_FRAME::OnImportGedaFiles( wxCommandEvent& event )
{
    ImportNonKiCadProject( _( "Import gEDA / Lepton EDA Project Files" ),
                           FILEEXT::GedaProjectFilesWildcard(), { "prj", "sch" }, { "pcb" },
                           SCH_IO_MGR::SCH_GEDA, PCB_IO_MGR::GEDA_PCB );
}
