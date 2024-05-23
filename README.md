# Reverse UV Mapper 

# Build Instructions
```
git clone https://github.com/Pixenal/reverse-uv-mapper.git RUVM && cd RUVM
```
```
mkdir -p build/linux && cd build/linux <-- Of course, change to your preferred path
```
RUVM's only dependency, other than the c standard library, is zlib.  
Depending on your system, the below command may fail,
CMake will try to find zlib on its own, but might not be able to.  
```
```
If this does fail, you can disable auto search by toggling FIND_ZLIB,
and specify directories manually. For example on macos:
```
brew install zlib
```
```
cmake ../.. -DFIND_ZLIB=OFF -DZLIB_LIB="/opt/homebrew/opt/zlib/lib/libz.a" -DZLIB_INCLUDE="/opt/homebrew/opt/zlib/include"
```
Alternatively, zlib can also be found using Conan.  
If you've not used conan before, you may need to first run:
```
conan profile detect --force
```
Make sure to cd back out to the repository root directory,  
then install the package using conan install:
```
conan install . --output-folder=build --build=missing
```
Note that this will only get the release package,
and so attempting to build with s debug configuration,
such as in visual studio, may fail.  
If you wish to build RUVM in debug, append
--settings--build_type=Debug to the above command,
eg:
```
conan install . --output-folder=Build --build=missing --settings=build_type=Debug
```
```
cmake ../.. -DFIND_ZLIB_CONAN=ON -DCMAKE_TOOLCHAIN_FILE="../conan_toolchain.cmake"
```

Once build files are generated, compile as usual:
```
cmake --build . --config release
```

As is standard, shared/ dyanmic and static libary builds can be toggled with `-DBUILD_SHARED_LIBS`.  
And on windows, MSVC runtime type is toggled with `-DCMAKE_MSVC_RUNTIME_LIBRARY`.  
Eg:
```
cmake ../.. -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DBUILD_SHARED_LIBS=ON -DCMAKE_TOOLCHAIN_FILE="../conan_toolchain.cmake"
```
