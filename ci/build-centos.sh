#!/usr/bin/env bash
TAG=$(echo $TAG)
VERSIONS=${TAG//V/}
RELEASE=$(echo $CI_JOB_ID)

echo "Running build-centos.sh"
echo "TAG: $TAG"
echo "VERSIONS: $VERSIONS"

run_source() {
  echo "run_source() function"
	./util/makesrc $TAG $(echo $PAT)
}

run_build() {
  echo "run_build() function"
	mkdir -p ~/rpmbuild/SOURCES/
	mv -f ~/nano-${VERSIONS}.tar.gz ~/rpmbuild/SOURCES/.
	if [ "${LIVE:-}" == "1" ]; then
		scl enable devtoolset-7 'rpmbuild -ba nanocurrency.spec'
	else
		scl enable devtoolset-7 'rpmbuild -ba nanocurrency-beta.spec'
	fi
}

run_update() {
  echo "run_update() function"
	for file in ./nanocurrency*.in; do
		outfile="${file//.in/}"

		echo "Updating \"${outfile}\"..."

		rm -f "${file}.new"
		awk -v srch="@VERSION@" -v repl="$VERSIONS" -v srch2="@RELEASE@" -v repl2="$RELEASE" '{ sub(srch,repl,$0); sub(srch2,repl2, $0); print $0}' <${file} >${file}.new
		rm -fr "${outfile}"
		cat "${file}.new" >"${outfile}"
		rm -f "${file}.new"
		chmod 755 "${outfile}"
	done
}

run_update
run_source
run_build
