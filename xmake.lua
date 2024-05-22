add_requires("zlib")
add_rules("mode.debug", "mode.release")

target("RUVM")
    set_kind("static")
    add_files("Src/*.c")
    add_includedirs("Include", "Src")
    if is_plat("windows") then
        del_files("Src/PlatformLinux.c", "Src/ThreadPool.c")
        set_languages("c11")
        add_defines("WIN32", "PLATFORM_WINDOWS")
    elseif is_plat("linux") then
        del_files("Src/PlatformWindows.c", "Src/ThreadPoolWindows.c")
        add_defines("PLATFORM_LINUX")
        set_languages("c99")
    end
    add_packages("zlib")