/*
 * This program source code file is part of KiCad, a free EDA CAD application.
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
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#pragma once

#include <kicommon.h>
#include <memory>
#include <vector>
#include <config_params.h>

class wxConfigBase;
class PARAM_CFG;

/**
 * @defgroup advanced_config Advanced Configuration Variables
 *
 * Class containing "advanced" configuration options.
 *
 * Options set here are for developer or advanced users only. If a general user
 * needs to set one of these for normal KiCad use, either:
 * * They are working around some bug that should be fixed, or
 * * The parameter they are setting is of general interest and should be in the
 *   main application config, with UI provided.
 *
 * Options in this class are, in general, preferable to #defines, as they
 * allow more flexible configuration by developers, and don't hide code from
 * the compiler on other configurations, which can result in broken builds.
 *
 * Never use advanced configs in an untestable way. If a function depends on
 * advanced config such that you cannot test it without changing the config,
 * "lift" the config to a higher level and make pass it as parameter of the code
 * under test. The tests can pass their own values as needed.
 *
 * This also applies to code that does not depend on "common" - it cannot
 * use this class, so you must pass configuration in as proper parameters.
 *
 * Sometimes you can just use values directly, and sometimes helper functions
 * might be provided to allow extra logic (for example when a advanced config
 * applies only on certain platforms).
 *
 * For more information on what config keys set these parameters in the
 * config files, and why you might want to set them, see #AC_KEYS
 *
 */
#include <wx/string.h>
class KICOMMON_API ADVANCED_CFG
{
public:
    /**
     * Get the singleton instance's config, which is shared by all consumers.
     *
     * This configuration is read-only - to set options, users should add the parameters to
     * their config files at ~/.config/kicad/advanced, or the platform equivalent.
     */
    static const ADVANCED_CFG& GetCfg();

    /**
     * Reload the configuration from the configuration file.
     */
    void Reload();

    /**
     * Save the configuration to the configuration file.
     */
    void Save();

    const std::vector<std::unique_ptr<PARAM_CFG>>& GetEntries() const { return m_entries; }

    ///@{
    /// \ingroup advanced_config

    /**
     * Distance from an arc end point and the estimated end point, when rotating from the
     * start point to the end point.
     *
     * Setting name: "DrawArcAccuracy"
     * Valid values: 0 to 100000
     * Default value: 10
     */
    double m_DrawArcAccuracy;

    /**
     * When drawing an arc, the angle ( center - start ) - ( start - end ) can be limited to
     * avoid extremely high radii.
     *
     * Setting name: "DrawArcCenterStartEndMaxAngle"
     * Valid values: 0 to 100000
     * Default value: 50
     */
    double m_DrawArcCenterMaxAngle;

    /**
     * Maximum angle between the tangent line of an arc track and a connected straight track
     * in order to commence arc dragging. Units are degrees.
     *
     * Setting name: "MaxTangentTrackAngleDeviation"
     * Valid values: 0 to 90
     * Default value: 1
     */
    double m_MaxTangentAngleDeviation;

    /**
     * Maximum track length to keep after doing an arc track resizing operation. Units are mm.
     *
     * Setting name: "MaxTrackLengthToKeep"
     * Valid values: 0 to 1
     * Default value: 0.0005
     */
    double m_MaxTrackLengthToKeep;

    /**
     * When filling zones, we add an extra amount of clearance to each zone to ensure that
     * rounding errors do not overrun minimum clearance distances.
     *
     * This is the extra clearance in mm.
     *
     * Setting name: "ExtraFillMargin"
     * Valid values: 0 to 1
     * Default value: 0.0005
     */
    double m_ExtraClearance;

    /**
     * Enable the minimum slot width check for creepage
     *
     * Setting name: "EnableCreepageSlot"
     * Default value: false
     */
    bool m_EnableCreepageSlot;

    /**
     * Epsilon for DRC tests.
     *
     * @note Fo zone tests this is essentially additive with #m_ExtraClearance.  Units are mm.
     *
     * Setting name: "DRCEpsilon"
     * Valid values: 0 to 1
     * Default value: 0.0005
     */
    double m_DRCEpsilon;

    /**
     * Sliver width tolerance for DRC.
     *
     * Units are mm.
     *
     * Setting name: "DRCSliverWidthTolerance"
     * Valid values: 0.01 to 0.25
     * Default value: 0.08
     */
    double m_SliverWidthTolerance;

    /**
     * Sliver length tolerance for DRC.
     *
     * Units are mm.
     *
     * Setting name: "DRCSliverMinimumLength"
     * Valid values: 1e-9 to 10
     * Default value: 0.0008
     */
    double m_SliverMinimumLength;

    /**
     * Sliver angle to tolerance for DRC.
     *
     * Units are mm.
     *
     * Setting name: "DRCSliverAngleTolerance"
     * Valid values: 1 to 90
     * Default value: 20
     */
    double m_SliverAngleTolerance;


    /**
     * Dimension used to calculate the actual hole size from the finish hole size.
     *
     * @note IPC-6012 says 0.015-0.018mm; Cadence says at least 0.020mm for a Class 2 board and
     *       at least 0.025mm for Class 3.  Units are mm.
     *
     * Setting name: "HoleWallPlatingThickness"
     * Valid values: 1 to 90
     * Default value: 0.02
     */
    double m_HoleWallThickness;


    /**
     * Configure the coroutine stack size in bytes.
     *
     * @note This should be allocated in multiples of the system page size (n*4096 is generally
     *       safe)
     *
     * Setting name: "CoroutineStackSize"
     * Valid values: 32 * 4096 to 4096 * 4096
     * Default value: 256 * 4096
     */
    int m_CoroutineStackSize;

    /**
     * The update interval the wxWidgets sends wxUpdateUIEvents to windows.
     *
     * Setting this to -1 will disable all automatic UI events.  Any other
     * value is the number of milliseconds between events.
     *
     * @see https://docs.wxwidgets.org/3.0/classwx_update_u_i_event.html#a24daac56f682b866baac592e761ccede.
     *
     * Setting name: "UpdateUIEventInterval"
     * Valid values: -1 to 100000
     * Default value: 0
     */
    int m_UpdateUIEventInterval;

    /**
     * Show PNS router debug graphics while routing
     *
     * Setting name: "ShowRouterDebugGraphics"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_ShowRouterDebugGraphics;

    /**
     * Enable PNS router to dump state information for debug purpose (press `0` while routing)
     *
     * Setting name: "EnableRouterDump"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_EnableRouterDump;

    /**
     * Slide the zoom steps over for debugging things "up close".
     *
     * Setting name: "HyperZoom"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_HyperZoom;

    /**
     * Save files in compact display mode
     *
     * When set to true, this will wrap polygon point sets at 4 points per line rather
     * than a single point per line.  Single point per line helps with version control systems.
     *
     * Setting name: "CompactSave"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_CompactSave;

    /**
     * Enable drawing the triangulation outlines with a visible color.
     *
     * @note This only affects the OpenGL GAL.
     *
     * Setting name: "StrokeTriangulation"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_DrawTriangulationOutlines;

    /**
     * When true, adds zone-display-modes for stroking the zone fracture boundaries and the zone
     * triangulation.
     *
     * Setting name: "ExtraZoneDisplayModes"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_ExtraZoneDisplayModes;

    /**
     * Absolute minimum pen width for plotting.
     *
     * @note Some formats (PDF, for example) don't like ultra-thin lines.  PDF seems happy
     *       enough with 0.0212mm which equates to 1px @ 1200dpi.  Units are mm.
     *
     * Setting name: "MinPlotPenWidth"
     * Valid values: 0 to 1
     * Default value: 0.0212
     */
    double m_MinPlotPenWidth;

    /**
     * A mode that dumps the various stages of a F_Cu fill into In1_Cu through In9_Cu.
     *
     * Setting name: "DebugZoneFiller"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_DebugZoneFiller;

    /**
     * A mode that writes PDFs without compression.
     *
     * Setting name: "DebugPDFWriter"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_DebugPDFWriter;

    /**
     * Stroke font line width factor relative to EM size for PDF stroke fonts.
     *
     * Setting name: "PDFStrokeFontWidthFactor"
     * Valid values: 0.0 to 1.0 (practical range 0.005 - 0.1)
     * Default value: 0.04
     */
    double m_PDFStrokeFontWidthFactor;

    /**
     * Horizontal offset factor applied to stroke font glyph coordinates (in EM units) after
     * to compensate misalignment. Positive values move glyphs right.
     *
     * Setting name: "PDFStrokeFontXOffset"
     * Valid values: -1.0 to 1.0
     * Default value: 0.0
     */
    double m_PDFStrokeFontXOffset;

    /**
     * Vertical offset factor applied to stroke font glyph coordinates (in EM units) after
     * Y inversion to compensate baseline misalignment. Positive values move glyphs up.
     *
     * Setting name: "PDFStrokeFontYOffset"
     * Valid values: -1.0 to 1.0
     * Default value: 0.0
     */
    double m_PDFStrokeFontYOffset;

    /**
     * Multiplier applied to stroke width factor when rendering bold stroke font subsets.
     *
     * Setting name: "PDFStrokeFontBoldMultiplier"
     * Valid values: 1.0 to 5.0
     * Default value: 1.6
     */
    double m_PDFStrokeFontBoldMultiplier;

    /**
     * Kerning (spacing) factor applied to glyph advance (width). Values < 1 tighten spacing.
     * Applied uniformly across stroke font PDF output.
     *
     * Setting name: "PDFStrokeFontKerningFactor"
     * Valid values: 0.5 to 2.0
     * Default value: 0.9
     */
    double m_PDFStrokeFontKerningFactor;

    /**
     * Use legacy wxWidgets-based printing.
     *
     * Setting name: "UsePdfPrint"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_UsePdfPrint;

    /**
     * The diameter of the drill marks on print and plot outputs (in mm) when the "Drill marks"
     * option is set to "Small mark".
     *
     * Setting name: "SmallDrillMarkSize"
     * Valid values: 0 to 3
     * Default value: 0.35
     */
    double m_SmallDrillMarkSize;

    /**
     * Enable the hotkeys dumper feature for generating documentation.
     *
     * Setting name: "HotkeysDumper"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_HotkeysDumper;

    /**
     * Draw GAL bounding boxes in painters.
     *
     * Setting name: "DrawBoundingBoxes"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_DrawBoundingBoxes;

    /**
     * Enable exporting board editor netlist to a file for troubleshooting purposes.
     *
     * Setting name: "ShowPcbnewExportNetlist"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_ShowPcbnewExportNetlist;

    /**
     * Skip reading/writing 3D model file caches.
     *
     * This does not prevent the models from being cached in memory meaning reopening the 3D
     * viewer in the same project session will not reload model data from disk again.
     *
     * Setting name: "Skip3DModelFileCache"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_Skip3DModelFileCache;

    /**
     * Skip reading/writing 3D model memory caches.
     &
     * This ensures 3D models are always reloaded from disk even if we previously opened the 3D
     * viewer.
     *
     * Setting name: "Skip3DModelMemoryCache"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_Skip3DModelMemoryCache;

    /**
     * Hide the build version from the KiCad manager frame title.
     *
     * Useful for making screenshots/videos of KiCad without pinning to a specific version.
     *
     * Setting name: "HideVersionFromTitle"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_HideVersionFromTitle;

    /**
     * Shows debugging event counters in various places.
     *
     * Setting name: "ShowEventCounters"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_ShowEventCounters;

    /**
     * Show UUIDs of items in the message panel.
     *
     * Can be useful when debugging against a specific item
     * saved in a file.
     *
     * 0: do not show (default)
     * 1: show full UUID
     * 2: show only first 8 characters of UUID
     *
     * Setting name: "MsgPanelShowUuids"
     * Default value: 0
     */
    int m_MsgPanelShowUuids;

    /**
     * Allow manual scaling of canvas.
     *
     * Setting name: "AllowManualCanvasScale"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_AllowManualCanvasScale;

    /**
     * Set the bevel height of layer items in 3D viewer when ray tracing.
     *
     * Controls the start of curvature normal on the edge.  The value is in micrometer. Good
     * values should be around or less than the copper thickness.
     *
     * Setting name: "V3DRT_BevelHeight_um"
     * Valid values: 0 to std::numeric_limits<int>::max()
     * Default value: 30
     */
    int m_3DRT_BevelHeight_um;

    /**
     * 3D-Viewer raytracing factor applied to Extent.z of the item layer.
     *
     * This is used for calculating the bevel's height.
     *
     * Setting name: "V3DRT_BevelExtentFactor"
     * Valid values: 0 to 100
     * Default value: 1/16
     */
    double m_3DRT_BevelExtentFactor;

    /**
     * Use the 3DConnexion Driver.
     *
     * Setting name: "3DConnexionDriver"
     * Valid values: 0 or 1
     * Default value: 1
     */
    bool m_Use3DConnexionDriver;

    /**
     * Use the new incremental netlister for realtime jobs.
     *
     * Setting name: "IncrementalConnectivity"
     * Valid values: 0 or 1
     * Default value: 1
     */
    bool m_IncrementalConnectivity;

    /**
     * The number of milliseconds to wait in a click before showing a disambiguation menu.
     *
     * Setting name: "DisambiguationTime"
     * Valid values: 50 or 10000
     * Default value: 300
     */
    int m_DisambiguationMenuDelay;

    /**
     * Enable the new PCB Design Blocks feature
     *
     * Setting name: "EnablePcbDesignBlocks"
     * Valid values: true or false
     * Default value: false
     */
    bool m_EnablePcbDesignBlocks;

    /**
     * Enable support for generators.
     *
     * Setting name: "EnableGenerators"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_EnableGenerators;

    /**
     * KiCad Next: build every frame's menu bar from the shared common-root builder
     * (EDA_BASE_FRAME::buildCommonMenuBar) instead of the legacy per-frame menu code.
     * Additive/reversible: when 0, each frame's original doReCreateMenuBar() runs unchanged.
     *
     * Setting name: "UnifiedMenuBar"
     * Valid values: 0 or 1
     * Default value: 1 (ships ON; set 0 in kicad_advanced to opt out)
     */
    bool m_UnifiedMenuBar;

    /**
     * KiCad Next: regroup the unified menu bar into the Altium-style top-level set
     * (File / Edit / View / Project / Place / Design / Route / Reports / Tools / Window,
     * with the Preferences items folded into the tail of Tools until the title-bar gear
     * button hosts them).  Implies the unified menu-bar path even when "UnifiedMenuBar"
     * is 0.  Every menu item still fires the stock action; only its top-level grouping
     * changes, so the classic KiCad grouping returns unchanged when this is 0.
     *
     * Setting name: "ModernMenuLayout"
     * Valid values: 0 or 1
     * Default value: 1 (ships ON; set 0 in kicad_advanced to opt out)
     */
    bool m_ModernMenuLayout;

    /**
     * KiCad Next: Altium-style ("Active Bar") toolbar preset.  The left and right editor
     * toolbars are dropped; the drawing / routing tools fold into grouped palette buttons at
     * the end of the top toolbar, and the display toggles they carried surface in the View
     * menu when ModernMenuLayout is also set.  Independent of ModernMenuLayout so either can
     * be trialled alone.  When 0 the classic three-sided toolbar frame is used.  Only the
     * built-in defaults change: user-customised toolbars (Preferences > Toolbars) always win.
     *
     * Setting name: "ModernToolbarLayout"
     * Valid values: 0 or 1
     * Default value: 1 (ships ON; set 0 in kicad_advanced to opt out)
     */
    bool m_ModernToolbarLayout;

    /**
     * KiCad Next: open the auxiliary tools (Gerber Viewer, Image Converter, PCB
     * Calculator, Drawing Sheet Editor) as in-process KIWAY players instead of
     * spawning a separate executable, so they live in one process (one KIWAY /
     * one AI brain) and can be re-hosted as tabs in the project-manager shell.
     * Additive/reversible: when 0, the legacy separate-process launch runs
     * unchanged. This is Layer A of the single-window shell; the tab host
     * (Layer B) keys off the same flag.
     *
     * Setting name: "SingleWindowShell"
     * Valid values: 0 or 1
     * Default value: 1 (ships ON; set 0 in kicad_advanced to opt out)
     */
    bool m_SingleWindowShell;

    /**
     * KiCad Next: host a single AI chat panel in the project-manager shell (Cursor
     * style) instead of one panel per editor frame. When set together with
     * SingleWindowShell, the per-editor AI panels are suppressed and the shell owns
     * the only panel, retargeting it to whichever editor tab is active.
     * Additive/reversible: when 0, every editor keeps its own AI panel unchanged.
     *
     * Setting name: "CommonAiPanel"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_CommonAiPanel;

    /**
     * KiCad Next: in the single-window shell, mirror the active editor tab's status bar
     * (cursor X/Y, dx/dy, grid, units, zoom, current tool, constraints) into the shell's
     * own footer, which is otherwise the Project Manager's 2-field bar with no coordinates.
     * Needs SingleWindowShell. Additive/reversible: when 0, the shell footer is the original
     * 2-field bar and each docked editor's own footer stays hidden (the prior behaviour).
     *
     * Setting name: "UnifiedStatusBar"
     * Valid values: 0 or 1
     * Default value: 1 (ships ON; set 0 in kicad_advanced to opt out)
     */
    bool m_UnifiedStatusBar;

    /**
     * Single-window shell: hoist the ACTIVE editor tab's top toolbar (the "Standard" toolbar +
     * its aux row) OUT of the tab and into a shell strip ABOVE the editor tab bar, so the layout
     * order matches Altium: Title -> Menu -> Toolbar -> Tabs -> Workspace.  When 0, each editor's
     * top toolbar stays inside its tab (below the tab strip) as before.  Needs SingleWindowShell.
     * Additive/reversible.  Tool dispatch is preserved because ACTION_TOOLBAR holds a direct
     * pointer to the editor's TOOL_MANAGER, so reparenting the widget does not break its buttons.
     *
     * Setting name: "UnifiedToolbar"
     * Valid values: 0 or 1
     * Default value: 1 (ships ON; set 0 in kicad_advanced to opt out)
     */
    bool m_UnifiedToolbar;

    /**
     * Enable option to load lib files with text editor.
     *
     * Setting name: "EnableLibWithText"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_EnableLibWithText;

    /**
     * Enable option to open lib file directory.
     * Reveals one additional field under common preferences to set
     * system's file manager command in order for the context menu options to work.
     * On windows common settings preselect the default explorer with a hardcoded value.
     *
     * Examples,
     * Linux:  "nemo -n %F"
     *         "nautilus --browser %F"
     *         "dolphin --select %F" etc
     * Win11:  "explorer.exe /n,/select,%F"
     *
     * Setting name: "EnableLibDir"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_EnableLibDir;

    /**
     * Board object selection visibility limit.
     *
     * This ratio is used to determine if an object in a selected object layer stack is
     * visible.  All alpha ratios less or equal to this value are considered invisible
     * to the user and will be pruned from the list of selections.  Valid values are
     * between 0 and less than 1. A value of 1 disables this feature.  Reasonable values
     * are between 0.01 and 0.03 depending on the layer colors.
     *
     * Setting name: "PcbSelectionVisibilityRatio"
     * Valid values: 0.0 to 1.0
     * Default value: 1
     */
    double m_PcbSelectionVisibilityRatio;

    /**
     * Deviation between font's bezier curve ideal and the poligonized curve.  This
     * is 1/16 of the font's internal units.
     *
     * Setting name: "FontErrorSize"
     * Valid values: 0.01 to 100
     * Default value: 2
     */
    double m_FontErrorSize;

    /**
     * OCE (STEP/IGES) 3D Plugin Tesselation Linear Deflection
     *
     * Linear deflection determines the maximum distance between the original geometry
     * and the tessellated representation, measured in millimeters (mm), influencing
     * the precision of flat surfaces.
     *
     * Setting name: "OcePluginLinearDeflection"
     * Valid values: 0.01 to 1.0
     * Default value: 0.14
     */
    double m_OcePluginLinearDeflection;

    /**
     * OCE (STEP/IGES) 3D Plugin Tesselation Angular Deflection
     *
     * Angular deflection specifies the maximum deviation angle (in degrees) between
     * the normals of adjacent facets in the tessellated model. Lower values result
     * in smoother curved surfaces by creating more facets to closely approximate
     * the curve.
     *
     * Setting name: "OcePluginAngularDeflection"
     * Valid values: 0.1 to 180
     * Default value: 30
     */
    double m_OcePluginAngularDeflection;

    /**
     * The number of internal units that will be allowed to deflect from the base
     * segment when creating a new segment.
     *
     * Setting name: "TriangulateSimplificationLevel"
     * Valid values: 5 to 1000
     * Default value: 50
    */
    int m_TriangulateSimplificationLevel;

    /**
     * The minimum area of a polygon that can be left over after triangulation and
     * still consider the triangulation successful.  This is internal units, so
     * it is square nm in pcbnew.
     *
     * Setting name: "TriangulateMinimumArea"
     * Valid values: 25 to 100000
     * Default value: 1000
     */
    int m_TriangulateMinimumArea;

    /**
     * Enable the use of a cache-friendlier and therefore faster version of the
     * polygon fracture algorithm.
     *
     * Setting name: "EnableCacheFriendlyFracture"
     * Valid values: 0 or 1
     * Default value: 1
     */
    bool m_EnableCacheFriendlyFracture;

    /**
     * Log IPC API requests and responses
     *
     * Setting name: "EnableAPILogging"
     * Default value: false
     */
    bool m_EnableAPILogging;

    /**
     * Maximum number of filesystem watchers to use.
     *
     * Setting name: "MaxFilesystemWatchers"
     * Valid values: 0 to 2147483647
     * Default value: 16384
     */
    int m_MaxFilesystemWatchers;

    /**
     * Set the number of items in a schematic graph for it to be considered "minor"
     *
     * Setting name: "MinorSchematicGraphSize"
     * Valid values: 0 to 2147483647
     * Default value: 10000
     */
    int m_MinorSchematicGraphSize;

    /**
     * The number of recursions to resolve text variables.
     *
     * Setting name: "ResolveTextRecursionDepth"
     * Valid values: 0 to 10
     * Default value: 3
     */
    int m_ResolveTextRecursionDepth;

    /**
     * Enable snap anchors based on item line extensions.
     *
     * This should be removed when extension snaps are tuned up.
     *
     * Setting name: "EnableExtensionSnaps"
     * Default value: true
     */
    bool m_EnableExtensionSnaps;

    /**
     * If extension snaps are enabled, this is the timeout in milliseconds
     * before a hovered item gets extensions shown.
     *
     * This should be removed if a good value is agreed, or made configurable
     * if there's no universal good value.
     *
     * Setting name: "EnableExtensionSnapsMs"
     * Default value: 500
     * Valid values: >0
     */
    int m_ExtensionSnapTimeoutMs;

    /**
     * If extension snaps are enabled, 'activate' items on
     * hover, even if not near a snap point.
     *
     * This just to experiment with tuning.  It should either
     * be removed or made configurable when we know what feels best.
     *
     * Setting name: "ExtensionSnapActivateOnHover"
     * Default value: true
     */
    bool m_ExtensionSnapActivateOnHover;

    /**
     * Enable snap anchors debug visualization.
     *
     * Setting name: "EnableSnapAnchorsDebug"
     * Default value: false
     */
    bool m_EnableSnapAnchorsDebug;

    /**
     * Hysteresis in pixels used for snap activation and deactivation.
     *
     * Setting name: "SnapHysteresis"
     * Default value: 5
     * Valid values: 0 to 100
     */
    int m_SnapHysteresis;

    /**
     * Margin multiplier for preferring anchors over construction line snaps.
     * When an anchor is within (distance * margin) of a construction line snap,
     * the anchor will be preferred.
     *
     * Setting name: "SnapToAnchorMargin"
     * Default value: 1.1
     * Valid values: 1.0 to 2.0
     */
    double m_SnapToAnchorMargin;

    /**
     * Minimum overlapping angle for which an arc is considered to be parallel
     * to its paired arc.
     *
     * Setting name: "MinParallelAngle"
     * Default value: 0.001
     */
    double m_MinParallelAngle;

    /**
     * What factor to use when painting via and PTH pad hole walls, so that
     * the painted hole wall can be overemphasized compared to physical reality
     * to make the wall easier to see on-screen.
     *
     * Setting name: "HoleWallPaintingMultiplier"
     * Default value: 1.5
     */
    double m_HoleWallPaintingMultiplier;

    /**
     * Default value for the maximum number of threads to use for parallel processing.
     * Setting this value to 0 or less will mean that we use the number of cores available
     *
     * Setting name: "MaximumThreads"
     * Default value: 0
     */
    int m_MaximumThreads;

    /**
     * When updating the net inspector, it either recalculates all nets or iterates through items
     * one-by-one. This value controls the threshold at which all nets are recalculated rather than
     * iterating over the items.
     *
     * Setting name: "NetInspectorBulkUpdateOptimisationThreshold"
     * Default value: 25
     */
    int m_NetInspectorBulkUpdateOptimisationThreshold;

    /**
     * The line width in mils for the exclude from simulation outline.
     *
     * Setting name: "ExcludeFromSimulationLineWidth"
     * Default value: 25
     */
    int m_ExcludeFromSimulationLineWidth;

    /**
     * Maximum number of tuner combinations simulated when using multi-run mode.
     *
     * Setting name: "SimulatorMultiRunCombinationLimit"
     * Valid values: 1 to 100
     * Default value: 12
     */
    int m_SimulatorMultiRunCombinationLimit;

    /**
     * The interval in milliseconds to refresh the git icons in the project tree.
     *
     * Setting name: "GitIconRefreshInterval"
     * Default value: 10000
     */
    int m_GitIconRefreshInterval;

    /**
     * Set the maximum number of characters that can be pasted without warning.  Long
     * text strings can cause the application to freeze for a long time and are probably
     * not what the user intended.
     *
     * Setting name: "MaxPastedTextLength"
     * Default value: 100
     */
    int m_MaxPastedTextLength;

    /**
     * Timeout for the PNS router's processCluster wallclock timeout, in milliseconds.
     *
     * Setting name: "PNSProcessClusterTimeoutMs"
     * Valid values: 10 to 10000
     * Default value: 100
     */
    int m_PNSProcessClusterTimeout;

    /**
     * Timeout for the PNS router's followBranch path search, in milliseconds.
     *
     * This limits how long the router will spend searching for the longest path
     * through a complex track topology before returning the best path found so far.
     *
     * Setting name: "FollowBranchTimeoutMs"
     * Valid values: 50 to 5000
     * Default value: 500
     */
    int m_FollowBranchTimeout;

    /**
     * Skip importing component bodies when importing some format files, such as Altium.
     *
     * This can be used to drastically speed up the import when testing
     * import of boards when the bodies are not needed.
     *
     * Setting name: "ImportSkipComponentBodies"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_ImportSkipComponentBodies;

    /**
     * Skip the layer mapping step when importing.
     *
     * This can be convenient to speed up imports when testing other aspects of the import,
     * as you don't need to interact with the layer mapping dialog.
     *
     * Setting name: "ImportSkipLayerMapping"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_ImportSkipLayerMapping;

    /**
     * Screen DPI setting for display calculations.
     *
     * This setting controls the assumed screen DPI for various display calculations.
     * Can be used to adjust sizing for high-DPI displays or unusual screen configurations.
     *
     * Setting name: "ScreenDPI"
     * Valid values: 50 to 500
     * Default value: 91
     */
    int m_ScreenDPI;

    /**
     * Enable use Aui Perspective to store/load geometry of main editor frames.
     * the saved prms are position/size of toolbars and some other widgets
     *
     * Setting name: "EnableUseAuiPerspective"
     * Valid values: 0 or 1
     * Default value: 1
     */
    bool m_EnableUseAuiPerspective;

    /**
     * Stale lock timeout for local history repository locks, in seconds.
     *
     * When a KiCad process crashes while holding a lock on the .history repository,
     * the lock file remains. This setting controls how old a lock file must be
     * before it is considered "stale" and can be automatically removed.
     *
     * Setting name: "HistoryLockStaleTimeout"
     * Valid values: 10 to 86400 (10 seconds to 24 hours)
     * Default value: 300 (5 minutes)
     */
    int m_HistoryLockStaleTimeout;

    /**
     * PADS text height scale factor for PCB imports.
     * PADS text height includes leading/descender; multiply by this to get
     * character cell height.
     *
     * Setting name: "PadsPcbTextHeightScale"
     * Valid values: 0.1 to 1.0
     * Default value: 0.69
     */
    double m_PadsPcbTextHeightScale;

    /**
     * PADS text width scale factor for PCB imports.
     *
     * Setting name: "PadsPcbTextWidthScale"
     * Valid values: 0.1 to 1.0
     * Default value: 0.64
     */
    double m_PadsPcbTextWidthScale;

    /**
     * PADS text height scale factor for schematic imports.
     *
     * Setting name: "PadsSchTextHeightScale"
     * Valid values: 0.1 to 1.0
     * Default value: 0.50
     */
    double m_PadsSchTextHeightScale;

    /**
     * PADS text width scale factor for schematic imports.
     *
     * Setting name: "PadsSchTextWidthScale"
     * Valid values: 0.1 to 1.0
     * Default value: 0.46
     */
    double m_PadsSchTextWidthScale;

    /**
     * PADS text anchor offset in nanometers for PCB imports.
     * Compensates for the difference between PADS and KiCad text anchor positions.
     *
     * Setting name: "PadsTextAnchorOffsetNm"
     * Valid values: 0 to 1000000
     * Default value: 350000
     */
    int m_PadsTextAnchorOffsetNm;

    /**
     * Minimum object size in nanometers for PCB imports.
     * Any dimension (pad size, track width, via size, circle radius) that would be
     * smaller than this value is clamped to it, preventing zero-size objects that
     * can crash the renderer or cause division-by-zero in DRC.
     *
     * Setting name: "PcbImportMinObjectSizeNm"
     * Valid values: 100 to 1000000
     * Default value: 1000
     */
    int m_PcbImportMinObjectSizeNm;

    /**
     * Enable iterative zone filling to handle isolated islands in higher priority zones.
     *
     * When enabled, zones are filled in priority batches. After each batch, isolated islands
     * are identified and removed, then lower priority zones are refilled to occupy the newly
     * available space. This fixes issue 21746 where lower priority zones incorrectly knock
     * out areas that should fill after higher priority zone islands are removed.
     *
     * Setting name: "ZoneFillIterativeRefill"
     * Valid values: true or false
     * Default value: true
     */
    bool m_ZoneFillIterativeRefill;

    /**
     * Router test case directory.
     * 
     * Directory where the router stores the test cases (the '0' key dump)
     * Used to make creating test cases easier (a simple dialog instead of manually copying files)
     * 
     * Setting name: "RouterTestCaseDirectory"
     * Valid values: directory name
     * Default value: ""
     */
    wxString m_RouterTestCaseDirectory;

    wxString m_traceMasks; ///< Trace masks for wxLogTrace, loaded from the config file.

    /**
     * KiCad Next single-window shell: after the project-manager window is up, warm the
     * heavy editor KIFACEs (Symbol / Footprint / Gerber / Drawing-Sheet) in the
     * background, one at a time on the GUI thread, so the user's first click on one is
     * instant instead of "loading the whole app".  Only has any effect when
     * SingleWindowShell is also set; additive/reversible.
     *
     * OPT-IN (default 0): warming the Symbol/Footprint editors enumerates the full library
     * set synchronously on the GUI thread, which freezes the window during startup instead
     * of truly running in the background — so this is disabled until a non-blocking warm is
     * implemented.  Set to 1 only to experiment.
     *
     * Declared LAST in the struct on purpose: appending (instead of inserting mid-struct)
     * keeps every existing member's offset stable, so editor KIFACEs that read ADVANCED_CFG
     * need not be rebuilt for this addition — only kicommon and the kicad app do.
     *
     * Setting name: "ShellPrewarmEditors"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_ShellPrewarmEditors;

    /**
     * KiCad Next: open a project-tree file (schematic, PCB, ...) on a single mouse click
     * instead of requiring a double-click — VS Code / Cursor style.  Double-click keeps
     * working unchanged (it goes through the existing activate path); this only adds the
     * single-click open, so it is additive/reversible.  Directories and the +/- expand
     * button are left to the default handler.
     *
     * Declared LAST in the struct (after ShellPrewarmEditors) on purpose: see the ABI note
     * above — appending keeps every existing member's offset stable, so editor KIFACEs that
     * read ADVANCED_CFG need not be rebuilt for this addition.
     *
     * Setting name: "SingleClickOpen"
     * Valid values: 0 or 1
     * Default value: 1
     */
    bool m_SingleClickOpen;

    /**
     * KiCad Next / Anvil: folder-based symbol libraries (`*.kicad_symdir`, one `*.kicad_sym`
     * file per symbol) are loaded by opening and parsing every file in the directory on the
     * GUI thread.  With the Anvil library set (~223 libs / ~22,800 files) that synchronous
     * enumeration freezes the window ("Not Responding") on the first Symbol Chooser / editor
     * load — and a real-time antivirus scanning each opened file amplifies it badly.
     *
     * When enabled, a per-library consolidated cache (one `*.kicad_sym` aggregate + a small
     * manifest, kept in a sibling `.anvil_symcache/` folder) is read on load instead of the
     * thousands of individual files, collapsing N file-opens to 1.  The cache is keyed on the
     * directory's content fingerprint (the same TimestampDir value the staleness check already
     * uses), so any add/remove/edit — including edits made out-of-process by the Anvil Python
     * backend — invalidates it and triggers a one-time rebuild.  Purely a read accelerator:
     * symbol writes still go to the individual per-symbol files, and the manifest preserves the
     * original file grouping so saving is byte-identical.
     *
     * Declared LAST in the struct (after SingleClickOpen) on purpose: see the ABI note above —
     * appending keeps every existing member's offset stable, so editor KIFACEs that read
     * ADVANCED_CFG need not be rebuilt for this addition (only kicommon + eeschema, which reads
     * the flag in the symbol cache loader).
     *
     * OPT-IN (default 0) so behaviour is byte-identical until explicitly enabled.
     *
     * Setting name: "SymDirAggregateCache"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_SymDirAggregateCache;

    /**
     * KiCad Next / Anvil: paint the schematic editor's window chrome (the "frame": dockable
     * panel backgrounds, sashes/borders/captions, the AUI tool-bars and child controls) with
     * the Anvil "Vibrant Purple & Indigo" dark palette, instead of the OS/native colours.
     * Scoped to the schematic editor only — the project manager and the other editors are left
     * untouched.  The drawing canvas (the "screen") is intentionally NOT recoloured here; that
     * is driven by the colour theme and handled separately.
     *
     * Declared LAST in the struct (after SymDirAggregateCache) on purpose: see the ABI note
     * above — appending keeps every existing member's offset stable, so editor KIFACEs that read
     * ADVANCED_CFG need not be rebuilt for this addition (only kicommon + eeschema, which reads
     * the flag when building the schematic frame).
     *
     * SHIPS ON (default 1).  It is not really optional any more: the shell's custom title bar
     * paints its menu labels, quick-access glyphs and window controls in the ANVIL palette
     * (BONE text on a graphite face) whatever this flag says, so with the flag OFF the title bar
     * keeps the light OS menubar colour and the near-white labels become unreadable
     * light-on-light.  Set 0 only to compare against stock KiCad chrome.
     *
     * Setting name: "AnvilPurpleFrame"
     * Valid values: 0 or 1
     * Default value: 1
     */
    bool m_AnvilPurpleFrame;

    /**
     * KiCad Next: when ON, recolor the toolbar/menu icon set from KiCad blue to the NEMI emerald
     * hue at load time (a hue-selective remap in BITMAP_STORE::getImage).  Ships OFF: Anvil's icon
     * identity is now rich MULTI-COLOUR — the native full-colour palette for the general icon set
     * plus custom multi-colour editor icons (icon_eeschema/pcbnew/etc.).  Set 1 to opt back into the
     * single-emerald recolor.  Applied at startup (already-built bitmap bundles are cached), so this
     * is a restart-to-apply flag.
     *
     * Setting name: "AnvilEmeraldIcons"
     * Valid values: 0 or 1
     * Default value: 0 (ships OFF — multi-colour)
     */
    bool m_AnvilEmeraldIcons;

    /**
     * Anvil: tooltip popup delay in milliseconds for toolbar/control hover names.  The OS default
     * (~1s on Windows) feels sluggish when identifying toolbar icons; Anvil ships a snappier 200ms.
     * 0 shows tooltips instantly; -1 keeps the OS default delay.
     *
     * Setting name: "AnvilTooltipDelay"
     * Valid values: -1 to 5000
     * Default value: 200
     */
    int m_AnvilTooltipDelayMs;

    /**
     * KiCad Next / Anvil: ungroup toolbar tool-groups into individual buttons.  KiCad packs related
     * tools (route/via/tune, line/arc/rectangle/circle/polygon, dimensions, ...) into a single
     * button with a ">>" group dropdown; when this is on, ApplyConfiguration() adds every group
     * member as its OWN button instead, so all tools are visible at once with no group dropdowns.
     * Additive/reversible: set 0 for the stock grouped toolbars.
     *
     * Setting name: "AnvilFlatToolbars"
     * Valid values: 0 or 1
     * Default value: 1 (ships ON)
     */
    bool m_AnvilFlatToolbars;

    /**
     * KiCad Next / Anvil: base point size for the application UI font (menus' dropdowns, side
     * panels, project tree, status bars, dialogs — every wx control that derives its font from
     * the window font).  Applied once on each EDA_BASE_FRAME and DIALOG_SHIM at construction;
     * the KIUI font helpers (GetControlFont / GetInfoFont / ...) then derive from it, so the
     * whole UI follows.  The drawing canvas (schematic / PCB content) uses the GAL font stack,
     * not wxFont, and is intentionally untouched.  The single-window shell's top title-bar menu
     * sets its own absolute size and so is deliberately NOT governed by this value.
     *
     * Declared LAST in the struct (after AnvilPurpleFrame) on purpose: see the ABI note above —
     * appending keeps every existing member's offset stable, so the editor KIFACEs need not be
     * rebuilt for this addition (only kicommon, which owns EDA_BASE_FRAME / DIALOG_SHIM).
     *
     * Setting name: "AnvilUiFontPt"
     * Valid values: a point size, e.g. 10.0.  0 (or less) disables the override (native sizes).
     * Default value: 10.0
     */
    double m_AnvilUiFontPt;

    /**
     * NEMI brand UI font families applied app-wide alongside m_AnvilUiFontPt.
     * m_AnvilUiFontFace   - proportional UI face (default "Space Grotesk")
     * m_AnvilMonoFontFace - monospaced UI face (default "IBM Plex Mono")
     * An empty string leaves the inherited OS face untouched.
     */
    wxString m_AnvilUiFontFace;
    wxString m_AnvilMonoFontFace;

    /**
     * KiCad Next / Anvil: consolidated read cache for *.pretty footprint folder libraries — the
     * footprint twin of SymDirAggregateCache.  The PCB editor's first footprint load opens and
     * parses every *.kicad_mod in every *.pretty library on the loader thread; on a large library
     * (e.g. 155 folders / 15k files) with real-time antivirus scanning each open, that load blocks
     * long enough that the window shows "Not Responding".  When enabled, FP_CACHE::Load() reads a
     * single consolidated cache file per library (fingerprinted by the same directory timestamp it
     * already uses for staleness), collapsing N file-opens to 1 and removing the per-open AV scan.
     * Pure read accelerator: writes still go to the individual *.kicad_mod files; a stale or
     * missing cache transparently falls back to the per-file scan and rebuilds.
     *
     * Declared LAST in the struct (after AnvilUiFontPt) on purpose: see the ABI note above —
     * appending keeps every existing member's offset stable, so only kicommon + pcbnew (which
     * reads the flag in the footprint cache loader) need rebuilding for this addition.
     *
     * OPT-IN (default 0) so behaviour is byte-identical until explicitly enabled.
     *
     * Setting name: "FpDirAggregateCache"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_FpDirAggregateCache;

    /**
     * KiCad Next / Anvil: self-heal the global symbol AND footprint library tables on load.  A
     * shipped install seeds each global table as a single nested "KiCad" row pointing at the stock
     * template (share/kicad/template/sym-lib-table | fp-lib-table).  If that template is missing,
     * empty, or was replaced by an older/broken installer, the table flattens to ZERO libraries and
     * the symbol chooser shows "0 items loaded" / the footprint preview shows "Footprint not found"
     * even though the library files are present on disk.  When enabled, after the global tables are
     * loaded LIBRARY_MANAGER checks whether each of the global SYMBOL and FOOTPRINT tables resolves
     * to any library at all; if one resolves to none, it rebuilds that user table directly from the
     * matching installed stock directory — one ${KICADxx_SYMBOL_DIR}/<lib> row per *.kicad_symdir /
     * *.kicad_sym for symbols, one ${KICADxx_FOOTPRINT_DIR}/<lib> row per *.pretty for footprints —
     * backing up the broken table first, then reloads it.
     *
     * Strictly additive: the rebuild fires ONLY when a table already yields zero libraries, so a
     * working install is never touched (no-op).  Reversible — the broken table is preserved as
     * <name>.broken.bak beside it.
     *
     * Declared LAST in the struct (after FpDirAggregateCache) on purpose: see the ABI note above —
     * appending keeps every existing member's offset stable, so only kicommon (which owns
     * LIBRARY_MANAGER and reads this flag) needs rebuilding for this addition.
     *
     * Setting name: "LibTableSelfHeal"
     * Valid values: 0 or 1
     * Default value: 1
     */
    bool m_LibTableSelfHeal;

    /**
     * KiCad Next / Anvil: before a symbol picked from the library chooser is placed on the
     * sheet, or before a brand-new symbol is finalized in the symbol editor, show a small
     * confirmation dialog asking (a) Through-Hole vs Surface-Mount package preference and
     * (b) which candidate footprint/library to use for that part. Asked once per distinct
     * LIB_ID per schematic-editor session (subsequent placements of the same part reuse the
     * earlier answer silently); does not re-ask on every instance placed.
     *
     * Declared LAST in the struct (after LibTableSelfHeal) on purpose: see the ABI note above —
     * appending keeps every existing member's offset stable, so only kicommon + eeschema (which
     * read this flag) need rebuilding for this addition.
     *
     * OPT-IN (default 0) so symbol placement is byte-identical to stock KiCad until enabled.
     *
     * Setting name: "ConfirmComponentPackage"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_ConfirmComponentPackage;

    /**
     * KiCad Next / Anvil: VSCode-style "auto save to the real file".  Stock KiCad's autosave
     * timer commits the editor state to the git-backed .history snapshot store, NOT to the
     * actual .kicad_sch / .kicad_pcb the AI backend reads.  So a user's MANUAL edits stay in the
     * editor's memory (or only in .history) until an explicit Ctrl+S, and the AI — which reads the
     * real project files each turn — never sees them.  When enabled, the existing autosave timer
     * (already armed on every content change) instead writes the user's current in-memory design
     * straight to the real project file on disk (like VSCode files.autoSave=afterDelay) and SKIPS
     * the .history snapshot.  Result: the AI observes manual KiCad edits automatically, the
     * Cursor/Claude-Code way (file on disk = source of truth, read live), with no snapshot and no
     * per-turn "update" click.  Read by SCH_EDIT_FRAME::doAutoSave / PCB_EDIT_FRAME::DoAutoSave;
     * EDA_BASE_FRAME::GetAutoSaveInterval returns a short positive default when this is on so the
     * timer is guaranteed to arm.
     *
     * Declared LAST in the struct (after ConfirmComponentPackage) on purpose: see the ABI note
     * above — appending keeps every existing member's offset stable, so only kicommon + the two
     * editor KIFACEs (which read the flag in their autosave hooks) need rebuilding for this.
     *
     * OPT-IN (default 0) so autosave is byte-identical to stock KiCad (.history snapshot) until
     * explicitly enabled.
     *
     * Setting name: "AnvilAutoSaveRealFile"
     * Valid values: 0 or 1
     * Default value: 0
     */
    bool m_AnvilAutoSaveRealFile;

    /**
     * NEMI brand: point size of the single-window shell's top title-bar menu buttons.  The menu
     * bar is deliberately a bit larger than the app-wide UI size (m_AnvilUiFontPt) so it reads as
     * the primary navigation; this makes that size a knob instead of a hardcoded literal.  The
     * face still comes from m_AnvilUiFontFace.
     *
     * Declared LAST in the struct on purpose: see the ABI note above — appending keeps every
     * existing member's offset stable.
     *
     * Setting name: "AnvilMenuFontPt"
     * Valid values: a point size, e.g. 11.0.  0 (or less) falls back to m_AnvilUiFontPt.
     * Default value: 11.0
     */
    double m_AnvilMenuFontPt;

    /**
     * Anvil: point size for the custom title-bar glyph buttons (save/undo/redo, +, gear,
     * min/max/close).  At the app UI size (10pt) the caption glyphs read noticeably smaller
     * than the 24px toolbar icons below them, so the title bar ships with a larger dedicated
     * size.  0 (or less) falls back to tracking m_AnvilUiFontPt as before.
     *
     * Declared LAST in the struct on purpose: see the ABI note above — appending keeps every
     * existing member's offset stable (only kicommon + the kicad shell read this).
     *
     * Setting name: "AnvilTitlebarGlyphPt"
     * Valid values: a point size.  0 disables (track AnvilUiFontPt).
     * Default value: 13.0
     */
    double m_AnvilTitlebarGlyphPt;
    ///@}

private:
    ADVANCED_CFG();

    ADVANCED_CFG( ADVANCED_CFG&& other ) = default;

    /**
     * Load the config from the normal configuration file.
     */
    void loadFromConfigFile();

    /**
     * Load config from the given configuration base.
     */
    void loadSettings( wxConfigBase& aCfg );

    std::vector<std::unique_ptr<PARAM_CFG>> m_entries;
};
