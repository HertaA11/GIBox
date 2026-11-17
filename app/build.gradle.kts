// AGP 9 compiles Kotlin itself (built-in Kotlin); only the Compose compiler plugin is applied.
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.gibox.tool"
    // Compose 1.13 / Material 3 1.5 need compileSdk 37.1.
    compileSdk {
        version = release(37) { minorApiLevel = 1 }
    }
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "com.gibox.tool"
        minSdk = 26
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
            isMinifyEnabled = false
            if (keystoreFile != null) signingConfig = signingConfigs.getByName("release")
        }
    }
}

// SESL uses the androidx package names. Keep the upstream copies out of every variant.
configurations.configureEach {
    listOf(
        "androidx.core" to "core", "androidx.core" to "core-ktx",
        "androidx.appcompat" to "appcompat",
        "androidx.customview" to "customview",
        "androidx.coordinatorlayout" to "coordinatorlayout",
        "androidx.drawerlayout" to "drawerlayout",
        "androidx.fragment" to "fragment", "androidx.fragment" to "fragment-ktx",
        "androidx.preference" to "preference",
        "androidx.recyclerview" to "recyclerview",
        "androidx.slidingpanelayout" to "slidingpanelayout",
        "androidx.swiperefreshlayout" to "swiperefreshlayout",
        "androidx.viewpager" to "viewpager", "androidx.viewpager2" to "viewpager2",
        "com.google.android.material" to "material",
    ).forEach { (group, module) -> exclude(group = group, module = module) }
}

dependencies {
    compileOnly("de.robv.android.xposed:api:82")
    // Modern Xposed API (ModernEntry, META-INF/xposed) and its service (the LSPosed scope in the app). Apache-2.0.
    compileOnly("io.github.libxposed:api:102.0.0")
    implementation("io.github.libxposed:service:102.0.0")
    // One UI 8 includes the matching SESL View libraries, including Preference/AppCompat.
    implementation("io.github.tribalfs:oneui-design:0.9.20+oneui8")
    implementation("sesl.androidx.preference:preference:1.2.1+1.0.0-sesl8+rev1")
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.compose.material3:material3:1.5.0-beta01")
    implementation("androidx.compose.material:material-icons-core:1.7.8")
}
