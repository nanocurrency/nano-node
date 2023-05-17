#!/usr/bin/env bash
TAG=$(echo $TAG)
VERSION=$(echo "${TAG}" | sed 's/V//' | sed 's/-/_/g')
RPM_RELEASE=$(echo $RPM_RELEASE)
REPO_TO_BUILD=$(echo $REPO_TO_BUILD)

run_source() {
	./util/makesrc $TAG $REPO_TO_BUILD
}

run_build() {
	mkdir -p ~/rpmbuild/SOURCES/
	mv -f ~/nano-${VERSION}.tar.gz ~/rpmbuild/SOURCES/.
	if [ "${LIVE:-}" == "1" ]; then
		scl enable gcc-toolset-12 'rpmbuild -ba nanocurrency.spec'
	else
		scl enable gcc-toolset-12 'rpmbuild -ba nanocurrency-beta.spec'
	fi
}

run_update() {
	for file in ./nanocurrency*.in; do
		outfile="${file//.in/}"

		echo "Updating \"${outfile}\"..."

		rm -f "${file}.new"
		awk -v srch="@VERSION@" -v repl="$VERSION" -v srch2="@RELEASE@" -v repl2="$RPM_RELEASE" '{ sub(srch,repl,$0); sub(srch2,repl2, $0); print $0}' <${file} >${file}.new
		rm -fr "${outfile}"
		cat "${file}.new" >"${outfile}"
		rm -f "${file}.new"
		chmod 755 "${outfile}"
	done
}

set -x

run_update
run_source
run_build
