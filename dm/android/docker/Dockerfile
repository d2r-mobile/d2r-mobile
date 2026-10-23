# 安卓 App 的 Linux 构建环境（Gradle + NDK + CMake + JDK + Python），WSL / Linux 上不装 Windows 工具也能出 APK。
# 版本照 dm/android/README.md 与 app/build.gradle：compileSdk 36、NDK 29.0.14206865、CMake 3.31.6、Gradle 9.8.1、JDK 17。
# 钉死：基础镜像按摘要、apt 按 Ubuntu 快照日期、命令行工具按构建号 + Google 仓库清单里的 SHA-1、Gradle 按版本 + SHA-256；
# SDK 各包由 sdkmanager 按仓库清单的 SHA-1 校验。用法见 build.sh。
#
# Linux build environment for the Android App (Gradle + NDK + CMake + JDK + Python), so WSL / Linux can produce the APK without Windows tools.
# Versions follow dm/android/README.md and app/build.gradle: compileSdk 36, NDK 29.0.14206865, CMake 3.31.6, Gradle 9.8.1, JDK 17.
# Pinned: base image by digest, apt by Ubuntu snapshot date, command-line tools by build number + the SHA-1 in Google's repository listing, Gradle by version + SHA-256;
# sdkmanager checks every SDK package against the repository listing's SHA-1. Usage: see build.sh.

FROM ubuntu@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55

ARG APT_SNAPSHOT=20261010T000000Z
ARG CMDLINE_TOOLS=16111833
ARG CMDLINE_TOOLS_SHA1=e025545c62a8e64c7559119566a569fb1dec5f60
ARG GRADLE=9.8.1
ARG GRADLE_SHA256=dce76f55f8e251a3a1f130eb120f30b3d271de2b76c9b0729d316b5a1b6dc01f
ARG SDK_PACKAGES="platforms;android-36 build-tools;36.0.0 ndk;29.0.14206865 cmake;3.31.6"

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8

# 快照服务只走 HTTPS：先从镜像自带源装 ca-certificates，其余都从快照装
# The snapshot service is HTTPS-only: ca-certificates comes from the image's own archive first, everything else from the snapshot
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates \
 && apt-get update --snapshot "$APT_SNAPSHOT" \
 && apt-get install -y --no-install-recommends --snapshot "$APT_SNAPSHOT" \
      openjdk-17-jdk-headless python3 curl unzip \
 && rm -rf /var/lib/apt/lists/*

ENV JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64 \
    ANDROID_HOME=/opt/android-sdk \
    ANDROID_SDK_ROOT=/opt/android-sdk

RUN curl -fsSL -o /tmp/clt.zip \
      "https://dl.google.com/android/repository/commandlinetools-linux-${CMDLINE_TOOLS}_latest.zip" \
 && echo "${CMDLINE_TOOLS_SHA1}  /tmp/clt.zip" | sha1sum -c - \
 && mkdir -p "$ANDROID_HOME/cmdline-tools" \
 && unzip -q /tmp/clt.zip -d "$ANDROID_HOME/cmdline-tools" \
 && mv "$ANDROID_HOME/cmdline-tools/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest" \
 && rm /tmp/clt.zip

# 包名带精确版本，装的就是这几个。sdkmanager 解出的可执行文件是 744（只有 root 能跑），容器按宿主用户 UID 跑，所以同一层里补上 a+rX
# Package names carry exact versions, so exactly these are installed. sdkmanager unpacks executables as 744 (root only) and the container runs as the host user's UID, so a+rX is added in the same layer
RUN yes | "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --licenses > /dev/null \
 && "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" $SDK_PACKAGES \
 && chmod -R a+rX "$ANDROID_HOME" \
 && rm -rf /root/.android /root/.cache

RUN curl -fsSL -o /tmp/gradle.zip \
      "https://services.gradle.org/distributions/gradle-${GRADLE}-bin.zip" \
 && echo "${GRADLE_SHA256}  /tmp/gradle.zip" | sha256sum -c - \
 && unzip -q /tmp/gradle.zip -d /opt \
 && ln -s "/opt/gradle-${GRADLE}/bin/gradle" /usr/local/bin/gradle \
 && rm /tmp/gradle.zip

# Gradle 缓存在具名卷 /cache；调试签名钥匙在宿主目录、挂到 /android-user（见 build.sh）。都全员可写：容器按宿主用户 UID 跑
# Gradle caches live on a named volume at /cache; the debug signing key lives in a host directory mounted at /android-user (see build.sh). Both world-writable: the container runs as the host user's UID
RUN mkdir -m 1777 /cache /cache/home /cache/gradle /android-user

ENV HOME=/cache/home \
    GRADLE_USER_HOME=/cache/gradle \
    ANDROID_USER_HOME=/android-user

WORKDIR /src
