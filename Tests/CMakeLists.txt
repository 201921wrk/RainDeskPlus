# Tests/CMakeLists.txt
# 工具链就位并接入 GoogleTest 后启用：在根 CMakeLists.txt 取消
# `add_subdirectory(Tests)` 的注释，并在下方实现 FetchContent 拉取 gtest。
#
# FetchContent_Declare(googletest
#   GIT_REPOSITORY https://github.com/google/googletest.git
#   GIT_TAG        v1.14.0)
# FetchContent_MakeAvailable(googletest)
#
# add_executable(RainDeskPlusTests test_skeleton.cpp)
# target_link_libraries(RainDeskPlusTests PRIVATE gtest_main RainDeskPlusCore)
# include(GoogleTest)
# gtest_discover_tests(RainDeskPlusTests)
