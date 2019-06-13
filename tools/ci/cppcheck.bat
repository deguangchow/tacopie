ECHO ======== Check CppCheck Start ========

cppcheck --enable=warning,performance --inconclusive includes sources examples/.*cpp tests/sources > cppcheck.log

ECHO ======== Check CppCheck Result ========