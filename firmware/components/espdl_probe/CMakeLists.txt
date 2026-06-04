set(srcs "espdl_probe.c")
set(priv_requires esp_timer)
set(espdl_probe_have_rodata_model OFF)
set(espdl_probe_have_runtime OFF)

if(CONFIG_LABGUARD_ESPDL_PROBE_ENABLE)
    set(espdl_probe_model "${CMAKE_CURRENT_LIST_DIR}/model/model.espdl")

    idf_build_get_property(component_targets __COMPONENT_TARGETS)
    if("___idf_espressif__esp-dl" IN_LIST component_targets)
        set(espdl_component espressif__esp-dl)
        idf_component_get_property(espdl_dir espressif__esp-dl COMPONENT_DIR)
    elseif("___idf_esp-dl" IN_LIST component_targets)
        set(espdl_component esp-dl)
        idf_component_get_property(espdl_dir esp-dl COMPONENT_DIR)
    endif()

    if(espdl_dir)
        if(CONFIG_LABGUARD_ESPDL_PROBE_LOAD_RODATA)
            if(EXISTS "${espdl_probe_model}")
                list(APPEND srcs "espdl_probe_espdl.cpp")
                list(APPEND priv_requires ${espdl_component})
                set(espdl_probe_have_runtime ON)
                set(cmake_dir "${espdl_dir}/fbs_loader/cmake")
                include("${cmake_dir}/utilities.cmake")
                set(espdl_probe_have_rodata_model ON)
            else()
                message(WARNING "LABGUARD_ESPDL_PROBE_LOAD_RODATA is selected, but ${espdl_probe_model} is missing.")
            endif()
        elseif(CONFIG_LABGUARD_ESPDL_PROBE_LOAD_SDCARD)
            list(APPEND srcs "espdl_probe_espdl.cpp")
            list(APPEND priv_requires ${espdl_component})
            set(espdl_probe_have_runtime ON)
        endif()
    elseif(NOT espdl_dir)
        message(WARNING "LABGUARD_ESPDL_PROBE_ENABLE is enabled, but esp-dl component was not found.")
    endif()
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${priv_requires}
)

if(espdl_probe_have_runtime)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LABGUARD_ESPDL_PROBE_HAVE_RUNTIME=1)
endif()

if(espdl_probe_have_rodata_model)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LABGUARD_ESPDL_PROBE_HAVE_RODATA_MODEL=1)
    target_add_aligned_binary_data(${COMPONENT_LIB} "${espdl_probe_model}" BINARY)
endif()
