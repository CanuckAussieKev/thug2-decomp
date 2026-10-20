# Runs ImGui's binary_to_compressed_c on a font (TOOL, INPUT, SYMBOL, OUTPUT): a C array of the stb-compressed TTF,
# <SYMBOL>_compressed_size / <SYMBOL>_compressed_data, for ImFontAtlas::AddFontFromMemoryCompressedTTF.
execute_process(COMMAND "${TOOL}" -u32 "${INPUT}" "${SYMBOL}"
    OUTPUT_FILE "${OUTPUT}.tmp"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "binary_to_compressed_c failed on ${INPUT}: ${result}")
endif()
file(RENAME "${OUTPUT}.tmp" "${OUTPUT}")
