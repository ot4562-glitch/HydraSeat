include_guard(GLOBAL)

# Applies the repository's strict, portable warning baseline to one target.
# Keep warning policy here so UI/runtime/provider/test declarations describe
# ownership and dependencies instead of duplicating compiler branches.
function(hydra_enable_strict_warnings target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "hydra_enable_strict_warnings: unknown target '${target_name}'")
    endif()

    if(MSVC)
        target_compile_options("${target_name}" PRIVATE /W4 /permissive-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options("${target_name}" PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endfunction()
