# cmake/BundleLayout.cmake
# Assembles the .ofx.bundle directory structure required by the OFX spec.
#
# Bundle layout:
#   MasterFilm.ofx.bundle/
#     Contents/
#       Info.plist            (macOS only)
#       Resources/
#         MasterFilm.xml      (plugin metadata / localisation)
#         shaders/            (embedded GLSL sources)
#       MacOS/                (or Win64/ or Linux-x86-64/)
#         MasterFilm.ofx

# ── Determine arch subfolder ──────────────────────────────────────────────────
if(APPLE)
    # Universal binary or native arm64/x86_64
    set(OFX_ARCH_DIR "MacOS")
elseif(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(OFX_ARCH_DIR "Win64")
    else()
        set(OFX_ARCH_DIR "Win32")
    endif()
else()
    set(OFX_ARCH_DIR "Linux-x86-64")
endif()

set(BUNDLE_ROOT "${CMAKE_BINARY_DIR}/MasterFilm.ofx.bundle")
set(BUNDLE_CONTENTS "${BUNDLE_ROOT}/Contents")
set(BUNDLE_ARCH_DIR "${BUNDLE_CONTENTS}/${OFX_ARCH_DIR}")
set(BUNDLE_RESOURCES "${BUNDLE_CONTENTS}/Resources")

# ── Post-build: copy .ofx into bundle ────────────────────────────────────────
add_custom_command(TARGET MasterFilm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_ARCH_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_RESOURCES}/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:MasterFilm>" "${BUNDLE_ARCH_DIR}/MasterFilm.ofx"
    COMMENT "Assembling MasterFilm.ofx.bundle"
)

# ── Copy GLSL shaders into Resources ─────────────────────────────────────────
add_custom_command(TARGET MasterFilm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders/glsl"
        "${BUNDLE_RESOURCES}/shaders"
    COMMENT "Copying GLSL shaders into bundle"
)

# ── macOS Info.plist ──────────────────────────────────────────────────────────
if(APPLE)
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
        "${BUNDLE_CONTENTS}/Info.plist"
        @ONLY
    )
endif()

# ── Install target ────────────────────────────────────────────────────────────
install(
    DIRECTORY "${BUNDLE_ROOT}"
    DESTINATION "${PLUGIN_INSTALL_DIR}"
    USE_SOURCE_PERMISSIONS
)

# ── Post-build: auto-deploy to Resolve's OFX path (Windows) ──────────────────
# Runs after every build so you don't need to manually run the install target.
# Requires VS to be running as Administrator, OR the OFX folder permissions
# relaxed for your user account (see note below).
if(WIN32)
    set(OFX_DEPLOY_DIR "C:/Program Files/Common Files/OFX/Plugins")
    add_custom_command(TARGET MasterFilm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${OFX_DEPLOY_DIR}/MasterFilm.ofx.bundle/Contents/${OFX_ARCH_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${OFX_DEPLOY_DIR}/MasterFilm.ofx.bundle/Contents/Resources/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:MasterFilm>"
            "${OFX_DEPLOY_DIR}/MasterFilm.ofx.bundle/Contents/${OFX_ARCH_DIR}/MasterFilm.ofx"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/shaders/glsl"
            "${OFX_DEPLOY_DIR}/MasterFilm.ofx.bundle/Contents/Resources/shaders"
        COMMENT "Deploying MasterFilm.ofx.bundle → ${OFX_DEPLOY_DIR}"
        VERBATIM
    )
endif()