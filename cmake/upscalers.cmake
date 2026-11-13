# The upscalers of the Vulkan renderer (src/gpu_vulkan/vulkan_upscale*.cpp; docs/design/vulkan.md "Upscaling"),
# fetched at configure time and checked against their SHA-256 (THIRD_PARTY_NOTICES.md). Both are optional and 64-bit
# only (their libraries are): the code that uses them builds without them, and the game falls back (dlss to fsr, fsr to
# off). Nothing is linked: the DLLs are loaded by name at run time, so no compiler-specific import library is needed.
#
#   THUG2_FSR   AMD's FSR SDK 2.4.0 (github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK): the C API's headers (MIT) and
#               the signed, prebuilt amd_fidelityfx_loader_vk.dll / amd_fidelityfx_upscaler_vk.dll (AMD's binary licence:
#               redistributable with its notice) - FSR 3.1.6 and FSR 4.1 in one DLL.
#   THUG2_DLSS  NVIDIA Streamline 2.14.16 (github.com/NVIDIA-RTX/Streamline, MIT): its headers and its signed DLLs -
#               sl.interposer, sl.common, sl.dlss (Super Resolution / DLAA), sl.dlss_d (Ray Reconstruction) - with
#               NVIDIA's nvngx_dlss.dll / nvngx_dlssd.dll (the NVIDIA RTX SDKs licence: redistributable in an
#               application, its notices kept). Only the files used are taken out of the release's zip.
set(THUG2_UPSCALERS_POSSIBLE OFF)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND THUG2_VULKAN)
    set(THUG2_UPSCALERS_POSSIBLE ON)
endif()
option(THUG2_FSR "Fetch AMD's FSR SDK (headers + signed Vulkan DLLs) for [Graphics] upscaler=fsr (64-bit Windows)" ${THUG2_UPSCALERS_POSSIBLE})
option(THUG2_DLSS "Fetch NVIDIA Streamline (headers + signed DLLs, DLSS) for [Graphics] upscaler=dlss (64-bit Windows)" ${THUG2_UPSCALERS_POSSIBLE})
if(NOT THUG2_UPSCALERS_POSSIBLE)
    set(THUG2_FSR OFF)
    set(THUG2_DLSS OFF)
endif()
set(THUG2_UPSCALER_DLLS "")

if(THUG2_FSR)
    set(fsr_commit 7539ae8b6568ad29ed78a753404351daf3b5cca7) # (the tag v2.4.0)
    set(fsr_base "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/${fsr_commit}/Kits/FidelityFX")
    set(fsr_dir "${CMAKE_BINARY_DIR}/_deps/fsr_sdk-2.4.0")
    # path in the SDK | SHA-256
    set(fsr_files
        "api/include/ffx_api.h|4d9dcf243ef5efbf03340a02f16b51a22053243a22e8a9a0b3ce91c26982b4bf"
        "api/include/ffx_api_types.h|53f07c113a9ba493f297656e4481b6539aed0afc9f3cc8da682aa477d270c46c"
        "api/include/vk/ffx_api_vk.h|09ac1855bc4c2f711243d6982909385072ca3f8d30db44d1a82ee5bd637832ad"
        "upscalers/include/ffx_upscale.h|13091e7fe5fd9c82b1fb7c044b9f08f664d9653f599d2ea500738709236fca4d"
        "signedbin/amd_fidelityfx_loader_vk.dll|38a56a01fb3565c0757d9f5c1e4e69de4b65e5f2e5769b323b200dc1f142bab9"
        "signedbin/amd_fidelityfx_upscaler_vk.dll|4a7e895af6e0b615d40a84d59eb8c2b1054ad91cdd0c739567940270d51bdab7"
        "docs/license.md|32357259b965d6a8001e9e508ba60eda6034027c5923a7d226ffbaff813c2bfb")
    foreach(entry IN LISTS fsr_files)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 file)
        list(GET parts 1 hash)
        if(NOT EXISTS "${fsr_dir}/${file}")
            message(STATUS "Fetching FSR SDK: ${file}")
            file(DOWNLOAD "${fsr_base}/${file}" "${fsr_dir}/${file}" EXPECTED_HASH SHA256=${hash} STATUS fetched)
            list(GET fetched 0 fetched_code)
            if(NOT fetched_code EQUAL 0)
                file(REMOVE "${fsr_dir}/${file}")
                message(FATAL_ERROR "FSR SDK: ${file} couldn't be fetched (${fetched}) - configure with -DTHUG2_FSR=OFF to build without it")
            endif()
        endif()
    endforeach()
    add_library(fsr_sdk INTERFACE)
    target_include_directories(fsr_sdk SYSTEM INTERFACE "${fsr_dir}/api/include" "${fsr_dir}/upscalers/include")
    target_compile_definitions(fsr_sdk INTERFACE THUG2_FSR)
    list(APPEND THUG2_UPSCALER_DLLS "${fsr_dir}/signedbin/amd_fidelityfx_loader_vk.dll"
                                    "${fsr_dir}/signedbin/amd_fidelityfx_upscaler_vk.dll")
    # (its binary licence beside the DLLs: THIRD_PARTY_NOTICES.md)
    configure_file("${fsr_dir}/docs/license.md" "${fsr_dir}/amd_fidelityfx_license.md" COPYONLY)
    list(APPEND THUG2_UPSCALER_DLLS "${fsr_dir}/amd_fidelityfx_license.md")
endif()

if(THUG2_DLSS)
    set(sl_version 2.14.16)
    set(sl_dir "${CMAKE_BINARY_DIR}/_deps/streamline-${sl_version}")
    set(sl_zip "${CMAKE_BINARY_DIR}/_deps/streamline-sdk-v${sl_version}.zip")
    set(sl_wanted
        include/sl.h include/sl_appidentity.h include/sl_consts.h include/sl_core_api.h include/sl_core_types.h
        include/sl_device_wrappers.h include/sl_dlss.h include/sl_dlss_d.h include/sl_helpers_vk.h include/sl_hooks.h
        include/sl_result.h include/sl_security.h include/sl_struct.h include/sl_version.h include/sl_matrix_helpers.h
        bin/x64/sl.interposer.dll bin/x64/sl.common.dll bin/x64/sl.dlss.dll bin/x64/sl.dlss_d.dll
        bin/x64/nvngx_dlss.dll bin/x64/nvngx_dlssd.dll bin/x64/nvngx_dlss.license.txt license.txt)
    set(sl_complete ON)
    foreach(file IN LISTS sl_wanted)
        if(NOT EXISTS "${sl_dir}/${file}")
            set(sl_complete OFF)
        endif()
    endforeach()
    if(NOT sl_complete)
        if(NOT EXISTS "${sl_zip}")
            message(STATUS "Fetching NVIDIA Streamline ${sl_version} (about 280 MB, once)")
            file(DOWNLOAD "https://github.com/NVIDIA-RTX/Streamline/releases/download/v${sl_version}/streamline-sdk-v${sl_version}.zip"
                "${sl_zip}.part" EXPECTED_HASH SHA256=32041e51db0cab709a271b72a352281b690c2abbd669a827809401a5dd23956b
                STATUS fetched)
            list(GET fetched 0 fetched_code)
            if(NOT fetched_code EQUAL 0)
                file(REMOVE "${sl_zip}.part")
                message(FATAL_ERROR "Streamline couldn't be fetched (${fetched}) - configure with -DTHUG2_DLSS=OFF to build without it")
            endif()
            file(RENAME "${sl_zip}.part" "${sl_zip}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${sl_zip}" DESTINATION "${sl_dir}" PATTERNS ${sl_wanted})
    endif()
    add_library(streamline_sdk INTERFACE)
    target_include_directories(streamline_sdk SYSTEM INTERFACE "${sl_dir}/include")
    target_compile_definitions(streamline_sdk INTERFACE THUG2_DLSS)
    foreach(dll IN ITEMS sl.interposer.dll sl.common.dll sl.dlss.dll sl.dlss_d.dll nvngx_dlss.dll nvngx_dlssd.dll
                         nvngx_dlss.license.txt)
        list(APPEND THUG2_UPSCALER_DLLS "${sl_dir}/bin/x64/${dll}")
    endforeach()
    # (Streamline's MIT licence beside its DLLs)
    configure_file("${sl_dir}/license.txt" "${sl_dir}/streamline_license.txt" COPYONLY)
    list(APPEND THUG2_UPSCALER_DLLS "${sl_dir}/streamline_license.txt")
endif()
