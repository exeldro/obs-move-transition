# Move transition for OBS Studio

Plugin for OBS Studio to move source to a new position during scene transition

# Installation
Download from https://obsproject.com/forum/resources/move-transition.913/

Or enter `flatpak install com.obsproject.Studio.Plugin.MoveTransition` on your terminal

# Build
1. In-tree build
    - Build OBS Studio: https://obsproject.com/wiki/Install-Instructions
    - Check out this repository to plugins/move-transition
    - Add `add_subdirectory(move-transition)` to plugins/CMakeLists.txt
    - Rebuild OBS Studio

1. Stand-alone build (Linux only)
    - Verify that you have package with development files for OBS
    - Check out this repository and run `cmake -S . -B build -DBUILD_OUT_OF_TREE=On && cmake --build build`

# Donations
- [GitHub Sponsor](https://github.com/sponsors/exeldro)
- [Ko-fi](https://ko-fi.com/exeldro)
- [Patreon](https://www.patreon.com/Exeldro)
- [PayPal](https://www.paypal.me/exeldro)
