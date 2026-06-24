#!/usr/bin/env bash
# Create the droppix-android Fedora distrobox and install JDK + Android SDK.
set -euo pipefail
NAME=droppix-android
IMAGE=registry.fedoraproject.org/fedora:44
SDK=/home/Spinjitsudoomyt/android-sdk

if ! distrobox list | grep -q "\b${NAME}\b"; then
  distrobox create --name "${NAME}" --image "${IMAGE}" --yes
fi

distrobox enter "${NAME}" -- bash -lc '
  set -e
  sudo dnf install -y java-17-openjdk-devel unzip wget which 2>/dev/null || \
  sudo dnf install -y java-21-openjdk-devel unzip wget which 2>/dev/null || {
    # Fedora 44 ships only java-25-openjdk-devel / java-latest-openjdk-devel;
    # neither AGP 8.5.2 nor Gradle 8.7 run on JDK 25 daemons. Pull JDK 17 from
    # the Eclipse Temurin repo instead so the brief's pinned toolchain works.
    sudo tee /etc/yum.repos.d/adoptium.repo > /dev/null <<EOF
[Adoptium]
name=Adoptium
baseurl=https://packages.adoptium.net/artifactory/rpm/fedora/\$releasever/\$basearch
enabled=1
gpgcheck=1
gpgkey=https://packages.adoptium.net/artifactory/api/gpg/key/public
EOF
    sudo dnf install -y temurin-17-jdk unzip wget which
  }
  # Pin JDK 17 as the default java/javac so the build commands resolve the right
  # JDK via `which java` (Fedora 44 defaults alternatives to java-25, which AGP
  # 8.5.2 / Gradle 8.7 reject).
  TJAVA=$(alternatives --display java 2>/dev/null | grep -oE "/usr/lib/jvm/[^ ]*temurin[^ ]*/bin/java" | head -1)
  if [ -n "$TJAVA" ]; then
    sudo alternatives --set java "$TJAVA" || true
    sudo alternatives --set javac "${TJAVA}c" || true
  fi
  SDK=/home/Spinjitsudoomyt/android-sdk
  if [ ! -d "$SDK/cmdline-tools/latest" ]; then
    mkdir -p "$SDK/cmdline-tools"
    cd /tmp
    wget -q https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip -O cmdline.zip
    unzip -q -o cmdline.zip -d "$SDK/cmdline-tools"
    mv "$SDK/cmdline-tools/cmdline-tools" "$SDK/cmdline-tools/latest"
  fi
  yes | "$SDK/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$SDK" --licenses >/dev/null
  "$SDK/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$SDK" \
    "platform-tools" "platforms;android-34" "build-tools;34.0.0"
'
echo "droppix-android ready. SDK at ${SDK}"
