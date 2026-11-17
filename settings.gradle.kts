pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
        maven("https://api.xposed.info/")
        // GitHub Packages requires read:packages even for public SESL artifacts.
        listOf("oneui-design", "sesl-androidx", "sesl-material-components-android").forEach { repository ->
            maven("https://maven.pkg.github.com/tribalfs/$repository") {
                name = "tribalfs-${repository}"
                credentials {
                    username = providers.gradleProperty("gpr.user").orNull
                        ?: providers.environmentVariable("SESL_MAVEN_USER").orNull
                        ?: providers.environmentVariable("GITHUB_ACTOR").orNull
                    password = providers.gradleProperty("gpr.key").orNull
                        ?: providers.environmentVariable("SESL_MAVEN_TOKEN").orNull
                        ?: providers.environmentVariable("GITHUB_TOKEN").orNull
                        ?: providers.environmentVariable("GH_TOKEN").orNull
                }
                content {
                    when (repository) {
                        "oneui-design" -> includeModule("io.github.tribalfs", "oneui-design")
                        "sesl-androidx" -> includeGroupByRegex("sesl\\.androidx\\..*")
                        else -> includeGroup("sesl.com.google.android.material")
                    }
                }
            }
        }
    }
}
rootProject.name = "GIBox"
include(":app")
