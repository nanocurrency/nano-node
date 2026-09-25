#!/usr/bin/env bash

get_exec_extension() {
    case "$(uname -s)" in
        Linux*|Darwin*)     
            echo ""
            ;;
        CYGWIN*|MINGW32*|MSYS*|MINGW*)
            echo ".exe"
            ;;
        *)
            echo "Unknown OS"
            exit 1
            ;;
    esac
}

get_test_executable() {
    echo "./${1}$(get_exec_extension)"
}

get_processor_count() {
    case "$(uname -s)" in
        Linux*)
            nproc
            ;;
        Darwin*)
            sysctl -n hw.ncpu
            ;;
        CYGWIN*|MINGW32*|MSYS*|MINGW*)
            echo "${NUMBER_OF_PROCESSORS}"
            ;;
        *)
            echo "Unknown OS"
            exit 1
            ;;
    esac
}
