import com.android.build.api.dsl.Packaging

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    // Eliminado: alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.example.holamundo2"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.example.holamundo2"
        minSdk = 29  // Quest 2 requiere Android 10 (API 29) mínimo
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters.add("arm64-v8a") // Quest 2 usa ARM64
        }

        externalNativeBuild {
            cmake {
                cppFlags("-std=c++17", "-fexceptions", "-frtti")
                arguments(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_PLATFORM=android-29"
                )
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )

            ndk {
                debugSymbolLevel = "SYMBOL_TABLE"
            }
        }

        debug {
            isDebuggable = true
            isJniDebuggable = true
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    kotlinOptions {
        jvmTarget = "1.8"
    }

    packaging {
        resources {
            pickFirsts += "**/libc++_shared.so"
        }
        jniLibs {
            pickFirsts += "**/libc++_shared.so"
        }
    }
}

dependencies {
    // Dependencias básicas para VR
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")

    // Solo necesario si usas logging o utilidades adicionales
    implementation("com.google.android.material:material:1.11.0")
}