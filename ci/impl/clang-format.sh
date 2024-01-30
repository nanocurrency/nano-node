#!/usr/bin/env bash

###################################################################################################

CLANG_FORMAT=""
CLANG_FORMAT_VERSION="17"

###################################################################################################

does_clang_format_exist()
{
    local attempts=("clang-format" "clang-format-$CLANG_FORMAT_VERSION")
    for itr in ${attempts[@]}; do
        version=$(_is_clang_format_usable $itr $CLANG_FORMAT_VERSION)
        if [[ $? == 1 ]]; then
            continue
        fi

        if [[ $? == 0 ]]; then
            CLANG_FORMAT=$itr
            break
        fi

        echo "Detected '$itr' with version '$version' " \
             "(different than '$CLANG_FORMAT_VERSION'), skipping it."
    done

    if [[ -z $CLANG_FORMAT ]]; then
        echo "No 'clang-format' of version '$CLANG_FORMAT_VERSION' could be detected in your "    \
             "PATH. Try 'sudo apt-get install clang-format-$CLANG_FORMAT_VERSION' or, if macOS, " \
             "'brew install clang-format'. Or up/down grade, if installed differently."
        return 1
    fi

    echo "Using '$CLANG_FORMAT' version '$CLANG_FORMAT_VERSION'"
    return 0
}

###################################################################################################

clang_format_do()
{
    _clang_format_perform "do"
}

###################################################################################################

clang_format_check()
{
    _clang_format_perform "check"
}

###################################################################################################

_is_clang_format_usable()
{
    if [[ $(builtin type -p $1) ]]; then
        local output=$($1 --version)
        if [[ $output =~ ^(.)*$2(.)*$ ]]; then
            return 0
        fi

        echo $output
        return 1
    fi

    return 2
}

###################################################################################################

_clang_format_perform()
{
    if [[ -z "$CLANG_FORMAT" ]]; then
        echo "Logic error: '_lang_format_perform' called, but 'CLANG_FORMAT' " \
             "is empty. Have you called 'does_clang_format_exist'?"
        return 2
    fi

    find "$ROOTPATH/nano" -type f \( -iname "*.hpp" \
                                     -o             \
                                     -iname "*.cpp" \
                                  \)                \
                     -print0 |
        while read -d $'\0' file
        do
            if [[ $1 == "do" ]]; then
                "$CLANG_FORMAT" -i "$file"
            elif [[ $1 == "check" ]]; then
                "$CLANG_FORMAT" -style=file -Werror --dry-run "$file"
                if [[ $? != 0 ]]; then
                    return 1
                fi
            else
                echo "Logic error: '_clang_format_perform' called " \
                     "with neither 'do' nor 'check' as argument, but '$1'"
                return 2
            fi
        done
}

###################################################################################################
