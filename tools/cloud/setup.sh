#!/bin/bash
# One-time setup of the check machine: Docker, an Arch image with what Opsis builds and runs its checks with, and CEF
# for x86_64: Tacit's engine as it ships, tacit-cef and tacit-cef-sdk, whose packages are copied to ~ first (from the
# laptop's ~/.cache/tacit-repo/x86_64).
set -e
mkdir -p ~/opsis && cd ~/opsis
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq docker.io zstd >/dev/null
# Chromium's sandbox makes user namespaces as a normal user; Ubuntu 24.04 refuses that by default
echo 'kernel.apparmor_restrict_unprivileged_userns=0' | sudo tee /etc/sysctl.d/60-opsis-userns.conf >/dev/null
sudo sysctl -q --system
cat > Dockerfile <<DOCKER
FROM archlinux:latest
RUN pacman -Syu --noconfirm --needed base-devel cmake ninja git rust \
    qt6-base qt6-declarative qt6-webengine qt6-webchannel qt6-websockets qt6-shadertools qt6-svg qt6-imageformats \
    cups sqlite nss openssl dbus gnome-keyring libsecret hunspell fontconfig ttf-liberation noto-fonts \
    pipewire wireplumber pipewire-pulse libpulse weston xorg-xwayland \
    gtk3 alsa-lib at-spi2-core libdrm libxkbcommon libxcomposite libxdamage libxrandr mesa pango cairo libglvnd \
    python procps-ng which unzip bzip2 && pacman -Scc --noconfirm
# the laptop's language: the engine tells pages the system's
RUN echo 'en_GB.UTF-8 UTF-8' >> /etc/locale.gen && locale-gen
RUN useradd -m -u $(id -u) abdul
USER abdul
WORKDIR /work
DOCKER
sudo docker build --network=host -q -t opsis-arch .
sudo docker run --rm opsis-arch pacman -Q qt6-base qt6-webengine qt6-declarative cmake rust gcc
# A stock CEF lacks Tacit's patches (Opsis's world, error pages, history, security keys, wheel ends), and the checks of
# what they do fail on it. Laid out as CEF's distribution: the SDK, and the engine in Release/ (its data files beside
# libcef.so, as /usr/lib/tacit/cef has them).
if [ ! -d cef-tacit ]; then
    t=$(mktemp -d)
    tar --zstd -C "$t" -xf ~/tacit-cef-154.0.34-2-x86_64.pkg.tar.zst usr
    tar --zstd -C "$t" -xf ~/tacit-cef-sdk-154.0.34-2-any.pkg.tar.zst usr
    mkdir -p cef-tacit/Release
    cp -a "$t/usr/share/tacit/cef-sdk/." cef-tacit/
    cp -a "$t/usr/lib/tacit/cef/." cef-tacit/Release/
    rm -rf "$t"
fi
echo SETUP-DONE
