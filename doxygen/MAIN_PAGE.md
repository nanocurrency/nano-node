This provides a basic source-code generated documentation for the core classes of the celerix-node.
Doxygen docs may look a bit overwhelming as it tries to document all the smaller pieces of code. For
this reason only the files from `celerix` directory were added to this. Some other
files were also excluded as the `EXCLUDE_PATTERN` configuration stated below.

    EXCLUDE_PATTERNS       = */celerix/*_test/* \
                             */celerix/test_common/* \
                             */celerix/boost/* \
                             */celerix/qt/* \
                             */celerix/celerix_wallet/*

