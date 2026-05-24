if( NOT EXISTS "F:/NimX-CAD/kicad-source-mirror/install_manifest.txt" )
    message( FATAL_ERROR "Cannot find install manifest: \"F:/NimX-CAD/kicad-source-mirror/install_manifest.txt\"" )
endif()

file( READ "F:/NimX-CAD/kicad-source-mirror/install_manifest.txt" files )
string( REGEX REPLACE "\n" ";" files "${files}" )

foreach( file ${files} )
    message( STATUS "Uninstalling \"$ENV{DESTDIR}${file}\"" )
    if( EXISTS "$ENV{DESTDIR}${file}" )
        exec_program(
            "C:/Program Files/CMake/bin/cmake.exe" ARGS "-E remove \"$ENV{DESTDIR}${file}\""
            OUTPUT_VARIABLE rm_out
            RETURN_VALUE rm_retval
            )
        if( NOT "${rm_retval}" STREQUAL "0" )
            message( STATUS "Problem when removing \"$ENV{DESTDIR}${file}\"" )
        endif()
    else()
        message( STATUS "File \"$ENV{DESTDIR}${file}\" does not exist." )
    endif()
endforeach()
