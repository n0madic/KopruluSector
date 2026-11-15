#!/usr/bin/env bash
# The native core's boundaries the compiler does not check (ADR 0023), by reading the sources:
#   - no mutable static or global state in native/kp_core/src (two games and two interfaces live in one process: a
#     rewind's preview, tests on threads); a const table instead of a lazily built one, the state in KpGame or KpUi.
#     The only process-wide one: kp_smk.c's video_codec, set once before any game (kp_smk_set_video).
#   - the layers over the simulation (the interface native/kp_core/src/ui, the application native/kp_app, the
#     presentation native/kp_present) read a unit through kp_unit_get_<field> (KP_UNIT_VIEW_FIELDS, kp_game.h) and
#     write its view state through the simulation's functions (KP_UNIT_VIEW_STATE), never through g->u or KpUnits.
# Exits 1 with the offending lines.
#   tools/boundaries.sh
set -euo pipefail
cd "$(dirname "$0")/.."

failed=0

# A static that is no function and no const object: a line starting `static` whose part before any `=` (an anonymous
# struct's members left out) has no `(` (a function or its prototype), or a `static const T *name` (a pointer to const,
# itself mutable).
# The scan's output comes first, so that a find or perl that fails stops the script (set -e, pipefail); grep's exit 1
# (nothing left) is the clean case, any other status a failure.
found="$(find native/kp_core/src -name '*.[ch]' -print0 | sort -z | xargs -0 perl -ne '
  if (/^\s*static\b/ && !/^\s*static\s+inline\b/) {
    my $head = $_;
    $head =~ s/=.*//s;
    $head =~ s/\{[^}]*\}//g; # the members of an anonymous struct
    my $hit = $head =~ /^\s*static\s+const\b/ ? ($head =~ /\*\s*[A-Za-z_]\w*\s*(;|\[|$)/ && $head !~ /\*\s*const\b/)
      : ($head !~ /\(/);
    print "$ARGV:$.: $_" if $hit;
  }
} continue { close ARGV if eof;')"
statics="$(printf '%s\n' "$found" \
  | grep -vE '^native/kp_core/src/[a-z]+/kp_smk\.c:[0-9]+: static const KpSmkVideo \*video_codec;$' || [ $? -eq 1 ])"
if [ -n "$statics" ]; then
  echo "boundaries: mutable static state in native/kp_core/src (a const table, or a field of KpGame or KpUi):" >&2
  printf '%s\n' "$statics" >&2
  failed=1
fi

# `->u` not followed by a name's character: a field (`->u.hp`) and the whole record (`&g->u`) alike
units="$(grep -rnE --include='*.[ch]' --include='*.cpp' -e '->u([^A-Za-z0-9_]|$)|KpUnits' native/kp_core/src/ui \
  native/kp_app native/kp_present || [ $? -eq 1 ])"
if [ -n "$units" ]; then
  echo "boundaries: a unit's fields outside the simulation (kp_unit_get_<field>, KP_UNIT_VIEW_FIELDS in kp_game.h):" >&2
  printf '%s\n' "$units" >&2
  failed=1
fi

exit "$failed"
