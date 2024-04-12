# Build vmmem_test binary file
#bindir := "/bin"
AM_CPPFLAGS := \
    -W \
    -Wall \
    -Wextra \
    -Wunused

bin_PROGRAMS = vmmem-test

vmmem_test_SOURCES := vmmem_test.cpp
vmmem_test_CPPFLAGS = @LIBBASE_CFLAGS@ -I =${includedir}/vmmem
vmmem_test_LDFLAGS := @LIBBASE_LIBS@ -lvmmem
