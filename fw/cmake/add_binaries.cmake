
# Creates C resources file from files in given directory recursively
function(add_binaries dir output)
    # Collect input files
    file(GLOB bin_paths ${dir}/*/*)
    list(LENGTH bin_paths bin_count)

    # Create empty output file
    file(WRITE ${CMAKE_BINARY_DIR}/${output}.cc "#include <cstdint>\n#include \"${output}.h\"\n\n")
    file(WRITE ${CMAKE_BINARY_DIR}/${output}.h  "#pragma once\n#include <cstdint>\n\n")

    # Iterate through input files
    foreach(bin ${bin_paths})
        # Get short filenames
        get_filename_component(filename ${bin} NAME)

        # FILE NAME ESCAPED
        string(REGEX REPLACE "[\ \\./-]" "_" filename_escaped ${filename})

        # SECTION
        get_filename_component(dir_path ${bin} DIRECTORY)
        get_filename_component(section ${dir_path} NAME)

        if(section STREQUAL "disabled") 
            continue() 
        endif()

        message(STATUS "BINARY found: ${filename}")
        message(STATUS "Section name: ${section}")

        # Read hex data from file
        file(READ ${bin} filedata HEX)
        file(SIZE ${bin} filesize)
        # Convert hex data for C compatibility
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," filedata ${filedata})
        # Append data to output file
        file(APPEND ${CMAKE_BINARY_DIR}/${output}.cc "const uint8_t  ${filename_escaped}[${filesize}] __attribute__ ((section (\".${section}\"))) = {${filedata}};\n" )
        file(APPEND ${CMAKE_BINARY_DIR}/${output}.h "extern const uint8_t  ${filename_escaped}[${filesize}];\n" )
    endforeach()
 
endfunction()
