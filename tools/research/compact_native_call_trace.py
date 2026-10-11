#!/usr/bin/env python3
"""Publish a compact derivative of a completed original-code probe report.

Only complete call-trace arrays are summarized. Case assertions, native outputs,
execution bounds, provenance and source hashes are preserved. The input and
canonical full trace hashes allow recovery against a retained private report.
This tool does not execute original instructions or turn a prefix into a return.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


def sha(value):
    return hashlib.sha256(value).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    assert args.input.resolve() != args.out.resolve()
    raw = args.input.read_bytes()
    report = json.loads(raw)
    assert report['original_code_executed'] and report['all_passed']
    assert report.get('helper_stubs') == []
    assert report.get('complete_game_executed') is False
    assert report.get('prefix_cases', 0) == 0
    count = 0
    entries = 0

    def compact(node):
        nonlocal count, entries
        if isinstance(node, dict):
            if isinstance(node.get('calls'), list):
                trace = node['calls']
                assert all(isinstance(item, dict) and isinstance(item.get('callee'), str)
                           for item in trace)
                canonical = json.dumps(trace, sort_keys=True, separators=(',', ':')).encode()
                node['call_trace_summary'] = {
                    'original_entry_count': len(trace),
                    'canonical_full_trace_sha256': sha(canonical),
                    'callee_counts': dict(sorted(Counter(item['callee'] for item in trace).items())),
                    'first_entries': trace[:8],
                    'last_entries': trace[-8:] if len(trace) > 8 else [],
                    'arguments_are_samples_not_full_published_trace': True,
                }
                del node['calls']
                count += 1
                entries += len(trace)
            for value in node.values():
                compact(value)
        elif isinstance(node, list):
            for value in node:
                compact(value)

    compact(report)
    assert count
    report['generated_call_trace_compaction'] = {
        'script': Path(__file__).name,
        'script_sha256': sha(Path(__file__).read_bytes()),
        'original_probe_report_sha256': sha(raw),
        'original_probe_report_bytes': len(raw),
        'trace_arrays_summarized': count,
        'trace_entries_summarized': entries,
        'original_instructions_executed_by_this_compactor': False,
        'case_outputs_and_assertions_preserved': True,
        'raw_report_retained_privately': True,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'trace_arrays': count, 'entries': entries,
                      'input_bytes': len(raw), 'output_bytes': args.out.stat().st_size}))


if __name__ == '__main__':
    main()
