This provides a basic source-code generated documentation for the core classes of the nano-node.
Doxygen docs may look a bit overwhelming as it tries to document all the smaller pieces of code. For
this reason only the files from `nano` directory were added to this. Some other
files were also excluded as the `EXCLUDE_PATTERN` configuration stated below.

    EXCLUDE_PATTERNS       = */nano/*_test/* \
                             */nano/test_common/* \
                             */nano/boost/* \
                             */nano/qt/* \
                             */nano/nano_wallet/*

