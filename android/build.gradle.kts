plugins {
    id("com.android.application") version "8.5.2" apply false
    id("org.jetbrains.kotlin.android") version "1.9.24" apply false
}

// Repo is on a CIFS mount (no exec/symlink) — build off-mount, under whoever is building.
val offMount = file(System.getProperty("user.home") + "/droppix-android-build")
rootProject.layout.buildDirectory.set(offMount.resolve("root"))
subprojects {
    layout.buildDirectory.set(offMount.resolve(name))
}
