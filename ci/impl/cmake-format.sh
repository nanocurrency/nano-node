#!/usr/bin/env bash

###################################################################################################

CMAKE_FORMAT=""
CMAKE_FORMAT_VERSION="0.6.13"

###################################################################################################

does_cmake_format_exist()
{
    local attempts=("cmake-format")
    for itr in ${attempts[@]}; do
        version=$(_is_cmake_format_usable $itr $CMAKE_FORMAT_VERSION)
        if [[ $? == 1 ]]; then
            continue
        fi

        if [[ $? == 0 ]]; then
            CMAKE_FORMAT=$itr
            break
        fi

        echo "Detected '$itr' with version '$version' " \
             "(different than '$CMAKE_FORMAT_VERSION'), skipping it."
    done

    if [[ -z $CMAKE_FORMAT ]]; then
        echo "No 'cmake-format' of version '$CMAKE_FORMAT_VERSION' could be detected in your " \
             "PATH. Try 'pip3 install cmake-format'. Or up/down grade, if installed differently."
        return 1
    fi

    echo "Using '$CMAKE_FORMAT' version '$CMAKE_FORMAT_VERSION'"
    return 0
}

###################################################################################################

cmake_format_do()
{
    _cmake_format_perform "do"
}

###################################################################################################

cmake_format_check()
{
    _cmake_format_perform "check"
}

###################################################################################################

_is_cmake_format_usable()
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

_cmake_format_perform()
{
    if [[ -z "$CMAKE_FORMAT" ]]; then
        echo "Logic error: '_cmake_format_perform' called, but 'CMAKE_FORMAT' " \
             "is empty. Have you called 'does_cmake_format_exist'?"
        return 2
    fi

    find "$ROOTPATH" -type f \( -iwholename "$ROOTPATH/CMakeLists.txt"          \
                                -o                                              \
                                -iwholename "$ROOTPATH/coverage/CMakeLists.txt" \
                                -o                                              \
                                -iwholename "$ROOTPATH/nano/*/CMakeLists.txt"   \
                                -o                                              \
                                -iwholename "$ROOTPATH/cmake/*/*.cmake"         \
                             \)                                                 \
                     -print0 |
        while read -d $'\0' file
        do
            if [[ $1 == "do" ]]; then
                "$CMAKE_FORMAT" -i "$file"
            elif [[ $1 == "check" ]]; then
                "$CMAKE_FORMAT" "$file" -o tmp

                diff "$file" tmp > /dev/null
                if [[ $? != 0 ]]; then
                    rm tmp
                    return 1
                fi

                rm tmp
            else
                echo "Logic error: '_cmake_format_perform' called " \
                     "with neither 'do' nor 'check' as argument, but '$1'"
                return 2
            fi
        done
}

###################################################################################################
