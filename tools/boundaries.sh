#!/usr/bin/env bash
# The native core's boundaries the compiler does not check (ADR 0023), by reading the sources:
#   - no mutable static or global state in native/kp_core/src (two games and two interfaces live in one process: a
#     rewind's preview, tests on threads); a const table instead of a lazily built one, the state in KpGame or KpUi.
#     The only process-wide one: kp_smk.c's video_codec, set once before any game (kp_smk_set_video).
# Exits 1 with the offending lines.
#   tools/boundaries.sh
set -euo pipefail
cd "$(dirname "$0")/.."

failed=0

# A static that is no function and no const object: a line starting `static` whose part before any `=` (an anonymous
# struct's members left out) has no `(` (a function or its prototype), or a `static const T *name` (a pointer to const,
# itself mutable).
statics="$(find native/kp_core/src -name '*.[ch]' -print0 | sort -z | xargs -0 perl -ne '
  if (/^\s*static\b/ && !/^\s*static\s+inline\b/) {
    my $head = $_;
    $head =~ s/=.*//s;
    $head =~ s/\{[^}]*\}//g; # the members of an anonymous struct
    my $hit = $head =~ /^\s*static\s+const\b/ ? ($head =~ /\*\s*[A-Za-z_]\w*\s*(;|\[|$)/ && $head !~ /\*\s*const\b/)
      : ($head !~ /\(/);
    print "$ARGV:$.: $_" if $hit;
  }
} continue { close ARGV if eof;' | grep -vE '^native/kp_core/src/[a-z]+/kp_smk\.c:[0-9]+: static const KpSmkVideo \*video_codec;$' || true)"
if [ -n "$statics" ]; then
  echo "boundaries: mutable static state in native/kp_core/src (a const table, or a field of KpGame or KpUi):" >&2
  printf '%s\n' "$statics" >&2
  failed=1
fi

exit "$failed"
