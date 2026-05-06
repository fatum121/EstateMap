#!/bin/bash
# =============================================================================
# EstateMap — skrypt instalacji zależności
# Uruchom przed pierwszym buildem: chmod +x install_deps.sh && ./install_deps.sh
# =============================================================================

set -e

echo "========================================================"
echo " EstateMap — instalacja zależności"
echo "========================================================"

# Wykryj system operacyjny
if [ -f /etc/debian_version ]; then
    OS="debian"
elif [ -f /etc/fedora-release ]; then
    OS="fedora"
elif [ -f /etc/arch-release ]; then
    OS="arch"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
else
    echo "Nieobsługiwany system. Zainstaluj ręcznie:"
    echo "  libcurl (dev)"
    echo "  GDAL (dev)"
    exit 1
fi

echo "Wykryto system: $OS"
echo ""

case $OS in
    debian)
        echo "Instalowanie przez apt..."
        sudo apt update
        sudo apt install -y \
            build-essential \
            cmake \
            libcurl4-openssl-dev \
            libgdal-dev \
            python3 \
            pkg-config
        ;;
    fedora)
        echo "Instalowanie przez dnf..."
        sudo dnf install -y \
            gcc-c++ \
            cmake \
            libcurl-devel \
            gdal-devel \
            python3 \
            pkgconfig
        ;;
    arch)
        echo "Instalowanie przez pacman..."
        sudo pacman -S --needed \
            base-devel \
            cmake \
            curl \
            gdal \
            python \
            pkgconf
        ;;
    macos)
        if ! command -v brew &> /dev/null; then
            echo "Homebrew nie znaleziony. Zainstaluj z: https://brew.sh"
            exit 1
        fi
        echo "Instalowanie przez brew..."
        brew install \
            cmake \
            curl \
            gdal \
            python3 \
            pkg-config
        ;;
esac

echo ""
echo "========================================================"
echo " Zależności zainstalowane!"
echo " Teraz zbuduj projekt w CLion lub przez terminal:"
echo "   mkdir -p cmake-build-debug && cd cmake-build-debug"
echo "   cmake .. && make -j4"
echo "========================================================"
