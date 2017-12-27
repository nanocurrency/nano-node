#!/bin/sh

if git diff-index --quiet HEAD ; then
  >&2 echo "ERROR: No uncommited changes!";
  exit 1;
fi

git diff -U0 --no-color HEAD^ | ./clang-format-diff.py -p1 -i
