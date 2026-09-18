set -eu

LC_ALL=C
export LC_ALL

cli=${1:-./fch}
tmpdir=$(mktemp -d)
trap 'rm -r "$tmpdir"' 0

fail() {
	printf 'FAIL: %s\n' "$1" >&2
	exit 1
}

digest_from() {
	sed 's/[[:space:]].*$//' "$1"
}

path_for_cli() {
	if [ -n "${MSYSTEM:-}" ] && command -v cygpath >/dev/null 2>&1; then
		cygpath -m "$1"
	else
		printf '%s\n' "$1"
	fi
}

input="$tmpdir/input file.bin"
second="$tmpdir/second.bin"
printf '\015\012\032\000\377FCH\200binary\012' > "$input"
printf 'second input\012' > "$second"
input_arg=$(path_for_cli "$input")
second_arg=$(path_for_cli "$second")

for bits in 256 512; do
	file_output="$tmpdir/file-$bits.out"
	stdin_output="$tmpdir/stdin-$bits.out"

	"$cli" "-$bits" "$input_arg" > "$file_output"
	"$cli" "-$bits" < "$input" > "$stdin_output"

	file_digest=$(digest_from "$file_output")
	stdin_digest=$(digest_from "$stdin_output")
	[ "$file_digest" = "$stdin_digest" ] ||
		fail "FCH-$bits file and stdin digests differ"

	case "$bits" in
		256) expected_length=64 ;;
		512) expected_length=128 ;;
	esac
	[ "${#file_digest}" -eq "$expected_length" ] ||
		fail "FCH-$bits digest length is invalid"
	case "$file_digest" in
		*[!0-9a-f]*) fail "FCH-$bits digest is not lowercase hexadecimal" ;;
	esac

	[ "$(cat "$file_output")" = "$file_digest  $input_arg" ] ||
		fail "FCH-$bits file label is invalid"
	[ "$(cat "$stdin_output")" = "$stdin_digest  -" ] ||
		fail "FCH-$bits stdin label is invalid"
done

"$cli" -256 "$second_arg" > "$tmpdir/second.out"
"$cli" -256 "$input_arg" "$second_arg" > "$tmpdir/multiple.out"
second_digest=$(digest_from "$tmpdir/second.out")
[ "$(cat "$tmpdir/second.out")" = "$second_digest  $second_arg" ] ||
	fail "second file label is invalid"

expected_first=$(digest_from "$tmpdir/file-256.out")
expected_second=$second_digest
actual_first=$(sed -n '1{s/[[:space:]].*$//;p;}' "$tmpdir/multiple.out")
actual_second=$(sed -n '2{s/[[:space:]].*$//;p;}' "$tmpdir/multiple.out")
line_count=$(wc -l < "$tmpdir/multiple.out")
[ "$line_count" -eq 2 ] || fail "multiple-file output has $line_count lines"
[ "$actual_first" = "$expected_first" ] ||
	fail "multiple-file first digest is invalid"
[ "$actual_second" = "$expected_second" ] ||
	fail "multiple-file second digest is invalid"

"$cli" --help > "$tmpdir/help.out" 2> "$tmpdir/help.err" ||
	fail "--help returned failure"
[ ! -s "$tmpdir/help.out" ] || fail "--help wrote to stdout"
grep -q '^Usage:' "$tmpdir/help.err" || fail "--help omitted usage"

missing_status=0
"$cli" "$tmpdir/missing.bin" > "$tmpdir/missing.out" 2> "$tmpdir/missing.err" ||
	missing_status=$?
[ "$missing_status" -eq 2 ] || fail "missing file returned $missing_status"
[ ! -s "$tmpdir/missing.out" ] || fail "missing file wrote to stdout"
grep -q '^fch: cannot open ' "$tmpdir/missing.err" ||
	fail "missing file omitted its error"

if [ -e /dev/full ]; then
	output_status=0
	"$cli" "$input" > /dev/full 2> "$tmpdir/output.err" ||
		output_status=$?
	[ "$output_status" -eq 2 ] ||
		fail "output failure returned $output_status"
	grep -q '^fch: failed to write output$' "$tmpdir/output.err" ||
		fail "output failure omitted its error"
fi

printf 'PASS: CLI input and error handling\n'
