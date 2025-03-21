
# Creates C resources file from files in given directory recursively
function(add_roms dir output)
    # Collect input files
    file(GLOB bin_paths ${dir}/*/*.rom)
    list(LENGTH bin_paths bin_count)

    # Create empty output file
    file(WRITE ${CMAKE_BINARY_DIR}/${output}.cc "#include <cstdint>\n#include \"${output}.h\"\n\n")
    file(WRITE ${CMAKE_BINARY_DIR}/${output}.h  "#pragma once\n#include <cstdint>\n\n")
    file(WRITE ${CMAKE_BINARY_DIR}/${output}.list.h "#pragma once\n#include \"${output}.h\"\nstatic const Config::Cartridge cartridge_list[${bin_count}] = {\n")

    # Iterate through input files
    foreach(bin ${bin_paths})
        # Get short filenames
        get_filename_component(filename ${bin} NAME)

        # SECTION
        get_filename_component(dir_path ${bin} DIRECTORY)
        get_filename_component(section ${dir_path} NAME)

        if(section STREQUAL "disabled") 
            continue() 
        endif()


        message(STATUS "ROM found: ${filename}")
        message(STATUS "Section name: ${section}")
        
        # ROM NAME
        string(REGEX MATCH "^[^.]+" rom_name ${filename})
        string(REGEX REPLACE "[\ \\./-]" "_" rom_escaped_name ${rom_name})
        message(STATUS "ROM Name: ${rom_name}")

        # MAPPER
        string(REGEX MATCH  "MAPPER([^.]+)" mapper ${filename})
        if(NOT "${mapper}" STREQUAL "") 
            set(mapper ".cartridge_type=Config::${mapper},") 
        endif()
        message(STATUS "Mapper: ${mapper}")


        # Read hex data from file
        file(READ ${bin} filedata HEX)
        file(SIZE ${bin} filesize)
        # Convert hex data for C compatibility
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," filedata ${filedata})
        # Append data to output file
        file(APPEND ${CMAKE_BINARY_DIR}/${output}.cc "const uint8_t  ${rom_escaped_name}[${filesize}] __attribute__ ((section (\".${section}\"))) = {${filedata}};\n" )
        file(APPEND ${CMAKE_BINARY_DIR}/${output}.h "extern const uint8_t  ${rom_escaped_name}[${filesize}];\n" )
        file(APPEND ${CMAKE_BINARY_DIR}/${output}.list.h "{.name=\"${rom_name}\", ${mapper} .rom_base=${rom_escaped_name} },\n")
    endforeach()
    
    file(APPEND ${CMAKE_BINARY_DIR}/${output}.list.h "};\n")

endfunction()
