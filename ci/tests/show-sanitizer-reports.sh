#!/bin/bash
set -uo pipefail

issue_reported=false

# Check for sanitizer reports using glob
shopt -s nullglob
reports=(./sanitizer_report*)

if [[ ${#reports[@]} -gt 0 ]]; then
    for report in "${reports[@]}"; do
        report_name=$(basename "${report}")
        echo "::group::Report: $report_name"
        
        cat "${report}"
        
        echo "::endgroup::"
        
        issue_reported=true
    done
else
    echo "No report has been generated."
fi

echo "issue_reported=${issue_reported}" >> $GITHUB_OUTPUT

if $issue_reported; then
    echo "::error::Issues were reported in the sanitizer report."
    exit 1
else
    echo "No issues found in the sanitizer reports."
    exit 0
fi