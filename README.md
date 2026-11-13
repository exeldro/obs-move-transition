# Move transition for OBS Studio

Plugin for OBS Studio to move source to a new position during scene transition

# Installation
Download from https://obsproject.com/forum/resources/move-transition.913/

Or enter `flatpak install com.obsproject.Studio.Plugin.MoveTransition` on your terminal

# obs-websocket
Vendor `move` accepts this request through obs-websocket's `CallVendorRequest`:

- `CaptureTransform`: stores the current transform of a Move Source filter's scene item in the filter, like its Get Transform button.
    - Request: `{"sourceName": "<scene or group>", "filterName": "<Move Source filter>", "itemName": "<scene item>"}`
    - `itemName` is optional. It first sets the filter's source, like picking one in its properties. Create the filter without a source and set it here: a Move Source filter that has a source but no transform moves that item to scale 0.
    - Response: `{"success": true}`, or `{"success": false, "error": "<reason>"}`

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
