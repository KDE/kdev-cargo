# Find the cargo executable
#
# Defines the following variables
#  Cargo_EXECUTABLE - path of the cargo executable

#=============================================================================
# Copyright 2017 Friedrich W. H. Kossebau <kossebau@kde.org>
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================

find_program(Cargo_EXECUTABLE NAMES cargo)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Cargo DEFAULT_MSG Cargo_EXECUTABLE)

mark_as_advanced(Cargo_EXECUTABLE)

set_package_properties(Cargo PROPERTIES
    DESCRIPTION "The Rust package manager"
    URL "https://github.com/rust-lang/cargo/"
)
