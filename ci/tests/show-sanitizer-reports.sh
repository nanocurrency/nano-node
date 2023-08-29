#!/bin/bash

issue_reported=false
reports=$(ls ./sanitizer_report* 2>/dev/null)
if [[ -n "${reports}" ]]; then
    echo "Report Output:"
    for report in ${reports}; do
    echo "File: $report"
    cat ${report}
    echo
    done
    issue_reported=true
else
    echo "No report has been generated."
fi

echo "issue_reported=${issue_reported}" >> $GITHUB_OUTPUT