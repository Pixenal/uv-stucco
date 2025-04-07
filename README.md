<div align="left">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://s3.us-west-1.wasabisys.com/mediahost/UvStucco_Media/uv_stucco_dark_path_optimised.svg/uv_stucco_dark_path_optimised.svg?">
    <img alt="Shows a logo with a checkered square, and the text UV Stucco" src="https://s3.us-west-1.wasabisys.com/mediahost/UvStucco_Media/uv_stucco_light_path_optimised.svg/uv_stucco_light_path_optimised.svg?" align="center">
  </picture>
</div>

## A library for mapping geometry to meshes

# Build Instructions
```
git clone https://github.com/Pixenal/uv-stucco.git uv-stucco && cd uv-stucco
```
```
mkdir -p build && cd build
```
UV Stucco uses zlib and [MikkTSpace](https://github.com/mmikk/MikkTSpace), with the latter included as a submodule.  
CMake will try to find zlib on it's own, though this may fail depending on the environment.  
To build with search on just run:
```
cmake ../..
```
If it does fail, you can disable auto search with `-DFIND_ZLIB=OFF`, and specify the paths manually.  
For example on macos:
```
brew install zlib
```
```
cmake ../.. -DFIND_ZLIB=OFF -DZLIB_LIB="/opt/homebrew/opt/zlib/lib/libz.a" -DZLIB_INCLUDE="/opt/homebrew/opt/zlib/include"
```
If auto search does work, check the header CMake found matches the lib. If you have multiple zlib installs, they can sometimes be mismatched.  
\
Alternatively, zlib can also be found using Conan. To do so, cd back into the root dir if your still in build/, and run:
```
conan install . --output-folder=build --build=missing
```
This will install the release package for zlib,
if you wan't to run UV Stucco in debug, you'll need the debug package as well:
```
conan install . --output-folder=Build --build=missing --settings=build_type=Debug
```
Now you can build with:
```
cmake ../.. -DCMAKE_TOOLCHAIN_FILE="../conan_toolchain.cmake"
```
\
Toggle shared/ dynamic lib with `-DBUILD_SHARED_LIBS`.  
and MSVC runtime with `-DCMAKE_MSVC_RUNTIME_LIBRARY` if on windows:
```
cmake ../.. -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DBUILD_SHARED_LIBS=ON -DCMAKE_TOOLCHAIN_FILE="../conan_toolchain.cmake"
```
