plugins {
    id("com.android.application")
}

android {
    namespace = "org.libsdl.app"
    compileSdk = 35
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "dev.tmb.nativeport.smoke"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0-host-smoke"
        ndk { abiFilters += listOf("arm64-v8a") }
    }

    sourceSets {
        getByName("main") { jniLibs.srcDirs("src/main/jniLibs") }
    }

    buildTypes {
        release { isMinifyEnabled = false }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
