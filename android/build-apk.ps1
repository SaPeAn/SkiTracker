# Собирает APK. Инструменты Android не любят кириллицу в пути,
# поэтому исходники копируются в C:\Android-build\src и собираются там.
param([string[]]$Tasks = @('assembleRelease'))
$b = 'C:\Android-build'
$env:JAVA_HOME = (Get-ChildItem "$b\jdk" -Directory | Select-Object -First 1).FullName
$env:ANDROID_HOME = "$b\sdk"
$env:GRADLE_USER_HOME = "$b\gradle-home"
$env:ANDROID_USER_HOME = "$b\android-home"
New-Item -ItemType Directory -Force $env:ANDROID_USER_HOME | Out-Null
robocopy $PSScriptRoot "$b\src" /MIR /XD build .gradle /NFL /NDL /NJH /NJS /NP | Out-Null
& "$b\gradle\gradle-8.7\bin\gradle.bat" -p "$b\src" --no-daemon --console=plain @Tasks
if ($LASTEXITCODE -ne 0) { exit 1 }
$apk = "$b\src\app\build\outputs\apk\release\app-release.apk"
if (($Tasks -contains 'assembleRelease') -and (Test-Path $apk)) {
  Copy-Item $apk (Join-Path (Split-Path $PSScriptRoot) 'SkiTracker.apk') -Force
}
