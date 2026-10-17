# Gallery concept

A runnable Qt Quick design for **Gallery**, application ID `com.openwave.photos`.
The supplied `Photos.svg` is preserved as `assets/icon.svg`.

The concept uses OpenWave's central Source Sans Pro fonts, thin white controls,
cyan emphasis, translucent surfaces, and a wallpaper-based background. It includes:

- A three-column photo timeline grouped by local date, with search and selection.
- A two-column Albums view, opening a filtered photo collection.
- A viewer with swipe navigation, pinch/double-tap zoom, panning, animated images,
  a slideshow, session favorites, and file information.
- Open-image and folder dialogs for trying local files on the development host.

The OpenWave userspace build installs Gallery at
`/System/Applications/com.openwave.photos`. It appears in the launcher as Gallery
and runs with normal application isolation. On OpenWave, **Open image** and
**Add photos** use the native File Chooser and its permission broker. Imported
images are stored under the application's persistent `Data/Gallery/Images`
directory, using content hashes to avoid duplicate copies. Atomic writes keep
incomplete imports out of the library. Saved photos reopen after app or system
restarts and survive cache deletion. The app implements the
standard application quit endpoint and uses the central font and wallpaper APIs.

Swipe left in the viewer for the next photo or right for the previous one;
mouse and pen drags use the same gesture. Photos follow the pointer and settle
into place, with a crossfade between the outgoing and incoming titles. Short or
vertical drags keep the current photo. Pinching cancels navigation, and dragging
a zoomed photo pans it. The system's reduced-motion setting disables animated
sliding and title fades while keeping swipe navigation available.

The desktop build retains local image/folder dialogs for design previews. Native
folder enumeration, persistent favorites, and background indexing remain future
work. The desktop folder scan is limited to 1,000 images and does not recurse
into subfolders. Timeline dates currently come from file modification times;
sample libraries can supply dates explicitly. EXIF capture-date indexing is a
later implementation step.
Selection and favorites are temporary; the concept never deletes or edits the original photos.

## Preview

From the repository root, using the exported host Qt development tools:

```sh
cmake -S OceanLauncher/OceanPhotos -B out/ocean-photos-concept/build -G Ninja \
  -DCMAKE_PREFIX_PATH="$PWD/toolchain" -DCMAKE_BUILD_TYPE=Release
cmake --build out/ocean-photos-concept/build --parallel 4
out/ocean-photos-concept/build/ocean-photos --concept --directory "$HOME/Pictures"
```

`--concept` adds a decorative status bar and home indicator for standalone
previews. The real compositor owns those elements on OpenWave. The default app
view leaves them out. The software scene renderer uses a tinted background and
rectangular thumbnails; the OpenWave GPU renderer enables blur and rounded masks.

```sh
out/ocean-photos-concept/build/ocean-photos --image /path/to/photo.png
out/ocean-photos-concept/build/ocean-photos --list-formats
QT_QPA_PLATFORM=offscreen out/ocean-photos-concept/build/ocean-photos \
  --concept --directory /path/to/photos --view albums --capture /tmp/gallery-albums.png
QT_QPA_PLATFORM=offscreen ctest --test-dir out/ocean-photos-concept/build --output-on-failure
```

`--view` accepts `photos`, `albums`, or `viewer`. A sample-library JSON array can
supply `path`, `album`, and ISO `date` fields using `--sample-library file.json`.
Concept captures in `out/ocean-photos-concept` reference existing local landscape
wallpapers; those images are not copied into this source directory or bundled.

## Image formats

Decoding uses `QImageReader` and Qt Quick's image loading system, including
orientation metadata and installed image-format plugins. The supported-format
menu and `--list-formats` query the actual runtime; no list of promised decoders
is hardcoded. GIF animations use `AnimatedImage` in the viewer.

The present SDK reports BMP, CUR, GIF, ICNS, ICO, JFIF, JPEG, JPG, PBM, PGM, PNG,
PPM, SVG, SVGZ, TGA, TIF, TIFF, WBMP, WEBP, XBM, and XPM. Some names are aliases.
HEIF/HEIC, AVIF, and camera RAW require additional decoder plugins; this concept
does not claim to decode them. More Qt image plugins can add formats without
changing the gallery interface.
