#!/bin/bash

if [[ "$TRAVIS_OS_NAME" = "osx" ]]; then
	brew update
	brew install gnutls
	brew install nettle
	brew outdated autoconf || brew upgrade autoconf
	brew outdated automake || brew upgrade automake
	brew outdated libtool || brew upgrade libtool
	brew install doxygen
	brew outdated gettext || brew upgrade gettext
	brew install flex
	brew install libidn
	brew install xz
	brew install lbzip2
	brew install lzip
	brew link --force gettext
elif [[ "$TRAVIS_OS_NAME" = "linux" ]]; then
	pip install --user cpp-coveralls

	# Install libidn2 from the latest source
	wget "https://alpha.gnu.org/gnu/libidn/" -O list.html || exit 1
	archive="`grep -o "libidn2-[0-9]*\.[0-9]*\.[0-9]*.tar.xz" list.html | sort -V | tail -n 1`"
	test -z "$archive" && echo "Cannot find latest archive" && exit 1
	wget "https://alpha.gnu.org/gnu/libidn/$archive" -O "$archive" || exit 1
	dirname="${archive%.tar.xz}"
	tar -axf "$archive" || exit 1
	(
		cd "$dirname" || exit 1
		mkdir build || exit 1
		cd build || exit 1
		../configure --prefix=/usr || exit 1
		make -j3 || exit 1
		sudo make -j3 install || exit 1
	)
fi
