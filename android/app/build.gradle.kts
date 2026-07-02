plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.droppix.app"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.droppix.app"
        minSdk = 21
        targetSdk = 34
        versionCode = 1
        versionName = "0.1"
    }
    // Release signing is driven by env vars (set by packaging/android/build-apk.sh) so no
    // keystore or password ever lives in the repo. Absent those vars, release stays unsigned.
    signingConfigs {
        create("release") {
            System.getenv("DROPPIX_KEYSTORE")?.let { ks ->
                storeFile = file(ks)
                storePassword = System.getenv("DROPPIX_KS_PASS")
                keyAlias = System.getenv("DROPPIX_KEY_ALIAS") ?: "droppix"
                keyPassword = System.getenv("DROPPIX_KEY_PASS") ?: System.getenv("DROPPIX_KS_PASS")
            }
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            if (System.getenv("DROPPIX_KEYSTORE") != null) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    implementation("com.google.android.material:material:1.12.0")
    testImplementation("junit:junit:4.13.2")
}
