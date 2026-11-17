# TINH

TINH (`com.openwave.tinh`) is an Android Home application with a black screen and
the centered message “There is nothing here...”. It requires Android 13 (API 33)
or later. The release APK includes ARM64 (`arm64-v8a`) and x86_64 support.

`MainActivity` handles `MAIN` / `HOME` / `DEFAULT` and remains exported so Android
can start it as Home. It has no `LAUNCHER` entry and no Compose preview. Other
apps can still explicitly start the exported activity.

## Build and verify

Use JDK 25 and Android SDK 37, as configured by this project. On this Mac:

```sh
JAVA_HOME='/Applications/Android Studio.app/Contents/jbr/Contents/Home' \
  ./gradlew :app:assembleRelease :app:lintRelease :app:testDebugUnitTest
```

The signed APK is written to `app/build/outputs/apk/release/app-release.apk`.
Release builds use full R8 code and resource optimization through the AGP 9.5
`optimization` DSL. Native libraries are restricted to ARM64 and x86_64.

## Release signing

The local release key is stored in `.signing/tinh-release.p12`, with alias
`tinh-release`. `.signing/keystore.properties` contains its passwords and the
project-relative keystore path. The directory is excluded from Git, restricted
to the current user, and should be backed up privately in full. Reuse this key
for future app updates; do not regenerate it for each build.

The build loads this local configuration when present. Without it, release APKs
are unsigned until signing is configured in Gradle or Android Studio. Debug
builds continue to use Android's debug key.

## Run and integrate

In Android Studio, set the run configuration's Launch option to **Specified
Activity** and choose `com.openwave.tinh.MainActivity`. There is no ordinary
launcher activity for the default launch option to discover.

After installation, select TINH in Android's default Home app settings. For a
direct launch during development:

```sh
adb shell am start -n com.openwave.tinh/.MainActivity
```

Ocean integrations that enumerate all installed Android packages must exclude
`com.openwave.tinh` themselves; removing `LAUNCHER` only changes normal launcher
discovery. The shared Back button belongs in OpenWave's host integration or
Android SystemUI. TINH does not request Accessibility permissions.
