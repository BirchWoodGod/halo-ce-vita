# test_out.sh: the run_*.sh tests' output folder (sourced: `. "$here/test_out.sh"`).
# A test's folder is deleted when the test passes, and kept, its path said, when
# it fails or with HALO_TEST_KEEP=1. A folder the test did not make itself (a
# HALO_TEST_OUT that was already there) is never deleted. The harness runs leave
# 0.5-1.7 GB of logs, cache files and copies each: kept in a tmpfs /tmp, they
# filled it (Oct 2026).

# test_out_begin DIR: $out is DIR (made absolute), not made yet (the test
# makes it as before); test_out_made is 1 when it is not there now (the
# test's own, to delete)
test_out_begin() {
	out=$1
	case $out in /*) ;; *) out=$PWD/$out ;; esac
	test_out_made=0
	[ -e "$out" ] || [ -L "$out" ] || test_out_made=1
	return 0
}

# test_out_done STATUS: STATUS 0 and no HALO_TEST_KEEP=1: the folder deleted
# when the test made it; else kept and its path said
test_out_done() {
	if [ "${1:-1}" = 0 ] && [ "${HALO_TEST_KEEP:-0}" != 1 ]; then
		if [ "${test_out_made:-0}" = 1 ] && [ -n "${out:-}" ]; then
			rm -rf -- "${out:?}"
		fi
	elif [ -e "${out:-}" ]; then
		echo "output kept in $out"
	fi
	return 0
}
