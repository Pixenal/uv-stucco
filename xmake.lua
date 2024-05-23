add_requires("zlib")
add_rules("mode.debug", "mode.release")

target("RUVM")
    set_kind("static")
    add_files("Src/*.c")
    add_includedirs("Include", "Src")
    print("hi")
    if is_plat("windows") then
        remove_files("Src/PlatformLinux.c", "Src/ThreadPool.c")
        set_languages("c11")
        add_defines("WIN32", "PLATFORM_WINDOWS")
    elseif is_plat("linux") or is_plat("macosx") then
	if is_plat("macosx") then
		add_defines("MACOS")
	end
	print("Building for unix")
        remove_files("Src/PlatformWindows.c", "Src/ThreadPoolWindows.c")
        add_defines("PLATFORM_LINUX")
        set_languages("c99")
    end
    add_packages("zlib")
