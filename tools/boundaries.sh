#!/usr/bin/env bash
# The native core's boundaries the compiler does not check (ADR 0023), by reading the sources:
#   - no mutable static or global state in native/kp_core/src (two games and two interfaces live in one process: a
#     rewind's preview, tests on threads); a const table instead of a lazily built one, the state in KpGame or KpUi.
#     The only process-wide one: kp_smk.c's video_codec, set once before any game (kp_smk_set_video).
#   - the layers over the simulation (the interface native/kp_core/src/ui, the application native/kp_app, the
#     presentation native/kp_present) read a unit through kp_unit_get_<field> (KP_UNIT_VIEW_FIELDS, kp_game.h) and
#     write its view state through the simulation's functions (KP_UNIT_VIEW_STATE), never through g->u or KpUnits.
#   - the headless application's layers (CMakeLists.txt's KP_APP_DATA_MODULES, KP_APP_NET_MODULES, KP_APP_FLOW_MODULES:
#     kp_app_data under kp_app_net under kp_app_flow): a source or header of native/kp_app includes no header of a later
#     layer, directly or through its other headers (the headers without a source, kp_casc.h and kp_ogg_crc_table.h, are
#     under them all). The product's sources (sdl/, ios/, web/) are over them all.
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

# The scan's output first (a perl that fails stops the script, set -e), then its lines are the offences.
layers="$(perl -e '
  open(my $cm, "<", "CMakeLists.txt") or die "CMakeLists.txt: $!\n";
  my (%layer, $lists);
  my %rank = (DATA => 0, NET => 1, FLOW => 2);
  while (<$cm>) {
    next unless /^set\(KP_APP_(DATA|NET|FLOW)_MODULES (.*)\)\s*$/;
    $layer{"kp_$_"} = $rank{$1} for split " ", $2;
    $lists++;
  }
  die "boundaries: CMakeLists.txt has not the three KP_APP_*_MODULES lists\n" unless $lists && $lists == 3;
  my @name = qw(kp_app_data kp_app_net kp_app_flow);
  my %inc;
  for my $f (glob "native/kp_app/*.[ch]") {
    open(my $h, "<", $f) or die "$f: $!\n";
    (my $b = $f) =~ s{.*/}{};
    $inc{$b} = [];
    while (<$h>) { push @{$inc{$b}}, "$1.h" if /^\s*#\s*include\s+"(kp_\w+)\.h"/ && -e "native/kp_app/$1.h"; }
  }
  for my $f (sort keys %inc) {
    (my $m = $f) =~ s/\.[ch]$//;
    my $l = $layer{$m} // 0;
    my %via = map { $_ => $f } @{$inc{$f}};
    my @todo = @{$inc{$f}};
    while (defined(my $h = shift @todo)) {
      for my $n (@{$inc{$h} // []}) { next if exists $via{$n}; $via{$n} = $h; push @todo, $n; }
    }
    for my $h (sort keys %via) {
      (my $hm = $h) =~ s/\.h$//;
      my $hl = $layer{$hm} // 0;
      next unless $hl > $l;
      my @chain = ($h);
      unshift @chain, $via{$chain[0]} while $via{$chain[0]} ne $f;
      print "native/kp_app/$f ($name[$l]) includes $h ($name[$hl]): ", join(" -> ", $f, @chain), "\n";
    }
  }
')"
if [ -n "$layers" ]; then
  echo "boundaries: a layer of the headless application over a later one's header (CMakeLists.txt's KP_APP_*_MODULES):" >&2
  printf '%s\n' "$layers" >&2
  failed=1
fi

exit "$failed"
