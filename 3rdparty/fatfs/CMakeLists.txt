add_library(fatfs source/ff.c)
target_include_directories(fatfs PUBLIC source)
set_source_files_properties(source/ff.c PROPERTIES LANGUAGE CXX)
