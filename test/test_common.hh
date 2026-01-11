#ifndef TEST_COMMON_HH
#define TEST_COMMON_HH

#define _BEGIN_ISOLATED_NAMESPACE2(name, line, cnt) namespace name_##line##_##cnt {
#define _BEGIN_ISOLATED_NAMESPACE1(name, line, cnt) _BEGIN_ISOLATED_NAMESPACE2(name, line, cnt)
#define BEGIN_ISOLATED_NAMESPACE _BEGIN_ISOLATED_NAMESPACE1(TEST_FILE_NAME, __LINE__, __COUNTER__)
#define END_ISOLATED_NAMESPACE }


#endif // TEST_COMMON_HH
