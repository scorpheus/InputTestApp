cmake_minimum_required(VERSION 3.16)
project(ImGuiSDLApp)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Détection de la plateforme de compilation
set(IS_PROSPERO FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Prospero" OR PLATFORM STREQUAL "PROSPERO")
    set(IS_PROSPERO TRUE)
    message(STATUS "Configuration pour Prospero (PS5)")
else()
    message(STATUS "Configuration pour x64 (Windows/Desktop)")
endif()

# Ajouter les chemins vers les bibliothèques externes
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui)

# Configuration spécifique à la plateforme
if(IS_PROSPERO)
    # Chemins vers le SDK Prospero
    set(SCE_PROSPERO_SDK_DIR $ENV{SCE_PROSPERO_SDK_DIR})
    if(NOT SCE_PROSPERO_SDK_DIR)
        message(FATAL_ERROR "La variable d'environnement SCE_PROSPERO_SDK_DIR n'est pas définie")
    endif()
    
    # Configurations pour Prospero
    set(SDL2_DIR ${SCE_PROSPERO_SDK_DIR}/target/include/SDL2)
    
    # Inclure les bibliothèques spécifiques PS5
    include_directories(${SCE_PROSPERO_SDK_DIR}/target/include)
    link_directories(${SCE_PROSPERO_SDK_DIR}/target/lib)
    
    # Configurations spécifiques pour la compilation PS5
    add_definitions(-D__PROSPERO__)
    
    # Note: SDL2 est généralement déjà intégré dans le SDK Prospero
    set(SDL_LIBRARIES SDL2 SDL2main)
else()
    # Configuration pour Windows/x64
    set(SDL2_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/SDL)
    
    # Activer SDL2 en tant que sous-projet
    set(SDL_SHARED ON CACHE BOOL "Build a shared SDL2 library" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Build a static SDL2 library" FORCE)
    set(SDL_TEST OFF CACHE BOOL "Build the SDL2 test suite" FORCE)
    
    # Ajouter SDL2 en tant que sous-projet
    add_subdirectory(${SDL2_DIR} ${CMAKE_BINARY_DIR}/SDL2)
endif()

# Compiler ImGui (commun aux deux plateformes)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
)

# Ajout du backend spécifique à la plateforme
if(IS_PROSPERO)
    # Pour PS5, utiliser le backend spécifique de Sony ou OpenGL
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
    # Si PS5 utilise OpenGL, ajouter également:
    # list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
else()
    # Pour Windows, utiliser le backend SDL Renderer comme avant
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
endif()

# Créer l'exécutable
if(IS_PROSPERO)
    # Pour PS5, on n'utilise pas WIN32 comme type d'application
    add_executable(ImGuiSDLApp main.cpp UI.cpp UI.h ${IMGUI_SOURCES})
    
    # Configurations spécifiques PS5
    target_compile_options(ImGuiSDLApp PRIVATE -O2)
    
    # Définir les symboles nécessaires pour PS5
    target_compile_definitions(ImGuiSDLApp PRIVATE 
        _SCE_TARGET_OS_PROSPERO 
        SDL_MAIN_HANDLED
    )
else()
    # Pour Windows, utiliser WIN32 comme avant
    add_executable(ImGuiSDLApp WIN32 main.cpp UI.cpp UI.h ${IMGUI_SOURCES})
endif()

# Inclure les chemins d'en-têtes (commun aux deux plateformes)
target_include_directories(ImGuiSDLApp PRIVATE 
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Ajouter les chemins d'inclusion spécifiques à la plateforme
if(IS_PROSPERO)
    target_include_directories(ImGuiSDLApp PRIVATE 
        ${SCE_PROSPERO_SDK_DIR}/target/include/SDL2
    )
else()
    target_include_directories(ImGuiSDLApp PRIVATE 
        ${SDL2_DIR}/include
    )
endif()

# Lier avec les bibliothèques
if(IS_PROSPERO)
    # Lier avec les bibliothèques PS5
    target_link_libraries(ImGuiSDLApp PRIVATE 
        ${SDL_LIBRARIES}
        -lSceGnmDriver_stub_weak
        -lSceGnm_stub_weak
        -lSceGpuAddress_stub_weak
    )
    
    # Propriétés spécifiques pour PS5
    set_target_properties(ImGuiSDLApp PROPERTIES
        SCE_PROSPERO_OUTPUT_FS "eboot.bin"
        SCE_PROSPERO_BOOT_PARAM 1
    )
    
    # Générer le fichier param.sfo pour PS5
    add_custom_command(TARGET ImGuiSDLApp POST_BUILD
        COMMAND $ENV{SCE_PROSPERO_SDK_DIR}/host_tools/bin/prospero-create-param-sfo
        --title "ImGui SDL2 Example" 
        --content-id "UP0000-ABCD12345_00-0000000000000000" 
        --app-type internal
        "${CMAKE_CURRENT_BINARY_DIR}/param.sfo"
    )
else()
    # Lier avec les bibliothèques Windows
    target_link_libraries(ImGuiSDLApp PRIVATE SDL2::SDL2 SDL2::SDL2main)
    
    # Ajouter les définitions de préprocesseur pour Windows
    target_compile_definitions(ImGuiSDLApp PRIVATE _CRT_SECURE_NO_WARNINGS)
    
    if(MSVC)
        # Définir le point d'entrée principal pour les applications Windows
        set_target_properties(ImGuiSDLApp PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()
    
    # Copier le SDL2.dll à côté de l'exécutable
    add_custom_command(TARGET ImGuiSDLApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL2::SDL2>
        $<TARGET_FILE_DIR:ImGuiSDLApp>)
endif()