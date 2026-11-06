# smbx2compat: our edits inside two submodules we can't push to (WohlSoft's
# PGE-File-Library-STL and LuaJIT) are kept as patches in smbx2compat/
# (CLAUDE.md, "PGE_File_Formats"). A fresh clone (another computer, the
# Android build) gets them applied here, at configure time; a submodule that
# already has them is left alone. Without them the build fails (PGE's fields)
# or, worse, builds a LuaJIT without Lua 5.2 syntax and SMBX2's scripts break.

find_package(Git QUIET)

function(smbx2compat_apply_patch SUBDIR PATCH)
    set(dir "${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}")
    set(patch "${CMAKE_CURRENT_SOURCE_DIR}/${PATCH}")

    if(NOT GIT_FOUND)
        message(WARNING "smbx2compat: git not found; apply ${PATCH} to ${SUBDIR} by hand (CLAUDE.md)")
        return()
    endif()
    if(NOT EXISTS "${dir}/.git")
        message(FATAL_ERROR "smbx2compat: ${SUBDIR} is empty; run: git submodule update --init --recursive")
    endif()

    # already applied: the patch reverses cleanly
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" apply --reverse --check "${patch}"
                    RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
    if(applied EQUAL 0)
        return()
    endif()

    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" apply "${patch}"
                    RESULT_VARIABLE result ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "smbx2compat: ${PATCH} doesn't apply to ${SUBDIR} (wrong submodule commit, or line endings changed?):\n${err}")
    endif()
    message(STATUS "smbx2compat: applied ${PATCH} to ${SUBDIR}")
endfunction()

smbx2compat_apply_patch(3rdparty/PGE_File_Formats smbx2compat/pge-file-library.patch)
smbx2compat_apply_patch(3rdparty/LuaJIT smbx2compat/luajit.patch)
