#  James Bigler, NVIDIA Corp (nvidia.com - jbigler)
#  Abe Stephens, SCI Institute -- http://www.sci.utah.edu/~abe/FindCuda.html
#
#  Copyright (c) 2008 - 2009 NVIDIA Corporation.  All rights reserved.
#
#  Copyright (c) 2007-2009
#  Scientific Computing and Imaging Institute, University of Utah
#
#  This code is licensed under the MIT License.  See the FindCUDA.cmake script
#  for the text of the license.

# The MIT License
#
# License for the specific language governing rights and limitations under
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

#######################################################################
# This converts a file written in makefile syntax into one that can be included
# by CMake.

# Input variables
#
# verbose:BOOL=<>          OFF: Be as quiet as possible (default)
#                          ON : Extra output
#
# input_file:FILEPATH=<>   Path to dependecy file in makefile format
#
# output_file:FILEPATH=<>  Path to file with dependencies in CMake readable variable
#

file(READ ${input_file} depend_text)

if (NOT "${depend_text}" STREQUAL "")

  # message("FOUND DEPENDS")

  string(REPLACE "\\ " " " depend_text ${depend_text})

  # This works for the nvcc -M generated dependency files.
  string(REGEX REPLACE "^.* : " "" depend_text ${depend_text})
  string(REGEX REPLACE "[ \\\\]*\n" ";" depend_text ${depend_text})

  set(dependency_list "")

  foreach(file ${depend_text})

    string(REGEX REPLACE "^ +" "" file ${file})

    # OK, now if we had a UNC path, nvcc has a tendency to only output the first '/'
    # instead of '//'.  Here we will test to see if the file exists, if it doesn't then
    # try to prepend another '/' to the path and test again.  If it still fails remove the
    # path.

    if(NOT EXISTS "${file}")
      if (EXISTS "/${file}")
        set(file "/${file}")
      else()
        if(verbose)
          message(WARNING " Removing non-existent dependency file: ${file}")
        endif()
        set(file "")
      endif()
    endif()

    # Make sure we check to see if we have a file, before asking if it is not a directory.
    # if(NOT IS_DIRECTORY "") will return TRUE.
    if(file AND NOT IS_DIRECTORY "${file}")
      # If softlinks start to matter, we should change this to REALPATH.  For now we need
      # to flatten paths, because nvcc can generate stuff like /bin/../include instead of
      # just /include.
      get_filename_component(file_absolute "${file}" ABSOLUTE)
      list(APPEND dependency_list "${file_absolute}")
    endif()

  endforeach()

else()
  # message("FOUND NO DEPENDS")
endif()

# Remove the duplicate entries and sort them.
list(REMOVE_DUPLICATES dependency_list)
list(SORT dependency_list)

foreach(file ${dependency_list})
  set(cuda_nvcc_depend "${cuda_nvcc_depend} \"${file}\"\n")
endforeach()

file(WRITE ${output_file} "# Generated by: make2cmake.cmake\nSET(CUDA_NVCC_DEPEND\n ${cuda_nvcc_depend})\n\n")
