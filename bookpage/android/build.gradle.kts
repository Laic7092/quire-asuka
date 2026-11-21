// The bookpage package's Kotlin for Android: a library module the Asuka CLI
// builds into the apps that import the package. Its Kotlin is in its
// namespace; its dependencies (Maven's) go here, its permissions and
// services in src/main/AndroidManifest.xml.
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "bookpage"
}

dependencies {
}
