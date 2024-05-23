# Reverse UV Mapper 

# Build (Temp for reference)
```
git clone https://github.com/Pixenal/reverse-uv-mapper.git RUVM
cd RUVM
```
RUVM's only dependancy other than the standard library, is zlib.
Cmake may be unable to find zlib on your system (this will likely
be the case on Windows).
RUVM uses Conan for package managament, and so you can use this
to retreive zlib in such cases.
If you've not used conan before, you may need to first run:
```
conan profile detect --force
```
Then get the package as so:
```
conan install . --output-folder=build --build=missing
```
Note that this will only get the release package,
and so attempting to build with s debug configuration,
such as in visual studio, may fail.
If you wish to build RUVM in debug, append
--settings--build_type=Debug to the above command, eg:
```
conan install . --output-folder=Build --build=missing --settings=build_type=Debug
```
Now that dependancies are sorted, the project files can be built:
```
mkdir -p build/LinuxStatic <-- Of course replace with your desired path name
cd build/LinuxStatic
cmake ../.. -DCMAKE_TOOLCHAIN_FILE="../conan_toolchain.cmake"
cmake --build . --config release
```
As is standard, shared/ dyanmic and static libarry builds are toggled
using `-DBUILD_SHARED_LIBS`.
And MSVC runtime type is toggled with `-DCMAKE_MSVC_RUNTIME_LIBRARY` if on Windows.
Eg:
```
cmake ../.. -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DBUILD_SHARED_LIBS=ON -DCMAKE_TOOLCHAIN_FILE="../conan_toolchain.cmake"
```