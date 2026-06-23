# Add all the warnings to the files
if( COMPILER_SUPPORTS_WARNINGS )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARN_FLAGS_CXX}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${WARN_FLAGS_C}")
endif()

add_compile_definitions( BITMAP_2_CMP )

include_directories( BEFORE ${INC_BEFORE} )
include_directories( ${INC_AFTER} )

# Sources that make up the bitmap2component kiface module (everything except the
# thin single_top launcher and the kiface entry point bitmap2cmp_main.cpp).
set( BITMAP2COMPONENT_SRCS
    bitmap2cmp_control.cpp
    bitmap2cmp_settings.cpp
    bitmap2component.cpp
    bitmap2cmp_frame.cpp
    bitmap2cmp_panel.cpp
    bitmap2cmp_panel_base.cpp
    )

if( WIN32 )
    if( MINGW )
        # BITMAP2COMPONENT_RESOURCES variable is set by the macro.
        mingw_resource_compiler( bitmap2component )
    else()
        set( BITMAP2COMPONENT_RESOURCES ${CMAKE_SOURCE_DIR}/resources/msw/bitmap2component.rc )
    endif()
endif()

if( APPLE )
    # setup bundle
    set( BITMAP2COMPONENT_RESOURCES bitmap2component.icns )
    set_source_files_properties( "${CMAKE_CURRENT_SOURCE_DIR}/bitmap2component.icns" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
        )
    set( MACOSX_BUNDLE_ICON_FILE bitmap2component.icns )
    set( MACOSX_BUNDLE_GUI_IDENTIFIER org.kicad.kicad )
    set( MACOSX_BUNDLE_NAME bitmap2component )
endif()

# The thin top-level launcher executable.  With BUILD_KIWAY_DLL it loads the kiface
# DLL at runtime (exactly like pcb_calculator / gerbview), so the very same code can
# be hosted in-process by the project manager and docked as a tab.
add_executable( bitmap2component WIN32 MACOSX_BUNDLE
    ${CMAKE_SOURCE_DIR}/common/single_top.cpp
    ${BITMAP2COMPONENT_RESOURCES}
    )

set_source_files_properties( ${CMAKE_SOURCE_DIR}/common/single_top.cpp PROPERTIES
    COMPILE_DEFINITIONS     "TOP_FRAME=FRAME_BM2CMP;BUILD_KIWAY_DLL"
    )

target_link_libraries( bitmap2component
    kicommon
    ${wxWidgets_LIBRARIES}
    )

target_link_options( bitmap2component PRIVATE
    $<$<BOOL:${KICAD_MAKE_LINK_MAPS}>:-Wl,--cref,-Map=bitmap2component.map>
    )

# The main bitmap2component program, in DSO (kiface) form, so the project manager
# can load it in-process via KIWAY (FACE_BMP2CMP -> "_bitmap2component") and dock it
# as a tab instead of spawning a separate window.
add_library( bitmap2component_kiface MODULE
    bitmap2cmp_main.cpp
    ${BITMAP2COMPONENT_SRCS}
    )

set_target_properties( bitmap2component_kiface PROPERTIES
    OUTPUT_NAME     bitmap2component
    PREFIX          ${KIFACE_PREFIX}
    SUFFIX          ${KIFACE_SUFFIX}
    )

target_link_libraries( bitmap2component_kiface
    common
    kicommon
    potrace
    ${wxWidgets_LIBRARIES}
    )

set_source_files_properties( bitmap2cmp_main.cpp PROPERTIES
    # The KIFACE is in bitmap2cmp_main.cpp, export it:
    COMPILE_DEFINITIONS     "BUILD_KIWAY_DLL;COMPILING_DLL"
    )

target_link_options( bitmap2component_kiface PRIVATE
    $<$<BOOL:${KICAD_MAKE_LINK_MAPS}>:-Wl,--cref,-Map=_bitmap2component.kiface.map>
    )

# if building bitmap2component, then also build bitmap2component_kiface if out of date.
add_dependencies( bitmap2component bitmap2component_kiface )

# these 2 binaries are a matched set, keep them together
if( APPLE )
    set_target_properties( bitmap2component PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${PROJECT_BINARY_DIR}/bitmap2component/Info.plist
        )

    # puts binaries into the *.app bundle while linking
    set_target_properties( bitmap2component_kiface PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${OSX_BUNDLE_BUILD_KIFACE_DIR}
        )

    set_target_properties( bitmap2component PROPERTIES INSTALL_RPATH
            "@executable_path/../Frameworks;@executable_path/../Frameworks/Python.framework" )
    set_target_properties( bitmap2component_kiface PROPERTIES INSTALL_RPATH
            "@executable_path/../Frameworks;@executable_path/../Frameworks/Python.framework" )
    set_target_properties( bitmap2component_kiface PROPERTIES BUILD_WITH_INSTALL_RPATH 1 )

    # put individual bundle outside of main bundle as a first step
    # will be pulled into the main bundle when creating main bundle
    install( TARGETS bitmap2component
        DESTINATION ${KICAD_BIN}
        COMPONENT binary
        )

    install( CODE "
        set( KICAD_CMAKE_MODULE_PATH \"${KICAD_CMAKE_MODULE_PATH}\" )
        set( KICAD_BIN \"${KICAD_BIN}\" )
        set( OSX_BUNDLE_INSTALL_BIN_DIR \"${OSX_BUNDLE_INSTALL_BIN_DIR}\" )
        set( OSX_BUNDLE_INSTALL_LIB_DIR \"${OSX_BUNDLE_INSTALL_LIB_DIR}\" )
    " )

    install( CODE [[
        include( ${KICAD_CMAKE_MODULE_PATH}/InstallSteps/InstallMacOS.cmake )

        # Install any dependencies (this will generally duplicate kicad.app but we can't be sure)
        install_runtime_deps( "${KICAD_BIN}/bitmap2component.app/Contents/MacOS/bitmap2component"
            ""
            ""
            )
    ]] )
else()
    if( MSVC )
        # version resource for the kiface DLL, if present (optional)
        if( EXISTS ${CMAKE_SOURCE_DIR}/resources/msw/bitmap2component-dll.rc )
            target_sources( bitmap2component_kiface PRIVATE ${CMAKE_SOURCE_DIR}/resources/msw/bitmap2component-dll.rc )
        endif()
    endif()

    install( TARGETS bitmap2component
        DESTINATION ${KICAD_BIN}
        COMPONENT binary
        )
    install( TARGETS bitmap2component_kiface
        DESTINATION ${KICAD_KIFACE}
        COMPONENT binary
        )
endif()

if( KICAD_WIN32_INSTALL_PDBS )
    # Get the PDBs to copy over for MSVC
    install(FILES $<TARGET_PDB_FILE:bitmap2component> DESTINATION ${KICAD_BIN})
    install(FILES $<TARGET_PDB_FILE:bitmap2component_kiface> DESTINATION ${KICAD_KIFACE})
endif()
