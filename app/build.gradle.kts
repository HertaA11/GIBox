// AGP 9 compiles Kotlin itself (built-in Kotlin); only the Compose compiler plugin is applied.
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.gibox.tool"
    // COUI-Kit and Compose use the current Android SDK.
    compileSdk {
        version = release(37) { minorApiLevel = 1 }
    }
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "com.gibox.tool"
        minSdk = 31
        targetSdk = 34
        versionCode = 151
        versionName = "1.0.19"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                targets += "gibox"
                arguments += "-DANDROID_STL=c++_static"
                cppFlags += "-std=c++17"
            }
        }
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    buildFeatures { compose = true }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    packaging {
        // The game process loads libgibox.so straight out of the module APK (Entry), so keep it a plain zip entry.
        jniLibs { useLegacyPackaging = true }
        resources { excludes += "/META-INF/{AL2.0,LGPL2.1}" }
    }
    // Release signing comes from the environment (CI: GitHub Secrets); without it the release APK is unsigned.
    val keystoreFile = System.getenv("KEYSTORE_FILE")?.let { file(it) }?.takeIf { it.exists() }
    signingConfigs {
        if (keystoreFile != null) {
            create("release") {
                storeFile = keystoreFile
                storePassword = System.getenv("KEYSTORE_PASSWORD")
                keyAlias = System.getenv("KEY_ALIAS")
                keyPassword = System.getenv("KEY_PASSWORD")
            }
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            if (keystoreFile != null) signingConfig = signingConfigs.getByName("release")
        }
        // An installable release-mode build for testing the libraries' ART profiles without release keys.
        create("profiled") {
            initWith(getByName("release"))
            signingConfig = signingConfigs.getByName("debug")
            matchingFallbacks += "release"
        }
    }
}

dependencies {
    compileOnly("de.robv.android.xposed:api:82")
    // Modern Xposed API (ModernEntry, META-INF/xposed) and its service (the LSPosed scope in the app). Apache-2.0.
    compileOnly("io.github.libxposed:api:102.0.0")
    implementation("io.github.libxposed:service:102.0.0")
    val couiVersion = "0.0.1-alpha11"
    implementation("io.github.itoseo:coui:$couiVersion")
    implementation("io.github.itoseo:coui-icons:$couiVersion")
    implementation("androidx.activity:activity-compose:1.13.0")
}
